#include "wasm_engine.hpp"

#include <QFile>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <stdexcept>

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
        default: return "Unknown trap";
    }
}

// -----------------------------------------------------------------------------
// WasmMemory
// -----------------------------------------------------------------------------

WasmMemory::WasmMemory(uint32_t initialPages, uint32_t maxPages)
    : maxPages_(std::max(initialPages, maxPages)) {
    data_.resize(static_cast<size_t>(initialPages) * PageSize, 0);
}

bool WasmMemory::grow(uint32_t deltaPages) {
    const uint32_t current = sizePages();
    if (current + deltaPages > maxPages_) {
        return false;
    }
    data_.resize(static_cast<size_t>(current + deltaPages) * PageSize, 0);
    return true;
}

bool WasmMemory::read(uint32_t offset, void* dst, size_t size) const {
    if (!dst || offset + size > data_.size() || (offset + size < offset)) {
        return false;
    }
    std::memcpy(dst, data_.data() + offset, size);
    return true;
}

bool WasmMemory::write(uint32_t offset, const void* src, size_t size) {
    if (!src || offset + size > data_.size() || (offset + size < offset)) {
        return false;
    }
    std::memcpy(data_.data() + offset, src, size);
    return true;
}

std::string WasmMemory::readString(uint32_t offset, size_t maxLen) const {
    if (offset >= data_.size()) return {};
    const size_t available = data_.size() - offset;
    const size_t limit = std::min(available, maxLen);
    const char* start = reinterpret_cast<const char*>(data_.data() + offset);
    size_t len = 0;
    while (len < limit && start[len] != '\0') {
        len++;
    }
    return std::string(start, len);
}

bool WasmMemory::writeString(uint32_t offset, const std::string& str, size_t maxLen) {
    const size_t len = std::min(str.size(), maxLen - 1);
    if (offset + len + 1 > data_.size()) {
        return false;
    }
    std::memcpy(data_.data() + offset, str.data(), len);
    data_[offset + len] = '\0';
    return true;
}

// -----------------------------------------------------------------------------
// LEB128 Helpers
// -----------------------------------------------------------------------------

