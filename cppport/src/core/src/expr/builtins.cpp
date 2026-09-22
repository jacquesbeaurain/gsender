#include "builtins.hpp"

#include "gs/expr/expression.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>

namespace gs::expr {
namespace {

using Args = std::vector<Value>;
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

Value arg(const Args& args, std::size_t i) {
    return i < args.size() ? args[i] : Value{};
}

double numArg(const Args& args, std::size_t i) {
    return toNumber(arg(args, i));
}

// Clamps a JS relative index (negative counts from the end) into [0, size].
std::size_t relativeIndex(double value, std::size_t size, std::size_t fallback) {
    if (std::isnan(value)) {
        return fallback == size ? size : 0;
    }
    const double n = std::trunc(value);
    if (n < 0) {
        const double from = static_cast<double>(size) + n;
        return from < 0 ? 0 : static_cast<std::size_t>(from);
    }
    return n > static_cast<double>(size) ? size : static_cast<std::size_t>(n);
}

Value fn(std::string name, NativeFunction f) {
    return Value::function(std::move(name), std::move(f));
}

Value mathFn1(std::string name, double (*f)(double)) {
    return fn(name, [f](const Value&, const Args& a) -> std::optional<Value> { return Value(f(numArg(a, 0))); });
}

Value makeMath() {
    Value math = Value::object();
    math.set("PI", Value(std::numbers::pi));
    math.set("E", Value(std::numbers::e));
    math.set("LN2", Value(std::numbers::ln2));
    math.set("LN10", Value(std::numbers::ln10));
    math.set("LOG2E", Value(std::numbers::log2e));
    math.set("LOG10E", Value(std::numbers::log10e));
    math.set("SQRT2", Value(std::numbers::sqrt2));
    math.set("SQRT1_2", Value(1 / std::numbers::sqrt2));

    math.set("abs", mathFn1("abs", [](double x) { return std::fabs(x); }));
    math.set("acos", mathFn1("acos", [](double x) { return std::acos(x); }));
    math.set("asin", mathFn1("asin", [](double x) { return std::asin(x); }));
    math.set("atan", mathFn1("atan", [](double x) { return std::atan(x); }));
    math.set("cbrt", mathFn1("cbrt", [](double x) { return std::cbrt(x); }));
    math.set("ceil", mathFn1("ceil", [](double x) { return std::ceil(x); }));
    math.set("cos", mathFn1("cos", [](double x) { return std::cos(x); }));
    math.set("exp", mathFn1("exp", [](double x) { return std::exp(x); }));
    math.set("floor", mathFn1("floor", [](double x) { return std::floor(x); }));
    math.set("log", mathFn1("log", [](double x) { return std::log(x); }));
    math.set("log10", mathFn1("log10", [](double x) { return std::log10(x); }));
    math.set("log2", mathFn1("log2", [](double x) { return std::log2(x); }));
    math.set("round", mathFn1("round", [](double x) { return js::mathRound(x); }));
    math.set("sign", mathFn1("sign", [](double x) {
                 if (std::isnan(x) || x == 0) return x;
                 return x > 0 ? 1.0 : -1.0;
             }));
    math.set("sin", mathFn1("sin", [](double x) { return std::sin(x); }));
    math.set("sqrt", mathFn1("sqrt", [](double x) { return std::sqrt(x); }));
    math.set("tan", mathFn1("tan", [](double x) { return std::tan(x); }));
    math.set("trunc", mathFn1("trunc", [](double x) { return std::trunc(x); }));
    math.set("atan2", fn("atan2", [](const Value&, const Args& a) -> std::optional<Value> {
                 return Value(std::atan2(numArg(a, 0), numArg(a, 1)));
             }));
    math.set("pow", fn("pow", [](const Value&, const Args& a) -> std::optional<Value> {
                 return Value(std::pow(numArg(a, 0), numArg(a, 1)));
             }));
    math.set("hypot", fn("hypot", [](const Value&, const Args& a) -> std::optional<Value> {
                 double sum = 0;
                 for (const Value& v : a) {
                     const double d = toNumber(v);
                     sum += d * d;
                 }
                 return Value(std::sqrt(sum));
             }));
    math.set("max", fn("max", [](const Value&, const Args& a) -> std::optional<Value> {
                 double result = -std::numeric_limits<double>::infinity();
                 for (const Value& v : a) {
                     const double d = toNumber(v);
                     if (std::isnan(d)) return Value(kNaN);
                     result = std::max(result, d);
                 }
                 return Value(result);
             }));
    math.set("min", fn("min", [](const Value&, const Args& a) -> std::optional<Value> {
                 double result = std::numeric_limits<double>::infinity();
                 for (const Value& v : a) {
                     const double d = toNumber(v);
                     if (std::isnan(d)) return Value(kNaN);
                     result = std::min(result, d);
                 }
                 return Value(result);
             }));
    math.set("random", fn("random", [](const Value&, const Args&) -> std::optional<Value> {
                 static thread_local std::mt19937_64 engine{std::random_device{}()};
                 return Value(std::uniform_real_distribution<double>(0.0, 1.0)(engine));
             }));
    return math;
}

Value makeNumber() {
    Value number = fn("Number", [](const Value&, const Args& a) -> std::optional<Value> {
        return Value(a.empty() ? 0.0 : toNumber(a[0]));
    });
    auto& props = *number.functionData().properties;
    props.set("isNaN", fn("isNaN", [](const Value&, const Args& a) -> std::optional<Value> {
                  const Value v = arg(a, 0);
                  return Value(v.isNumber() && std::isnan(v.asNumber()));
              }));
    props.set("isFinite", fn("isFinite", [](const Value&, const Args& a) -> std::optional<Value> {
                  const Value v = arg(a, 0);
                  return Value(v.isNumber() && std::isfinite(v.asNumber()));
              }));
    props.set("isInteger", fn("isInteger", [](const Value&, const Args& a) -> std::optional<Value> {
                  const Value v = arg(a, 0);
                  return Value(v.isNumber() && std::isfinite(v.asNumber()) && std::trunc(v.asNumber()) == v.asNumber());
              }));
    props.set("parseFloat", fn("parseFloat", [](const Value&, const Args& a) -> std::optional<Value> {
                  return Value(js::parseFloat(toString(arg(a, 0))));
              }));
    props.set("parseInt", fn("parseInt", [](const Value&, const Args& a) -> std::optional<Value> {
                  return Value(js::parseInt(toString(arg(a, 0)), a.size() > 1 ? static_cast<int>(numArg(a, 1)) : 0));
              }));
    props.set("MAX_SAFE_INTEGER", Value(9007199254740991.0));
    props.set("MIN_SAFE_INTEGER", Value(-9007199254740991.0));
    props.set("EPSILON", Value(std::numeric_limits<double>::epsilon()));
    props.set("MAX_VALUE", Value(std::numeric_limits<double>::max()));
    props.set("MIN_VALUE", Value(std::numeric_limits<double>::denorm_min()));
    props.set("POSITIVE_INFINITY", Value(std::numeric_limits<double>::infinity()));
    props.set("NEGATIVE_INFINITY", Value(-std::numeric_limits<double>::infinity()));
    props.set("NaN", Value(kNaN));
    return number;
}

Value makeString() {
    Value string = fn("String", [](const Value&, const Args& a) -> std::optional<Value> {
        return Value(a.empty() ? std::string() : toString(a[0]));
    });
    string.functionData().properties->set(
        "fromCharCode", fn("fromCharCode", [](const Value&, const Args& a) -> std::optional<Value> {
            std::string out;
            for (const Value& v : a) {
                const auto code = static_cast<unsigned>(toInt32(v)) & 0xFFFFu;
                if (code < 0x80) {
                    out += static_cast<char>(code);
                } else if (code < 0x800) {
                    out += static_cast<char>(0xC0 | (code >> 6));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                } else {
                    out += static_cast<char>(0xE0 | (code >> 12));
                    out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (code & 0x3F));
                }
            }
            return Value(out);
        }));
    return string;
}

Value makeJson() {
    Value json = Value::object();
    json.set("stringify", fn("stringify", [](const Value&, const Args& a) -> std::optional<Value> {
                 std::optional<std::string> text = toJson(arg(a, 0));
                 return text ? Value(*text) : Value{};
             }));
    json.set("parse", fn("parse", [](const Value&, const Args& a) -> std::optional<Value> {
                 return fromJson(toString(arg(a, 0)));  // syntax error -> unresolved
             }));
    return json;
}

Value makeObject() {
    Value object = fn("Object", [](const Value&, const Args& a) -> std::optional<Value> {
        const Value v = arg(a, 0);
        return v.isNullish() ? Value::object() : v;
    });
    auto& props = *object.functionData().properties;
    props.set("keys", fn("keys", [](const Value&, const Args& a) -> std::optional<Value> {
                  const Value v = arg(a, 0);
                  if (v.isNullish()) return std::nullopt;
                  std::vector<Value> keys;
                  if (v.isObject()) {
                      for (const auto& [key, value] : v.objectData().properties) keys.emplace_back(key);
                  } else if (v.isArray()) {
                      for (std::size_t i = 0; i < v.arrayData().items.size(); ++i) keys.emplace_back(std::to_string(i));
                  }
                  return Value::array(std::move(keys));
              }));
    props.set("values", fn("values", [](const Value&, const Args& a) -> std::optional<Value> {
                  const Value v = arg(a, 0);
                  if (v.isNullish()) return std::nullopt;
                  std::vector<Value> values;
                  if (v.isObject()) {
                      for (const auto& [key, value] : v.objectData().properties) values.push_back(value);
                  } else if (v.isArray()) {
                      values = v.arrayData().items;
                  }
                  return Value::array(std::move(values));
              }));
    return object;
}

Value makeDate() {
    Value date = fn("Date", [](const Value&, const Args&) -> std::optional<Value> {
        return std::nullopt;  // constructing dates is not supported
    });
    date.functionData().properties->set("now", fn("now", [](const Value&, const Args&) -> std::optional<Value> {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        return Value(static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(now).count()));
    }));
    return date;
}

