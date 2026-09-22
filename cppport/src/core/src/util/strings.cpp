#include "gs/util/strings.hpp"

namespace gs::str {
namespace {

constexpr std::string_view kBom = "\xEF\xBB\xBF";
constexpr std::string_view kNbsp = "\xC2\xA0";

// Length of the whitespace sequence starting at s[0], or 0.
std::size_t leadingSpaceLength(std::string_view s) noexcept {
    if (s.empty()) {
        return 0;
    }
    if (isAsciiSpace(s.front())) {
        return 1;
    }
    if (s.starts_with(kBom)) {
        return kBom.size();
    }
    if (s.starts_with(kNbsp)) {
        return kNbsp.size();
    }
    return 0;
}

// Length of the whitespace sequence ending at s.back(), or 0.
std::size_t trailingSpaceLength(std::string_view s) noexcept {
    if (s.empty()) {
        return 0;
    }
    if (isAsciiSpace(s.back())) {
        return 1;
    }
    if (s.ends_with(kBom)) {
        return kBom.size();
    }
    if (s.ends_with(kNbsp)) {
        return kNbsp.size();
    }
    return 0;
}

}  // namespace

bool isAsciiSpace(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool isAsciiDigit(char c) noexcept {
    return c >= '0' && c <= '9';
}

bool isAsciiAlpha(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

char toUpperAscii(char c) noexcept {
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

char toLowerAscii(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

std::string_view trimLeft(std::string_view s) noexcept {
    while (std::size_t n = leadingSpaceLength(s)) {
        s.remove_prefix(n);
    }
    return s;
}

std::string_view trimRight(std::string_view s) noexcept {
    while (std::size_t n = trailingSpaceLength(s)) {
        s.remove_suffix(n);
    }
    return s;
}

std::string_view trim(std::string_view s) noexcept {
    return trimRight(trimLeft(s));
}

std::string toUpper(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = toUpperAscii(c);
    }
    return out;
}

std::string toLower(std::string_view s) {
    std::string out(s);
    for (char& c : out) {
        c = toLowerAscii(c);
    }
    return out;
}

bool iequals(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (toLowerAscii(a[i]) != toLowerAscii(b[i])) {
            return false;
        }
    }
    return true;
}

bool icontains(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty()) {
        return true;
    }
    if (needle.size() > haystack.size()) {
        return false;
    }
    for (std::size_t i = 0; i + needle.size() <= haystack.size(); ++i) {
        if (iequals(haystack.substr(i, needle.size()), needle)) {
            return true;
        }
    }
    return false;
}

bool contains(std::string_view haystack, std::string_view needle) noexcept {
    return haystack.find(needle) != std::string_view::npos;
}

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> parts;
    for (std::string_view part : splitView(s, sep)) {
        parts.emplace_back(part);
    }
    return parts;
}

std::vector<std::string_view> splitView(std::string_view s, char sep) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const std::size_t pos = s.find(sep, start);
        if (pos == std::string_view::npos) {
            parts.push_back(s.substr(start));
            break;
        }
        parts.push_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return parts;
}

std::vector<std::string_view> splitLines(std::string_view text) {
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c != '\n' && c != '\r') {
            continue;
        }
        lines.push_back(text.substr(start, i - start));
        if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n') {
            ++i;
        }
        start = i + 1;
    }
    if (start < text.size()) {
        lines.push_back(text.substr(start));
    }
    return lines;
}

std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

std::string replaceAll(std::string s, std::string_view from, std::string_view to) {
    if (from.empty()) {
        return s;
    }
    std::size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string removeWhitespace(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    while (!s.empty()) {
        if (std::size_t n = leadingSpaceLength(s)) {
            s.remove_prefix(n);
            continue;
        }
        out.push_back(s.front());
        s.remove_prefix(1);
    }
    return out;
}

}  // namespace gs::str
