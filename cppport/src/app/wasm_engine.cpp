#include "wasm_engine.hpp"

#include <QFile>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

static_assert(std::endian::native == std::endian::little, "Wasm linear memory is accessed in host byte order");

namespace gs::app {

const char* wasmTrapToString(WasmTrap trap) {
    switch (trap) {
        case WasmTrap::None: return "None";
        case WasmTrap::Unreachable: return "Unreachable opcode executed";
        case WasmTrap::OutOfBoundsMemoryAccess: return "Out of bounds memory access";
        case WasmTrap::DivisionByZero: return "Integer divide by zero";
        case WasmTrap::IntegerOverflow: return "Integer overflow";
        case WasmTrap::StackUnderflow: return "Operand stack underflow";
        case WasmTrap::CallStackExhausted: return "Call stack limit exceeded";
        case WasmTrap::InvalidOpcode: return "Invalid or unsupported Wasm opcode";
        case WasmTrap::UndefinedElement: return "Call to undefined function or element";
        case WasmTrap::TypeMismatch: return "Argument or signature type mismatch";
        case WasmTrap::InvalidConversion: return "Invalid conversion to integer";
        case WasmTrap::OutOfBoundsTableAccess: return "Out of bounds table access";
        case WasmTrap::OutOfFuel: return "Instruction budget exhausted";
        case WasmTrap::HostError: return "Host function failed";
    }
    return "Unknown trap";
}

// -----------------------------------------------------------------------------
// WasmMemory
// -----------------------------------------------------------------------------

WasmMemory::WasmMemory(uint32_t initialPages, uint32_t maxPages)
    : maxPages_(std::max(initialPages, maxPages)) {
    data_.resize(static_cast<size_t>(initialPages) * PageSize, 0);
}

bool WasmMemory::grow(uint32_t deltaPages) {
    const uint64_t target = static_cast<uint64_t>(sizePages()) + deltaPages;
    if (target > maxPages_) {
        return false;
    }
    data_.resize(static_cast<size_t>(target) * PageSize, 0);
    return true;
}

bool WasmMemory::read(uint32_t offset, void* dst, size_t size) const {
    if (!dst || static_cast<uint64_t>(offset) + size > data_.size()) {
        return false;
    }
    if (size > 0) {
        std::memcpy(dst, data_.data() + offset, size);
    }
    return true;
}

bool WasmMemory::write(uint32_t offset, const void* src, size_t size) {
    if (!src || static_cast<uint64_t>(offset) + size > data_.size()) {
        return false;
    }
    if (size > 0) {
        std::memcpy(data_.data() + offset, src, size);
    }
    return true;
}

std::string WasmMemory::readString(uint32_t offset, size_t maxLen) const {
    if (offset >= data_.size()) {
        return {};
    }
    const size_t limit = std::min(data_.size() - offset, maxLen);
    const char* start = reinterpret_cast<const char*>(data_.data() + offset);
    size_t len = 0;
    while (len < limit && start[len] != '\0') {
        ++len;
    }
    return std::string(start, len);
}

bool WasmMemory::writeString(uint32_t offset, const std::string& str, size_t maxLen) {
    if (maxLen == 0) {
        return false;
    }
    const size_t len = std::min(str.size(), maxLen - 1);
    if (static_cast<uint64_t>(offset) + len + 1 > data_.size()) {
        return false;
    }
    std::memcpy(data_.data() + offset, str.data(), len);
    data_[offset + len] = '\0';
    return true;
}

// -----------------------------------------------------------------------------
// Decoding
// -----------------------------------------------------------------------------

namespace {

constexpr uint16_t kPrefixFC = 0x100;
constexpr uint32_t kFunctionEnd = std::numeric_limits<uint32_t>::max();

bool isValType(uint8_t b) {
    return b == 0x7F || b == 0x7E || b == 0x7D || b == 0x7C || b == 0x7B || b == 0x70 || b == 0x6F;
}

}  // namespace

class WasmModuleParser {
public:
    WasmModuleParser(WasmModule& module, const uint8_t* data, size_t size) : m_(module), p_(data), end_(data + size) {}

    void parse() {
        if (end_ - p_ < 8 || std::memcmp(p_, "\0asm", 4) != 0) {
            fail("Invalid Wasm magic header");
        }
        if (p_[4] != 0x01 || p_[5] != 0 || p_[6] != 0 || p_[7] != 0) {
            fail("Unsupported Wasm version");
        }
        p_ += 8;

        std::vector<uint32_t> funcTypes;
        while (p_ < end_) {
            const uint8_t id = u8();
            const uint32_t len = u32();
            if (static_cast<size_t>(end_ - p_) < len) {
                fail("Section exceeds module size");
            }
            const uint8_t* sectionEnd = p_ + len;
            const uint8_t* outerEnd = end_;
            end_ = sectionEnd;
            switch (id) {
                case 0: p_ = sectionEnd; break;  // custom (names, producers, debug info)
                case 1: typeSection(); break;
                case 2: importSection(); break;
                case 3: {
                    const uint32_t n = u32();
                    for (uint32_t i = 0; i < n; ++i) {
                        const uint32_t t = u32();
                        if (t >= m_.types_.size()) fail("Function type index out of range");
                        funcTypes.push_back(t);
                    }
                    break;
                }
                case 4: tableSection(); break;
                case 5: memorySection(); break;
                case 6: globalSection(); break;
                case 7: exportSection(); break;
                case 8: {
                    const uint32_t f = u32();
                    m_.start_ = f;
                    break;
                }
                case 9: elementSection(); break;
                case 10: codeSection(funcTypes); break;
                case 11: dataSection(); break;
                case 12: u32(); break;  // data count
                default: fail("Unknown section id " + std::to_string(id));
            }
            if (p_ != sectionEnd) {
                fail("Section size mismatch (id " + std::to_string(id) + ")");
            }
            end_ = outerEnd;
        }

        if (m_.functions_.size() != funcTypes.size()) {
            fail("Function and code section counts differ");
        }
        for (const WasmExport& exp : m_.exports_) {
            if ((exp.kind == 0 && exp.index >= m_.functionCount()) || (exp.kind == 1 && exp.index >= m_.tables_.size()) ||
                (exp.kind == 2 && (exp.index != 0 || !m_.hasMemory_)) || (exp.kind == 3 && exp.index >= m_.globals_.size())) {
                fail("Export '" + exp.name + "' refers to a missing item");
            }
        }
        if (m_.start_ && *m_.start_ >= m_.functionCount()) {
            fail("Start function index out of range");
        }
    }

private:
    [[noreturn]] static void fail(const std::string& message) { throw std::runtime_error(message); }

    uint8_t u8() {
        if (p_ >= end_) fail("Unexpected end of module");
        return *p_++;
    }

    uint64_t leb(unsigned bits, bool isSigned) {
        uint64_t result = 0;
        unsigned shift = 0;
        uint8_t byte = 0;
        do {
            if (shift >= bits + 7) fail("LEB128 value too long");
            byte = u8();
            result |= static_cast<uint64_t>(byte & 0x7F) << shift;
            shift += 7;
        } while (byte & 0x80);
        if (isSigned && shift < 64 && (byte & 0x40)) {
            result |= ~uint64_t(0) << shift;
        }
        return result;
    }

    uint32_t u32() { return static_cast<uint32_t>(leb(32, false)); }
    int32_t s32() { return static_cast<int32_t>(static_cast<uint32_t>(leb(32, true))); }
    int64_t s64() { return static_cast<int64_t>(leb(64, true)); }

    template <typename T>
    T raw() {
        if (static_cast<size_t>(end_ - p_) < sizeof(T)) fail("Unexpected end of module");
        T v;
        std::memcpy(&v, p_, sizeof(T));
        p_ += sizeof(T);
        return v;
    }

    std::string name() {
        const uint32_t len = u32();
        if (static_cast<size_t>(end_ - p_) < len) fail("Name exceeds section");
        std::string s(reinterpret_cast<const char*>(p_), len);
        p_ += len;
        return s;
    }