namespace {

uint32_t readVarUint32(const uint8_t*& p, const uint8_t* end) {
    uint32_t res = 0;
    uint32_t shift = 0;
    while (p < end) {
        uint8_t b = *p++;
        res |= static_cast<uint32_t>(b & 0x7F) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
        if (shift >= 35) throw std::runtime_error("VarUint32 overflow");
    }
    return res;
}

int32_t readVarInt32(const uint8_t*& p, const uint8_t* end) {
    int32_t res = 0;
    uint32_t shift = 0;
    uint8_t b = 0;
    while (p < end) {
        b = *p++;
        res |= static_cast<int32_t>(b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
        if (shift >= 35) throw std::runtime_error("VarInt32 overflow");
    }
    if (shift < 32 && (b & 0x40) != 0) {
        res |= (~0U << shift);
    }
    return res;
}

int64_t readVarInt64(const uint8_t*& p, const uint8_t* end) {
    int64_t res = 0;
    uint32_t shift = 0;
    uint8_t b = 0;
    while (p < end) {
        b = *p++;
        res |= static_cast<int64_t>(b & 0x7F) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
        if (shift >= 70) throw std::runtime_error("VarInt64 overflow");
    }
    if (shift < 64 && (b & 0x40) != 0) {
        res |= (~0ULL << shift);
    }
    return res;
}

std::string readWasmString(const uint8_t*& p, const uint8_t* end) {
    const uint32_t len = readVarUint32(p, end);
    if (p + len > end) throw std::runtime_error("String exceeds section bounds");
    std::string s(reinterpret_cast<const char*>(p), len);
    p += len;
    return s;
}

}  // namespace

// -----------------------------------------------------------------------------
// WasmModule
// -----------------------------------------------------------------------------

const WasmExport* WasmModule::findExport(const std::string& name) const {
    for (const auto& exp : exports_) {
        if (exp.name == name) return &exp;
    }
    return nullptr;
}

std::shared_ptr<WasmModule> WasmModule::loadFromBytes(const uint8_t* data, size_t size, std::string* outError) {
    if (!data || size < 8) {
        if (outError) *outError = "Data too small for Wasm header";
        return nullptr;
    }

    // Verify magic 0x00 0x61 0x73 0x6D (\0asm)
    if (data[0] != 0x00 || data[1] != 0x61 || data[2] != 0x73 || data[3] != 0x6D) {
        if (outError) *outError = "Invalid Wasm magic header";
        return nullptr;
    }
    // Verify version 1
    if (data[4] != 0x01 || data[5] != 0x00 || data[6] != 0x00 || data[7] != 0x00) {
        if (outError) *outError = "Unsupported Wasm version";
        return nullptr;
    }

    auto module = std::make_shared<WasmModule>();
    const uint8_t* p = data + 8;
    const uint8_t* end = data + size;

    try {
        while (p < end) {
            const uint8_t sectionId = *p++;
            const uint32_t sectionLen = readVarUint32(p, end);
            const uint8_t* secEnd = p + sectionLen;
            if (secEnd > end) {
                if (outError) *outError = "Section length exceeds payload";
                return nullptr;
            }

            switch (sectionId) {
                case 1: { // Type section
                    const uint32_t count = readVarUint32(p, secEnd);
                    module->types_.reserve(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        const uint8_t form = *p++;
                        if (form != 0x60) throw std::runtime_error("Invalid func type form");
                        const uint32_t paramCount = readVarUint32(p, secEnd);
                        WasmFuncType ft;
                        for (uint32_t j = 0; j < paramCount; ++j) {
                            ft.params.push_back(static_cast<WasmValType>(*p++));
                        }
                        const uint32_t resCount = readVarUint32(p, secEnd);
                        for (uint32_t j = 0; j < resCount; ++j) {
                            ft.results.push_back(static_cast<WasmValType>(*p++));
                        }
                        module->types_.push_back(std::move(ft));
                    }
                    break;
                }
                case 2: { // Import section
                    const uint32_t count = readVarUint32(p, secEnd);
                    for (uint32_t i = 0; i < count; ++i) {
                        WasmImport imp;
                        imp.moduleName = readWasmString(p, secEnd);
                        imp.fieldName = readWasmString(p, secEnd);
                        imp.kind = *p++;
                        if (imp.kind == 0) { // function
                            imp.typeIndex = readVarUint32(p, secEnd);
                        } else if (imp.kind == 2) { // memory
                            const uint8_t flags = *p++;
                            uint32_t initPages = readVarUint32(p, secEnd);
                            uint32_t maxPages = (flags & 1) ? readVarUint32(p, secEnd) : 256;
                            module->initialMemoryPages_ = initPages;
                            module->maxMemoryPages_ = maxPages;
                        } else {
                            // Table or global import: skip payload
                            readVarUint32(p, secEnd);
                        }
                        module->imports_.push_back(std::move(imp));
                    }
                    break;
                }
                case 3: { // Function section
                    const uint32_t count = readVarUint32(p, secEnd);
                    module->funcTypeIndices_.reserve(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        module->funcTypeIndices_.push_back(readVarUint32(p, secEnd));
                    }
                    break;
                }
                case 5: { // Memory section
                    const uint32_t count = readVarUint32(p, secEnd);
                    for (uint32_t i = 0; i < count; ++i) {
                        const uint8_t flags = *p++;
                        module->initialMemoryPages_ = readVarUint32(p, secEnd);
                        if (flags & 1) {
                            module->maxMemoryPages_ = readVarUint32(p, secEnd);
                        }
                    }
                    break;
                }
                case 7: { // Export section
                    const uint32_t count = readVarUint32(p, secEnd);
                    for (uint32_t i = 0; i < count; ++i) {
                        WasmExport exp;
                        exp.name = readWasmString(p, secEnd);
                        exp.kind = *p++;
                        exp.index = readVarUint32(p, secEnd);
                        module->exports_.push_back(std::move(exp));
                    }
                    break;
                }
                case 10: { // Code section
                    const uint32_t count = readVarUint32(p, secEnd);
                    module->code_.reserve(count);
                    for (uint32_t i = 0; i < count; ++i) {
                        const uint32_t bodySize = readVarUint32(p, secEnd);
                        const uint8_t* bodyEnd = p + bodySize;
                        const uint32_t localCount = readVarUint32(p, bodyEnd);
                        WasmFuncBody fb;
                        for (uint32_t j = 0; j < localCount; ++j) {
                            const uint32_t countEntries = readVarUint32(p, bodyEnd);
                            const auto vt = static_cast<WasmValType>(*p++);
                            fb.locals.push_back({countEntries, vt});
                        }
                        fb.code.assign(p, bodyEnd);
                        p = bodyEnd;
                        module->code_.push_back(std::move(fb));
                    }
                    break;
                }
                case 11: { // Data section
                    const uint32_t count = readVarUint32(p, secEnd);
                    for (uint32_t i = 0; i < count; ++i) {
                        WasmDataSegment seg;
                        seg.memoryIndex = readVarUint32(p, secEnd);
                        // init expr (e.g. i32.const offset end)
                        const uint8_t opcode = *p++;
                        if (opcode == 0x41) { // i32.const
                            seg.offset = readVarInt32(p, secEnd);
                        }
                        if (*p++ != 0x0B) throw std::runtime_error("Expected end in data offset expr");
                        const uint32_t len = readVarUint32(p, secEnd);
                        seg.data.assign(p, p + len);
                        p += len;
                        module->dataSegments_.push_back(std::move(seg));
                    }
                    break;
                }
                default:
                    p = secEnd; // Skip unhandled sections (e.g. custom, table, global)
                    break;
            }
            p = secEnd;
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
    return loadFromBytes(reinterpret_cast<const uint8_t*>(bytes.constData()), bytes.size(), outError);
}

// -----------------------------------------------------------------------------
// WasmInstance
// -----------------------------------------------------------------------------

WasmInstance::WasmInstance(std::shared_ptr<WasmModule> module)
    : module_(std::move(module)),
      memory_(module_ ? module_->initialMemoryPages() : 1, module_ ? module_->maxMemoryPages() : 256) {}

void WasmInstance::linkHostFunction(const std::string& moduleName, const std::string& fieldName, WasmHostFunc func) {
    hostFuncs_[{moduleName, fieldName}] = std::move(func);
}

bool WasmInstance::instantiate(std::string* outError) {
    if (!module_) {
        if (outError) *outError = "No module loaded";
        return false;
    }

    linkedImportFuncs_.clear();
    for (const auto& imp : module_->imports()) {
        if (imp.kind == 0) { // function import
            auto it = hostFuncs_.find({imp.moduleName, imp.fieldName});
            if (it == hostFuncs_.end()) {
                // Try wildcard module import ("*" or "env")
                it = hostFuncs_.find({"env", imp.fieldName});
            }
            if (it == hostFuncs_.end()) {
                it = hostFuncs_.find({"*", imp.fieldName});
            }
            if (it != hostFuncs_.end()) {
                linkedImportFuncs_.push_back(it->second);
            } else {
                if (outError) {
                    *outError = "Unresolved host import: " + imp.moduleName + "." + imp.fieldName;
                }
                return false;
            }
        }
    }

    // Apply data segments to linear memory
    for (const auto& seg : module_->dataSegments()) {
        if (!memory_.write(static_cast<uint32_t>(seg.offset), seg.data.data(), seg.data.size())) {
            if (outError) *outError = "Data segment out of bounds memory write";
            return false;
        }
    }

    return true;
}

std::string WasmInstance::lastErrorMessage() const {
    if (!customError_.empty()) return customError_;
    return wasmTrapToString(lastTrap_);
}

std::optional<WasmVal> WasmInstance::invoke(const std::string& exportName, const std::vector<WasmVal>& args) {
    lastTrap_ = WasmTrap::None;
    customError_.clear();

    if (!module_) {
        lastTrap_ = WasmTrap::UndefinedElement;
        customError_ = "Module not instantiated";
        return std::nullopt;
    }

    const WasmExport* exp = module_->findExport(exportName);
    if (!exp || exp->kind != 0) {
        lastTrap_ = WasmTrap::UndefinedElement;
        customError_ = "Exported function '" + exportName + "' not found";
        return std::nullopt;
    }

    return executeFunction(exp->index, args);
}

std::optional<WasmVal> WasmInstance::executeFunction(uint32_t funcIndex, const std::vector<WasmVal>& args) {
    const size_t importCount = linkedImportFuncs_.size();
    if (funcIndex < importCount) {
        return linkedImportFuncs_[funcIndex](*this, args);
    }

    const size_t internalIndex = funcIndex - importCount;
    if (internalIndex >= module_->code().size()) {
        lastTrap_ = WasmTrap::UndefinedElement;
        return std::nullopt;
    }

    const auto& body = module_->code()[internalIndex];
    std::vector<WasmVal> locals = args;
    for (const auto& [count, valType] : body.locals) {
        for (uint32_t i = 0; i < count; ++i) {
            switch (valType) {
                case WasmValType::I32: locals.push_back(int32_t(0)); break;
                case WasmValType::I64: locals.push_back(int64_t(0)); break;
                case WasmValType::F32: locals.push_back(0.0f); break;
                case WasmValType::F64: locals.push_back(0.0); break;
                default: locals.push_back(int32_t(0)); break;
            }
        }
    }

    std::vector<WasmVal> stack;
    stack.reserve(64);

    const uint8_t* p = body.code.data();
    const uint8_t* end = p + body.code.size();

    // Block nesting tracker for label jumps
    struct Block {
        uint8_t opcode = 0;
        size_t stackSize = 0;
        const uint8_t* startIp = nullptr;
    };
    std::vector<Block> blocks;

    while (p < end) {
        const uint8_t op = *p++;
        switch (op) {
            case 0x00: // unreachable
                lastTrap_ = WasmTrap::Unreachable;
                return std::nullopt;

            case 0x01: // nop
                break;

            case 0x02: // block
            case 0x03: // loop
            case 0x04: // if
                readVarInt32(p, end); // block signature
                if (op == 0x04) {
                    if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                    int32_t cond = std::get<int32_t>(stack.back());
                    stack.pop_back();
                    if (!cond) {
                        // Skip to else or matching end
                        int depth = 1;
                        while (p < end && depth > 0) {
                            uint8_t sub = *p++;
                            if (sub == 0x02 || sub == 0x03 || sub == 0x04) depth++;
                            else if (sub == 0x05 && depth == 1) break; // found matching else
                            else if (sub == 0x0B) depth--;
                        }
                    }
                }
                blocks.push_back({op, stack.size(), p});
                break;

            case 0x05: // else
                // Skip to matching end
                if (!blocks.empty()) {
                    int depth = 1;
                    while (p < end && depth > 0) {
                        uint8_t sub = *p++;
                        if (sub == 0x02 || sub == 0x03 || sub == 0x04) depth++;
                        else if (sub == 0x0B) depth--;
                    }
                    blocks.pop_back();
                }
                break;

            case 0x0B: // end
                if (!blocks.empty()) {
                    blocks.pop_back();
                } else {
                    // Function return
                    if (!stack.empty()) return stack.back();
                    return std::nullopt;
                }
                break;

            case 0x0C: // br
            case 0x0D: { // br_if
                uint32_t depth = readVarUint32(p, end);
                if (op == 0x0D) {
                    if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                    int32_t cond = std::get<int32_t>(stack.back());
                    stack.pop_back();
                    if (!cond) break;
                }
                if (depth < blocks.size()) {
                    const auto& target = blocks[blocks.size() - 1 - depth];
                    if (target.opcode == 0x03) { // loop: jump back to loop start
                        p = target.startIp;
                    } else { // block: jump to end
                        int d = depth + 1;
                        while (p < end && d > 0) {
                            uint8_t sub = *p++;
                            if (sub == 0x02 || sub == 0x03 || sub == 0x04) d++;
                            else if (sub == 0x0B) d--;
                        }
                    }
                }
                break;
            }

            case 0x0F: // return
                if (!stack.empty()) return stack.back();
                return std::nullopt;

            case 0x10: { // call
                const uint32_t targetFuncIdx = readVarUint32(p, end);
                const WasmFuncType* ft = nullptr;
                if (targetFuncIdx < importCount) {
                    ft = &module_->types()[module_->imports()[targetFuncIdx].typeIndex];
                } else {
                    const uint32_t typeIdx = module_->funcTypeIndices()[targetFuncIdx - importCount];
                    ft = &module_->types()[typeIdx];
                }

                std::vector<WasmVal> callArgs(ft->params.size());
                for (size_t i = ft->params.size(); i > 0; --i) {
                    if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                    callArgs[i - 1] = stack.back();
                    stack.pop_back();
                }

                auto res = executeFunction(targetFuncIdx, callArgs);
                if (lastTrap_ != WasmTrap::None) return std::nullopt;
                if (res.has_value()) {
                    stack.push_back(*res);
                }
                break;
            }

            case 0x1A: // drop
                if (!stack.empty()) stack.pop_back();
                break;

            case 0x1B: { // select
                if (stack.size() < 3) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                int32_t cond = std::get<int32_t>(stack.back()); stack.pop_back();
                WasmVal val2 = stack.back(); stack.pop_back();
                WasmVal val1 = stack.back(); stack.pop_back();
                stack.push_back(cond ? val1 : val2);
                break;
            }

            case 0x20: { // local.get
                const uint32_t idx = readVarUint32(p, end);
                if (idx < locals.size()) stack.push_back(locals[idx]);
                else { lastTrap_ = WasmTrap::UndefinedElement; return std::nullopt; }
                break;
            }
            case 0x21: { // local.set
                const uint32_t idx = readVarUint32(p, end);
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                if (idx < locals.size()) { locals[idx] = stack.back(); stack.pop_back(); }
                else { lastTrap_ = WasmTrap::UndefinedElement; return std::nullopt; }
                break;
            }
            case 0x22: { // local.tee
                const uint32_t idx = readVarUint32(p, end);
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                if (idx < locals.size()) { locals[idx] = stack.back(); }
                else { lastTrap_ = WasmTrap::UndefinedElement; return std::nullopt; }
                break;
            }

            case 0x28: { // i32.load
                readVarUint32(p, end); // align
                const uint32_t offset = readVarUint32(p, end);
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const uint32_t base = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                int32_t val = 0;
                if (!memory_.read(base + offset, &val, sizeof(val))) {
                    lastTrap_ = WasmTrap::OutOfBoundsMemoryAccess;
                    return std::nullopt;
                }
                stack.push_back(val);
                break;
            }
            case 0x2C: // i32.load8_s
            case 0x2D: { // i32.load8_u
                readVarUint32(p, end);
                const uint32_t offset = readVarUint32(p, end);
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const uint32_t base = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                uint8_t byteVal = 0;
                if (!memory_.read(base + offset, &byteVal, 1)) {
                    lastTrap_ = WasmTrap::OutOfBoundsMemoryAccess;
                    return std::nullopt;
                }
                stack.push_back(op == 0x2C ? static_cast<int32_t>(static_cast<int8_t>(byteVal)) : static_cast<int32_t>(byteVal));
                break;
            }
            case 0x2B: { // f64.load
                readVarUint32(p, end);
                const uint32_t offset = readVarUint32(p, end);
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const uint32_t base = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                double val = 0.0;
                if (!memory_.read(base + offset, &val, sizeof(val))) {
                    lastTrap_ = WasmTrap::OutOfBoundsMemoryAccess;
                    return std::nullopt;
                }
                stack.push_back(val);
                break;
            }

            case 0x36: { // i32.store
                readVarUint32(p, end); // align
                const uint32_t offset = readVarUint32(p, end);
                if (stack.size() < 2) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const int32_t val = std::get<int32_t>(stack.back()); stack.pop_back();
                const uint32_t base = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                if (!memory_.write(base + offset, &val, sizeof(val))) {
                    lastTrap_ = WasmTrap::OutOfBoundsMemoryAccess;
                    return std::nullopt;
                }
                break;
            }
            case 0x3A: { // i32.store8
                readVarUint32(p, end);
                const uint32_t offset = readVarUint32(p, end);
                if (stack.size() < 2) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const uint8_t val = static_cast<uint8_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                const uint32_t base = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                if (!memory_.write(base + offset, &val, 1)) {
                    lastTrap_ = WasmTrap::OutOfBoundsMemoryAccess;
                    return std::nullopt;
                }
                break;
            }
            case 0x39: { // f64.store
                readVarUint32(p, end);
                const uint32_t offset = readVarUint32(p, end);
                if (stack.size() < 2) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const double val = std::get<double>(stack.back()); stack.pop_back();
                const uint32_t base = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                if (!memory_.write(base + offset, &val, sizeof(val))) {
                    lastTrap_ = WasmTrap::OutOfBoundsMemoryAccess;
                    return std::nullopt;
                }
                break;
            }

            case 0x3F: // memory.size
                p++; // reserved 0
                stack.push_back(static_cast<int32_t>(memory_.sizePages()));
                break;

            case 0x40: { // memory.grow
                p++; // reserved 0
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                const uint32_t delta = static_cast<uint32_t>(std::get<int32_t>(stack.back())); stack.pop_back();
                const uint32_t prevPages = memory_.sizePages();
                stack.push_back(memory_.grow(delta) ? static_cast<int32_t>(prevPages) : -1);
                break;
            }

            case 0x41: // i32.const
                stack.push_back(readVarInt32(p, end));
                break;
            case 0x42: // i64.const
                stack.push_back(readVarInt64(p, end));
                break;
            case 0x44: { // f64.const
                double d = 0.0;
                std::memcpy(&d, p, sizeof(d));
                p += sizeof(d);
                stack.push_back(d);
                break;
            }

            // Arithmetic & Logic (i32)
            case 0x45: { // i32.eqz
                if (stack.empty()) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                int32_t a = std::get<int32_t>(stack.back()); stack.pop_back();
                stack.push_back(a == 0 ? 1 : 0);
                break;
            }
            case 0x46: // i32.eq
            case 0x47: // i32.ne
            case 0x48: // i32.lt_s
            case 0x49: // i32.lt_u
            case 0x4A: // i32.gt_s
            case 0x4B: // i32.gt_u
            case 0x4C: // i32.le_s
            case 0x4D: // i32.le_u
            case 0x4E: // i32.ge_s
            case 0x4F: // i32.ge_u
            case 0x6A: // i32.add
            case 0x6B: // i32.sub
            case 0x6C: // i32.mul
            case 0x6D: // i32.div_s
            case 0x6E: // i32.div_u
            case 0x6F: // i32.rem_s
            case 0x70: // i32.rem_u
            case 0x71: // i32.and
            case 0x72: // i32.or
            case 0x73: // i32.xor
            case 0x74: // i32.shl
            case 0x75: // i32.shr_s
            case 0x76: { // i32.shr_u
                if (stack.size() < 2) { lastTrap_ = WasmTrap::StackUnderflow; return std::nullopt; }
                int32_t b = std::get<int32_t>(stack.back()); stack.pop_back();
                int32_t a = std::get<int32_t>(stack.back()); stack.pop_back();

                switch (op) {
                    case 0x46: stack.push_back(a == b ? 1 : 0); break;
                    case 0x47: stack.push_back(a != b ? 1 : 0); break;
                    case 0x48: stack.push_back(a < b ? 1 : 0); break;
                    case 0x49: stack.push_back(static_cast<uint32_t>(a) < static_cast<uint32_t>(b) ? 1 : 0); break;
                    case 0x4A: stack.push_back(a > b ? 1 : 0); break;
                    case 0x4B: stack.push_back(static_cast<uint32_t>(a) > static_cast<uint32_t>(b) ? 1 : 0); break;
                    case 0x4C: stack.push_back(a <= b ? 1 : 0); break;
                    case 0x4D: stack.push_back(static_cast<uint32_t>(a) <= static_cast<uint32_t>(b) ? 1 : 0); break;
                    case 0x4E: stack.push_back(a >= b ? 1 : 0); break;
                    case 0x4F: stack.push_back(static_cast<uint32_t>(a) >= static_cast<uint32_t>(b) ? 1 : 0); break;
                    case 0x6A: stack.push_back(a + b); break;
                    case 0x6B: stack.push_back(a - b); break;
                    case 0x6C: stack.push_back(a * b); break;
                    case 0x6D:
                        if (b == 0) { lastTrap_ = WasmTrap::DivisionByZero; return std::nullopt; }
                        stack.push_back(a / b);
                        break;
                    case 0x6E:
                        if (b == 0) { lastTrap_ = WasmTrap::DivisionByZero; return std::nullopt; }
                        stack.push_back(static_cast<int32_t>(static_cast<uint32_t>(a) / static_cast<uint32_t>(b)));
                        break;
                    case 0x6F:
                        if (b == 0) { lastTrap_ = WasmTrap::DivisionByZero; return std::nullopt; }
                        stack.push_back(a % b);
                        break;
                    case 0x70:
                        if (b == 0) { lastTrap_ = WasmTrap::DivisionByZero; return std::nullopt; }
                        stack.push_back(static_cast<int32_t>(static_cast<uint32_t>(a) % static_cast<uint32_t>(b)));
                        break;
                    case 0x71: stack.push_back(a & b); break;
                    case 0x72: stack.push_back(a | b); break;
                    case 0x73: stack.push_back(a ^ b); break;
                    case 0x74: stack.push_back(a << (b & 31)); break;
                    case 0x75: stack.push_back(a >> (b & 31)); break;
                    case 0x76: stack.push_back(static_cast<int32_t>(static_cast<uint32_t>(a) >> (b & 31))); break;
                    default: break;
                }
                break;
            }

            default:
                lastTrap_ = WasmTrap::InvalidOpcode;
                return std::nullopt;
        }
    }

    if (!stack.empty()) return stack.back();
    return std::nullopt;
}

}  // namespace gs::app