// ---- primitive members ---------------------------------------------------------

std::optional<Value> stringMember(const std::string& s, std::string_view name) {
    std::size_t index = 0;
    if (name == "length") {
        return Value(static_cast<double>(s.size()));
    }
    if (!name.empty() && std::all_of(name.begin(), name.end(), [](char c) { return str::isAsciiDigit(c); })) {
        index = static_cast<std::size_t>(js::stringToNumber(name));
        return index < s.size() ? Value(std::string(1, s[index])) : Value{};
    }
    const auto method = [&](NativeFunction f) { return fn(std::string(name), std::move(f)); };

    if (name == "toUpperCase") {
        return method([](const Value& self, const Args&) -> std::optional<Value> { return Value(str::toUpper(toString(self))); });
    }
    if (name == "toLowerCase") {
        return method([](const Value& self, const Args&) -> std::optional<Value> { return Value(str::toLower(toString(self))); });
    }
    if (name == "trim") {
        return method([](const Value& self, const Args&) -> std::optional<Value> { return Value(str::trim(toString(self))); });
    }
    if (name == "trimStart") {
        return method([](const Value& self, const Args&) -> std::optional<Value> { return Value(str::trimLeft(toString(self))); });
    }
    if (name == "trimEnd") {
        return method([](const Value& self, const Args&) -> std::optional<Value> { return Value(str::trimRight(toString(self))); });
    }
    if (name == "toString" || name == "valueOf") {
        return method([](const Value& self, const Args&) -> std::optional<Value> { return Value(toString(self)); });
    }
    if (name == "includes") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            return Value(toString(self).find(toString(arg(a, 0))) != std::string::npos);
        });
    }
    if (name == "startsWith") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            return Value(toString(self).starts_with(toString(arg(a, 0))));
        });
    }
    if (name == "endsWith") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            return Value(toString(self).ends_with(toString(arg(a, 0))));
        });
    }
    if (name == "indexOf" || name == "lastIndexOf") {
        const bool last = name == "lastIndexOf";
        return method([last](const Value& self, const Args& a) -> std::optional<Value> {
            const std::string text = toString(self);
            const std::string needle = toString(arg(a, 0));
            const std::size_t pos = last ? text.rfind(needle) : text.find(needle);
            return Value(pos == std::string::npos ? -1.0 : static_cast<double>(pos));
        });
    }
    if (name == "charAt") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            const std::string text = toString(self);
            const double i = a.empty() ? 0 : numArg(a, 0);
            if (std::isnan(i) || i < 0 || i >= static_cast<double>(text.size())) return Value("");
            return Value(std::string(1, text[static_cast<std::size_t>(i)]));
        });
    }
    if (name == "charCodeAt") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            const std::string text = toString(self);
            const double i = a.empty() ? 0 : numArg(a, 0);
            if (std::isnan(i) || i < 0 || i >= static_cast<double>(text.size())) return Value(kNaN);
            return Value(static_cast<double>(static_cast<unsigned char>(text[static_cast<std::size_t>(i)])));
        });
    }
    if (name == "slice" || name == "substring" || name == "substr") {
        const std::string kind(name);
        return method([kind](const Value& self, const Args& a) -> std::optional<Value> {
            const std::string text = toString(self);
            const std::size_t size = text.size();
            if (kind == "substr") {
                const std::size_t start = relativeIndex(a.empty() ? 0 : numArg(a, 0), size, 0);
                const double length = a.size() > 1 ? numArg(a, 1) : static_cast<double>(size);
                const std::size_t count = std::isnan(length) || length < 0 ? 0 : static_cast<std::size_t>(std::min(length, static_cast<double>(size)));
                return Value(text.substr(start, count));
            }
            if (kind == "slice") {
                const std::size_t start = relativeIndex(a.empty() ? 0 : numArg(a, 0), size, 0);
                const std::size_t end = a.size() > 1 && !arg(a, 1).isUndefined() ? relativeIndex(numArg(a, 1), size, size) : size;
                return Value(end > start ? text.substr(start, end - start) : std::string());
            }
            const auto clampIndex = [size](double v) -> std::size_t {
                if (std::isnan(v) || v < 0) return 0;
                return v > static_cast<double>(size) ? size : static_cast<std::size_t>(v);
            };
            std::size_t start = clampIndex(a.empty() ? 0 : numArg(a, 0));
            std::size_t end = a.size() > 1 && !arg(a, 1).isUndefined() ? clampIndex(numArg(a, 1)) : size;
            if (start > end) std::swap(start, end);
            return Value(text.substr(start, end - start));
        });
    }
    if (name == "split") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            const std::string text = toString(self);
            std::vector<Value> parts;
            if (a.empty() || arg(a, 0).isUndefined()) {
                parts.emplace_back(text);
                return Value::array(std::move(parts));
            }
            const std::string sep = toString(arg(a, 0));
            if (sep.empty()) {
                for (char c : text) parts.emplace_back(std::string(1, c));
                return Value::array(std::move(parts));
            }
            std::size_t start = 0;
            while (true) {
                const std::size_t pos = text.find(sep, start);
                if (pos == std::string::npos) {
                    parts.emplace_back(text.substr(start));
                    break;
                }
                parts.emplace_back(text.substr(start, pos - start));
                start = pos + sep.size();
            }
            return Value::array(std::move(parts));
        });
    }
    if (name == "replace" || name == "replaceAll") {
        const bool all = name == "replaceAll";
        return method([all](const Value& self, const Args& a) -> std::optional<Value> {
            std::string text = toString(self);
            const std::string from = toString(arg(a, 0));
            const std::string to = toString(arg(a, 1));
            if (all) return Value(str::replaceAll(text, from, to));
            const std::size_t pos = text.find(from);
            if (pos != std::string::npos) text.replace(pos, from.size(), to);
            return Value(text);
        });
    }
    if (name == "padStart" || name == "padEnd") {
        const bool start = name == "padStart";
        return method([start](const Value& self, const Args& a) -> std::optional<Value> {
            std::string text = toString(self);
            const double target = numArg(a, 0);
            const std::string pad = a.size() > 1 ? toString(arg(a, 1)) : std::string(" ");
            if (std::isnan(target) || target <= static_cast<double>(text.size()) || pad.empty()) return Value(text);
            std::string fill;
            const auto needed = static_cast<std::size_t>(target) - text.size();
            while (fill.size() < needed) fill += pad;
            fill.resize(needed);
            return Value(start ? fill + text : text + fill);
        });
    }
    if (name == "repeat") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            const double count = numArg(a, 0);
            if (std::isnan(count) || count < 0 || !std::isfinite(count)) return std::nullopt;
            std::string out;
            const std::string text = toString(self);
            for (int i = 0; i < static_cast<int>(count); ++i) out += text;
            return Value(out);
        });
    }
    if (name == "concat") {
        return method([](const Value& self, const Args& a) -> std::optional<Value> {
            std::string out = toString(self);
            for (const Value& v : a) out += toString(v);
            return Value(out);
        });
    }
    return std::nullopt;
}

