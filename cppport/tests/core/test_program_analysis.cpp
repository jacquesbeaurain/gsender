// Program analysis: estimates on the sender's numbering and the file stats.

#include "gs/controller/streaming.hpp"
#include "gs/job/program_analysis.hpp"

#include <gtest/gtest.h>

#include <numeric>
#include <string>

using namespace gs;
using namespace gs::job;

namespace {

TEST(ProgramAnalysis, EstimatesFollowTheSendersLineNumbering) {
    const std::string program = "G21\n\nG0 X10\n  \nG1 X20 F600\r\nM30\n";
    const ProgramAnalysis analysis = analyzeProgram(program);
    EXPECT_EQ(analysis.totalLines, 6u);   // blank lines count for the visualizer
    EXPECT_EQ(analysis.lineTime.size(), 4u);  // but not for the sender

    runtime::ManualEventLoop loop;
    controller::Sender sender(loop, controller::Sender::Protocol::CharacterCounting, 100);
    ASSERT_TRUE(sender.load("job.nc", program));
    EXPECT_EQ(sender.total(), analysis.lineTime.size());

    // 10 mm at 600 mm/min is at least a second; the whole file sums up.
    EXPECT_GE(analysis.lineTime[2], 1.0);
    EXPECT_NEAR(std::accumulate(analysis.lineTime.begin(), analysis.lineTime.end(), 0.0), analysis.estimatedTime,
                1e-9);
    EXPECT_EQ(analysis.bytes, program.size());
}

TEST(ProgramAnalysis, StatisticsDescribeTheFile) {
    const ProgramAnalysis analysis =
        analyzeProgram("G20\nT2 M6\nM3 S12000\nG1 X1 Y2 F30\nG1 Z-0.5 F10\nQ5\n");
    EXPECT_EQ(analysis.fileModal, "G20");
    EXPECT_EQ(analysis.tools, std::vector<std::string>{"T2"});
    EXPECT_EQ(analysis.spindleSpeeds, std::vector<std::string>{"S12000"});
    EXPECT_EQ(analysis.feedrates.size(), 2u);
    EXPECT_EQ(analysis.usedAxes, "XYZ");
    EXPECT_EQ(analysis.toolChangeLines, std::vector<std::size_t>{2});
    EXPECT_EQ(analysis.invalidLines.size(), 1u);
    EXPECT_NEAR(analysis.bounds.max.x, 25.4, 1e-9);  // inches become millimetres
    EXPECT_EQ(analysis.fileType, gcode::FileType::Default);
    EXPECT_FALSE(analysis.cancelled);
}

TEST(ProgramAnalysis, SlowerMachinesTakeLonger) {
    const std::string program = "G1 X1000 F10000\n";
    protocol::FirmwareSettings slow;
    slow.settings.set("$120", "10");
    EXPECT_GT(analyzeProgram(program, {}, estimatorSettingsFor(slow, {})).estimatedTime,
              analyzeProgram(program).estimatedTime);
}

TEST(ProgramAnalysis, AnEstimateAloneMatchesTheAnalysis) {
    const std::string program = "G21\n\n(comment)\nG0 X10\nG1 X20 F600\nG4 P1\nM30\n";
    const ProgramAnalysis analysis = analyzeProgram(program);
    const std::optional<gcode::EstimateResult> estimate = estimateProgram(program, {});
    ASSERT_TRUE(estimate);
    EXPECT_EQ(estimate->lineTime, analysis.lineTime);
    EXPECT_EQ(estimate->lineKind, analysis.lineKind);
    EXPECT_EQ(estimate->totalTime, analysis.estimatedTime);
    EXPECT_FALSE(estimateProgram(std::string(5000, '\n'), {}, [] { return true; }));
}

class CountingSink final : public gcode::GeometrySink {
public:
    int lines = 0;
    int arcs = 0;
    std::vector<std::size_t> senderLines;
    void atLine(std::size_t index) override { senderLines.push_back(index); }
    void addLine(const gcode::Modal&, const gcode::Vec4&, const gcode::Vec4&) override { ++lines; }
    void addArc(const gcode::Modal&, const gcode::Vec4&, const gcode::Vec4&, const gcode::Vec4&) override { ++arcs; }
};

TEST(ProgramAnalysis, GeometryGoesToTheSinkWithSenderLineNumbers) {
    CountingSink sink;
    analyzeProgram("G0 X1\n\nG1 Y1 F100\nG2 X2 Y0 I0.5 J-0.5\n", {}, {}, &sink);
    EXPECT_EQ(sink.lines, 2);
    EXPECT_EQ(sink.arcs, 1);
    EXPECT_EQ(sink.senderLines, (std::vector<std::size_t>{0, 1, 2}));  // the blank line is not streamed
}

TEST(ProgramAnalysis, SenderLinesKnowTheirFileLines) {
    // Blank lines are not streamed; comments are.
    EXPECT_EQ(job::senderLineNumbers("G21\n\n  \n(comment)\r\nG0 X1\n"),
              (std::vector<std::size_t>{1, 4, 5}));
    EXPECT_TRUE(job::senderLineNumbers("").empty());
}

TEST(ProgramAnalysis, ACancelledAnalysisStopsEarly) {
    std::string program;
    for (int i = 0; i < 20000; ++i) {
        program += "G1 X" + std::to_string(i % 100) + " F1000\n";
    }
    int polls = 0;
    const ProgramAnalysis analysis = analyzeProgram(program, {}, {}, nullptr, [&] { return ++polls >= 2; });
    EXPECT_TRUE(analysis.cancelled);
    EXPECT_LT(analysis.lineTime.size(), 20000u);
}

}  // namespace
