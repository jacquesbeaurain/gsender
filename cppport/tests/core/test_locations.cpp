// The DRO's moves to places (DRO/utils/RapidPosition.ts, Parking.tsx,
// GoTo.tsx, DRO.ts). The first groups port upstream's
// utils/tests/RapidPosition.test.ts and component/tests/Parking.test.ts.

#include "gs/controller/locations.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace gs::controller;

namespace {

using Lines = std::vector<std::string>;

LocationSettings limits(std::string xMax, std::string yMax, std::string homing = {}, std::string dirMask = "0") {
    LocationSettings settings;
    settings.xMaxTravel = std::move(xMax);
    settings.yMaxTravel = std::move(yMax);
    settings.homing = std::move(homing);
    settings.homingDirMask = std::move(dirMask);
    return settings;
}

std::string xyMove(MachineCorner corner, const LocationSettings& settings, bool flag = true, double pullOff = 1) {
    const Lines gcode = cornerCommands(corner, settings, flag, pullOff, false);
    return gcode.size() == 2 ? gcode[1] : std::string("(none)");
}

}  // namespace

// ---- RapidPosition.test.ts ----

TEST(Locations, TheMachineBedRunsFromTheHomingCorner) {
    // Back right ($23=0): travel towards -X and -Y.
    WorkRect bed = machineBedWorkRect("0", 800, 600, 0, 0);
    EXPECT_EQ(bed.minX, -800);
    EXPECT_EQ(bed.maxX, 0);
    EXPECT_EQ(bed.minY, -600);
    EXPECT_EQ(bed.maxY, 0);
    // Front left ($23=3) and a work offset: the bed seen from the workspace.
    bed = machineBedWorkRect("3", 800, 600, 100, 50);
    EXPECT_EQ(bed.minX, -100);
    EXPECT_EQ(bed.maxX, 700);
    EXPECT_EQ(bed.minY, -50);
    EXPECT_EQ(bed.maxY, 550);
    // Back left: +X, -Y.
    bed = machineBedWorkRect("1", 10, 20, 0, 0);
    EXPECT_EQ(bed.minX, 0);
    EXPECT_EQ(bed.maxX, 10);
    EXPECT_EQ(bed.minY, -20);
    const WorkRect keepout = keepoutWorkRect(-50, -10, -30, -5, -100, -100);
    EXPECT_EQ(keepout.minX, 50);
    EXPECT_EQ(keepout.maxY, 95);
}

TEST(RapidPosition, ReadsTheOriginResetBitOfHoming) {
    LocationSettings settings;
    for (const auto& [homing, set] : std::vector<std::pair<std::string, bool>>{
             {"8", true}, {"9", true}, {"4", false}, {"1", false}, {"0", false}, {"", false}}) {
        settings.homing = homing;
        EXPECT_EQ(settings.homingSetsOrigin(), set) << homing;
    }
}

TEST(RapidPosition, RetractsToOneBelowTheTopWhenHomingSetsTheOrigin) {
    EXPECT_EQ(cornerCommands(MachineCorner::FrontRight, limits("300", "300", "9"), true, 5, false)[0],
              "G53 G21 G0 Z-1");
}

TEST(RapidPosition, RetractsToThePullOffOtherwise) {
    EXPECT_EQ(cornerCommands(MachineCorner::FrontRight, limits("300", "300", "1"), true, 2, false)[0],
              "G53 G21 G0 Z-2");
    // $22 missing entirely.
    EXPECT_EQ(cornerCommands(MachineCorner::FrontRight, limits("300", "300"), true, 3, false)[0], "G53 G21 G0 Z-3");
    EXPECT_EQ(cornerCommands(MachineCorner::FrontRight, limits("300", "300", "1"), true, 2.5, false)[0],
              "G53 G21 G0 Z-2.5");
    // gsender#911's scenario: $27=2 without the origin reset.
    EXPECT_EQ(cornerCommands(MachineCorner::BackRight, limits("220", "380", "1"), true, 2, false)[0],
              "G53 G21 G0 Z-2");
}

TEST(RapidPosition, GrblHalTakesTheHomingFlagFromHoming) {
    const LocationSettings settings = limits("300", "300", "1", "2");
    EXPECT_EQ(cornerCommands(MachineCorner::FrontRight, settings, true, 1, true)[1],
              cornerCommands(MachineCorner::FrontRight, settings, false, 1, true)[1]);
    // Plain Grbl uses the flag it is given.
    EXPECT_NE(cornerCommands(MachineCorner::FrontRight, settings, true, 1, false)[1],
              cornerCommands(MachineCorner::FrontRight, settings, false, 1, false)[1]);
}

