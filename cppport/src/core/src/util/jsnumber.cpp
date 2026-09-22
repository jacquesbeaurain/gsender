#include "gs/util/jsnumber.hpp"

#include "gs/util/strings.hpp"

#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <string>
#include <system_error>

namespace gs::js {
namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
constexpr double kInf = std::numeric_limits<double>::infinity();

// Fixed notation with `precision` fraction digits of the exact binary value
// (round-half-even on exact ties, as printf does).
std::string toCharsFixed(double value, int precision) {
    // |value| < 1e21 here, so the integer part has at most 22 digits.
    std::string buffer(static_cast<std::size_t>(precision) + 32, '\0');
    auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value,
                                std::chars_format::fixed, precision);
    buffer.resize(static_cast<std::size_t>(result.ptr - buffer.data()));
    return buffer;
}

// Adds one unit in the last decimal place of a non-negative decimal string.
std::string incrementLastDigit(std::string digits) {
    for (std::size_t i = digits.size(); i-- > 0;) {
        if (digits[i] == '.') {
            continue;
        }
        if (digits[i] == '9') {
            digits[i] = '0';
            continue;
        }
        ++digits[i];
        return digits;
    }
    return "1" + digits;
}

// True when `value` lies exactly halfway between two multiples of 10^-digits.
bool isExactTie(double value, int digits) {
    // Cheap filter: a tie shows as "...5000..." well past the rounding digit.
    const std::string probe = toCharsFixed(value, digits + 20);
    const std::size_t tieDigit = probe.size() - 20;
    if (probe[tieDigit] != '5' || probe.find_first_not_of('0', tieDigit + 1) != std::string::npos) {
        return false;
    }
    // Confirm on the full expansion; a double has at most 1074 fraction digits.
    const std::string exact = toCharsFixed(value, 1100);
    const std::size_t point = exact.find('.');
    const std::size_t exactTieDigit = point + 1 + static_cast<std::size_t>(digits);
    return exact[exactTieDigit] == '5' &&
           exact.find_first_not_of('0', exactTieDigit + 1) == std::string::npos;
}

// Validates a StrDecimalLiteral without sign; returns the normalized form
// accepted by from_chars ("5." -> "5.0", ".5" -> "0.5") or an empty string.
std::string normalizeDecimal(std::string_view s) {
    std::size_t i = 0;
    const std::size_t intStart = i;
    while (i < s.size() && str::isAsciiDigit(s[i])) {
        ++i;
    }
    std::string_view intPart = s.substr(intStart, i - intStart);
    std::string_view fracPart;
    if (i < s.size() && s[i] == '.') {
        ++i;
        const std::size_t fracStart = i;
        while (i < s.size() && str::isAsciiDigit(s[i])) {
            ++i;
        }
        fracPart = s.substr(fracStart, i - fracStart);
    }
    if (intPart.empty() && fracPart.empty()) {
        return {};
    }
    std::string exponent;
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        ++i;
        if (i < s.size() && (s[i] == '+' || s[i] == '-')) {
            exponent.push_back(s[i]);
            ++i;
        }
        const std::size_t expStart = i;
        while (i < s.size() && str::isAsciiDigit(s[i])) {
            ++i;
        }
        if (i == expStart) {
            return {};
        }
        exponent.append(s.substr(expStart, i - expStart));
    }
    if (i != s.size()) {
        return {};
    }
    std::string normalized(intPart.empty() ? std::string_view("0") : intPart);
    if (!fracPart.empty()) {
        normalized.push_back('.');
        normalized.append(fracPart);
    }
    if (!exponent.empty()) {
        normalized.push_back('e');
        normalized.append(exponent);
    }
    return normalized;
}

// Converts a validated, normalized decimal literal.
double convertDecimal(const std::string& normalized) {
    double value = 0;
    auto [ptr, ec] = std::from_chars(normalized.data(), normalized.data() + normalized.size(), value);
    if (ec == std::errc::result_out_of_range) {
        // Overflow becomes Infinity, underflow becomes 0 - as in JavaScript.
        const std::size_t e = normalized.find('e');
        const bool negativeExponent = e != std::string::npos && e + 1 < normalized.size() &&
                                      normalized[e + 1] == '-';
        return negativeExponent ? 0.0 : kInf;
    }
    if (ec != std::errc() || ptr != normalized.data() + normalized.size()) {
        return kNaN;
    }
    return value;
}

int digitValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'z') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 10;
    }
    return 99;
}

}  // namespace