std::optional<Value> numberMember(std::string_view name) {
    if (name == "toFixed") {
        return fn("toFixed", [](const Value& self, const Args& a) -> std::optional<Value> {
            const double digits = a.empty() ? 0 : numArg(a, 0);
            if (std::isnan(digits) || digits < 0 || digits > 100) return std::nullopt;  // RangeError
            return Value(js::toFixed(toNumber(self), static_cast<int>(digits)));
        });
    }
    if (name == "toString") {
        return fn("toString", [](const Value& self, const Args& a) -> std::optional<Value> {
            const double radix = a.empty() || arg(a, 0).isUndefined() ? 10 : numArg(a, 0);
            const double value = toNumber(self);
            if (radix == 10) return Value(js::numberToString(value));
            if (radix < 2 || radix > 36 || !std::isfinite(value) || std::trunc(value) != value) return std::nullopt;
            static const char* kDigits = "0123456789abcdefghijklmnopqrstuvwxyz";
            const int base = static_cast<int>(radix);
            double magnitude = std::fabs(value);
            std::string digits;
            do {
                digits.insert(digits.begin(), kDigits[static_cast<int>(std::fmod(magnitude, base))]);
                magnitude = std::floor(magnitude / base);
            } while (magnitude > 0);
            return Value(value < 0 ? "-" + digits : digits);
        });
    }
    if (name == "valueOf") {
        return fn("valueOf", [](const Value& self, const Args&) -> std::optional<Value> { return Value(toNumber(self)); });
    }
    return std::nullopt;
}

