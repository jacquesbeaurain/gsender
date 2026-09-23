// The G-code Step Through's line index and tool list (upstream's
// linePositionIndex.test.ts and tools.test.ts cases, on the port's types).

#include "gs/job/program_analysis.hpp"
#include "gs/job/step_through.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>

using namespace gs;
using job::StepIndex;

namespace {

void expectAt(const StepIndex& index, std::size_t line, double x, double y, double z = 0) {
    const gcode::Vec4 p = index.position(line);
    EXPECT_DOUBLE_EQ(p.x, x) << "line " << line;
    EXPECT_DOUBLE_EQ(p.y, y) << "line " << line;
    EXPECT_DOUBLE_EQ(p.z, z) << "line " << line;
}

}  // namespace

TEST(StepIndex, AnEmptyIndexAnswersWithDefaults) {
    const StepIndex index = job::buildStepIndex("");
    EXPECT_EQ(index.lineCount(), 0u);
    expectAt(index, 1, 0, 0);
    EXPECT_EQ(index.streamedThrough(1), 0u);
    EXPECT_EQ(index.modals(1), job::StepModalReadout{});
}

TEST(StepIndex, PositionsCarryOverLinesThatDoNotMove) {
    const StepIndex index = job::buildStepIndex("G1 X10 Y0 F1000\nM5\nG1 X10 Y10\n");
    ASSERT_EQ(index.lineCount(), 3u);
    expectAt(index, 1, 10, 0);
    expectAt(index, 2, 10, 0);  // M5 does not move
    expectAt(index, 3, 10, 10);
    // Clamped to the file at both ends.
    expectAt(index, 0, 10, 0);
    expectAt(index, 999, 10, 10);
}

TEST(StepIndex, PositionsAreInMillimetresWithG92Applied) {
    const StepIndex index = job::buildStepIndex("G20\nG0 X1\nG92 X0\nG0 X1\nG21 G91 X-5");
    expectAt(index, 2, 25.4, 0);
    expectAt(index, 3, 25.4, 0);  // G92 renames the position; the tool stays
    expectAt(index, 4, 50.8, 0);
    expectAt(index, 5, 45.8, 0);
}

TEST(StepIndex, SenderLinesSkipOnlyBlankLines) {
    // The toolpath numbers its segments by the lines the sender streams:
    // comments count, blank lines do not.
    const StepIndex index = job::buildStepIndex("G1 X10 F100\n(just a comment)\n\nG1 X20\n");
    EXPECT_EQ(index.streamedThrough(0), 0u);
    EXPECT_EQ(index.streamedThrough(1), 1u);
    EXPECT_EQ(index.streamedThrough(2), 2u);
    EXPECT_EQ(index.streamedThrough(3), 2u);
    EXPECT_EQ(index.streamedThrough(4), 3u);
}

TEST(StepIndex, AMalformedLineCarriesTheStateForward) {
    const StepIndex index = job::buildStepIndex("G1 X10 Y10 F100\n!!! not gcode ???\nG1 X20 Y20");
    expectAt(index, 2, 10, 10);
    expectAt(index, 3, 20, 20);
}

TEST(StepIndex, ModalsReadAsGrblsDollarG) {
    const StepIndex index =
        job::buildStepIndex("G21 G90\nG0 X1\nG1 X2 F1000.456\nM3 S12000 T2\nM8\nG91 G55 G18\nM5 M9");
    // Before anything is set: a board's power-on state.
    EXPECT_EQ(index.modals(1),
              (job::StepModalReadout{"G0", "G54", "G17", "G21", "G90", "G94", "M5", "M9", "T0", "F0", "S0"}));
    EXPECT_EQ(index.modals(3)[0], "G1");
    EXPECT_EQ(index.modals(3)[9], "F1000.46");  // two decimals
    const job::StepModalReadout spindle = index.modals(4);
    EXPECT_EQ(spindle[6], "M3");
    EXPECT_EQ(spindle[8], "T2");
    EXPECT_EQ(spindle[10], "S12000");
    EXPECT_EQ(index.modals(5)[7], "M8");
    const job::StepModalReadout moved = index.modals(6);
    EXPECT_EQ(moved[1], "G55");
    EXPECT_EQ(moved[2], "G18");
    EXPECT_EQ(moved[4], "G91");
    EXPECT_EQ(index.modals(7)[6], "M5");
    EXPECT_EQ(index.modals(7)[7], "M9");
    // Lines share their modal-group combinations.
    EXPECT_LT(index.modalTable().size(), 7u);
}

TEST(StepIndex, BuildingReportsProgressAndCanBeCancelled) {
    std::string program;
    for (int i = 0; i < 20005; ++i) {
        program += "G1 X1 F100\n";
    }
    std::vector<std::pair<std::size_t, std::size_t>> reports;
    const StepIndex index = job::buildStepIndex(program, {}, {}, [&](std::size_t done, std::size_t total) {
        reports.emplace_back(done, total);
    });
    EXPECT_EQ(index.lineCount(), 20005u);
    ASSERT_EQ(reports.size(), 2u);
    EXPECT_EQ(reports[0], std::make_pair(std::size_t{20000}, std::size_t{20005}));
    EXPECT_EQ(reports[1], std::make_pair(std::size_t{20005}, std::size_t{20005}));
    EXPECT_TRUE(job::buildStepIndex(program, {}, [] { return true; }).empty());
}