std::string numberToString(double value) {
    if (std::isnan(value)) {
        return "NaN";
    }
    if (value == 0) {
        return "0";
    }
    if (std::isinf(value)) {
        return value < 0 ? "-Infinity" : "Infinity";
    }
    if (value < 0) {
        return "-" + numberToString(-value);
    }

    char buffer[64];
    auto result = std::to_chars(buffer, buffer + sizeof(buffer), value, std::chars_format::scientific);
    const std::string_view scientific(buffer, static_cast<std::size_t>(result.ptr - buffer));
    const std::size_t ePos = scientific.find('e');

    std::string digits;
    for (char c : scientific.substr(0, ePos)) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    int exponent = 0;
    std::string_view expText = scientific.substr(ePos + 1);
    if (!expText.empty() && expText.front() == '+') {
        expText.remove_prefix(1);
    }
    std::from_chars(expText.data(), expText.data() + expText.size(), exponent);

    const int k = static_cast<int>(digits.size());
    const int n = exponent + 1;  // decimal point position, per ECMA-262

    if (k <= n && n <= 21) {
        return digits + std::string(static_cast<std::size_t>(n - k), '0');
    }
    if (0 < n && n <= 21) {
        return digits.substr(0, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
    }
    if (-6 < n && n <= 0) {
        return "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
    }
    const int e = n - 1;
    std::string exp = (e < 0 ? "-" : "+") + std::to_string(e < 0 ? -e : e);
    if (k == 1) {
        return digits + "e" + exp;
    }
    return digits.substr(0, 1) + "." + digits.substr(1) + "e" + exp;
}

std::string toFixed(double value, int digits) {
    if (digits < 0) {
        digits = 0;
    } else if (digits > 100) {
        digits = 100;
    }
    if (std::isnan(value)) {
        return "NaN";
    }
    if (std::fabs(value) >= 1e21) {
        return numberToString(value);
    }
    const bool negative = value < 0;  // -0 formats without a sign, as in JS
    const double magnitude = negative ? -value : value;

    std::string formatted;
    if (isExactTie(magnitude, digits)) {
        // Exact tie: JavaScript picks the larger candidate.
        std::string truncated = toCharsFixed(magnitude, digits + 1);
        truncated.pop_back();  // the tie digit '5'
        if (!truncated.empty() && truncated.back() == '.') {
            truncated.pop_back();
        }
        formatted = incrementLastDigit(std::move(truncated));
    } else {
        formatted = toCharsFixed(magnitude, digits);
    }
    return negative ? "-" + formatted : formatted;
}

double stringToNumber(std::string_view text) {
    std::string_view s = str::trim(text);
    if (s.empty()) {
        return 0;
    }
    if (s == "Infinity" || s == "+Infinity") {
        return kInf;
    }
    if (s == "-Infinity") {
        return -kInf;
    }
    if (s.size() > 2 && s[0] == '0') {
        int radix = 0;
        switch (s[1]) {
            case 'x':
            case 'X':
                radix = 16;
                break;
            case 'o':
            case 'O':
                radix = 8;
                break;
            case 'b':
            case 'B':
                radix = 2;
                break;
            default:
                break;
        }
        if (radix != 0) {
            double result = 0;
            for (char c : s.substr(2)) {
                const int d = digitValue(c);
                if (d >= radix) {
                    return kNaN;
                }
                result = result * radix + d;
            }
            return result;
        }
    }
    double sign = 1;
    if (s.front() == '+' || s.front() == '-') {
        sign = s.front() == '-' ? -1 : 1;
        s.remove_prefix(1);
    }
    const std::string normalized = normalizeDecimal(s);
    if (normalized.empty()) {
        return kNaN;
    }
    return sign * convertDecimal(normalized);
}

double parseFloat(std::string_view text) {
    std::string_view s = str::trimLeft(text);
    double sign = 1;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        sign = s.front() == '-' ? -1 : 1;
        s.remove_prefix(1);
    }
    if (s.starts_with("Infinity")) {
        return sign * kInf;
    }
    // Longest prefix that is a decimal literal.
    std::size_t i = 0;
    std::size_t intDigits = 0;
    while (i < s.size() && str::isAsciiDigit(s[i])) {
        ++i;
        ++intDigits;
    }
    std::size_t fracDigits = 0;
    if (i < s.size() && s[i] == '.') {
        std::size_t j = i + 1;
        while (j < s.size() && str::isAsciiDigit(s[j])) {
            ++j;
            ++fracDigits;
        }
        if (intDigits > 0 || fracDigits > 0) {
            i = j;
        }
    }
    if (intDigits == 0 && fracDigits == 0) {
        return kNaN;
    }
    if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
        std::size_t j = i + 1;
        if (j < s.size() && (s[j] == '+' || s[j] == '-')) {
            ++j;
        }
        const std::size_t expStart = j;
        while (j < s.size() && str::isAsciiDigit(s[j])) {
            ++j;
        }
        if (j > expStart) {
            i = j;
        }
    }
    const std::string normalized = normalizeDecimal(s.substr(0, i));
    return normalized.empty() ? kNaN : sign * convertDecimal(normalized);
}

double parseInt(std::string_view text, int radix) {
    std::string_view s = str::trimLeft(text);
    double sign = 1;
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        sign = s.front() == '-' ? -1 : 1;
        s.remove_prefix(1);
    }
    bool stripPrefix = true;
    if (radix != 0) {
        if (radix < 2 || radix > 36) {
            return kNaN;
        }
        if (radix != 16) {
            stripPrefix = false;
        }
    } else {
        radix = 10;
    }
    if (stripPrefix && s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        s.remove_prefix(2);
        radix = 16;
    }
    double result = 0;
    std::size_t consumed = 0;
    for (char c : s) {
        const int d = digitValue(c);
        if (d >= radix) {
            break;
        }
        result = result * radix + d;
        ++consumed;
    }
    return consumed == 0 ? kNaN : sign * result;
}

double mathRound(double value) {
    if (!std::isfinite(value)) {
        return value;
    }
    const double floored = std::floor(value);
    return (value - floored >= 0.5) ? floored + 1 : floored;
}

bool isFinite(double value) {
    return std::isfinite(value);
}

}  // namespace gs::js