    WasmValType valType() {
        const uint8_t b = u8();
        if (!isValType(b)) fail("Invalid value type");
        if (b == 0x7B) fail("SIMD (v128) is not supported");
        return static_cast<WasmValType>(b);
    }

    void limits(uint32_t& min, uint32_t& max, bool& hasMax) {
        const uint8_t flags = u8();
        if (flags > 1) fail("Shared or 64-bit memories are not supported");
        min = u32();
        hasMax = flags == 1;
        max = hasMax ? u32() : 0;
        if (hasMax && max < min) fail("Limits maximum below minimum");
    }

    void typeSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            if (u8() != 0x60) fail("Invalid function type form");
            WasmFuncType t;
            const uint32_t np = u32();
            for (uint32_t j = 0; j < np; ++j) t.params.push_back(valType());
            const uint32_t nr = u32();
            for (uint32_t j = 0; j < nr; ++j) t.results.push_back(valType());
            m_.types_.push_back(std::move(t));
        }
    }

    void importSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            WasmImport imp;
            imp.moduleName = name();
            imp.fieldName = name();
            imp.kind = u8();
            if (imp.kind != 0) {
                // Plugins own their memory, tables and globals; the host only provides functions.
                fail("Unsupported import '" + imp.moduleName + "." + imp.fieldName + "': only functions can be imported");
            }
            imp.typeIndex = u32();
            if (imp.typeIndex >= m_.types_.size()) fail("Import type index out of range");
            if (m_.types_[imp.typeIndex].results.size() > 1) fail("Imported functions may return at most one value");
            m_.importFuncTypes_.push_back(imp.typeIndex);
            m_.imports_.push_back(std::move(imp));
        }
    }

    void tableSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            WasmTableDef t;
            t.elemType = valType();
            if (t.elemType != WasmValType::FuncRef && t.elemType != WasmValType::ExternRef) fail("Invalid table element type");
            limits(t.initial, t.max, t.hasMax);
            m_.tables_.push_back(t);
        }
    }

    void memorySection() {
        const uint32_t n = u32();
        if (n > 1 || (n == 1 && m_.hasMemory_)) fail("Only one memory is supported");
        if (n == 1) {
            bool hasMax = false;
            uint32_t max = 0;
            limits(m_.initialMemoryPages_, max, hasMax);
            m_.maxMemoryPages_ = hasMax ? max : 65536;
            if (m_.initialMemoryPages_ > 65536 || m_.maxMemoryPages_ > 65536) fail("Memory larger than 4 GiB");
            m_.hasMemory_ = true;
        }
    }

    std::vector<WasmInstr> constExpr() {
        std::vector<WasmInstr> expr;
        for (;;) {
            const uint8_t op = u8();
            WasmInstr in;
            in.op = op;
            switch (op) {
                case 0x0B: return expr;
                case 0x41: in.b = static_cast<uint32_t>(s32()); break;
                case 0x42: in.b = static_cast<uint64_t>(s64()); break;
                case 0x43: in.b = raw<uint32_t>(); break;
                case 0x44: in.b = raw<uint64_t>(); break;
                case 0x23:
                    in.a = u32();
                    if (in.a >= m_.globals_.size()) fail("Constant expression reads a missing global");
                    break;
                case 0xD0: u8(); break;
                case 0xD2: in.a = u32(); break;
                default: fail("Unsupported instruction in constant expression");
            }
            expr.push_back(in);
        }
    }

    void globalSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            WasmGlobalDef g;
            g.type = valType();
            const uint8_t mut = u8();
            if (mut > 1) fail("Invalid global mutability");
            g.mutableValue = mut == 1;
            g.init = constExpr();
            m_.globals_.push_back(std::move(g));
        }
    }

    void exportSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            WasmExport e;
            e.name = name();
            e.kind = u8();
            if (e.kind > 3) fail("Invalid export kind");
            e.index = u32();
            m_.exports_.push_back(std::move(e));
        }
    }

    void elementSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            WasmElementSegment seg;
            const uint32_t flags = u32();
            if (flags > 7) fail("Invalid element segment flags");
            const bool passiveOrDeclarative = flags & 1;
            const bool explicitTable = flags & 2;
            const bool exprs = flags & 4;
            if (passiveOrDeclarative) {
                seg.mode = explicitTable ? WasmElementSegment::Mode::Declarative : WasmElementSegment::Mode::Passive;
            } else {
                seg.tableIndex = explicitTable ? u32() : 0;
                if (seg.tableIndex >= m_.tables_.size()) fail("Element segment table index out of range");
                seg.offset = constExpr();
            }
            if (passiveOrDeclarative || explicitTable) {
                const uint8_t kind = u8();  // elemkind 0x00 (funcref) or a reference type
                if (!exprs && kind != 0x00) fail("Invalid element kind");
            }
            const uint32_t count = u32();
            for (uint32_t j = 0; j < count; ++j) {
                if (exprs) {
                    seg.items.push_back(constExpr());
                } else {
                    WasmInstr in;
                    in.op = 0xD2;
                    in.a = u32();
                    seg.items.push_back({in});
                }
            }
            m_.elements_.push_back(std::move(seg));
        }
    }

    void dataSection() {
        const uint32_t n = u32();
        for (uint32_t i = 0; i < n; ++i) {
            WasmDataSegment seg;
            const uint32_t flags = u32();
            if (flags == 1) {
                seg.active = false;
            } else if (flags == 0 || flags == 2) {
                if (flags == 2 && u32() != 0) fail("Data segment for a missing memory");
                seg.offset = constExpr();
            } else {
                fail("Invalid data segment flags");
            }
            const uint32_t len = u32();
            if (static_cast<size_t>(end_ - p_) < len) fail("Data segment exceeds section");
            seg.data.assign(p_, p_ + len);
            p_ += len;
            m_.data_.push_back(std::move(seg));
        }
    }

    void codeSection(const std::vector<uint32_t>& funcTypes) {
        const uint32_t n = u32();
        if (n != funcTypes.size()) fail("Function and code section counts differ");
        for (uint32_t i = 0; i < n; ++i) {
            const uint32_t size = u32();
            if (static_cast<size_t>(end_ - p_) < size) fail("Function body exceeds section");
            const uint8_t* bodyEnd = p_ + size;
            const uint8_t* sectionEnd = end_;
            end_ = bodyEnd;

            WasmFunction fn;
            fn.typeIndex = funcTypes[i];
            const uint32_t groups = u32();
            uint64_t total = 0;
            for (uint32_t g = 0; g < groups; ++g) {
                const uint32_t count = u32();
                const WasmValType type = valType();
                total += count;
                if (total > 50000) fail("Too many locals");
                fn.localTypes.insert(fn.localTypes.end(), count, type);
            }
            decodeBody(fn);
            if (p_ != bodyEnd) fail("Function body size mismatch");
            end_ = sectionEnd;
            m_.functions_.push_back(std::move(fn));
        }
    }

    void blockType(uint32_t& params, uint32_t& results) {
        if (p_ >= end_) fail("Unexpected end of function body");
        const uint8_t b = *p_;
        if (b == 0x40) {
            ++p_;
            params = results = 0;
        } else if (isValType(b)) {
            ++p_;
            params = 0;
            results = 1;
        } else {
            const int64_t idx = static_cast<int64_t>(leb(33, true));
            if (idx < 0 || static_cast<uint64_t>(idx) >= m_.types_.size()) fail("Block type index out of range");
            params = static_cast<uint32_t>(m_.types_[static_cast<size_t>(idx)].params.size());
            results = static_cast<uint32_t>(m_.types_[static_cast<size_t>(idx)].results.size());
        }
    }

    void decodeBody(WasmFunction& fn) {
        const uint32_t localCount = static_cast<uint32_t>(m_.types_[fn.typeIndex].params.size() + fn.localTypes.size());
        std::vector<uint32_t> open;  // block indices of the enclosing blocks
        std::vector<bool> isIf;
        auto checkDepth = [&](uint32_t depth) {
            if (depth > open.size()) fail("Branch depth out of range");
        };
        auto needMemory = [&] {
            if (!m_.hasMemory_) fail("Memory instruction without a memory");
        };
        auto memarg = [&](WasmInstr& in) {
            needMemory();
            const uint32_t align = u32();
            if (align & 0x40) fail("Multiple memories are not supported");
            in.b = u32();
        };

        for (;;) {
            const uint32_t pc = static_cast<uint32_t>(fn.code.size());
            WasmInstr in;
            const uint8_t op = u8();
            in.op = op;
            switch (op) {
                case 0x00: case 0x01: case 0x0F: case 0x1A: case 0x1B: case 0xD1:
                    break;
                case 0x02: case 0x03: case 0x04: {
                    WasmBlockInfo info;
                    blockType(info.params, info.results);
                    in.a = static_cast<uint32_t>(fn.blocks.size());
                    fn.blocks.push_back(info);
                    open.push_back(in.a);
                    isIf.push_back(op == 0x04);
                    break;
                }
                case 0x05:
                    if (open.empty() || !isIf.back() || fn.blocks[open.back()].elsePc != 0) fail("Misplaced else");
                    in.a = open.back();
                    fn.blocks[in.a].elsePc = pc;
                    break;
                case 0x0B:
                    if (open.empty()) {
                        in.a = kFunctionEnd;
                        fn.code.push_back(in);
                        return;
                    }
                    in.a = open.back();
                    fn.blocks[in.a].endPc = pc;
                    open.pop_back();
                    isIf.pop_back();
                    break;
                case 0x0C: case 0x0D:
                    in.a = u32();
                    checkDepth(in.a);
                    break;
                case 0x0E: {
                    const uint32_t count = u32();
                    if (count > 100000) fail("br_table too large");
                    in.a = static_cast<uint32_t>(fn.brTables.size());
                    fn.brTables.push_back(count);
                    for (uint32_t k = 0; k <= count; ++k) {
                        const uint32_t depth = u32();
                        checkDepth(depth);
                        fn.brTables.push_back(depth);
                    }
                    break;
                }
                case 0x10: case 0x12:
                    in.a = u32();  // range-checked once every function is known
                    break;
                case 0x11: case 0x13:
                    in.a = u32();
                    in.b = u32();
                    if (in.a >= m_.types_.size()) fail("call_indirect type out of range");
                    if (in.b >= m_.tables_.size()) fail("call_indirect table out of range");
                    break;
                case 0x1C: {
                    const uint32_t count = u32();
                    for (uint32_t k = 0; k < count; ++k) valType();
                    in.op = 0x1B;
                    break;
                }
                case 0x20: case 0x21: case 0x22:
                    in.a = u32();
                    if (in.a >= localCount) fail("Local index out of range");
                    break;
                case 0x23: case 0x24:
                    in.a = u32();
                    if (in.a >= m_.globals_.size()) fail("Global index out of range");
                    if (op == 0x24 && !m_.globals_[in.a].mutableValue) fail("Write to an immutable global");
                    break;
                case 0x25: case 0x26:
                    in.a = u32();
                    if (in.a >= m_.tables_.size()) fail("Table index out of range");
                    break;
                case 0x3F: case 0x40:
                    needMemory();
                    if (u8() != 0) fail("Multiple memories are not supported");
                    break;
                case 0x41: in.b = static_cast<uint32_t>(s32()); break;
                case 0x42: in.b = static_cast<uint64_t>(s64()); break;
                case 0x43: in.b = raw<uint32_t>(); break;
                case 0x44: in.b = raw<uint64_t>(); break;
                case 0xD0: u8(); break;
                case 0xD2: in.a = u32(); break;
                case 0xFC: {
                    const uint32_t sub = u32();
                    if (sub > 17) fail("Unsupported 0xFC opcode " + std::to_string(sub));
                    in.op = static_cast<uint16_t>(kPrefixFC + sub);
                    switch (sub) {
                        case 8:  // memory.init
                            needMemory();
                            in.a = u32();  // range-checked once the data section is read
                            if (u8() != 0) fail("Multiple memories are not supported");
                            break;
                        case 9: in.a = u32(); break;  // data.drop
                        case 10:                      // memory.copy
                            needMemory();
                            if (u8() != 0 || u8() != 0) fail("Multiple memories are not supported");
                            break;
                        case 11:  // memory.fill
                            needMemory();
                            if (u8() != 0) fail("Multiple memories are not supported");
                            break;
                        case 12:  // table.init elem table
                            in.a = u32();
                            in.b = u32();
                            if (in.b >= m_.tables_.size()) fail("Table index out of range");
                            break;
                        case 13: in.a = u32(); break;  // elem.drop
                        case 14:                       // table.copy dst src
                            in.a = u32();
                            in.b = u32();
                            if (in.a >= m_.tables_.size() || in.b >= m_.tables_.size()) fail("Table index out of range");
                            break;
                        case 15: case 16: case 17:
                            in.a = u32();
                            if (in.a >= m_.tables_.size()) fail("Table index out of range");
                            break;
                        default: break;  // 0..7: saturating truncations
                    }
                    break;
                }
                default:
                    if (op >= 0x28 && op <= 0x3E) {
                        memarg(in);
                    } else if (op >= 0x45 && op <= 0xC4) {
                        // numeric, no immediates
                    } else {
                        char hex[8];
                        std::snprintf(hex, sizeof hex, "0x%02X", op);
                        fail(std::string("Unsupported opcode ") + hex);
                    }
            }
            if (fn.code.size() >= 4'000'000) fail("Function body too large");
            fn.code.push_back(in);
        }
    }

    WasmModule& m_;
    const uint8_t* p_;
    const uint8_t* end_;
};

