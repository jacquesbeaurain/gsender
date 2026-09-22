#pragma once

// G-code line tokenization.
//
// Two entry points mirror the two tokenizers gSender uses:
//  * parseLine()  - port of the `gcode-parser` package's parseLine(), used by
//                   the controllers to classify lines they stream (M0/M1/M6...).
//  * scanLine()   - port of the app's scanLineFast(), used by the interpreter
//                   that builds visualizer geometry and time estimates.
//
// Both share one comment/whitespace stripping step. Deliberate deviation from
// the JavaScript: an unclosed "(" comments out the rest of the line, which is
// how Grbl/grblHAL treat it (gcode-parser would still scan words after it).

#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::gcode {

// A word as produced by parseLine(): letter plus argument text.
struct Word {
    char letter = 0;        // upper case
    std::string argument;   // text after the letter, e.g. "01" or "1.500"
    double value = 0;       // Number(argument); NaN when not numeric

    bool isNumeric() const noexcept { return !std::isnan(value); }

    // `letter + value` the way JavaScript concatenates it: "G01" -> "G1",
    // "M06" -> "M6", "X1.500" -> "X1.5".
    std::string flat() const;
};

struct ParsedLine {
    std::string line;                  // the input line
    std::vector<Word> words;
    std::vector<std::string> cmds;     // "%..."/"{..." (whole trimmed line), "$..." commands
    std::optional<double> lineNumber;  // first N word
    std::optional<double> checksum;    // first *nn
    bool checksumError = false;

    // True when a flattened word equals `flatWord`, e.g. hasWord("M6").
    bool hasWord(std::string_view flatWord) const;
    std::vector<std::string> flatWords() const;
};

// Removes "( ... )" comments (an unclosed one runs to end of line), the rest
// of the line after ';', and all whitespace.
std::string stripCommentsAndWhitespace(std::string_view line);

ParsedLine parseLine(std::string_view line);

// Splits text into lines, trims them, skips empty ones and parses the rest.
std::vector<ParsedLine> parseText(std::string_view text);

// Joins the text of every "( ... )" and "; ..." comment on the line with
// spaces - the "comment string" gSender attaches to M0/M6 pause events.
std::string extractComments(std::string_view line);

// ---- Fast scanner ---------------------------------------------------------

struct Token {
    char letter = 0;         // upper-cased word letter, or '%', '{', '$', '*'
    std::string_view value;  // argument text; views into LineScan::buffer
};

struct LineScan {
    std::string buffer;  // stripped line the tokens view into
    std::vector<Token> tokens;
    bool hasInvalidTokens = false;
};

// Tokenizes one line into `out` (reused between calls to avoid allocations).
// N words are dropped; letters outside the G-code vocabulary gSender accepts
// still produce tokens but set hasInvalidTokens.
void scanLine(std::string_view line, LineScan& out);

}  // namespace gs::gcode
