#include "gs/util/strings.hpp"

#include <gtest/gtest.h>

using namespace gs::str;

TEST(Strings, TrimRemovesAsciiSpaceBomAndNbsp) {
    EXPECT_EQ(trim("  G0 X1 \r\n"), "G0 X1");
    EXPECT_EQ(trim("\xEF\xBB\xBFG21"), "G21");
    EXPECT_EQ(trim("\xC2\xA0M3\xC2\xA0"), "M3");
    EXPECT_EQ(trim("   "), "");
}

TEST(Strings, SplitKeepsEmptyFields) {
    const auto parts = split("a,,b,", ',');
    ASSERT_EQ(parts.size(), 4u);
    EXPECT_EQ(parts[0], "a");
    EXPECT_EQ(parts[1], "");
    EXPECT_EQ(parts[2], "b");
    EXPECT_EQ(parts[3], "");
}

TEST(Strings, SplitLinesHandlesAllTerminators) {
    const auto lines = splitLines("a\r\nb\nc\rd\n");
    ASSERT_EQ(lines.size(), 4u);
    EXPECT_EQ(lines[0], "a");
    EXPECT_EQ(lines[1], "b");
    EXPECT_EQ(lines[2], "c");
    EXPECT_EQ(lines[3], "d");
    EXPECT_EQ(splitLines("x").size(), 1u);
    EXPECT_TRUE(splitLines("").empty());
}

TEST(Strings, CaseInsensitiveHelpers) {
    EXPECT_TRUE(iequals("GrblHAL", "grblhal"));
    EXPECT_FALSE(iequals("Grbl", "GrblHAL"));
    EXPECT_TRUE(icontains("[VER:1.1f.20240512:]", "1.1F"));
    EXPECT_EQ(toUpper("g0x1"), "G0X1");
}

TEST(Strings, ReplaceAllAndRemoveWhitespace) {
    EXPECT_EQ(replaceAll("a-b-c", "-", "+"), "a+b+c");
    EXPECT_EQ(removeWhitespace(" G0 \tX 1 "), "G0X1");
}