// -----------------------------------------------------------------------------
// WasmModule
// -----------------------------------------------------------------------------

const WasmExport* WasmModule::findExport(const std::string& name) const {
    for (const auto& exp : exports_) {
        if (exp.name == name) return &exp;
    }
    return nullptr;
}

const WasmFuncType& WasmModule::functionType(uint32_t funcIndex) const {
    if (funcIndex < importFuncTypes_.size()) {
        return types_[importFuncTypes_[funcIndex]];
    }
    return types_[functions_[funcIndex - importFuncTypes_.size()].typeIndex];
}

std::shared_ptr<WasmModule> WasmModule::loadFromBytes(const uint8_t* data, size_t size, std::string* outError) {
    if (!data || size < 8) {
        if (outError) *outError = "Data too small for Wasm header";
        return nullptr;
    }
    auto module = std::make_shared<WasmModule>();
    try {
        WasmModuleParser(*module, data, size).parse();
        // Calls were range-checked loosely while decoding (later bodies are not yet counted); check exactly now.
        for (const WasmFunction& fn : module->functions_) {
            for (const WasmInstr& in : fn.code) {
                if ((in.op == 0x10 || in.op == 0x12 || in.op == 0xD2) && in.a >= module->functionCount()) {
                    throw std::runtime_error("Function index out of range");
                }
                if ((in.op == kPrefixFC + 8 || in.op == kPrefixFC + 9) && in.a >= module->data_.size()) {
                    throw std::runtime_error("Data segment index out of range");
                }
                if ((in.op == kPrefixFC + 12 || in.op == kPrefixFC + 13) && in.a >= module->elements_.size()) {
                    throw std::runtime_error("Element segment index out of range");
                }
            }
        }
        for (const WasmElementSegment& seg : module->elements_) {
            for (const auto& item : seg.items) {
                for (const WasmInstr& in : item) {
                    if (in.op == 0xD2 && in.a >= module->functionCount()) {
                        throw std::runtime_error("Element refers to a missing function");
                    }
                }
            }
        }
    } catch (const std::exception& ex) {
        if (outError) *outError = ex.what();
        return nullptr;
    }
    return module;
}

std::shared_ptr<WasmModule> WasmModule::loadFromFile(const QString& filePath, std::string* outError) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        if (outError) *outError = "Cannot open Wasm file: " + filePath.toStdString();
        return nullptr;
    }
    const QByteArray bytes = f.readAll();
    return loadFromBytes(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size()), outError);
}

