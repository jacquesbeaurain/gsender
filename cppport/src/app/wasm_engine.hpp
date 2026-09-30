#pragma once

// A WebAssembly interpreter for plugins: the binary format of WebAssembly 2.0
// (MVP plus sign extension, saturating float-to-int, bulk memory, multi-value
// and reference types), which is what clang, Rust and AssemblyScript emit by
// default. Function bodies are decoded once, at load, into instructions with
// resolved branch targets; execution keeps its own call stack, so a runaway
// plugin can exhaust its fuel, stack or memory limits and trap, but never
// overflows the host's stack or reads outside its own linear memory.

#include <QString>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gs::app {

enum class WasmValType : uint8_t {
    I32 = 0x7F,
    I64 = 0x7E,
    F32 = 0x7D,
    F64 = 0x7C,
    V128 = 0x7B,
    FuncRef = 0x70,
    ExternRef = 0x6F,
    Void = 0x40
};

using WasmVal = std::variant<int32_t, int64_t, float, double>;

enum class WasmTrap {
    None = 0,
    Unreachable,
    OutOfBoundsMemoryAccess,
    DivisionByZero,
    IntegerOverflow,
    StackUnderflow,
    CallStackExhausted,
    InvalidOpcode,
    UndefinedElement,
    TypeMismatch,
    InvalidConversion,
    OutOfBoundsTableAccess,
    OutOfFuel,
    HostError
};

const char* wasmTrapToString(WasmTrap trap);

struct WasmFuncType {
    std::vector<WasmValType> params;
    std::vector<WasmValType> results;

    bool operator==(const WasmFuncType&) const = default;
};

struct WasmImport {
    std::string moduleName;
    std::string fieldName;
    uint8_t kind = 0;  // 0=func, 1=table, 2=mem, 3=global
    uint32_t typeIndex = 0;
};

struct WasmExport {
    std::string name;
    uint8_t kind = 0;  // 0=func, 1=table, 2=mem, 3=global
    uint32_t index = 0;
};

class WasmMemory {
public:
    explicit WasmMemory(uint32_t initialPages = 0, uint32_t maxPages = 0);

    uint32_t sizePages() const noexcept { return static_cast<uint32_t>(data_.size() / PageSize); }
    size_t sizeBytes() const noexcept { return data_.size(); }
    uint32_t maxPages() const noexcept { return maxPages_; }
    bool grow(uint32_t deltaPages);

    bool read(uint32_t offset, void* dst, size_t size) const;
    bool write(uint32_t offset, const void* src, size_t size);

    std::string readString(uint32_t offset, size_t maxLen = 4096) const;
    bool writeString(uint32_t offset, const std::string& str, size_t maxLen = 4096);

    uint8_t* rawData() noexcept { return data_.data(); }
    const uint8_t* rawData() const noexcept { return data_.data(); }

    static constexpr size_t PageSize = 65536;

private:
    uint32_t maxPages_;
    std::vector<uint8_t> data_;
};

/** One decoded instruction. `op` is the opcode, 0xFC-prefixed ones as 0x100 + sub-opcode. */
struct WasmInstr {
    uint16_t op = 0;
    uint32_t a = 0;
    uint64_t b = 0;
};

/** Where a block, loop or if ends, where its else starts, and its signature's arity. */
struct WasmBlockInfo {
    uint32_t elsePc = 0;  // the else instruction, or 0 when there is none
    uint32_t endPc = 0;   // the matching end instruction
    uint32_t params = 0;
    uint32_t results = 0;
};

struct WasmFunction {
    uint32_t typeIndex = 0;
    std::vector<WasmValType> localTypes;  // declared locals, after the parameters
    std::vector<WasmInstr> code;
    std::vector<WasmBlockInfo> blocks;
    std::vector<uint32_t> brTables;  // per br_table: count, targets..., default
};

struct WasmGlobalDef {
    WasmValType type = WasmValType::I32;
    bool mutableValue = false;
    std::vector<WasmInstr> init;
};

struct WasmTableDef {
    WasmValType elemType = WasmValType::FuncRef;
    uint32_t initial = 0;
    uint32_t max = 0;
    bool hasMax = false;
};

struct WasmElementSegment {
    enum class Mode { Active, Passive, Declarative } mode = Mode::Active;
    uint32_t tableIndex = 0;
    std::vector<WasmInstr> offset;
    std::vector<std::vector<WasmInstr>> items;  // each a constant expression yielding a funcref
};

struct WasmDataSegment {
    bool active = true;
    std::vector<WasmInstr> offset;
    std::vector<uint8_t> data;
};

class WasmModule {
public:
    static std::shared_ptr<WasmModule> loadFromBytes(const uint8_t* data, size_t size, std::string* outError = nullptr);
    static std::shared_ptr<WasmModule> loadFromFile(const QString& filePath, std::string* outError = nullptr);

