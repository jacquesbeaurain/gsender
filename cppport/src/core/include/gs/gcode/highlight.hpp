#pragma once

// Syntax colouring of one G-code line, as gSender's G-code views show it
// (GCodeSourcePanel, GcodeEditor): react-syntax-highlighter's "gcode"
// language - highlight.js 10.7's lib/languages/gcode.js run by lowlight with
// ignoreIllegals - one line at a time, coloured by the a11y-light /
// a11y-dark themes.
//
// The grammar is ported with highlight.js's matching rules: at each point
// the leftmost match of the mode's rules wins, ties going to the earlier
// rule; modes without an end close right after their match; keywords are
// looked up in the top mode's plain text. Its quirks are kept, since they
// decide the colours: an "N" anywhere in code starts a line-number symbol
// running to the next digits ("; End" colours "nd" onwards), numbers match
// inside words, ';' is not a comment, "#" runs to the next digits.
// tests/data/gcode_highlight_golden.json pins it against highlight.js.

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gs::gcode {

// The classes the a11y themes colour; text of any other class takes its
// enclosing class's colour (Plain: the theme's base colour).
enum class HighlightClass : std::uint8_t { Plain, Comment, Meta, Number, BuiltIn, Name, String, Symbol, Keyword };

struct HighlightRun {
    HighlightClass cls = HighlightClass::Plain;
    std::size_t length = 0;  // bytes of the UTF-8 line
};

// The line's colour runs, adjacent runs of one class merged; their lengths
// add up to the line's.
std::vector<HighlightRun> highlightLine(std::string_view line);

// The a11y themes' colours (0xRRGGBB): a11y-light, or a11y-dark.
std::uint32_t highlightColor(HighlightClass cls, bool dark) noexcept;

}  // namespace gs::gcode
