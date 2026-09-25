#include "gs/gcode/highlight.hpp"

#include <array>
#include <cctype>

namespace gs::gcode {
namespace {

// JavaScript regexes without the u flag: \d, \w and case folding are ASCII
// only, so a byte-wise scan of UTF-8 classifies exactly as they do.
bool digit(char c) {
    return c >= '0' && c <= '9';
}
char lower(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}
bool identStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}
bool identPart(char c) {
    return identStart(c) || digit(c) || c == '.';
}
// Case-insensitive "starts with" at `at` (the grammar is case_insensitive).
bool startsWithI(std::string_view s, std::size_t at, std::string_view word) {
    if (s.size() - at < word.size()) {
        return false;
    }
    for (std::size_t i = 0; i < word.size(); ++i) {
        if (lower(s[at + i]) != lower(word[i])) {
            return false;
        }
    }
    return true;
}
std::size_t digitsFrom(std::string_view s, std::size_t at) {
    std::size_t end = at;
    while (end < s.size() && digit(s[end])) {
        ++end;
    }
    return end - at;
}

// NUMBER: C_NUMBER_MODE with begin `([-+]?((\.\d+)|(\d+)(\.\d*)?))|` +
// C_NUMBER_RE. Wherever the C_NUMBER_RE alternative could match, the first
// one matches too and wins (alternation takes the first that matches), so
// only the first is needed. 0 when none.
std::size_t numberAt(std::string_view s, std::size_t at) {
    const std::size_t start = at < s.size() && (s[at] == '-' || s[at] == '+') ? at + 1 : at;
    if (start < s.size() && s[start] == '.') {
        const std::size_t digits = digitsFrom(s, start + 1);
        return digits ? start + 1 + digits - at : 0;
    }
    const std::size_t digits = digitsFrom(s, start);
    if (!digits) {
        return 0;
    }
    std::size_t end = start + digits;
    if (end < s.size() && s[end] == '.') {
        end += 1 + digitsFrom(s, end + 1);
    }
    return end - at;
}

// '([G])([0-9]+\.?[0-9]?)' and the same for M.
std::size_t codeAt(std::string_view s, std::size_t at, char letter) {
    if (lower(s[at]) != letter) {
        return 0;
    }
    const std::size_t digits = digitsFrom(s, at + 1);
    if (!digits) {
        return 0;
    }
    std::size_t end = at + 1 + digits;
    if (end < s.size() && s[end] == '.') {
        ++end;
    }
    if (end < s.size() && digit(s[end])) {
        ++end;
    }
    return end - at;
}

constexpr std::array<std::string_view, 12> kBuiltIns{"ATAN", "ABS", "ACOS", "ASIN", "SIN", "COS",
                                                     "EXP",  "FIX", "FUP",  "ROUND", "LN", "TAN"};
constexpr std::array<std::string_view, 21> kKeywords{"if",  "do",     "while", "endwhile", "call",   "endif", "sub",
                                                     "endsub", "goto", "repeat", "endrepeat", "eq", "lt", "gt",
                                                     "ne",  "ge",     "le",    "or",       "xor"};

// The top mode's rules, in the grammar's order.
enum class Rule { Percent, ProgramNumber, LineComment, BlockComment, ParenComment, Number, Apos, Quote, GCode,
                  MCode, Variable, ZOffset, BuiltIn, Symbol };
struct Match {
    Rule rule;
    std::size_t length;
};

bool ruleAt(std::string_view s, std::size_t at, Match& match) {
    const char c = s[at];
    const auto found = [&](Rule rule, std::size_t length) {
        match = {rule, length};
        return true;
    };
    if (c == '%') {
        return found(Rule::Percent, 1);
    }
    if (lower(c) == 'o' && digitsFrom(s, at + 1)) {  // '([O])([0-9]+)'
        return found(Rule::ProgramNumber, 1 + digitsFrom(s, at + 1));
    }
    if (startsWithI(s, at, "//")) {
        return found(Rule::LineComment, 2);
    }
    if (startsWithI(s, at, "/*")) {
        return found(Rule::BlockComment, 2);
    }
    if (c == '(') {
        return found(Rule::ParenComment, 1);
    }
    if (const std::size_t length = numberAt(s, at)) {
        return found(Rule::Number, length);
    }
    if (c == '\'') {
        return found(Rule::Apos, 1);
    }
    if (c == '"') {
        return found(Rule::Quote, 1);
    }
    if (const std::size_t length = codeAt(s, at, 'g')) {
        return found(Rule::GCode, length);
    }
    if (const std::size_t length = codeAt(s, at, 'm')) {
        return found(Rule::MCode, length);
    }
    if (startsWithI(s, at, "VC") || startsWithI(s, at, "VS")) {  // '(VC|VS|#)'
        return found(Rule::Variable, 2);
    }
    if (c == '#') {
        return found(Rule::Variable, 1);
    }
    if (startsWithI(s, at, "VZOFX") || startsWithI(s, at, "VZOFY") || startsWithI(s, at, "VZOFZ")) {
        return found(Rule::ZOffset, 5);
    }
    for (const std::string_view name : kBuiltIns) {  // '(ATAN|...)(\[)'
        if (startsWithI(s, at, name) && at + name.size() < s.size() && s[at + name.size()] == '[') {
            return found(Rule::BuiltIn, name.size() + 1);
        }
    }
    if (lower(c) == 'n') {
        return found(Rule::Symbol, 1);
    }
    return false;
}

class Runs {
public:
    void add(HighlightClass cls, std::size_t length) {
        if (length == 0) {
            return;
        }
        if (!runs_.empty() && runs_.back().cls == cls) {
            runs_.back().length += length;
        } else {
            runs_.push_back({cls, length});
        }
    }
    std::vector<HighlightRun> take() { return std::move(runs_); }

private:
    std::vector<HighlightRun> runs_;
};

// The top mode's plain text: keywords are the whole matches of
// `[A-Z_][A-Z0-9_.]*` found in it (processKeywords).
void addPlain(Runs& runs, std::string_view text) {
    std::size_t at = 0;
    std::size_t plain = 0;
    while (at < text.size()) {
        if (!identStart(text[at])) {
            ++at;
            continue;
        }
        std::size_t end = at + 1;
        while (end < text.size() && identPart(text[end])) {
            ++end;
        }
        bool keyword = false;
        for (const std::string_view word : kKeywords) {
            keyword = keyword || (end - at == word.size() && startsWithI(text, at, word));
        }
        if (keyword) {
            runs.add(HighlightClass::Plain, at - plain);
            runs.add(HighlightClass::Keyword, end - at);
            plain = end;
        }
        at = end;
    }
    runs.add(HighlightClass::Plain, text.size() - plain);
}

// A mode that ends with its first run of digits (included), or the line:
// "#"/VC/VS variables and the "N" symbol (whose illegal \W is ignored).
std::size_t untilDigits(std::string_view s, std::size_t from) {
    std::size_t at = from;
    while (at < s.size() && !digit(s[at])) {
        ++at;
    }
    return at + digitsFrom(s, at);
}

}  // namespace

