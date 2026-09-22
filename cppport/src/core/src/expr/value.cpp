#include "gs/expr/value.hpp"

#include "gs/util/jsnumber.hpp"

#include <boost/json.hpp>

#include <cmath>
#include <limits>

namespace gs::expr {

// ---- Value -------------------------------------------------------------------

Value Value::null() {
    Value v;
    v.data_ = NullTag{};
    return v;
}

Value Value::object() {
    Value v;
    v.data_ = std::make_shared<ObjectData>();
    return v;
}

Value Value::array(std::vector<Value> items) {
    Value v;
    auto data = std::make_shared<ArrayData>();
    data->items = std::move(items);
    v.data_ = std::move(data);
    return v;
}

Value Value::function(std::string name, NativeFunction fn) {
    Value v;
    auto data = std::make_shared<FunctionData>();
    data->name = std::move(name);
    data->fn = std::move(fn);
    v.data_ = std::move(data);
    return v;
}

Value::Kind Value::kind() const noexcept {
    switch (data_.index()) {
        case 0: return Kind::Undefined;
        case 1: return Kind::Null;
        case 2: return Kind::Boolean;
        case 3: return Kind::Number;
        case 4: return Kind::String;
        case 5: return Kind::Object;
        case 6: return Kind::Array;
        default: return Kind::Function;
    }
}

Value Value::get(std::string_view key) const {
    if (!isObject()) {
        return {};
    }
    const Value* found = objectData().find(key);
    return found ? *found : Value{};
}

bool Value::has(std::string_view key) const {
    return isObject() && objectData().find(key) != nullptr;
}

void Value::set(std::string_view key, Value value) const {
    if (isObject()) {
        objectData().set(key, std::move(value));
    }
}

void Value::remove(std::string_view key) const {
    if (isObject()) {
        objectData().remove(key);
    }
}

bool Value::sameAs(const Value& other) const noexcept {
    if (kind() != other.kind()) {
        return false;
    }
    if (isNumber()) {
        const double a = asNumber();
        const double b = other.asNumber();
        return a == b || (std::isnan(a) && std::isnan(b));
    }
    return data_ == other.data_;
}

// ---- ObjectData --------------------------------------------------------------

Value* ObjectData::find(std::string_view key) {
    for (auto& [name, value] : properties) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

const Value* ObjectData::find(std::string_view key) const {
    for (const auto& [name, value] : properties) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

void ObjectData::set(std::string_view key, Value value) {
    if (Value* existing = find(key)) {
        *existing = std::move(value);
        return;
    }
    properties.emplace_back(std::string(key), std::move(value));
}

void ObjectData::remove(std::string_view key) {
    for (auto it = properties.begin(); it != properties.end(); ++it) {
        if (it->first == key) {
            properties.erase(it);
            return;
        }
    }
}

// ---- Conversions ---------------------------------------------------------------

bool toBoolean(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::Undefined:
        case Value::Kind::Null:
            return false;
        case Value::Kind::Boolean:
            return v.asBoolean();
        case Value::Kind::Number: {
            const double d = v.asNumber();
            return d != 0 && !std::isnan(d);
        }
        case Value::Kind::String:
            return !v.asString().empty();
        default:
            return true;
    }
}

double toNumber(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::Undefined:
            return std::numeric_limits<double>::quiet_NaN();
        case Value::Kind::Null:
            return 0;
        case Value::Kind::Boolean:
            return v.asBoolean() ? 1 : 0;
        case Value::Kind::Number:
            return v.asNumber();
        case Value::Kind::String:
            return js::stringToNumber(v.asString());
        case Value::Kind::Array:
            return js::stringToNumber(toString(v));
        default:
            return std::numeric_limits<double>::quiet_NaN();
    }
}

std::string toString(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::Undefined:
            return "undefined";
        case Value::Kind::Null:
            return "null";
        case Value::Kind::Boolean:
            return v.asBoolean() ? "true" : "false";
        case Value::Kind::Number:
            return js::numberToString(v.asNumber());
        case Value::Kind::String:
            return v.asString();
        case Value::Kind::Object:
            return "[object Object]";
        case Value::Kind::Array: {
            std::string out;
            const auto& items = v.arrayData().items;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    out += ',';
                }
                if (!items[i].isNullish()) {
                    out += toString(items[i]);
                }
            }
            return out;
        }
        case Value::Kind::Function:
            return "function " + v.functionData().name + "() { [native code] }";
    }
    return {};
}

std::int32_t toInt32(const Value& v) {
    const double d = toNumber(v);
    if (!std::isfinite(d)) {
        return 0;
    }
    const double truncated = std::trunc(d);
    const double modulo = std::fmod(truncated, 4294967296.0);
    std::uint32_t bits = static_cast<std::uint32_t>(modulo < 0 ? modulo + 4294967296.0 : modulo);
    return static_cast<std::int32_t>(bits);
}