    const std::vector<WasmFuncType>& types() const noexcept { return types_; }
    const std::vector<WasmImport>& imports() const noexcept { return imports_; }
    const std::vector<WasmExport>& exports() const noexcept { return exports_; }
    const std::vector<WasmFunction>& functions() const noexcept { return functions_; }
    bool hasMemory() const noexcept { return hasMemory_; }
    uint32_t initialMemoryPages() const noexcept { return initialMemoryPages_; }
    uint32_t maxMemoryPages() const noexcept { return maxMemoryPages_; }

    const WasmExport* findExport(const std::string& name) const;

    /** Imported functions come first in the function index space. */
    uint32_t importedFunctionCount() const noexcept { return static_cast<uint32_t>(importFuncTypes_.size()); }
    uint32_t functionCount() const noexcept { return importedFunctionCount() + static_cast<uint32_t>(functions_.size()); }
    const WasmFuncType& functionType(uint32_t funcIndex) const;

private:
    friend class WasmModuleParser;
    friend class WasmInstance;

    std::vector<WasmFuncType> types_;
    std::vector<WasmImport> imports_;
    std::vector<uint32_t> importFuncTypes_;
    std::vector<WasmFunction> functions_;
    std::vector<WasmTableDef> tables_;
    std::vector<WasmGlobalDef> globals_;
    std::vector<WasmExport> exports_;
    std::vector<WasmElementSegment> elements_;
    std::vector<WasmDataSegment> data_;
    std::optional<uint32_t> start_;
    bool hasMemory_ = false;
    uint32_t initialMemoryPages_ = 0;
    uint32_t maxMemoryPages_ = 65536;
};

class WasmInstance;

using WasmHostFunc = std::function<std::optional<WasmVal>(WasmInstance&, const std::vector<WasmVal>&)>;

class WasmInstance {
public:
    explicit WasmInstance(std::shared_ptr<WasmModule> module);

    void linkHostFunction(const std::string& moduleName, const std::string& fieldName, WasmHostFunc func);

    /** Limits for this instance; set before instantiate(). */
    void setMaxMemoryPages(uint32_t pages) { maxMemoryPagesLimit_ = pages; }
    /** Instructions one invoke() may execute before it traps with OutOfFuel. */
    void setFuelPerCall(uint64_t fuel) { fuelPerCall_ = fuel; }
    uint64_t fuelPerCall() const noexcept { return fuelPerCall_; }
    void setMaxCallDepth(uint32_t depth) { maxCallDepth_ = depth; }

    /** Links imports, builds memory, tables and globals, applies segments and runs the start function. */
    bool instantiate(std::string* outError = nullptr);

    /** Calls an exported function; nullopt on a trap (see lastTrap) or when it returns nothing. */
    std::optional<WasmVal> invoke(const std::string& exportName, const std::vector<WasmVal>& args = {});

    bool hasExport(const std::string& exportName) const;

    /** For host functions: fail the running call with a HostError trap once the host function returns. */
    void raiseHostTrap(const std::string& message);

    WasmMemory& memory() noexcept { return memory_; }
    const WasmMemory& memory() const noexcept { return memory_; }

    WasmTrap lastTrap() const noexcept { return lastTrap_; }
    std::string lastErrorMessage() const;

private:
    struct Frame {
        uint32_t funcIndex = 0;  // index into module functions (not the function index space)
        uint32_t pc = 0;
        uint32_t localsBase = 0;
        uint32_t floor = 0;  // localsBase + locals: the operand stack starts here
        uint32_t labelBase = 0;
    };
    struct Label {
        uint32_t contPc = 0;
        uint32_t height = 0;
        uint32_t arity = 0;
        bool loop = false;
    };
    struct Table {
        std::vector<uint64_t> elems;  // 0 is null; otherwise function index + 1
        uint32_t max = 0;
    };

    bool run(uint32_t funcIndex, std::vector<uint64_t>& values);
    bool callHost(uint32_t funcIndex);
    bool evalConst(const std::vector<WasmInstr>& expr, uint64_t& out) const;
    WasmVal toVal(WasmValType type, uint64_t bits) const;
    static uint64_t fromVal(WasmValType type, const WasmVal& val);

    std::shared_ptr<WasmModule> module_;
    WasmMemory memory_;
    std::vector<Table> tables_;
    std::vector<uint64_t> globals_;
    std::vector<bool> droppedData_;
    std::vector<bool> droppedElems_;
    std::map<std::pair<std::string, std::string>, WasmHostFunc> hostFuncs_;
    std::vector<WasmHostFunc> linkedImportFuncs_;
    bool instantiated_ = false;

    std::vector<uint64_t> stack_;
    std::vector<Frame> frames_;
    std::vector<Label> labels_;
    uint64_t fuel_ = 0;

    uint32_t maxMemoryPagesLimit_ = 1024;  // 64 MiB
    uint64_t fuelPerCall_ = 200'000'000;
    uint32_t maxCallDepth_ = 1024;
    static constexpr size_t MaxStackSlots = 1u << 20;

    WasmTrap lastTrap_ = WasmTrap::None;
    std::string customError_;
    bool hostTrapRaised_ = false;
};

}  // namespace gs::app
