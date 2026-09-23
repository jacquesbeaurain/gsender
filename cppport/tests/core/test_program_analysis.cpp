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
    EXPECT_EQ(analysis.estimates.size(), 4u);  // but not for the sender

    runtime::ManualEventLoop loop;
    controller::Sender sender(loop, controller::Sender::Protocol::CharacterCounting, 100);
    ASSERT_TRUE(sender.load("job.nc", program));
    EXPECT_EQ(sender.total(), analysis.estimates.size());

    // 10 mm at 600 mm/min is at least a second; the whole file sums up.
    EXPECT_GE(analysis.estimates[2], 1.0);
    EXPECT_NEAR(std::accumulate(analysis.estimates.begin(), analysis.estimates.end(), 0.0), analysis.estimatedTime,
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

TEST(ProgramAnalysis, MachineLimitsComeFromTheFirmwareSettings) {
    protocol::FirmwareSettings settings;
    settings.settings.set("$120", "300.000");
    settings.settings.set("$110", "2500");
    settings.settings.set("$121", "not a number");
    const gcode::InterpreterOptions options = interpreterOptionsFor(settings);
    EXPECT_EQ(options.x.acceleration, 300);
    EXPECT_EQ(options.x.maxFeed, 2500);
    EXPECT_EQ(options.y.acceleration, gcode::InterpreterOptions{}.y.acceleration);  // fallback
    EXPECT_EQ(options.z.maxFeed, gcode::InterpreterOptions{}.z.maxFeed);
    EXPECT_FALSE(options.atcEnabled);

    protocol::InfoValue newopt;
    newopt.options.emplace_back("ATC", "1");
    settings.info["NEWOPT"] = newopt;
    EXPECT_TRUE(interpreterOptionsFor(settings).atcEnabled);
}

TEST(ProgramAnalysis, SlowerMachinesTakeLonger) {
    // Long enough to reach full speed: gSender's (Slic3r) formula ignores
    // acceleration for moves that never do.
    const std::string program = "G1 X1000 F10000\n";
    protocol::FirmwareSettings slow;
    slow.settings.set("$120", "10");
    EXPECT_GT(analyzeProgram(program, interpreterOptionsFor(slow)).estimatedTime,
              analyzeProgram(program).estimatedTime);
}

class CountingSink final : public gcode::GeometrySink {
public:
    int lines = 0;
    int arcs = 0;
    void addLine(const gcode::Modal&, const gcode::Vec4&, const gcode::Vec4&) override { ++lines; }
    void addArc(const gcode::Modal&, const gcode::Vec4&, const gcode::Vec4&, const gcode::Vec4&) override { ++arcs; }
};

TEST(ProgramAnalysis, GeometryGoesToTheSink) {
    CountingSink sink;
    analyzeProgram("G0 X1\nG1 Y1 F100\nG2 X2 Y0 I0.5 J-0.5\n", {}, &sink);
    EXPECT_EQ(sink.lines, 2);
    EXPECT_EQ(sink.arcs, 1);
}

TEST(ProgramAnalysis, ACancelledAnalysisStopsEarly) {
    std::string program;
    for (int i = 0; i < 20000; ++i) {
        program += "G1 X" + std::to_string(i % 100) + " F1000\n";
    }
    int polls = 0;
    const ProgramAnalysis analysis = analyzeProgram(program, {}, nullptr, [&] { return ++polls >= 2; });
    EXPECT_TRUE(analysis.cancelled);
    EXPECT_LT(analysis.estimates.size(), 20000u);
}

}  // namespace
