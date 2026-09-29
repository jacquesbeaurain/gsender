#pragma once

#include <cstdint>
#include <cstddef>
#include <vector>
#include <string>
#include <map>
#include <functional>
#include <optional>
#include <variant>
#include <memory>
#include <QString>
#include <QByteArray>

namespace gs::app {

enum class WasmValType : uint8_t {
    I32 = 0x7F,
    I64 = 0x7E,
    F32 = 0x7D,
    F64 = 0x7C,
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
    TypeMismatch
};

const char* wasmTrapToString(WasmTrap trap);

struct WasmFuncType {
    std::vector<WasmValType> params;
    std::vector<WasmValType> results;
};

struct WasmImport {
    std::string moduleName;
    std::string fieldName;
    uint8_t kind = 0; // 0=func, 1=table, 2=mem, 3=global
    uint32_t typeIndex = 0;
};

struct WasmExport {
    std::string name;
    uint8_t kind = 0; // 0=func, 1=table, 2=mem, 3=global
    uint32_t index = 0;
};

struct WasmFuncBody {
    std::vector<std::pair<uint32_t, WasmValType>> locals;
    std::vector<uint8_t> code;
};

struct WasmDataSegment {
    uint32_t memoryIndex = 0;
    int32_t offset = 0;
    std::vector<uint8_t> data;
};

class WasmMemory {
public:
    explicit WasmMemory(uint32_t initialPages = 1, uint32_t maxPages = 256);

    uint32_t sizePages() const noexcept { return static_cast<uint32_t>(data_.size() / PageSize); }
    size_t sizeBytes() const noexcept { return data_.size(); }
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

class WasmModule {
public:
    static std::shared_ptr<WasmModule> loadFromBytes(const uint8_t* data, size_t size, std::string* outError = nullptr);
    static std::shared_ptr<WasmModule> loadFromFile(const QString& filePath, std::string* outError = nullptr);

    const std::vector<WasmFuncType>& types() const noexcept { return types_; }
    const std::vector<WasmImport>& imports() const noexcept { return imports_; }
    const std::vector<uint32_t>& funcTypeIndices() const noexcept { return funcTypeIndices_; }
    const std::vector<WasmExport>& exports() const noexcept { return exports_; }
    const std::vector<WasmFuncBody>& code() const noexcept { return code_; }
    const std::vector<WasmDataSegment>& dataSegments() const noexcept { return dataSegments_; }
    uint32_t initialMemoryPages() const noexcept { return initialMemoryPages_; }
    uint32_t maxMemoryPages() const noexcept { return maxMemoryPages_; }

    const WasmExport* findExport(const std::string& name) const;

private:
    std::vector<WasmFuncType> types_;
    std::vector<WasmImport> imports_;
    std::vector<uint32_t> funcTypeIndices_;
    std::vector<WasmExport> exports_;
    std::vector<WasmFuncBody> code_;
    std::vector<WasmDataSegment> dataSegments_;
    uint32_t initialMemoryPages_ = 1;
    uint32_t maxMemoryPages_ = 256;
};

class WasmInstance;

using WasmHostFunc = std::function<std::optional<WasmVal>(WasmInstance&, const std::vector<WasmVal>&)>;

class WasmInstance {
public:
    explicit WasmInstance(std::shared_ptr<WasmModule> module);

    void linkHostFunction(const std::string& moduleName, const std::string& fieldName, WasmHostFunc func);

    bool instantiate(std::string* outError = nullptr);

    std::optional<WasmVal> invoke(const std::string& exportName, const std::vector<WasmVal>& args = {});

    WasmMemory& memory() noexcept { return memory_; }
    const WasmMemory& memory() const noexcept { return memory_; }

    WasmTrap lastTrap() const noexcept { return lastTrap_; }
    std::string lastErrorMessage() const;

private:
    struct StackFrame {
        uint32_t funcIndex = 0;
        size_t ip = 0;
        std::vector<WasmVal> locals;
        size_t stackBase = 0;
    };

    std::optional<WasmVal> executeFunction(uint32_t funcIndex, const std::vector<WasmVal>& args);

    std::shared_ptr<WasmModule> module_;
    WasmMemory memory_;
    std::map<std::pair<std::string, std::string>, WasmHostFunc> hostFuncs_;
    std::vector<WasmHostFunc> linkedImportFuncs_;
    WasmTrap lastTrap_ = WasmTrap::None;
    std::string customError_;
};

}  // namespace gs::app
