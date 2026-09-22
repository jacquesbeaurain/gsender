#pragma once

// Small string helpers shared by the core library. Everything operates on
// UTF-8 bytes; "whitespace" follows what JavaScript's String.prototype.trim()
// strips for the inputs gSender sees (ASCII whitespace, the UTF-8 BOM and
// non-breaking spaces), so ported logic keeps its behaviour.

#include <string>
#include <string_view>
#include <vector>

namespace gs::str {

bool isAsciiSpace(char c) noexcept;
bool isAsciiDigit(char c) noexcept;
bool isAsciiAlpha(char c) noexcept;
char toUpperAscii(char c) noexcept;
char toLowerAscii(char c) noexcept;

std::string_view trimLeft(std::string_view s) noexcept;
std::string_view trimRight(std::string_view s) noexcept;
std::string_view trim(std::string_view s) noexcept;

std::string toUpper(std::string_view s);
std::string toLower(std::string_view s);

bool iequals(std::string_view a, std::string_view b) noexcept;
bool icontains(std::string_view haystack, std::string_view needle) noexcept;
bool contains(std::string_view haystack, std::string_view needle) noexcept;

// JavaScript String.prototype.split(sep): keeps empty fields.
std::vector<std::string> split(std::string_view s, char sep);
std::vector<std::string_view> splitView(std::string_view s, char sep);

// Splits text into lines on "\r\n", "\n" or a lone "\r". A trailing line
// terminator does not produce an extra empty line.
std::vector<std::string_view> splitLines(std::string_view text);

std::string join(const std::vector<std::string>& parts, std::string_view sep);

std::string replaceAll(std::string s, std::string_view from, std::string_view to);

// Removes every whitespace character (as defined above) from s.
std::string removeWhitespace(std::string_view s);

}  // namespace gs::str