std::string typeOf(const Value& v) {
    switch (v.kind()) {
        case Value::Kind::Undefined: return "undefined";
        case Value::Kind::Boolean: return "boolean";
        case Value::Kind::Number: return "number";
        case Value::Kind::String: return "string";
        case Value::Kind::Function: return "function";
        default: return "object";
    }
}

bool strictEquals(const Value& a, const Value& b) {
    if (a.kind() != b.kind()) {
        return false;
    }
    if (a.isNumber()) {
        return a.asNumber() == b.asNumber();  // NaN !== NaN, +0 === -0
    }
    return a.sameAs(b);
}

bool looseEquals(const Value& a, const Value& b) {
    if (a.kind() == b.kind()) {
        return strictEquals(a, b);
    }
    if (a.isNullish() && b.isNullish()) {
        return true;
    }
    if (a.isNullish() || b.isNullish()) {
        return false;
    }
    if (a.isNumber() && b.isString()) {
        return a.asNumber() == toNumber(b);
    }
    if (a.isString() && b.isNumber()) {
        return toNumber(a) == b.asNumber();
    }
    if (a.isBoolean()) {
        return looseEquals(Value(toNumber(a)), b);
    }
    if (b.isBoolean()) {
        return looseEquals(a, Value(toNumber(b)));
    }
    const bool aPrimitive = a.isNumber() || a.isString();
    const bool bPrimitive = b.isNumber() || b.isString();
    if (aPrimitive && (b.isObject() || b.isArray() || b.isFunction())) {
        return looseEquals(a, Value(toString(b)));
    }
    if (bPrimitive && (a.isObject() || a.isArray() || a.isFunction())) {
        return looseEquals(Value(toString(a)), b);
    }
    return false;
}

// ---- JSON --------------------------------------------------------------------

namespace {

void appendQuoted(std::string& out, std::string_view s) {
    out += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0xF];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

bool appendJson(std::string& out, const Value& v) {
    switch (v.kind()) {
        case Value::Kind::Undefined:
        case Value::Kind::Function:
            return false;
        case Value::Kind::Null:
            out += "null";
            return true;
        case Value::Kind::Boolean:
            out += v.asBoolean() ? "true" : "false";
            return true;
        case Value::Kind::Number:
            out += std::isfinite(v.asNumber()) ? js::numberToString(v.asNumber()) : "null";
            return true;
        case Value::Kind::String:
            appendQuoted(out, v.asString());
            return true;
        case Value::Kind::Array: {
            out += '[';
            const auto& items = v.arrayData().items;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) {
                    out += ',';
                }
                if (!appendJson(out, items[i])) {
                    out += "null";
                }
            }
            out += ']';
            return true;
        }
        case Value::Kind::Object: {
            out += '{';
            bool first = true;
            for (const auto& [key, value] : v.objectData().properties) {
                std::string item;
                if (!appendJson(item, value)) {
                    continue;
                }
                if (!first) {
                    out += ',';
                }
                first = false;
                appendQuoted(out, key);
                out += ':';
                out += item;
            }
            out += '}';
            return true;
        }
    }
    return false;
}

Value fromBoost(const boost::json::value& json) {
    switch (json.kind()) {
        case boost::json::kind::null:
            return Value::null();
        case boost::json::kind::bool_:
            return Value(json.get_bool());
        case boost::json::kind::int64:
            return Value(static_cast<double>(json.get_int64()));
        case boost::json::kind::uint64:
            return Value(static_cast<double>(json.get_uint64()));
        case boost::json::kind::double_:
            return Value(json.get_double());
        case boost::json::kind::string:
            return Value(std::string_view(json.get_string()));
        case boost::json::kind::array: {
            std::vector<Value> items;
            for (const auto& item : json.get_array()) {
                items.push_back(fromBoost(item));
            }
            return Value::array(std::move(items));
        }
        case boost::json::kind::object: {
            Value object = Value::object();
            for (const auto& [key, item] : json.get_object()) {
                object.set(std::string_view(key), fromBoost(item));
            }
            return object;
        }
    }
    return {};
}

}  // namespace

std::optional<std::string> toJson(const Value& v) {
    std::string out;
    if (!appendJson(out, v)) {
        return std::nullopt;
    }
    return out;
}

std::optional<Value> fromJson(std::string_view text) {
    boost::system::error_code ec;
    boost::json::value parsed = boost::json::parse(text, ec);
    if (ec) {
        return std::nullopt;
    }
    return fromBoost(parsed);
}

}  // namespace gs::expr