TEST(RapidPosition, MovesToThePullOffAtTheHomeCornerAndTheLimitsOpposite) {
    const LocationSettings settings = limits("300", "200", "9");
    EXPECT_EQ(xyMove(MachineCorner::BackRight, settings), "G53 G21 G0 X-1 Y-1");
    EXPECT_EQ(xyMove(MachineCorner::FrontLeft, settings), "G53 G21 G0 X-299 Y-199");
    // Unhomed: computed as if homing were back right.
    EXPECT_EQ(xyMove(MachineCorner::FrontRight, limits("300", "200", "9", "2"), false),
              xyMove(MachineCorner::FrontRight, settings));
}

TEST(RapidPosition, NothingWithoutTheTravelLimits) {
    LocationSettings settings;
    settings.homing = "9";
    EXPECT_TRUE(cornerCommands(MachineCorner::FrontRight, settings, true, 1, false).empty());
    // A limit equal to the pull-off leaves no travel (JS `!0`).
    EXPECT_TRUE(cornerCommands(MachineCorner::FrontRight, limits("1", "300"), true, 1, false).empty());
}

// ---- every corner from every homing corner ----

TEST(RapidPosition, CornersFromEachHomingCorner) {
    struct Case {
        const char* dirMask;
        MachineCorner corner;
        const char* move;
    };
    using C = MachineCorner;
    const Case cases[] = {
        {"2", C::FrontRight, "X-1 Y1"},    {"2", C::FrontLeft, "X-299 Y1"},  {"2", C::BackLeft, "X-299 Y199"},
        {"2", C::BackRight, "X-1 Y199"},   {"2", C::Center, "X-149.5 Y99.5"},
        {"3", C::FrontRight, "X299 Y1"},   {"3", C::FrontLeft, "X1 Y1"},     {"3", C::BackRight, "X299 Y199"},
        {"3", C::BackLeft, "X1 Y199"},     {"3", C::Center, "X149.5 Y99.5"},
        {"1", C::FrontRight, "X299 Y-199"}, {"1", C::FrontLeft, "X1 Y-199"}, {"1", C::BackLeft, "X1 Y-1"},
        {"1", C::BackRight, "X299 Y-1"},   {"1", C::Center, "X149.5 Y-99.5"},
        {"0", C::FrontRight, "X-1 Y-199"}, {"0", C::FrontLeft, "X-299 Y-199"}, {"0", C::BackLeft, "X-299 Y-1"},
        {"0", C::BackRight, "X-1 Y-1"},    {"0", C::Center, "X-149.5 Y-99.5"},
        // Only the XYZ bits count.
        {"9", C::BackLeft, "X1 Y-1"},
    };
    for (const Case& c : cases) {
        EXPECT_EQ(xyMove(c.corner, limits("300", "200", "9", c.dirMask)), std::string("G53 G21 G0 ") + c.move)
            << c.dirMask << " " << static_cast<int>(c.corner);
    }
}

TEST(RapidPosition, ZHomingDownIsNotComputed) {
    EXPECT_EQ(homingCorner("4"), MachineCorner::Other);
    EXPECT_EQ(homingCorner("7"), MachineCorner::Other);
    EXPECT_EQ(homingCorner(""), MachineCorner::BackRight);
    EXPECT_TRUE(cornerCommands(MachineCorner::FrontLeft, limits("300", "200", "9", "4"), true, 1, false).empty());
    // ... unless unhomed, when back right is assumed.
    EXPECT_EQ(xyMove(MachineCorner::FrontLeft, limits("300", "200", "9", "4"), false), "G53 G21 G0 X-299 Y-199");
}

TEST(RapidPosition, ANegativeZeroPrintsAsZero) {
    const Lines gcode = cornerCommands(MachineCorner::BackRight, limits("300", "200", "1"), true, 0, false);
    ASSERT_EQ(gcode.size(), 2U);
    EXPECT_EQ(gcode[0], "G53 G21 G0 Z0");
    EXPECT_EQ(gcode[1], "G53 G21 G0 X0 Y0");
}

// ---- Parking.test.ts ----

TEST(Parking, RetractsToOneBelowTheTopWhenHomingSetsTheOrigin) {
    LocationSettings settings;
    settings.homing = "9";
    settings.pullOff = "2";
    EXPECT_EQ(parkCommands({10, 20, -5}, settings)[0], "G53 G21 G0 Z-1");
}

