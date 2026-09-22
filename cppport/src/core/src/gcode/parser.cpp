#include "gs/gcode/parser.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

namespace gs::gcode {
namespace {

bool isSignedNumberChar(char c) noexcept {
    return str::isAsciiDigit(c) || c == '+' || c == '-' || c == '.';
}

bool isAlphaNumericHash(char c) noexcept {
    return str::isAsciiAlpha(c) || str::isAsciiDigit(c) || c == '#';
}

bool isAllowedWordLetter(char upper) noexcept {
    switch (upper) {
        case 'N':
        case 'G':
        case 'M':
        case 'X':
        case 'Y':
        case 'Z':
        case 'H':
        case 'I':
        case 'L':
        case 'T':
        case 'P':
        case 'A':
        case 'J':
        case 'K':
        case 'F':
        case 'R':
        case 'S':
            return true;
        default:
            return false;
    }
}

// Removes comments into `out` (whitespace is still present).
void stripComments(std::string_view line, std::string& out) {
    out.clear();
    out.reserve(line.size());
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == ';') {
            break;
        }
        if (c == '(') {
            const std::size_t close = line.find(')', i + 1);
            if (close == std::string_view::npos) {
                break;
            }
            i = close;
            continue;
        }
        out.push_back(c);
    }
}

// The word matcher shared by parseLine() and scanLine(): emulates the global
// regex /(%.*)|({.*)|((?:\$\$)|(?:\$[a-zA-Z0-9#]*))|([a-zA-Z][0-9+\-.]+)|(\*[0-9]+)/
// on the stripped line. `onWord` receives each match; `onSkip` each skipped byte.
template <typename OnWord, typename OnSkip>
void matchWords(std::string_view s, OnWord&& onWord, OnSkip&& onSkip) {
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const char c = s[i];
        if (c == '%' || c == '{') {
            onWord(s.substr(i));
            return;
        }
        if (c == '$') {
            if (i + 1 < n && s[i + 1] == '$') {
                onWord(s.substr(i, 2));
                i += 2;
                continue;
            }
            std::size_t j = i + 1;
            while (j < n && isAlphaNumericHash(s[j])) {
                ++j;
            }
            onWord(s.substr(i, j - i));
            i = j;
            continue;
        }
        if (str::isAsciiAlpha(c)) {
            std::size_t j = i + 1;
            while (j < n && isSignedNumberChar(s[j])) {
                ++j;
            }
            if (j > i + 1) {
                onWord(s.substr(i, j - i));
                i = j;
                continue;
            }
            onSkip(c);
            ++i;
            continue;
        }
        if (c == '*') {
            std::size_t j = i + 1;
            while (j < n && str::isAsciiDigit(s[j])) {
                ++j;
            }
            if (j > i + 1) {
                onWord(s.substr(i, j - i));
                i = j;
                continue;
            }
            onSkip(c);
            ++i;
            continue;
        }
        onSkip(c);
        ++i;
    }
}

int computeChecksum(std::string_view s) {
    const std::size_t star = s.rfind('*');
    if (star != std::string_view::npos) {
        s = s.substr(0, star);
    }
    int cs = 0;
    for (char c : s) {
        cs ^= static_cast<unsigned char>(c);
    }
    return cs;
}

}  // namespace

std::string Word::flat() const {
    std::string out(1, letter);
    out += isNumeric() ? js::numberToString(value) : argument;
    return out;
}

bool ParsedLine::hasWord(std::string_view flatWord) const {
    for (const Word& word : words) {
        if (word.flat() == flatWord) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ParsedLine::flatWords() const {
    std::vector<std::string> out;
    out.reserve(words.size());
    for (const Word& word : words) {
        out.push_back(word.flat());
    }
    return out;
}

std::string stripCommentsAndWhitespace(std::string_view line) {
    std::string withoutComments;
    stripComments(line, withoutComments);
    return str::removeWhitespace(withoutComments);
}

ParsedLine parseLine(std::string_view line) {
    ParsedLine result;
    result.line = std::string(line);

    const std::string stripped = stripCommentsAndWhitespace(line);
    matchWords(
        stripped,
        [&](std::string_view word) {
            const char letter = str::toUpperAscii(word.front());
            const std::string_view argument = word.substr(1);

            if (letter == '%' || letter == '{') {
                result.cmds.emplace_back(str::trim(line));
                return;
            }
            if (letter == '$') {
                result.cmds.emplace_back(word);
                return;
            }
            if (letter == 'N' && !result.lineNumber) {
                result.lineNumber = js::stringToNumber(argument);
                return;
            }
            if (letter == '*' && !result.checksum) {
                result.checksum = js::stringToNumber(argument);
                return;
            }
            Word w;
            w.letter = letter;
            w.argument = std::string(argument);
            w.value = js::stringToNumber(argument);
            result.words.push_back(std::move(w));
        },
        [](char) {});

    if (result.checksum && *result.checksum != 0 && !std::isnan(*result.checksum)) {
        result.checksumError = computeChecksum(line) != *result.checksum;
    }
    return result;
}

std::vector<ParsedLine> parseText(std::string_view text) {
    std::vector<ParsedLine> results;
    for (std::string_view raw : str::splitLines(text)) {
        const std::string_view line = str::trim(raw);
        if (!line.empty()) {
            results.push_back(parseLine(line));
        }
    }
    return results;
}

std::string extractComments(std::string_view line) {
    // /\(([^)]*)\)|;(.*)/g - parenthesized text, or everything after ';'.
    std::vector<std::string> parts;
    std::size_t i = 0;
    while (i < line.size()) {
        const char c = line[i];
        if (c == '(') {
            const std::size_t close = line.find(')', i + 1);
            if (close == std::string_view::npos) {
                ++i;
                continue;
            }
            const std::string_view text = str::trim(line.substr(i + 1, close - i - 1));
            if (!text.empty()) {
                parts.emplace_back(text);
            }
            i = close + 1;
            continue;
        }
        if (c == ';') {
            const std::string_view text = str::trim(line.substr(i + 1));
            if (!text.empty()) {
                parts.emplace_back(text);
            }
            break;
        }
        ++i;
    }
    return str::join(parts, " ");
}

void scanLine(std::string_view line, LineScan& out) {
    out.tokens.clear();
    out.hasInvalidTokens = false;

    std::string& buffer = out.buffer;
    stripComments(line, buffer);
    // Drop whitespace in place: "G0 X 10" is "G0X10" to the firmware.
    std::size_t w = 0;
    for (std::size_t r = 0; r < buffer.size(); ++r) {
        if (!str::isAsciiSpace(buffer[r])) {
            buffer[w++] = buffer[r];
        }
    }
    buffer.resize(w);
    const std::string_view view = str::trim(buffer);  // BOM / NBSP at the ends

    matchWords(
        view,
        [&](std::string_view word) {
            const char first = word.front();
            if (first == '%' || first == '{') {
                out.tokens.push_back({first, str::trim(word.substr(1))});
                return;
            }
            if (first == '$') {
                if (word.size() == 1) {
                    out.hasInvalidTokens = true;
                    return;
                }
                out.tokens.push_back({'$', word.substr(1)});
                return;
            }
            if (first == '*') {
                out.tokens.push_back({'*', word.substr(1)});
                return;
            }
            const char letter = str::toUpperAscii(first);
            if (letter == 'N') {
                return;  // line numbers are consumed but not recorded
            }
            if (!isAllowedWordLetter(letter)) {
                out.hasInvalidTokens = true;
            }
            out.tokens.push_back({letter, word.substr(1)});
        },
        [&](char) { out.hasInvalidTokens = true; });
}

}  // namespace gs::gcode
