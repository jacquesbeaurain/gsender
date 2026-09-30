#pragma once

// Faithful ports of the JavaScript number <-> string conversions that gSender's
// logic relies on. Generated G-code ("X${x.toFixed(3)}"), macro expression
// results and settings values must format exactly like the original, so these
// follow the ECMAScript algorithms rather than printf conventions.

#include <cmath>
#include <string>
#include <string_view>

namespace gs::js {

// Number.prototype.toString() in radix 10 (shortest round-trip digits,
// exponent notation outside [1e-7, 1e21)).
std::string numberToString(double value);

// Number.prototype.toFixed(digits) for 0 <= digits <= 100. Ties round away
// from zero on the exact binary value, unlike printf's round-half-even.
std::string toFixed(double value, int digits);

// Number(string): the whole (trimmed) string must be a numeric literal,
// "" is 0, anything else is NaN.
double stringToNumber(std::string_view text);

// Global parseFloat(): longest numeric prefix, NaN when there is none.
double parseFloat(std::string_view text);

// Global parseInt(text, radix). A radix of 0 means "detect" (10, or 16 for 0x).
double parseInt(std::string_view text, int radix = 10);

// Math.round(): halves round towards +infinity.
double mathRound(double value);

// True for anything other than NaN and +/-Infinity.
bool isFinite(double value);

// Number(value.toFixed(digits)): rounding to `digits` places the way
// gSender does before it compares or formats a coordinate.
double toFixedNumber(double value, int digits);

// Number(value.toFixed(3)), the rounding most of gSender uses.
inline double fixed3(double value) {
    return toFixedNumber(value, 3);
}

// Boolean(value): false for 0 and NaN.
inline bool truthy(double value) {
    return value != 0 && !std::isnan(value);
}

}  // namespace gs::js