std::optional<Value> arrayMember(const Value& array, std::string_view name) {
    if (name == "length") {
        return Value(static_cast<double>(array.arrayData().items.size()));
    }
    if (name == "join") {
        return fn("join", [](const Value& self, const Args& a) -> std::optional<Value> {
            const std::string sep = a.empty() || arg(a, 0).isUndefined() ? std::string(",") : toString(arg(a, 0));
            std::string out;
            const auto& items = self.arrayData().items;
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (i > 0) out += sep;
                if (!items[i].isNullish()) out += toString(items[i]);
            }
            return Value(out);
        });
    }
    if (name == "indexOf" || name == "includes") {
        const bool includes = name == "includes";
        return fn(std::string(name), [includes](const Value& self, const Args& a) -> std::optional<Value> {
            const auto& items = self.arrayData().items;
            const Value needle = arg(a, 0);
            for (std::size_t i = 0; i < items.size(); ++i) {
                if (strictEquals(items[i], needle)) {
                    return includes ? Value(true) : Value(static_cast<double>(i));
                }
            }
            return includes ? Value(false) : Value(-1.0);
        });
    }
    if (name == "slice") {
        return fn("slice", [](const Value& self, const Args& a) -> std::optional<Value> {
            const auto& items = self.arrayData().items;
            const std::size_t size = items.size();
            const std::size_t start = relativeIndex(a.empty() ? 0 : numArg(a, 0), size, 0);
            const std::size_t end = a.size() > 1 && !arg(a, 1).isUndefined() ? relativeIndex(numArg(a, 1), size, size) : size;
            std::vector<Value> out;
            for (std::size_t i = start; i < end; ++i) out.push_back(items[i]);
            return Value::array(std::move(out));
        });
    }
    if (name == "concat") {
        return fn("concat", [](const Value& self, const Args& a) -> std::optional<Value> {
            std::vector<Value> out = self.arrayData().items;
            for (const Value& v : a) {
                if (v.isArray()) {
                    out.insert(out.end(), v.arrayData().items.begin(), v.arrayData().items.end());
                } else {
                    out.push_back(v);
                }
            }
            return Value::array(std::move(out));
        });
    }
    if (name == "toString") {
        return fn("toString", [](const Value& self, const Args&) -> std::optional<Value> { return Value(toString(self)); });
    }
    return std::nullopt;
}

}  // namespace

