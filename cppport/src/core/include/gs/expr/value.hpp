#pragma once

// Dynamic value for gSender's macro expression language, a JavaScript
// subset. Objects, arrays and functions have reference semantics (copies of a
// Value share the underlying object), exactly like JavaScript - macros rely on
// that: `%global.state.x = 1` mutates a context object that later lines see.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace gs::expr {

class Value;
struct ObjectData;
struct ArrayData;
struct FunctionData;

// Native function: receives `this` and the arguments. Returning nullopt means
// "unresolved" (the whole expression then fails, as a thrown error would).
using NativeFunction = std::function<std::optional<Value>(const Value& self, const std::vector<Value>& args)>;

class Value {
public:
    enum class Kind { Undefined, Null, Boolean, Number, String, Object, Array, Function };

    Value() = default;  // undefined
    Value(bool b) : data_(b) {}
    Value(double d) : data_(d) {}
    Value(int i) : data_(static_cast<double>(i)) {}
    Value(std::string s) : data_(std::move(s)) {}
    Value(std::string_view s) : data_(std::string(s)) {}
    Value(const char* s) : data_(std::string(s)) {}

    static Value null();
    static Value object();
    static Value array(std::vector<Value> items = {});
    static Value function(std::string name, NativeFunction fn);

    Kind kind() const noexcept;
    bool isUndefined() const noexcept { return kind() == Kind::Undefined; }
    bool isNull() const noexcept { return kind() == Kind::Null; }
    bool isNullish() const noexcept { return isUndefined() || isNull(); }
    bool isBoolean() const noexcept { return kind() == Kind::Boolean; }
    bool isNumber() const noexcept { return kind() == Kind::Number; }
    bool isString() const noexcept { return kind() == Kind::String; }
    bool isObject() const noexcept { return kind() == Kind::Object; }
    bool isArray() const noexcept { return kind() == Kind::Array; }
    bool isFunction() const noexcept { return kind() == Kind::Function; }

    bool asBoolean() const { return std::get<bool>(data_); }
    double asNumber() const { return std::get<double>(data_); }
    const std::string& asString() const { return std::get<std::string>(data_); }
    ObjectData& objectData() const { return *std::get<std::shared_ptr<ObjectData>>(data_); }
    ArrayData& arrayData() const { return *std::get<std::shared_ptr<ArrayData>>(data_); }
    const FunctionData& functionData() const { return *std::get<std::shared_ptr<FunctionData>>(data_); }

    // Object property helpers (no-ops / undefined for non-objects).
    Value get(std::string_view key) const;
    bool has(std::string_view key) const;
    void set(std::string_view key, Value value) const;
    void remove(std::string_view key) const;

    // Identity for objects/arrays/functions, value equality for primitives
    // (JavaScript ===, except that NaN === NaN here for test convenience).
    bool sameAs(const Value& other) const noexcept;

private:
    struct NullTag {
        bool operator==(const NullTag&) const = default;
    };
    std::variant<std::monostate, NullTag, bool, double, std::string, std::shared_ptr<ObjectData>,
                 std::shared_ptr<ArrayData>, std::shared_ptr<FunctionData>>
        data_;
};

// Insertion-ordered properties, as JavaScript iterates plain objects.
struct ObjectData {
    std::vector<std::pair<std::string, Value>> properties;

    Value* find(std::string_view key);
    const Value* find(std::string_view key) const;
    void set(std::string_view key, Value value);
    void remove(std::string_view key);
};

struct ArrayData {
    std::vector<Value> items;
};

struct FunctionData {
    std::string name;
    NativeFunction fn;
    // Static members, e.g. Number.isFinite or String.fromCharCode.
    std::shared_ptr<ObjectData> properties = std::make_shared<ObjectData>();
};

// ---- JavaScript conversions -------------------------------------------------

bool toBoolean(const Value& v);
double toNumber(const Value& v);
std::string toString(const Value& v);  // String(v)
std::int32_t toInt32(const Value& v);
std::string typeOf(const Value& v);
bool strictEquals(const Value& a, const Value& b);  // ===
bool looseEquals(const Value& a, const Value& b);   // ==

// JSON.stringify(v); nullopt when v itself is not serializable (undefined, function).
std::optional<std::string> toJson(const Value& v);
// JSON.parse(text); nullopt on a syntax error.
std::optional<Value> fromJson(std::string_view text);

}  // namespace gs::expr
