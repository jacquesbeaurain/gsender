// G-code syntax colouring against highlight.js itself: every line of
// tests/data/gcode_highlight_golden.json (tools/gen_highlight_fixtures.mjs)
// coloured run for run.

#include "gs/gcode/highlight.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace json = boost::json;
using namespace gs::gcode;

namespace {

const json::array& cases() {
    static const json::value data = [] {
        std::ifstream in(GS_TEST_DATA_DIR "/gcode_highlight_golden.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return json::parse(text.str());
    }();
    return data.at("cases").as_array();
}

std::string className(HighlightClass cls) {
    switch (cls) {
        case HighlightClass::Plain: return "";
        case HighlightClass::Comment: return "comment";
        case HighlightClass::Meta: return "meta";
        case HighlightClass::Number: return "number";
        case HighlightClass::BuiltIn: return "built_in";
        case HighlightClass::Name: return "name";
        case HighlightClass::String: return "string";
        case HighlightClass::Symbol: return "symbol";
        case HighlightClass::Keyword: return "keyword";
    }
    return "?";
}

// UTF-16 code units of a UTF-8 piece, as JavaScript counts the fixture's lengths.
std::size_t utf16Length(std::string_view bytes) {
    std::size_t units = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const auto byte = static_cast<unsigned char>(bytes[i]);
        if ((byte & 0xC0) != 0x80) {
            units += byte >= 0xF0 ? 2 : 1;
        }
    }
    return units;
}

std::string describe(const std::vector<std::pair<std::string, std::size_t>>& runs) {
    std::string out;
    for (const auto& [cls, length] : runs) {
        out += "[" + cls + " " + std::to_string(length) + "]";
    }
    return out;
}

}  // namespace

TEST(GcodeHighlight, MatchesHighlightJsOnEveryGoldenLine) {
    ASSERT_GT(cases().size(), 1000u);
    int mismatches = 0;
    for (const json::value& item : cases()) {
        const std::string line(item.at("line").as_string());
        std::vector<std::pair<std::string, std::size_t>> expected;
        for (const json::value& run : item.at("runs").as_array()) {
            expected.emplace_back(std::string(run.as_array()[0].as_string()),
                                  static_cast<std::size_t>(run.as_array()[1].to_number<double>()));
        }
        std::vector<std::pair<std::string, std::size_t>> actual;
        std::size_t at = 0;
        std::size_t total = 0;
        for (const HighlightRun& run : highlightLine(line)) {
            actual.emplace_back(className(run.cls), utf16Length(std::string_view(line).substr(at, run.length)));
            at += run.length;
            total += run.length;
        }
        EXPECT_EQ(total, line.size()) << line;
        if (actual != expected && ++mismatches <= 20) {
            ADD_FAILURE() << "line: " << line << "\n  expected " << describe(expected) << "\n  actual   "
                          << describe(actual);
        }
    }
    EXPECT_EQ(mismatches, 0);
}

TEST(GcodeHighlight, ThemeColours) {
    EXPECT_EQ(highlightColor(HighlightClass::Name, false), 0xd91e18u);
    EXPECT_EQ(highlightColor(HighlightClass::Name, true), 0xffa07au);
    EXPECT_EQ(highlightColor(HighlightClass::Plain, false), 0x545454u);
    EXPECT_EQ(highlightColor(HighlightClass::Plain, true), 0xf8f8f2u);
    EXPECT_EQ(highlightColor(HighlightClass::Number, false), highlightColor(HighlightClass::Meta, false));
}