namespace detail {

std::optional<Value> primitiveMember(const Value& base, std::string_view name) {
    switch (base.kind()) {
        case Value::Kind::String:
            return stringMember(base.asString(), name);
        case Value::Kind::Number:
            return numberMember(name);
        case Value::Kind::Array:
            return arrayMember(base, name);
        case Value::Kind::Boolean:
            if (name == "toString" || name == "valueOf") {
                return fn(std::string(name), [](const Value& self, const Args&) -> std::optional<Value> {
                    return Value(toString(self));
                });
            }
            return std::nullopt;
        default:
            return std::nullopt;
    }
}

}  // namespace detail

void installGlobals(const Value& scope) {
    scope.set("Math", makeMath());
    scope.set("Number", makeNumber());
    scope.set("String", makeString());
    scope.set("Boolean", fn("Boolean", [](const Value&, const Args& a) -> std::optional<Value> {
                  return Value(!a.empty() && toBoolean(a[0]));
              }));
    scope.set("JSON", makeJson());
    scope.set("Object", makeObject());
    scope.set("Date", makeDate());
    scope.set("parseFloat", fn("parseFloat", [](const Value&, const Args& a) -> std::optional<Value> {
                  return Value(js::parseFloat(toString(arg(a, 0))));
              }));
    scope.set("parseInt", fn("parseInt", [](const Value&, const Args& a) -> std::optional<Value> {
                  return Value(js::parseInt(toString(arg(a, 0)), a.size() > 1 ? static_cast<int>(numArg(a, 1)) : 0));
              }));
}

}  // namespace gs::expr
