// The editor's and Step Through's search through a program's lines.

#include "gs/util/line_matches.hpp"

#include <gtest/gtest.h>

using gs::util::LineMatches;

TEST(LineMatches, FindsLinesIgnoringCaseAndStepsAroundTheEnds) {
    const std::vector<std::string> lines{"G21", "G0 X1 (Rough)", "G1 x2", "M5", "(rough pass)"};
    LineMatches matches;
    matches.find(lines, "  ROUGH ");
    EXPECT_EQ(matches.lines(), (std::vector<std::size_t>{1, 4}));
    EXPECT_TRUE(matches.contains(4));
    EXPECT_FALSE(matches.contains(2));
    EXPECT_EQ(matches.currentLine(), 1u);
    matches.next();
    EXPECT_EQ(matches.currentLine(), 4u);
    matches.next();
    EXPECT_EQ(matches.currentLine(), 1u);  // around
    matches.previous();
    EXPECT_EQ(matches.currentLine(), 4u);

    EXPECT_EQ(matches.after(1), 4u);
    EXPECT_EQ(matches.after(4), 1u);  // past the last: the first
    EXPECT_EQ(matches.after(0), 1u);

    matches.find(lines, "x", 2);  // a limit
    EXPECT_EQ(matches.lines(), (std::vector<std::size_t>{1, 2}));
    matches.find(lines, "   ");
    EXPECT_TRUE(matches.empty());
    EXPECT_FALSE(matches.current());
    EXPECT_FALSE(matches.after(0));
    matches.next();  // nothing to step through
    EXPECT_FALSE(matches.currentLine());
}
