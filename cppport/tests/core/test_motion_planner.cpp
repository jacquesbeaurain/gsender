// The time estimator: lib/timeEstimator/__tests__/MotionPlanner.test.ts,
// driven through the interpreter as upstream drives it through
// GCodeVirtualizer.

#include "gs/gcode/interpreter.hpp"
#include "gs/gcode/motion_planner.hpp"
#include "gs/job/program_analysis.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::gcode;

namespace {

constexpr double kFast = 1e7;  // effectively infinite rate/accel, isolates one effect per test

EstimateResult estimate(std::string_view program, const EstimatorSettings& settings = {}) {
    MotionPlanner planner(settings);
    Interpreter vm;
    vm.setEstimator(&planner);
    vm.processProgram(program);
    return planner.finish();
}

EstimatorSettings limits(std::array<double, 4> maxRate, std::array<double, 4> accel) {
    EstimatorSettings s;
    for (std::size_t i = 0; i < 4; ++i) {
        s.maxRate[i] = maxRate[i];
        s.accel[i] = accel[i];
    }
    return s;
}

const EstimatorSettings kBase = limits({10000, 10000, 10000, 10000}, {500, 500, 500, 500});
const EstimatorSettings kUnlimited = limits({kFast, kFast, kFast, kFast}, {kFast, kFast, kFast, kFast});

// What the Sender keeps.
std::vector<std::string> senderLines(std::string_view program) {
    std::vector<std::string> kept;
    for (std::string_view line : str::splitLines(program)) {
        if (!str::trim(line).empty()) {
            kept.emplace_back(line);
        }
    }
    return kept;
}

}  // namespace

TEST(TrapezoidTime, AcceleratesCruisesAndDecelerates) {
    // 100 mm at 100 mm/s, 500 mm/s^2: 0.2s + 0.2s ramps over 20 mm, 0.8s cruise
    EXPECT_NEAR(trapezoidTime(100, 500, 0, 0, 100 * 100), 1.2, 1e-6);
}

TEST(TrapezoidTime, HandlesMovesTooShortToReachNominalSpeed) {
    // 4 mm at 500 mm/s^2 peaks at sqrt(2000) mm/s
    EXPECT_NEAR(trapezoidTime(4, 500, 0, 0, 100 * 100), 2 * std::sqrt(2000.0) / 500, 1e-6);
}

TEST(MotionPlanner, TimesASingleStraightMoveWithAcceleration) {
    EXPECT_NEAR(estimate("G1 X100 F6000", kBase).totalTime, 1.2, 1e-3);
}

TEST(MotionPlanner, DoesNotSlowDownAtACollinearJunction) {
    EXPECT_NEAR(estimate("G1 X50 F6000\nX100", kBase).totalTime, 1.2, 1e-3);
}

TEST(MotionPlanner, SlowsNearlyToAStopAtANinetyDegreeCorner) {
    // two independent 1.2s moves minus a small saving from ~4 mm/s junction speed
    const double total = estimate("G1 X100 F6000\nY100", kBase).totalTime;
    EXPECT_LT(total, 2.4);
    EXPECT_GT(total, 2.3);
}

TEST(MotionPlanner, LimitsSpeedOnDenseShortSegmentsByThePlannerLookahead) {
    std::string program = "G1 F6000";
    for (int i = 1; i <= 1000; ++i) {
        program += "\nX" + js::toFixed(i * 0.1, 1);
    }
    EstimatorSettings shortBuffer = kBase;
    shortBuffer.plannerBlocks = 15;
    EstimatorSettings longBuffer = kBase;
    longBuffer.plannerBlocks = 2000;
    // 15 x 0.1 mm window: must be able to stop within 1.5 mm => ~38.7 mm/s
    EXPECT_GT(estimate(program, shortBuffer).totalTime, 2.3);
    EXPECT_NEAR(estimate(program, longBuffer).totalTime, 1.2, 1e-2);
}

TEST(MotionPlanner, ScalesRapidRateByEachAxisShareOfTheMove) {
    const EstimateResult result = estimate("G0 X100 Y100", limits({6000, 3000, kFast, kFast}, {kFast, kFast, kFast, kFast}));
    // Y limits: 3000 / 0.7071 = 4242.6 mm/min along the 141.4 mm diagonal
    EXPECT_NEAR(result.totalTime, 2.0, 1e-2);
    EXPECT_EQ(result.lineKind[0], kLineKindRapid);
}

