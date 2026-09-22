#include "gs/gcode/parser.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace gs::gcode;

namespace {

std::vector<std::string> flat(std::string_view line) {
    return parseLine(line).flatWords();
}

std::vector<std::pair<char, std::string>> tokens(std::string_view line) {
    LineScan scan;
    scanLine(line, scan);
    std::vector<std::pair<char, std::string>> out;
    for (const Token& token : scan.tokens) {
        out.emplace_back(token.letter, std::string(token.value));
    }
    return out;
}

}  // namespace

TEST(GcodeParseLine, SplitsWordsAndNormalizesNumbers) {
    EXPECT_EQ(flat("G0 X10 Y20"), (std::vector<std::string>{"G0", "X10", "Y20"}));
    EXPECT_EQ(flat("G01 X1.500 Y-.5"), (std::vector<std::string>{"G1", "X1.5", "Y-0.5"}));
    EXPECT_EQ(flat("g1x5f300"), (std::vector<std::string>{"G1", "X5", "F300"}));
    EXPECT_EQ(flat("G38.2 Z-10 F100"), (std::vector<std::string>{"G38.2", "Z-10", "F100"}));
}

TEST(GcodeParseLine, StripsCommentsAndWhitespace) {
    EXPECT_EQ(flat("G01 X1 (comment ; with semicolon) Y2 ; tail M6"),
              (std::vector<std::string>{"G1", "X1", "Y2"}));
    EXPECT_EQ(flat("G0 X 1 0"), (std::vector<std::string>{"G0", "X10"}));
    // Unclosed parenthesis comments out the rest of the line, as on Grbl.
    EXPECT_EQ(flat("G1 X5 (unclosed M6"), (std::vector<std::string>{"G1", "X5"}));
}

TEST(GcodeParseLine, DetectsProgramWords) {
    const ParsedLine line = parseLine("M06 T1 (Tool 1)");
    EXPECT_TRUE(line.hasWord("M6"));
    EXPECT_TRUE(line.hasWord("T1"));
    EXPECT_FALSE(line.hasWord("M0"));
    EXPECT_FALSE(parseLine("M600").hasWord("M6"));
    EXPECT_TRUE(parseLine("M6.0").hasWord("M6"));
}

TEST(GcodeParseLine, NonNumericArgumentsKeepTheirText) {
    const ParsedLine line = parseLine("G1 X1-2");
    ASSERT_EQ(line.words.size(), 2u);
    EXPECT_FALSE(line.words[1].isNumeric());
    EXPECT_EQ(line.words[1].flat(), "X1-2");
    // A letter without a value is not a word.
    EXPECT_EQ(flat("G0 X"), (std::vector<std::string>{"G0"}));
}

TEST(GcodeParseLine, CollectsCommands) {
    const ParsedLine home = parseLine("$H");
    EXPECT_TRUE(home.words.empty());
    ASSERT_EQ(home.cmds.size(), 1u);
    EXPECT_EQ(home.cmds[0], "$H");

    const ParsedLine jog = parseLine("$J=G91 X10 F100");
    ASSERT_EQ(jog.cmds.size(), 1u);
    EXPECT_EQ(jog.cmds[0], "$J");
    EXPECT_EQ(jog.flatWords(), (std::vector<std::string>{"G91", "X10", "F100"}));

    const ParsedLine settings = parseLine("$$");
    ASSERT_EQ(settings.cmds.size(), 1u);
    EXPECT_EQ(settings.cmds[0], "$$");

    const ParsedLine wait = parseLine("  %wait ");
    ASSERT_EQ(wait.cmds.size(), 1u);
    EXPECT_EQ(wait.cmds[0], "%wait");
}

TEST(GcodeParseLine, LineNumbersAndChecksums) {
    const ParsedLine line = parseLine("N10 G1 X5");
    ASSERT_TRUE(line.lineNumber.has_value());
    EXPECT_EQ(*line.lineNumber, 10);
    EXPECT_EQ(line.flatWords(), (std::vector<std::string>{"G1", "X5"}));

    // Checksum is the XOR of every byte before '*'.
    std::string body = "N1 G1 X1";
    int cs = 0;
    for (char c : body) {
        cs ^= static_cast<unsigned char>(c);
    }
    const ParsedLine good = parseLine(body + "*" + std::to_string(cs));
    EXPECT_FALSE(good.checksumError);
    const ParsedLine bad = parseLine(body + "*" + std::to_string(cs ^ 1));
    EXPECT_TRUE(bad.checksumError);
}

TEST(GcodeParseText, SkipsBlankLines) {
    const auto lines = parseText("G21\r\n\n  \nG0 X1\n");
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].line, "G21");
    EXPECT_EQ(lines[1].line, "G0 X1");
}

TEST(GcodeExtractComments, JoinsParenAndSemicolonComments) {
    EXPECT_EQ(extractComments("G0 X1 (move) Y2 ; fast"), "move fast");
    EXPECT_EQ(extractComments("M6 T2 (  Tool 2 - 1/4 endmill  )"), "Tool 2 - 1/4 endmill");
    EXPECT_EQ(extractComments("G0 X1"), "");
    EXPECT_EQ(extractComments("M0 ;Flip the part"), "Flip the part");
}

TEST(GcodeScanLine, TokenizesLikeTheVisualizer) {
    EXPECT_EQ(tokens("G0 X10 (c) Y20"),
              (std::vector<std::pair<char, std::string>>{{'G', "0"}, {'X', "10"}, {'Y', "20"}}));
    // N words are consumed but not recorded.
    EXPECT_EQ(tokens("N5 G1"), (std::vector<std::pair<char, std::string>>{{'G', "1"}}));
    // Values keep their raw text; normalization happens at use.
    EXPECT_EQ(tokens("g01 x1.50"),
              (std::vector<std::pair<char, std::string>>{{'G', "01"}, {'X', "1.50"}}));
}

TEST(GcodeScanLine, FlagsInvalidTokens) {
    LineScan scan;
    scanLine("G1 X5 E0.2", scan);
    EXPECT_TRUE(scan.hasInvalidTokens);  // E is not a CNC word
    scanLine("G1 X5 Y2", scan);
    EXPECT_FALSE(scan.hasInvalidTokens);
    scanLine("G1 X5 &", scan);
    EXPECT_TRUE(scan.hasInvalidTokens);
    scanLine("; only a comment", scan);
    EXPECT_TRUE(scan.tokens.empty());
    EXPECT_FALSE(scan.hasInvalidTokens);
}