std::vector<HighlightRun> highlightLine(std::string_view line) {
    Runs runs;
    std::size_t at = 0;    // where matching resumes
    std::size_t text = 0;  // start of the pending plain text
    while (at < line.size()) {
        Match match{};
        if (!ruleAt(line, at, match)) {
            ++at;
            continue;
        }
        addPlain(runs, line.substr(text, at - text));
        const std::size_t begin = at;
        std::size_t end = at + match.length;
        switch (match.rule) {
            case Rule::Percent:
            case Rule::ProgramNumber:
                runs.add(HighlightClass::Meta, end - begin);
                break;
            case Rule::Number:
                runs.add(HighlightClass::Number, end - begin);
                break;
            case Rule::GCode:
            case Rule::MCode:
                runs.add(HighlightClass::Name, end - begin);
                break;
            case Rule::ZOffset:  // "attr": not coloured
                runs.add(HighlightClass::Plain, end - begin);
                break;
            case Rule::Variable:
                end = untilDigits(line, end);
                runs.add(HighlightClass::Plain, end - begin);
                break;
            case Rule::Symbol:
                end = untilDigits(line, end);
                runs.add(HighlightClass::Symbol, end - begin);
                break;
            case Rule::LineComment:  // to '$'
                end = line.size();
                runs.add(HighlightClass::Comment, end - begin);
                break;
            case Rule::BlockComment:
            case Rule::ParenComment: {
                // Its doctag and phrasal-word modes never reach past the end.
                const std::string_view close = match.rule == Rule::BlockComment ? "*/" : ")";
                const std::size_t found = line.find(close, end);
                end = found == std::string_view::npos ? line.size() : found + close.size();
                runs.add(HighlightClass::Comment, end - begin);
                break;
            }
            case Rule::Apos:
            case Rule::Quote: {
                // BACKSLASH_ESCAPE ('\\[\s\S]') first, then the closing quote.
                const char quote = line[begin];
                while (end < line.size()) {
                    if (line[end] == '\\' && end + 1 < line.size()) {
                        end += 2;
                    } else if (line[end++] == quote) {
                        break;
                    }
                }
                runs.add(HighlightClass::String, end - begin);
                break;
            }
            case Rule::BuiltIn: {
                // Numbers inside, up to ']'.
                runs.add(HighlightClass::BuiltIn, end - begin);
                std::size_t plain = end;
                while (end < line.size()) {
                    if (const std::size_t number = numberAt(line, end)) {
                        runs.add(HighlightClass::BuiltIn, end - plain);
                        runs.add(HighlightClass::Number, number);
                        end += number;
                        plain = end;
                    } else if (line[end++] == ']') {
                        break;
                    }
                }
                runs.add(HighlightClass::BuiltIn, end - plain);
                break;
            }
        }
        at = text = end;
    }
    addPlain(runs, line.substr(text));
    return runs.take();
}

std::uint32_t highlightColor(HighlightClass cls, bool dark) noexcept {
    switch (cls) {
        case HighlightClass::Comment:
            return dark ? 0xd4d0ab : 0x696969;
        case HighlightClass::Name:
            return dark ? 0xffa07a : 0xd91e18;
        case HighlightClass::Number:
        case HighlightClass::BuiltIn:
        case HighlightClass::Meta:
            return dark ? 0xf5ab35 : 0xaa5d00;
        case HighlightClass::String:
        case HighlightClass::Symbol:
            return dark ? 0xabe338 : 0x008000;
        case HighlightClass::Keyword:
            return dark ? 0xdcc6e0 : 0x7928a1;
        case HighlightClass::Plain:
            break;
    }
    return dark ? 0xf8f8f2 : 0x545454;
}

}  // namespace gs::gcode