TEST(MotionPlanner, CapsFeedAtTheAxisMaxRate) {
    EXPECT_NEAR(estimate("G1 X100 F10000", limits({3000, kFast, kFast, kFast}, {kFast, kFast, kFast, kFast})).totalTime,
                2.0, 1e-2);
}

TEST(MotionPlanner, TimesArcsAlongTheArcNotTheChord) {
    // half circle of radius 10 at 10 mm/s (chords are ~0.002 mm inside the arc)
    EXPECT_NEAR(estimate("G1 F600\nG2 X20 Y0 I10 J0", kUnlimited).totalTime, std::numbers::pi, 1e-2);
}

TEST(MotionPlanner, TimesRFormatArcsAndHelicalZ) {
    EXPECT_NEAR(estimate("G1 F600\nG3 X20 Y0 Z-10 R10", kUnlimited).totalTime,
                std::hypot(std::numbers::pi * 10, 10) / 10, 1e-2);
}

TEST(MotionPlanner, SupportsInverseTimeFeed) {
    EXPECT_NEAR(estimate("G93 G1 X10 F2", kUnlimited).totalTime, 30, 1e-3);
}

TEST(MotionPlanner, ConvertsInchFeedsAndDistances) {
    EXPECT_NEAR(estimate("G20\nG1 X1 F10", kUnlimited).totalTime, 6, 1e-3);
}

TEST(MotionPlanner, TreatsG4PAsSecondsAndMarksItAsFixedTime) {
    const EstimateResult result = estimate("G4 P2", kBase);
    EXPECT_NEAR(result.totalTime, 2, 1e-6);
    EXPECT_EQ(result.lineKind[0], kLineKindFixed);
}

TEST(MotionPlanner, AddsToolChangeTimeOnlyWhenAtcTimeIsConfigured) {
    EXPECT_EQ(estimate("M6 T1", kBase).totalTime, 0);
    EstimatorSettings atc = kBase;
    atc.toolChangeTime = 45;
    EXPECT_NEAR(estimate("M6 T1", atc).totalTime, 45, 1e-6);
}

TEST(MotionPlanner, StopsAtSpindleChangesButNotInLaserMode) {
    const std::string program = "G1 X50 F6000\nM3 S1000\nG1 X100";
    EstimatorSettings laser = kBase;
    laser.laserMode = true;
    const double milled = estimate(program, kBase).totalTime;
    const double lasered = estimate(program, laser).totalTime;
    EXPECT_NEAR(lasered, 1.2, 1e-3);
    EXPECT_GT(milled, lasered + 0.1);
}

TEST(MotionPlanner, RunsPureRotaryMovesInDegreesPerMinute) {
    EXPECT_NEAR(estimate("G1 A360 F3600", kUnlimited).totalTime, 6, 1e-3);
}

TEST(MotionPlanner, UsesYLimitsWhenGrblRemapsAToY) {
    EstimatorSettings s = limits({kFast, 3600, kFast, 600}, {kFast, kFast, kFast, kFast});
    s.aUsesYLimits = true;
    EXPECT_NEAR(estimate("G0 A360", s).totalTime, 6, 1e-3);
}

TEST(MotionPlanner, AppliesFToTheCombinedVectorByDefault) {
    EXPECT_NEAR(estimate("G1 X10 A360 F600", kUnlimited).totalTime, std::hypot(10, 360) / 10, 1e-3);
}

TEST(MotionPlanner, AppliesFToTheLinearAxesWithTheGrblHalRotaryFix) {
    EstimatorSettings s = kUnlimited;
    s.firmware = Firmware::GrblHal;
    s.rotaryFix = true;
    EXPECT_NEAR(estimate("G1 X10 A360 F600", s).totalTime, 1, 1e-3);
}

TEST(MotionPlanner, ModelsSerialStarvationOnDenseFiles) {
    std::string program = "G1 F6000";
    for (int i = 1; i <= 1000; ++i) {
        program += "\nG1 X" + js::toFixed(i * 0.1, 4) + " Y0.0000 Z0.0000";
    }
    EstimatorSettings serial = kUnlimited;
    serial.serialBytesPerSecond = 11520;
    EXPECT_NEAR(estimate(program, kUnlimited).totalTime, 1, 1e-2);
    // 28 bytes per line at 11520 B/s is ~2.4 ms per 1 ms move
    EXPECT_NEAR(estimate(program, serial).totalTime, 1000.0 * 28 / 11520, 0.05);
}