TEST(Parking, RetractsToThePullOffOtherwise) {
    LocationSettings settings;
    settings.homing = "1";
    settings.pullOff = "2";
    EXPECT_EQ(parkCommands({10, 20, -5}, settings)[0], "G53 G21 G0 Z-2");
    settings.pullOff.clear();  // $27 missing: 1, not NaN
    EXPECT_EQ(parkCommands({10, 20, -5}, settings)[0], "G53 G21 G0 Z-1");
}

TEST(Parking, MovesToTheParkXYThenZ) {
    LocationSettings settings;
    settings.homing = "9";
    EXPECT_EQ(parkCommands({10, 20, -5}, settings),
              (Lines{"G53 G21 G0 Z-1", "G53 G21 G0 X10 Y20", "G53 G21 G0 Z-5"}));
}

TEST(Parking, TheSettingsGoToOmitsTheUnits) {
    LocationSettings settings;
    settings.homing = "1";
    settings.pullOff = "3.000";
    EXPECT_EQ(locationCommands({-12.5, -300, -40}, settings),
              (Lines{"G53 G0 Z-3", "G53 G0 X-12.5 Y-300", "G53 G0 Z-40"}));
}

// ---- Go To Location ----

TEST(GoToLocation, AbsoluteWithoutARetract) {
    GoToLocation location;
    location.x = 10;
    location.y = 20;
    location.z = 5;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G90", "G0 X10 Y20", "G90", "G0 Z5"}));
    location.aAvailable = true;
    location.a = 90;
    EXPECT_EQ(goToLocationCommands(location)[1], "G0 X10 Y20 A90");
    location.yAvailable = false;  // rotary mode
    EXPECT_EQ(goToLocationCommands(location)[1], "G0 X10 A90");
}

TEST(GoToLocation, LiftsByTheSafeHeightWithoutHoming) {
    GoToLocation location;
    location.x = 1;
    location.y = 2;
    location.z = -1;
    location.safeRetractHeight = 5;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G91", "G0Z5", "G90", "G0 X1 Y2", "G90", "G0 Z-1"}));
    // Incremental: back down to where Z was, plus the Z distance.
    location.mode = GoToMode::Incremental;
    location.workZ = 3;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G91", "G0Z5", "G91", "G0 X1 Y2", "G90 G0 Z2"}));
}

TEST(GoToLocation, RetractsInMachineCoordinatesWithHoming) {
    GoToLocation location;
    location.x = 1;
    location.y = 2;
    location.z = 3;
    location.homingEnabled = true;
    location.safeRetractHeight = 10;
    location.machineZ = -50;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G53 G0 Z-10", "G90", "G0 X1 Y2", "G90", "G0 Z3"}));
    // Already above the safe height: no lift.
    location.machineZ = -5;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G90", "G0 X1 Y2", "G90", "G0 Z3"}));
    // Incremental moves leave G91 modal, as upstream.
    location.mode = GoToMode::Incremental;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G91", "G0 X1 Y2", "G91", "G0 Z3"}));
}

TEST(GoToLocation, MachineCoordinatesMoveXYAndAOnly) {
    GoToLocation location;
    location.mode = GoToMode::Machine;
    location.x = -100;
    location.y = -200;
    location.z = -30;  // ignored, as upstream
    location.safeRetractHeight = 10;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G53 G0 X-100 Y-200"}));
}

TEST(GoToLocation, AnInchWorkspaceRetractsInInches) {
    GoToLocation location;
    location.metric = false;
    location.x = 1;
    location.y = 2;
    location.z = 0.5;
    location.safeRetractHeight = 10;
    EXPECT_EQ(goToLocationCommands(location), (Lines{"G91", "G0Z0.394", "G90", "G0 X1 Y2", "G90", "G0 Z0.5"}));
    location.homingEnabled = true;
    location.machineZ = -50;
    EXPECT_EQ(goToLocationCommands(location)[0], "G53 G0 Z-0.394");
}

// ---- typed positions and homing ----

TEST(DroCommands, ManualOffsetsAndSingleAxisHoming) {
    EXPECT_EQ(manualOffsetCommand('x', 12.5), "G10 P0 L20 X12.5");
    EXPECT_EQ(manualOffsetCommand('Z', -0.1), "G10 P0 L20 Z-0.1");
    EXPECT_EQ(homeAxisCommand('y'), "$HY");
    EXPECT_TRUE(singleAxisHomingEnabled("3"));
    EXPECT_TRUE(singleAxisHomingEnabled("35"));
    EXPECT_FALSE(singleAxisHomingEnabled("1"));
    EXPECT_FALSE(singleAxisHomingEnabled(""));
}