// -----------------------------------------------------------------------------
// WasmInstance
// -----------------------------------------------------------------------------

namespace {

inline uint32_t asU32(uint64_t v) { return static_cast<uint32_t>(v); }
inline int32_t asI32(uint64_t v) { return static_cast<int32_t>(static_cast<uint32_t>(v)); }
inline int64_t asI64(uint64_t v) { return static_cast<int64_t>(v); }
inline float asF32(uint64_t v) { return std::bit_cast<float>(static_cast<uint32_t>(v)); }
inline double asF64(uint64_t v) { return std::bit_cast<double>(v); }
inline uint64_t fromU32(uint32_t v) { return v; }
inline uint64_t fromI32(int32_t v) { return static_cast<uint32_t>(v); }
inline uint64_t fromI64(int64_t v) { return static_cast<uint64_t>(v); }
inline uint64_t fromF32(float v) { return std::bit_cast<uint32_t>(v); }
inline uint64_t fromF64(double v) { return std::bit_cast<uint64_t>(v); }
inline uint64_t fromBool(bool v) { return v ? 1 : 0; }

template <typename F>
F wasmMin(F a, F b) {
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<F>::quiet_NaN();
    if (a == 0 && b == 0) return std::signbit(a) ? a : b;
    return a < b ? a : b;
}

template <typename F>
F wasmMax(F a, F b) {
    if (std::isnan(a) || std::isnan(b)) return std::numeric_limits<F>::quiet_NaN();
    if (a == 0 && b == 0) return std::signbit(a) ? b : a;
    return a > b ? a : b;
}

// Range checks in double: exact for every f32 and for these bounds.
template <typename I>
bool truncInRange(double x) {
    if constexpr (std::is_same_v<I, int32_t>) return x > -2147483649.0 && x < 2147483648.0;
    if constexpr (std::is_same_v<I, uint32_t>) return x > -1.0 && x < 4294967296.0;
    if constexpr (std::is_same_v<I, int64_t>) return x >= -9223372036854775808.0 && x < 9223372036854775808.0;
    if constexpr (std::is_same_v<I, uint64_t>) return x > -1.0 && x < 18446744073709551616.0;
    return false;
}

template <typename I>
I truncSat(double x) {
    if (std::isnan(x)) return 0;
    if (truncInRange<I>(x)) return static_cast<I>(x);
    return x < 0 ? std::numeric_limits<I>::min() : std::numeric_limits<I>::max();
}

}  // namespace

WasmInstance::WasmInstance(std::shared_ptr<WasmModule> module) : module_(std::move(module)) {}

void WasmInstance::linkHostFunction(const std::string& moduleName, const std::string& fieldName, WasmHostFunc func) {
    hostFuncs_[{moduleName, fieldName}] = std::move(func);
}

bool WasmInstance::evalConst(const std::vector<WasmInstr>& expr, uint64_t& out) const {
    if (expr.size() != 1) return false;
    const WasmInstr& in = expr.front();
    switch (in.op) {
        case 0x41: case 0x42: case 0x43: case 0x44: out = in.b; return true;
        case 0x23:
            if (in.a >= globals_.size()) return false;
            out = globals_[in.a];
            return true;
        case 0xD0: out = 0; return true;
        case 0xD2: out = uint64_t(in.a) + 1; return true;
        default: return false;
    }
}

bool WasmInstance::instantiate(std::string* outError) {
    auto error = [&](const std::string& message) {
        if (outError) *outError = message;
        return false;
    };
    if (!module_) return error("No module loaded");
    if (instantiated_) return error("Already instantiated");

    linkedImportFuncs_.clear();
    for (const auto& imp : module_->imports()) {
        auto it = hostFuncs_.find({imp.moduleName, imp.fieldName});
        if (it == hostFuncs_.end()) it = hostFuncs_.find({"env", imp.fieldName});
        if (it == hostFuncs_.end()) it = hostFuncs_.find({"*", imp.fieldName});
        if (it == hostFuncs_.end()) return error("Unresolved host import: " + imp.moduleName + "." + imp.fieldName);
        linkedImportFuncs_.push_back(it->second);
    }

    if (module_->hasMemory()) {
        if (module_->initialMemoryPages() > maxMemoryPagesLimit_) {
            return error("Module needs more memory than plugins may use");
        }
        memory_ = WasmMemory(module_->initialMemoryPages(), std::min(module_->maxMemoryPages(), maxMemoryPagesLimit_));
    }

    tables_.clear();
    for (const WasmTableDef& def : module_->tables_) {
        if (def.initial > 1'000'000) return error("Table too large");
        Table t;
        t.elems.assign(def.initial, 0);
        t.max = def.hasMax ? def.max : 10'000'000;
        tables_.push_back(std::move(t));
    }

    globals_.clear();
    for (const WasmGlobalDef& def : module_->globals_) {
        uint64_t v = 0;
        if (!evalConst(def.init, v)) return error("Unsupported global initializer");
        globals_.push_back(v);
    }

    droppedElems_.assign(module_->elements_.size(), false);
    for (size_t i = 0; i < module_->elements_.size(); ++i) {
        const WasmElementSegment& seg = module_->elements_[i];
        if (seg.mode != WasmElementSegment::Mode::Active) {
            droppedElems_[i] = seg.mode == WasmElementSegment::Mode::Declarative;
            continue;
        }
        uint64_t offset = 0;
        if (!evalConst(seg.offset, offset)) return error("Unsupported element offset");
        Table& t = tables_[seg.tableIndex];
        if (uint64_t(asU32(offset)) + seg.items.size() > t.elems.size()) return error("Element segment out of table bounds");
        for (size_t k = 0; k < seg.items.size(); ++k) {
            uint64_t ref = 0;
            if (!evalConst(seg.items[k], ref)) return error("Unsupported element item");
            t.elems[asU32(offset) + k] = ref;
        }
        droppedElems_[i] = true;
    }

    droppedData_.assign(module_->data_.size(), false);
    for (size_t i = 0; i < module_->data_.size(); ++i) {
        const WasmDataSegment& seg = module_->data_[i];
        if (!seg.active) continue;
        uint64_t offset = 0;
        if (!evalConst(seg.offset, offset)) return error("Unsupported data offset");
        if (!memory_.write(asU32(offset), seg.data.data(), seg.data.size())) return error("Data segment out of memory bounds");
        droppedData_[i] = true;
    }

    instantiated_ = true;
    if (module_->start_) {
        std::vector<uint64_t> none;
        lastTrap_ = WasmTrap::None;
        customError_.clear();
        if (!run(*module_->start_, none)) return error("Start function trapped: " + lastErrorMessage());
    }
    return true;
}

bool WasmInstance::hasExport(const std::string& exportName) const {
    const WasmExport* exp = module_ ? module_->findExport(exportName) : nullptr;
    return exp && exp->kind == 0;
}

void WasmInstance::raiseHostTrap(const std::string& message) {
    hostTrapRaised_ = true;
    customError_ = message;
}

std::string WasmInstance::lastErrorMessage() const {
    if (!customError_.empty()) return customError_;
    return wasmTrapToString(lastTrap_);
}

WasmVal WasmInstance::toVal(WasmValType type, uint64_t bits) const {
    switch (type) {
        case WasmValType::I64: return asI64(bits);
        case WasmValType::F32: return asF32(bits);
        case WasmValType::F64: return asF64(bits);
        default: return asI32(bits);
    }
}

uint64_t WasmInstance::fromVal(WasmValType type, const WasmVal& val) {
    return std::visit(
        [type](auto v) -> uint64_t {
            switch (type) {
                case WasmValType::I64: return fromI64(static_cast<int64_t>(v));
                case WasmValType::F32: return fromF32(static_cast<float>(v));
                case WasmValType::F64: return fromF64(static_cast<double>(v));
                default: return fromI32(static_cast<int32_t>(v));
            }
        },
        val);
}