TEST(MotionPlanner, GivesEveryLineTheSenderStreamsExactlyOneTimeSlot) {
    const std::string program =
        "(header comment)\n"
        "\n"
        "G21 G90\n"
        "   \n"
        "; semicolon comment\n"
        "G0 X10\n"
        "\n"
        "G1 X20 F600 (inline comment)\n"
        "%\n"
        "G4 P1\n"
        "M30\n";
    const EstimateResult result = estimate(program);
    const std::vector<std::string> kept = senderLines(program);
    ASSERT_EQ(result.lineTime.size(), kept.size());

    const auto indexOf = [&](std::string_view text) {
        return static_cast<std::size_t>(
            std::find_if(kept.begin(), kept.end(), [&](const std::string& line) { return line.starts_with(text); }) -
            kept.begin());
    };
    EXPECT_EQ(result.lineKind[indexOf("(header")], kLineKindNone);
    EXPECT_EQ(result.lineKind[indexOf("G0 X10")], kLineKindRapid);
    EXPECT_EQ(result.lineKind[indexOf("G1 X20")], kLineKindFeed);
    EXPECT_GT(result.lineTime[indexOf("G1 X20")], 0.9);
    EXPECT_NEAR(result.lineTime[indexOf("G4")], 1, 1e-6);
    EXPECT_EQ(result.lineTime[indexOf("M30")], 0);
}

TEST(MotionPlanner, KeepsAllTimeFromMultipleMotionGroupsOnOneLine) {
    EXPECT_EQ(estimate("G0 X10 G1 Y10 F600\nG1 X0", kUnlimited).lineTime.size(), 2u);
}

// ---- config.ts --------------------------------------------------------------------

TEST(EstimatorConfig, FillsDefaultsForMissingOrBadValues) {
    EstimatorSettings s;
    s.maxRate[0] = 2500;
    s.maxRate[1] = -1;
    s.accel[2] = std::nan("");
    const EstimatorConfig c = createEstimatorConfig(s);
    EXPECT_EQ(c.maxRate[0], 2500);
    EXPECT_EQ(c.maxRate[1], 4000);
    EXPECT_EQ(c.accel[2], 500);
    EXPECT_EQ(c.plannerBlocks, 15);
    s.firmware = Firmware::GrblHal;
    EXPECT_EQ(createEstimatorConfig(s).plannerBlocks, 34);
}

TEST(EstimatorConfig, ComesFromTheFirmwareSettings) {
    protocol::FirmwareSettings settings;
    settings.settings.set("$110", "2500");
    settings.settings.set("$120", "300.000");
    settings.settings.set("$11", "0.02");
    settings.settings.set("$398", "100");
    settings.settings.set("$394", "2.5");
    settings.settings.set("$376", "1");
    settings.settings.set("$701", "3");

    job::EstimatorInputs grbl;
    grbl.baudRate = 115200;
    EstimatorSettings s = job::estimatorSettingsFor(settings, grbl);
    EXPECT_EQ(s.firmware, Firmware::Grbl);
    EXPECT_EQ(s.maxRate[0], 2500);
    EXPECT_EQ(s.accel[0], 300);
    EXPECT_FALSE(s.accel[1]);
    EXPECT_EQ(s.junctionDeviation, 0.02);
    EXPECT_EQ(s.plannerBlocks, 15);  // grbl's buffer whatever $398 says
    EXPECT_TRUE(s.aUsesYLimits);
    EXPECT_FALSE(s.rotaryFix);
    EXPECT_EQ(s.spindleDelay, 0);
    EXPECT_EQ(s.serialBytesPerSecond, 11520);
    EXPECT_EQ(s.toolChangeTime, 0);

    job::EstimatorInputs hal;
    hal.grblHal = true;
    hal.laserMode = true;
    s = job::estimatorSettingsFor(settings, hal);
    EXPECT_EQ(s.plannerBlocks, 99);  // Bf reports one block less
    EXPECT_FALSE(s.aUsesYLimits);
    EXPECT_TRUE(s.rotaryFix);
    EXPECT_TRUE(s.rotaryRevertMetric);
    EXPECT_TRUE(s.laserMode);
    EXPECT_EQ(s.spindleDelay, 2.5);
    EXPECT_EQ(s.serialBytesPerSecond, 0);

    protocol::InfoValue newopt;
    newopt.options.emplace_back("ATC", "1");
    settings.info["NEWOPT"] = newopt;
    EXPECT_EQ(job::estimatorSettingsFor(settings, hal).toolChangeTime, 45);
}
