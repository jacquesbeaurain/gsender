// The Spindle/Laser widget's mode switch and laser focus (features/Spindle).

#include "gs/controller/spindle.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace gs::controller;

namespace {

using Lines = std::vector<std::string>;

}  // namespace

TEST(Spindle, WorkCoordinateSystemNumbers) {
    EXPECT_EQ(wcsNumber("G54"), 1);
    EXPECT_EQ(wcsNumber("G57"), 4);
    EXPECT_EQ(wcsNumber("G59"), 6);
    EXPECT_EQ(wcsNumber("G59.1"), 0);
    EXPECT_EQ(wcsNumber(""), 0);
}

TEST(Spindle, TheLaserOffsetShiftsTheWorkPosition) {
    // To the laser: the current position reads position + offset.
    EXPECT_EQ(toolOffsetCommand({10, 5}, true, true, 0, 0, "G54"), "G10 L20 P1 X10 Y5");
    EXPECT_EQ(toolOffsetCommand({10, 5}, true, true, 20.5, -3, "G55"), "G10 L20 P2 X30.5 Y2");
    // Back to the spindle: minus the offset.
    EXPECT_EQ(toolOffsetCommand({10, 5}, false, true, 30.5, 2, "G55"), "G10 L20 P2 X20.5 Y-3");
    // Only the axes with an offset.
    EXPECT_EQ(toolOffsetCommand({10, 0}, true, true, 1, 1, "G54"), "G10 L20 P1 X11");
    EXPECT_EQ(toolOffsetCommand({0, -7.5}, true, true, 1, 1, "G54"), "G10 L20 P1 Y-6.5");
    EXPECT_EQ(toolOffsetCommand({0, 0}, true, true, 1, 1, "G54"), "");
    EXPECT_EQ(toolOffsetCommand({0.004, 0}, true, true, 1, 1, "G54"), "");  // rounds to nothing
}

TEST(Spindle, OffsetsRoundAsTheUnits) {
    // mm: 2 decimals.
    EXPECT_EQ(toolOffsetCommand({10.004, 0}, true, true, 1.2345, 0, "G54"), "G10 L20 P1 X11.23");
    // inches: the offset converted (3 decimals), the position divided.
    EXPECT_EQ(toolOffsetCommand({10, 0}, true, false, 25.4, 0, "G54"), "G10 L20 P1 X1.394");
    EXPECT_EQ(toolOffsetCommand({-10, 5}, false, false, 0, 0, "G54"), "G10 L20 P1 X0.394 Y-0.197");
}

TEST(Spindle, SwitchingModesWritesTheRangeAndLaserMode) {
    ModeSwitch toLaser;
    toLaser.offset = {10, 5};
    toLaser.range = std::pair{255.0, 0.0};
    EXPECT_EQ(modeSwitchCommands(toLaser), (Lines{"G21", "G10 L20 P1 X10 Y5", "$30=255", "$31=0", "$32=1", "G21"}));

    // Back to the spindle, the spindle's range; the spindle stops first.
    ModeSwitch toSpindle;
    toSpindle.toLaser = false;
    toSpindle.spindleOn = true;
    toSpindle.metric = false;
    toSpindle.deviceUnits = "G21";
    toSpindle.range = std::pair{30000.0, 10000.0};
    EXPECT_EQ(modeSwitchCommands(toSpindle), (Lines{"M5", "G20", "$30=30000", "$31=10000", "$32=0", "G21"}));

    // grblHAL keeps its laser's own range.
    ModeSwitch hal;
    EXPECT_EQ(modeSwitchCommands(hal), (Lines{"G21", "$32=1", "G21"}));
}

TEST(Spindle, TheLaserFocusFiresAtAFractionOfMaxPower) {
    EXPECT_EQ(laserOnCommand(100, 255), "G1F1 M3 S255");
    EXPECT_EQ(laserOnCommand(10, 255), "G1F1 M3 S25.5");
    EXPECT_EQ(laserOnCommand(1, 1000), "G1F1 M3 S10");
    EXPECT_EQ(laserOnCommand(33, 255), "G1F1 M3 S84.15");
}