TEST(StepperTools, DiametersAreReadOutOfToolComments) {
    EXPECT_EQ(job::parseToolDiameter("T1 D=6. CR=0. - flat end mill"), 6);
    EXPECT_EQ(job::parseToolDiameter("6mm endmill"), 6);
    EXPECT_NEAR(*job::parseToolDiameter("1/4\" endmill"), 6.35, 1e-4);
    EXPECT_NEAR(*job::parseToolDiameter("0.25 inch endmill"), 6.35, 1e-4);
    EXPECT_FALSE(job::parseToolDiameter(""));
    EXPECT_FALSE(job::parseToolDiameter("just a plain comment"));
}

TEST(StepperTools, TextOnAColourIsBlackOrWhiteWhicheverReadsBetter) {
    EXPECT_EQ(job::readableTextColor("#ffff00"), "#000000");
    EXPECT_EQ(job::readableTextColor("#000080"), "#ffffff");
    EXPECT_EQ(job::readableTextColor("#fff"), "#000000");
    EXPECT_EQ(job::readableTextColor("not-a-color"), "#ffffff");
}

TEST(StepperTools, TheActiveToolIsTheOneWhoseLinesHoldTheLine) {
    std::vector<job::StepperTool> tools(2);
    tools[0].startLine = 1;
    tools[0].endLine = 10;
    tools[1].startLine = 11;
    tools[1].endLine = 20;
    EXPECT_EQ(job::activeToolIndex({}, 5), -1);
    EXPECT_EQ(job::activeToolIndex(tools, 1), 0);
    EXPECT_EQ(job::activeToolIndex(tools, 10), 0);
    EXPECT_EQ(job::activeToolIndex(tools, 11), 1);
    EXPECT_EQ(job::activeToolIndex(tools, 20), 1);
    tools[0].startLine = 5;
    tools.pop_back();
    EXPECT_EQ(job::activeToolIndex(tools, 1), -1);
}

TEST(StepperTools, ToolChangesSplitTheFile) {
    const std::string program =
        "G21\n"
        "M6 T1 (6mm endmill)\n"
        "M3 S12000\n"
        "G1 X10 F500\n"
        "M6 T2\n"
        "S18000 M3\n"
        "G1 X20\n";
    const job::ProgramAnalysis analysis = job::analyzeProgram(program);
    const std::vector<job::StepperTool> tools =
        job::stepperTools(analysis.spindleToolEvents, analysis.totalLines, analysis.tools, "#3e85c7");
    ASSERT_EQ(tools.size(), 2u);
    EXPECT_EQ(tools[0].index, 1);
    EXPECT_EQ(tools[0].label, "T1");
    EXPECT_EQ(tools[0].color, "#3e85c7");  // the first draws in the cutting colour
    EXPECT_EQ(tools[0].startLine, 2u);
    EXPECT_EQ(tools[0].endLine, 4u);
    EXPECT_EQ(tools[0].comment, "6mm endmill");
    EXPECT_EQ(tools[0].diameter, 6);
    EXPECT_EQ(tools[0].spindleSpeed, 12000);  // the S nearest the change
    EXPECT_EQ(tools[1].label, "T2");
    EXPECT_EQ(tools[1].color, "#F08A4F");  // then the palette
    EXPECT_EQ(tools[1].startLine, 5u);
    EXPECT_EQ(tools[1].endLine, 7u);
    EXPECT_FALSE(tools[1].diameter);
    EXPECT_EQ(tools[1].spindleSpeed, 18000);
}

TEST(StepperTools, WithoutAToolChangeTheFirstToolNamedRunsTheFile) {
    EXPECT_TRUE(job::stepperTools({}, 100, {}, "#3e85c7").empty());
    const std::vector<job::StepperTool> tools = job::stepperTools({}, 100, {"t3"}, "#3e85c7");
    ASSERT_EQ(tools.size(), 1u);
    EXPECT_EQ(tools[0].index, 1);
    EXPECT_EQ(tools[0].toolNumber, 3);
    EXPECT_EQ(tools[0].label, "T3");
    EXPECT_EQ(tools[0].startLine, 1u);
    EXPECT_EQ(tools[0].endLine, 100u);
}

TEST(StepperTools, PlaybackSpreadsTheEstimateOverTheLines) {
    EXPECT_DOUBLE_EQ(job::playbackLinesPerSecond(1000, 100, 10), 100);  // 10 lines/s, 10x
    EXPECT_DOUBLE_EQ(job::playbackLinesPerSecond(1000, 0, 1), 100);     // no estimate
}