std::optional<WasmVal> WasmInstance::invoke(const std::string& exportName, const std::vector<WasmVal>& args) {
    lastTrap_ = WasmTrap::None;
    customError_.clear();
    if (!module_ || !instantiated_) {
        lastTrap_ = WasmTrap::UndefinedElement;
        customError_ = "Module not instantiated";
        return std::nullopt;
    }
    if (!frames_.empty()) {
        lastTrap_ = WasmTrap::HostError;
        customError_ = "Re-entrant call into the plugin";
        return std::nullopt;
    }
    const WasmExport* exp = module_->findExport(exportName);
    if (!exp || exp->kind != 0) {
        lastTrap_ = WasmTrap::UndefinedElement;
        customError_ = "Exported function '" + exportName + "' not found";
        return std::nullopt;
    }
    const WasmFuncType& type = module_->functionType(exp->index);
    if (args.size() != type.params.size()) {
        lastTrap_ = WasmTrap::TypeMismatch;
        customError_ = "Wrong number of arguments for '" + exportName + "'";
        return std::nullopt;
    }
    std::vector<uint64_t> values;
    for (size_t i = 0; i < args.size(); ++i) {
        values.push_back(fromVal(type.params[i], args[i]));
    }
    if (!run(exp->index, values)) {
        return std::nullopt;
    }
    if (type.results.empty()) {
        return std::nullopt;
    }
    return toVal(type.results.front(), values.front());
}

bool WasmInstance::callHost(uint32_t funcIndex) {
    const WasmFuncType& type = module_->functionType(funcIndex);
    const size_t n = type.params.size();
    std::vector<WasmVal> args;
    args.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        args.push_back(toVal(type.params[i], stack_[stack_.size() - n + i]));
    }
    stack_.resize(stack_.size() - n);
    hostTrapRaised_ = false;
    std::optional<WasmVal> result;
    try {
        result = linkedImportFuncs_[funcIndex](*this, args);
    } catch (const std::exception& ex) {
        raiseHostTrap(std::string("Host function threw: ") + ex.what());
    }
    if (hostTrapRaised_) {
        lastTrap_ = WasmTrap::HostError;
        return false;
    }
    if (!type.results.empty()) {
        stack_.push_back(result ? fromVal(type.results.front(), *result) : 0);
    }
    return true;
}

// The interpreter. Values live on stack_ as raw 64-bit patterns (i32 in the
// low half); each frame's locals sit at its base, operands above `floor`.
bool WasmInstance::run(uint32_t funcIndex, std::vector<uint64_t>& values) {
    const uint32_t importCount = module_->importedFunctionCount();
    fuel_ = fuelPerCall_;
    stack_.clear();
    frames_.clear();
    labels_.clear();
    stack_.insert(stack_.end(), values.begin(), values.end());

    struct Cleanup {
        WasmInstance& self;
        ~Cleanup() {
            self.frames_.clear();
            self.labels_.clear();
        }
    } cleanup{*this};

    if (funcIndex < importCount) {
        if (!callHost(funcIndex)) return false;
        values.assign(stack_.begin(), stack_.end());
        return true;
    }

    std::vector<uint64_t>& st = stack_;
    const WasmFunction* fn = nullptr;
    const WasmInstr* code = nullptr;
    Frame* fr = nullptr;
    uint32_t pc = 0;
    uint8_t* mem = memory_.rawData();
    uint64_t memSize = memory_.sizeBytes();

#define TRAP(t)                \
    do {                       \
        lastTrap_ = (t);       \
        return false;          \
    } while (0)
#define NEED(n)                                                              \
    do {                                                                     \
        if (st.size() < static_cast<size_t>(fr->floor) + (n)) TRAP(WasmTrap::StackUnderflow); \
    } while (0)
#define UN(expr)                  \
    {                             \
        NEED(1);                  \
        const uint64_t x = st.back(); \
        st.back() = (expr);       \
        break;                    \
    }
#define BIN(expr)                     \
    {                                 \
        NEED(2);                      \
        const uint64_t y = st.back(); \
        st.pop_back();                \
        const uint64_t x = st.back(); \
        st.back() = (expr);           \
        break;                        \
    }
#define LOAD(T, size, conv)                                          \
    {                                                                \
        NEED(1);                                                     \
        const uint64_t ea = uint64_t(asU32(st.back())) + in.b;       \
        if (ea + (size) > memSize) TRAP(WasmTrap::OutOfBoundsMemoryAccess); \
        T v;                                                         \
        std::memcpy(&v, mem + ea, sizeof(T));                        \
        st.back() = (conv);                                          \
        break;                                                       \
    }
#define STORE(T, conv)                                               \
    {                                                                \
        NEED(2);                                                     \
        const uint64_t x = st.back();                                \
        st.pop_back();                                               \
        const uint64_t ea = uint64_t(asU32(st.back())) + in.b;       \
        st.pop_back();                                               \
        if (ea + sizeof(T) > memSize) TRAP(WasmTrap::OutOfBoundsMemoryAccess); \
        const T v = (conv);                                          \
        std::memcpy(mem + ea, &v, sizeof(T));                        \
        break;                                                       \
    }
#define TRUNC(I, F, conv)                                            \
    {                                                                \
        NEED(1);                                                     \
        const double d = static_cast<double>(F(st.back()));          \
        if (std::isnan(d)) TRAP(WasmTrap::InvalidConversion);        \
        if (!truncInRange<I>(d)) TRAP(WasmTrap::IntegerOverflow);    \
        st.back() = conv(static_cast<I>(d));                         \
        break;                                                       \
    }
#define TRUNC_SAT(I, F, conv)                                        \
    {                                                                \
        NEED(1);                                                     \
        st.back() = conv(truncSat<I>(static_cast<double>(F(st.back())))); \
        break;                                                       \
    }

    // Enters an internal function whose arguments are on the stack.
    auto enter = [&](uint32_t index, bool tail) -> bool {
        const WasmFunction& callee = module_->functions_[index - importCount];
        const size_t params = module_->types_[callee.typeIndex].params.size();
        if (frames_.size() >= maxCallDepth_) {
            lastTrap_ = WasmTrap::CallStackExhausted;
            return false;
        }
        if (st.size() < params || (fr && st.size() < static_cast<size_t>(fr->floor) + params)) {
            lastTrap_ = WasmTrap::StackUnderflow;
            return false;
        }
        if (st.size() + callee.localTypes.size() + 1024 > MaxStackSlots) {
            lastTrap_ = WasmTrap::CallStackExhausted;
            return false;
        }
        if (fr && !tail) fr->pc = pc + 1;
        Frame f;
        f.funcIndex = index - importCount;
        f.localsBase = static_cast<uint32_t>(st.size() - params);
        st.insert(st.end(), callee.localTypes.size(), 0);
        f.floor = static_cast<uint32_t>(st.size());
        f.labelBase = static_cast<uint32_t>(labels_.size());
        frames_.push_back(f);
        fr = &frames_.back();
        fn = &callee;
        code = fn->code.data();
        pc = 0;
        return true;
    };

    if (!enter(funcIndex, false)) return false;

    for (;;) {
        if (fuel_ == 0) TRAP(WasmTrap::OutOfFuel);
        --fuel_;
        const WasmInstr& in = code[pc];
        switch (in.op) {
            case 0x00: TRAP(WasmTrap::Unreachable);
            case 0x01: break;
            case 0x02: {
                const WasmBlockInfo& bi = fn->blocks[in.a];
                NEED(bi.params);
                labels_.push_back({bi.endPc + 1, static_cast<uint32_t>(st.size() - bi.params), bi.results, false});
                break;
            }
            case 0x03: {
                const WasmBlockInfo& bi = fn->blocks[in.a];
                NEED(bi.params);
                labels_.push_back({pc + 1, static_cast<uint32_t>(st.size() - bi.params), bi.params, true});
                break;
            }
            case 0x04: {
                const WasmBlockInfo& bi = fn->blocks[in.a];
                NEED(1 + bi.params);
                const uint32_t cond = asU32(st.back());
                st.pop_back();
                if (cond) {
                    labels_.push_back({bi.endPc + 1, static_cast<uint32_t>(st.size() - bi.params), bi.results, false});
                } else if (bi.elsePc) {
                    labels_.push_back({bi.endPc + 1, static_cast<uint32_t>(st.size() - bi.params), bi.results, false});
                    pc = bi.elsePc;
                } else {
                    pc = bi.endPc;  // no else: skip past the end without a label
                }
                break;
            }
            case 0x05:
                // End of the then-arm: continue at the end instruction, which pops the label.
                pc = fn->blocks[in.a].endPc - 1;
                break;
            case 0x0B:
                if (in.a != kFunctionEnd) {
                    if (labels_.size() <= fr->labelBase) TRAP(WasmTrap::StackUnderflow);
                    labels_.pop_back();
                    break;
                }
                goto doReturn;
            case 0x0C: case 0x0D: case 0x0E: {
                uint32_t depth = in.a;
                if (in.op == 0x0D) {
                    NEED(1);
                    const uint32_t cond = asU32(st.back());
                    st.pop_back();
                    if (!cond) break;
                } else if (in.op == 0x0E) {
                    NEED(1);
                    const uint32_t i = asU32(st.back());
                    st.pop_back();
                    const uint32_t* table = fn->brTables.data() + in.a;
                    const uint32_t count = table[0];
                    depth = table[1 + std::min(i, count)];
                }
                const size_t inFrame = labels_.size() - fr->labelBase;
                if (depth >= inFrame) goto doReturn;
                const Label target = labels_[labels_.size() - 1 - depth];
                if (target.height < fr->floor || st.size() < static_cast<size_t>(target.height) + target.arity) {
                    TRAP(WasmTrap::StackUnderflow);
                }
                std::copy(st.end() - target.arity, st.end(), st.begin() + target.height);
                st.resize(static_cast<size_t>(target.height) + target.arity);
                labels_.resize(labels_.size() - depth - (target.loop ? 0 : 1));
                pc = target.contPc;
                continue;
            }
            case 0x0F:
            doReturn: {
                const size_t arity = module_->types_[fn->typeIndex].results.size();
                NEED(arity);
                std::copy(st.end() - static_cast<std::ptrdiff_t>(arity), st.end(), st.begin() + fr->localsBase);
                st.resize(fr->localsBase + arity);
                labels_.resize(fr->labelBase);
                frames_.pop_back();
                if (frames_.empty()) {
                    values.assign(st.begin(), st.end());
                    return true;
                }
                fr = &frames_.back();
                fn = &module_->functions_[fr->funcIndex];
                code = fn->code.data();
                pc = fr->pc;
                continue;
            }
            case 0x10: case 0x11: case 0x12: case 0x13: {
                const bool tail = in.op == 0x12 || in.op == 0x13;
                uint32_t target = in.a;
                if (in.op == 0x11 || in.op == 0x13) {
                    NEED(1);
                    const uint32_t i = asU32(st.back());
                    st.pop_back();
                    const Table& t = tables_[in.b];
                    if (i >= t.elems.size()) TRAP(WasmTrap::UndefinedElement);
                    const uint64_t ref = t.elems[i];
                    if (ref == 0) TRAP(WasmTrap::UndefinedElement);
                    target = static_cast<uint32_t>(ref - 1);
                    if (!(module_->functionType(target) == module_->types_[in.a])) TRAP(WasmTrap::TypeMismatch);
                }
                if (target < importCount) {
                    NEED(module_->functionType(target).params.size());
                    if (!callHost(target)) return false;
                    mem = memory_.rawData();
                    memSize = memory_.sizeBytes();
                    if (tail) goto doReturn;
                    break;
                }
                if (tail) {
                    // return_call: the callee replaces this frame, taking over its locals' place.
                    const size_t params = module_->functionType(target).params.size();
                    NEED(params);
                    std::copy(st.end() - static_cast<std::ptrdiff_t>(params), st.end(), st.begin() + fr->localsBase);
                    st.resize(fr->localsBase + params);
                    labels_.resize(fr->labelBase);
                    frames_.pop_back();
                    fr = frames_.empty() ? nullptr : &frames_.back();
                }
                if (!enter(target, tail)) return false;
                continue;
            }
            case 0x1A: NEED(1); st.pop_back(); break;
            case 0x1B: {
                NEED(3);
                const uint32_t cond = asU32(st.back());
                st.pop_back();
                const uint64_t second = st.back();
                st.pop_back();
                if (!cond) st.back() = second;
                break;
            }
            case 0x20: st.push_back(st[fr->localsBase + in.a]); break;
            case 0x21: NEED(1); st[fr->localsBase + in.a] = st.back(); st.pop_back(); break;
            case 0x22: NEED(1); st[fr->localsBase + in.a] = st.back(); break;
            case 0x23: st.push_back(globals_[in.a]); break;
            case 0x24: NEED(1); globals_[in.a] = st.back(); st.pop_back(); break;
            case 0x25: {
                NEED(1);
                const uint32_t i = asU32(st.back());
                if (i >= tables_[in.a].elems.size()) TRAP(WasmTrap::OutOfBoundsTableAccess);
                st.back() = tables_[in.a].elems[i];
                break;
            }
            case 0x26: {
                NEED(2);
                const uint64_t ref = st.back();
                st.pop_back();
                const uint32_t i = asU32(st.back());
                st.pop_back();
                if (i >= tables_[in.a].elems.size()) TRAP(WasmTrap::OutOfBoundsTableAccess);
                tables_[in.a].elems[i] = ref;
                break;
            }

            case 0x28: LOAD(uint32_t, 4, fromU32(v))
            case 0x29: LOAD(uint64_t, 8, v)
            case 0x2A: LOAD(uint32_t, 4, fromU32(v))
            case 0x2B: LOAD(uint64_t, 8, v)
            case 0x2C: LOAD(int8_t, 1, fromI32(v))
            case 0x2D: LOAD(uint8_t, 1, fromU32(v))
            case 0x2E: LOAD(int16_t, 2, fromI32(v))
            case 0x2F: LOAD(uint16_t, 2, fromU32(v))
            case 0x30: LOAD(int8_t, 1, fromI64(v))
            case 0x31: LOAD(uint8_t, 1, uint64_t(v))
            case 0x32: LOAD(int16_t, 2, fromI64(v))
            case 0x33: LOAD(uint16_t, 2, uint64_t(v))
            case 0x34: LOAD(int32_t, 4, fromI64(v))
            case 0x35: LOAD(uint32_t, 4, uint64_t(v))
            case 0x36: STORE(uint32_t, asU32(x))
            case 0x37: STORE(uint64_t, x)
            case 0x38: STORE(uint32_t, asU32(x))
            case 0x39: STORE(uint64_t, x)
            case 0x3A: STORE(uint8_t, static_cast<uint8_t>(x))
            case 0x3B: STORE(uint16_t, static_cast<uint16_t>(x))
            case 0x3C: STORE(uint8_t, static_cast<uint8_t>(x))
            case 0x3D: STORE(uint16_t, static_cast<uint16_t>(x))
            case 0x3E: STORE(uint32_t, asU32(x))
            case 0x3F: st.push_back(memory_.sizePages()); break;
            case 0x40: {
                NEED(1);
                const uint32_t before = memory_.sizePages();
                st.back() = memory_.grow(asU32(st.back())) ? fromU32(before) : fromI32(-1);
                mem = memory_.rawData();
                memSize = memory_.sizeBytes();
                break;
            }

            case 0x41: case 0x42: case 0x43: case 0x44: st.push_back(in.b); break;

            case 0x45: UN(fromBool(asU32(x) == 0))
            case 0x46: BIN(fromBool(asU32(x) == asU32(y)))
            case 0x47: BIN(fromBool(asU32(x) != asU32(y)))
            case 0x48: BIN(fromBool(asI32(x) < asI32(y)))
            case 0x49: BIN(fromBool(asU32(x) < asU32(y)))
            case 0x4A: BIN(fromBool(asI32(x) > asI32(y)))
            case 0x4B: BIN(fromBool(asU32(x) > asU32(y)))
            case 0x4C: BIN(fromBool(asI32(x) <= asI32(y)))
            case 0x4D: BIN(fromBool(asU32(x) <= asU32(y)))
            case 0x4E: BIN(fromBool(asI32(x) >= asI32(y)))
            case 0x4F: BIN(fromBool(asU32(x) >= asU32(y)))
            case 0x50: UN(fromBool(x == 0))
            case 0x51: BIN(fromBool(x == y))
            case 0x52: BIN(fromBool(x != y))
            case 0x53: BIN(fromBool(asI64(x) < asI64(y)))
            case 0x54: BIN(fromBool(x < y))
            case 0x55: BIN(fromBool(asI64(x) > asI64(y)))
            case 0x56: BIN(fromBool(x > y))
            case 0x57: BIN(fromBool(asI64(x) <= asI64(y)))
            case 0x58: BIN(fromBool(x <= y))
            case 0x59: BIN(fromBool(asI64(x) >= asI64(y)))
            case 0x5A: BIN(fromBool(x >= y))
            case 0x5B: BIN(fromBool(asF32(x) == asF32(y)))
            case 0x5C: BIN(fromBool(asF32(x) != asF32(y)))
            case 0x5D: BIN(fromBool(asF32(x) < asF32(y)))
            case 0x5E: BIN(fromBool(asF32(x) > asF32(y)))
            case 0x5F: BIN(fromBool(asF32(x) <= asF32(y)))
            case 0x60: BIN(fromBool(asF32(x) >= asF32(y)))
            case 0x61: BIN(fromBool(asF64(x) == asF64(y)))
            case 0x62: BIN(fromBool(asF64(x) != asF64(y)))
            case 0x63: BIN(fromBool(asF64(x) < asF64(y)))
            case 0x64: BIN(fromBool(asF64(x) > asF64(y)))
            case 0x65: BIN(fromBool(asF64(x) <= asF64(y)))
            case 0x66: BIN(fromBool(asF64(x) >= asF64(y)))

            case 0x67: UN(fromU32(static_cast<uint32_t>(std::countl_zero(asU32(x)))))
            case 0x68: UN(fromU32(static_cast<uint32_t>(std::countr_zero(asU32(x)))))
            case 0x69: UN(fromU32(static_cast<uint32_t>(std::popcount(asU32(x)))))
            case 0x6A: BIN(fromU32(asU32(x) + asU32(y)))
            case 0x6B: BIN(fromU32(asU32(x) - asU32(y)))
            case 0x6C: BIN(fromU32(asU32(x) * asU32(y)))
            case 0x6D: {
                NEED(2);
                const int32_t d = asI32(st.back());
                st.pop_back();
                const int32_t n = asI32(st.back());
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                if (n == std::numeric_limits<int32_t>::min() && d == -1) TRAP(WasmTrap::IntegerOverflow);
                st.back() = fromI32(n / d);
                break;
            }
            case 0x6E: {
                NEED(2);
                const uint32_t d = asU32(st.back());
                st.pop_back();
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                st.back() = fromU32(asU32(st.back()) / d);
                break;
            }
            case 0x6F: {
                NEED(2);
                const int32_t d = asI32(st.back());
                st.pop_back();
                const int32_t n = asI32(st.back());
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                st.back() = (d == -1) ? 0 : fromI32(n % d);
                break;
            }
            case 0x70: {
                NEED(2);
                const uint32_t d = asU32(st.back());
                st.pop_back();
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                st.back() = fromU32(asU32(st.back()) % d);
                break;
            }
            case 0x71: BIN(fromU32(asU32(x) & asU32(y)))
            case 0x72: BIN(fromU32(asU32(x) | asU32(y)))
            case 0x73: BIN(fromU32(asU32(x) ^ asU32(y)))
            case 0x74: BIN(fromU32(asU32(x) << (asU32(y) & 31)))
            case 0x75: BIN(fromI32(asI32(x) >> (asU32(y) & 31)))
            case 0x76: BIN(fromU32(asU32(x) >> (asU32(y) & 31)))
            case 0x77: BIN(fromU32(std::rotl(asU32(x), static_cast<int>(asU32(y) & 31))))
            case 0x78: BIN(fromU32(std::rotr(asU32(x), static_cast<int>(asU32(y) & 31))))

            case 0x79: UN(static_cast<uint64_t>(std::countl_zero(x)))
            case 0x7A: UN(static_cast<uint64_t>(std::countr_zero(x)))
            case 0x7B: UN(static_cast<uint64_t>(std::popcount(x)))
            case 0x7C: BIN(x + y)
            case 0x7D: BIN(x - y)
            case 0x7E: BIN(x * y)
            case 0x7F: {
                NEED(2);
                const int64_t d = asI64(st.back());
                st.pop_back();
                const int64_t n = asI64(st.back());
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                if (n == std::numeric_limits<int64_t>::min() && d == -1) TRAP(WasmTrap::IntegerOverflow);
                st.back() = fromI64(n / d);
                break;
            }
            case 0x80: {
                NEED(2);
                const uint64_t d = st.back();
                st.pop_back();
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                st.back() = st.back() / d;
                break;
            }
            case 0x81: {
                NEED(2);
                const int64_t d = asI64(st.back());
                st.pop_back();
                const int64_t n = asI64(st.back());
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                st.back() = (d == -1) ? 0 : fromI64(n % d);
                break;
            }
            case 0x82: {
                NEED(2);
                const uint64_t d = st.back();
                st.pop_back();
                if (d == 0) TRAP(WasmTrap::DivisionByZero);
                st.back() = st.back() % d;
                break;
            }
            case 0x83: BIN(x & y)
            case 0x84: BIN(x | y)
            case 0x85: BIN(x ^ y)
            case 0x86: BIN(x << (y & 63))
            case 0x87: BIN(fromI64(asI64(x) >> (y & 63)))
            case 0x88: BIN(x >> (y & 63))
            case 0x89: BIN(std::rotl(x, static_cast<int>(y & 63)))
            case 0x8A: BIN(std::rotr(x, static_cast<int>(y & 63)))

            case 0x8B: UN(fromU32(asU32(x) & 0x7FFFFFFFu))
            case 0x8C: UN(fromU32(asU32(x) ^ 0x80000000u))
            case 0x8D: UN(fromF32(std::ceil(asF32(x))))
            case 0x8E: UN(fromF32(std::floor(asF32(x))))
            case 0x8F: UN(fromF32(std::trunc(asF32(x))))
            case 0x90: UN(fromF32(std::nearbyint(asF32(x))))
            case 0x91: UN(fromF32(std::sqrt(asF32(x))))
            case 0x92: BIN(fromF32(asF32(x) + asF32(y)))
            case 0x93: BIN(fromF32(asF32(x) - asF32(y)))
            case 0x94: BIN(fromF32(asF32(x) * asF32(y)))
            case 0x95: BIN(fromF32(asF32(x) / asF32(y)))
            case 0x96: BIN(fromF32(wasmMin(asF32(x), asF32(y))))
            case 0x97: BIN(fromF32(wasmMax(asF32(x), asF32(y))))
            case 0x98: BIN(fromU32((asU32(x) & 0x7FFFFFFFu) | (asU32(y) & 0x80000000u)))
            case 0x99: UN(x & 0x7FFFFFFFFFFFFFFFull)
            case 0x9A: UN(x ^ 0x8000000000000000ull)
            case 0x9B: UN(fromF64(std::ceil(asF64(x))))
            case 0x9C: UN(fromF64(std::floor(asF64(x))))
            case 0x9D: UN(fromF64(std::trunc(asF64(x))))
            case 0x9E: UN(fromF64(std::nearbyint(asF64(x))))
            case 0x9F: UN(fromF64(std::sqrt(asF64(x))))
            case 0xA0: BIN(fromF64(asF64(x) + asF64(y)))
            case 0xA1: BIN(fromF64(asF64(x) - asF64(y)))
            case 0xA2: BIN(fromF64(asF64(x) * asF64(y)))
            case 0xA3: BIN(fromF64(asF64(x) / asF64(y)))
            case 0xA4: BIN(fromF64(wasmMin(asF64(x), asF64(y))))
            case 0xA5: BIN(fromF64(wasmMax(asF64(x), asF64(y))))
            case 0xA6: BIN((x & 0x7FFFFFFFFFFFFFFFull) | (y & 0x8000000000000000ull))

            case 0xA7: UN(fromU32(asU32(x)))
            case 0xA8: TRUNC(int32_t, asF32, fromI32)
            case 0xA9: TRUNC(uint32_t, asF32, fromU32)
            case 0xAA: TRUNC(int32_t, asF64, fromI32)
            case 0xAB: TRUNC(uint32_t, asF64, fromU32)
            case 0xAC: UN(fromI64(asI32(x)))
            case 0xAD: UN(uint64_t(asU32(x)))
            case 0xAE: TRUNC(int64_t, asF32, fromI64)
            case 0xAF: TRUNC(uint64_t, asF32, uint64_t)
            case 0xB0: TRUNC(int64_t, asF64, fromI64)
            case 0xB1: TRUNC(uint64_t, asF64, uint64_t)
            case 0xB2: UN(fromF32(static_cast<float>(asI32(x))))
            case 0xB3: UN(fromF32(static_cast<float>(asU32(x))))
            case 0xB4: UN(fromF32(static_cast<float>(asI64(x))))
            case 0xB5: UN(fromF32(static_cast<float>(x)))
            case 0xB6: UN(fromF32(static_cast<float>(asF64(x))))
            case 0xB7: UN(fromF64(static_cast<double>(asI32(x))))
            case 0xB8: UN(fromF64(static_cast<double>(asU32(x))))
            case 0xB9: UN(fromF64(static_cast<double>(asI64(x))))
            case 0xBA: UN(fromF64(static_cast<double>(x)))
            case 0xBB: UN(fromF64(static_cast<double>(asF32(x))))
            case 0xBC: UN(fromU32(asU32(x)))  // i32.reinterpret_f32: same bits
            case 0xBD: break;                 // i64.reinterpret_f64
            case 0xBE: UN(fromU32(asU32(x)))  // f32.reinterpret_i32
            case 0xBF: break;                 // f64.reinterpret_i64

            case 0xC0: UN(fromI32(static_cast<int8_t>(x)))
            case 0xC1: UN(fromI32(static_cast<int16_t>(x)))
            case 0xC2: UN(fromI64(static_cast<int8_t>(x)))
            case 0xC3: UN(fromI64(static_cast<int16_t>(x)))
            case 0xC4: UN(fromI64(static_cast<int32_t>(x)))

            case 0xD0: st.push_back(0); break;
            case 0xD1: UN(fromBool(x == 0))
            case 0xD2: st.push_back(uint64_t(in.a) + 1); break;

            case kPrefixFC + 0: TRUNC_SAT(int32_t, asF32, fromI32)
            case kPrefixFC + 1: TRUNC_SAT(uint32_t, asF32, fromU32)
            case kPrefixFC + 2: TRUNC_SAT(int32_t, asF64, fromI32)
            case kPrefixFC + 3: TRUNC_SAT(uint32_t, asF64, fromU32)
            case kPrefixFC + 4: TRUNC_SAT(int64_t, asF32, fromI64)
            case kPrefixFC + 5: TRUNC_SAT(uint64_t, asF32, uint64_t)
            case kPrefixFC + 6: TRUNC_SAT(int64_t, asF64, fromI64)
            case kPrefixFC + 7: TRUNC_SAT(uint64_t, asF64, uint64_t)
            case kPrefixFC + 8: {  // memory.init
                NEED(3);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint32_t src = asU32(st.back());
                st.pop_back();
                const uint32_t dst = asU32(st.back());
                st.pop_back();
                const std::vector<uint8_t>& seg = module_->data_[in.a].data;
                const uint64_t segSize = droppedData_[in.a] ? 0 : seg.size();
                if (uint64_t(src) + n > segSize || uint64_t(dst) + n > memSize) TRAP(WasmTrap::OutOfBoundsMemoryAccess);
                if (n) std::memcpy(mem + dst, seg.data() + src, n);
                break;
            }
            case kPrefixFC + 9: droppedData_[in.a] = true; break;
            case kPrefixFC + 10: {  // memory.copy
                NEED(3);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint32_t src = asU32(st.back());
                st.pop_back();
                const uint32_t dst = asU32(st.back());
                st.pop_back();
                if (uint64_t(src) + n > memSize || uint64_t(dst) + n > memSize) TRAP(WasmTrap::OutOfBoundsMemoryAccess);
                if (n) std::memmove(mem + dst, mem + src, n);
                break;
            }
            case kPrefixFC + 11: {  // memory.fill
                NEED(3);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint8_t value = static_cast<uint8_t>(st.back());
                st.pop_back();
                const uint32_t dst = asU32(st.back());
                st.pop_back();
                if (uint64_t(dst) + n > memSize) TRAP(WasmTrap::OutOfBoundsMemoryAccess);
                if (n) std::memset(mem + dst, value, n);
                break;
            }
            case kPrefixFC + 12: {  // table.init
                NEED(3);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint32_t src = asU32(st.back());
                st.pop_back();
                const uint32_t dst = asU32(st.back());
                st.pop_back();
                const WasmElementSegment& seg = module_->elements_[in.a];
                const uint64_t segSize = droppedElems_[in.a] ? 0 : seg.items.size();
                Table& t = tables_[in.b];
                if (uint64_t(src) + n > segSize || uint64_t(dst) + n > t.elems.size()) TRAP(WasmTrap::OutOfBoundsTableAccess);
                for (uint32_t k = 0; k < n; ++k) {
                    uint64_t ref = 0;
                    if (!evalConst(seg.items[src + k], ref)) TRAP(WasmTrap::InvalidOpcode);
                    t.elems[dst + k] = ref;
                }
                break;
            }
            case kPrefixFC + 13: droppedElems_[in.a] = true; break;
            case kPrefixFC + 14: {  // table.copy
                NEED(3);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint32_t src = asU32(st.back());
                st.pop_back();
                const uint32_t dst = asU32(st.back());
                st.pop_back();
                Table& d = tables_[in.a];
                const Table& s = tables_[in.b];
                if (uint64_t(src) + n > s.elems.size() || uint64_t(dst) + n > d.elems.size()) TRAP(WasmTrap::OutOfBoundsTableAccess);
                std::vector<uint64_t> tmp(s.elems.begin() + src, s.elems.begin() + src + n);
                std::copy(tmp.begin(), tmp.end(), d.elems.begin() + dst);
                break;
            }
            case kPrefixFC + 15: {  // table.grow
                NEED(2);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint64_t init = st.back();
                Table& t = tables_[in.a];
                const uint64_t before = t.elems.size();
                if (before + n > t.max || before + n > 10'000'000) {
                    st.back() = fromI32(-1);
                } else {
                    t.elems.resize(static_cast<size_t>(before + n), init);
                    st.back() = before;
                }
                break;
            }
            case kPrefixFC + 16: st.push_back(tables_[in.a].elems.size()); break;
            case kPrefixFC + 17: {  // table.fill
                NEED(3);
                const uint32_t n = asU32(st.back());
                st.pop_back();
                const uint64_t ref = st.back();
                st.pop_back();
                const uint32_t i = asU32(st.back());
                st.pop_back();
                Table& t = tables_[in.a];
                if (uint64_t(i) + n > t.elems.size()) TRAP(WasmTrap::OutOfBoundsTableAccess);
                std::fill(t.elems.begin() + i, t.elems.begin() + i + n, ref);
                break;
            }
            default: TRAP(WasmTrap::InvalidOpcode);
        }
        ++pc;
    }

#undef TRAP
#undef NEED
#undef UN
#undef BIN
#undef LOAD
#undef STORE
#undef TRUNC
#undef TRUNC_SAT
}

}  // namespace gs::app
