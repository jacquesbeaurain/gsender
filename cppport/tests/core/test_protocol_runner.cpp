// Port of src/server/controllers/__tests__/grblRunner.test.js plus coverage of
// the grblHAL-only state the runner accumulates.

#include "gs/protocol/runner.hpp"

#include <gtest/gtest.h>

using namespace gs::protocol;

namespace {

class RunnerTest : public ::testing::TestWithParam<Firmware> {};

void expectAxes(const AxisValues& v, std::initializer_list<double> expected) {
    ASSERT_EQ(v.count, expected.size());
    std::size_t i = 0;
    for (double e : expected) {
        EXPECT_DOUBLE_EQ(v.values[i++], e);
    }
}

}  // namespace

TEST_P(RunnerTest, WorkPositionIsMachineMinusOffset) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:5.000,2.000,0.000|FS:0,0|WCO:1.000,2.000,3.000>");
    expectAxes(runner.workPosition(), {4, 0, -3});
}

TEST_P(RunnerTest, OffsetIsStickyAcrossReports) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:0.000,0.000,0.000|FS:0,0|WCO:10.000,0.000,2.500>");
    runner.parse("<Run|MPos:15.000,1.000,2.500|FS:500,0>");
    expectAxes(runner.workPosition(), {5, 1, 0});
}

TEST_P(RunnerTest, DerivedPositionKeepsSourceDecimals) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:1.5,2.25,3|FS:0,0|WCO:0.5,0.25,1>");
    const AxisValues& w = runner.workPosition();
    expectAxes(w, {1, 2, 2});
    EXPECT_EQ(w.decimals[0], 1);
    EXPECT_EQ(w.decimals[1], 2);
    EXPECT_EQ(w.decimals[2], 0);
    // Rounded to the reported precision, as toFixed(digits) did.
    runner.parse("<Idle|MPos:1.123,0,0|FS:0,0|WCO:0.0004,0,0>");
    EXPECT_DOUBLE_EQ(runner.workPosition().values[0], 1.123);
}

TEST_P(RunnerTest, WithoutOffsetWorkEqualsMachine) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:1.000,2.000,3.000|FS:0,0>");
    expectAxes(runner.workPosition(), {1, 2, 3});
}

TEST_P(RunnerTest, FourthAxisIsDerived) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:1.000,2.000,3.000,4.000|FS:0,0|WCO:1.000,1.000,1.000,1.000>");
    expectAxes(runner.workPosition(), {0, 1, 2, 3});
}

TEST_P(RunnerTest, WorkPositionReportsDeriveMachinePosition) {
    Runner runner(GetParam());
    runner.parse("<Idle|WPos:1.000,2.000,3.000|FS:0,0|WCO:1.000,1.000,1.000>");
    expectAxes(runner.machinePosition(), {2, 3, 4});
}

TEST_P(RunnerTest, ProbePin) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:0,0,0|Pn:P|FS:0,0>");
    EXPECT_TRUE(runner.state().status.probeActive);
    runner.parse("<Idle|MPos:0,0,0|FS:0,0>");
    EXPECT_FALSE(runner.state().status.probeActive);
    runner.parse("<Idle|MPos:0,0,0|Pn:Z|FS:0,0>");
    EXPECT_FALSE(runner.state().status.probeActive);
    EXPECT_EQ(runner.state().status.pinState, "Z");
}

TEST_P(RunnerTest, EmptyInputIsIgnored) {
    Runner runner(GetParam());
    EXPECT_FALSE(runner.parse(""));
    EXPECT_FALSE(runner.parse("   "));
    EXPECT_FALSE(runner.parse("\n"));
    auto ok = runner.parse("ok\r\n");
    ASSERT_TRUE(ok);
    EXPECT_EQ(ok->raw, "ok");
    EXPECT_TRUE(std::holds_alternative<OkLine>(ok->line));
}

TEST_P(RunnerTest, AlarmAndErrorLines) {
    Runner runner(GetParam());
    auto alarm = runner.parse("ALARM:1");
    ASSERT_TRUE(alarm);
    EXPECT_EQ(std::get<AlarmLine>(alarm->line).message, "1");
    EXPECT_TRUE(runner.isAlarm());
    EXPECT_EQ(runner.state().status.alarmCode, "1");

    auto error = runner.parse("error:9");
    EXPECT_EQ(std::get<ErrorLine>(error->line).message, "9");
}

TEST_P(RunnerTest, SettingsAccumulate) {
    Runner runner(GetParam());
    const auto before = runner.settingsRevision();
    runner.parse("$10=511");
    runner.parse("$100=250.000");
    EXPECT_EQ(runner.setting("$10"), "511");
    EXPECT_EQ(runner.setting("$100"), "250.000");
    EXPECT_TRUE(runner.hasSettings());
    EXPECT_GT(runner.settingsRevision(), before);
    const auto after = runner.settingsRevision();
    runner.parse("$10=511");  // unchanged value -> no revision bump
    EXPECT_EQ(runner.settingsRevision(), after);
}

TEST_P(RunnerTest, ParserStateRemembersLastTool) {
    Runner runner(GetParam());
    runner.parse("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T3 F100 S0]");
    EXPECT_EQ(runner.modal().tool, "3");
    runner.parse("[GC:G1 G55 G17 G21 G90 G94 M3 M8 T0 F200 S1000]");
    EXPECT_EQ(runner.modal().tool, "3");
    EXPECT_EQ(runner.modal().wcs, "G55");
    EXPECT_EQ(runner.currentFeedrate(), "F200");
    EXPECT_EQ(runner.currentSpindleRate(), "1000");
}

TEST_P(RunnerTest, StateRevisionOnlyMovesOnChange) {
    Runner runner(GetParam());
    runner.parse("<Idle|MPos:1,2,3|FS:0,0>");
    const auto revision = runner.stateRevision();
    runner.parse("<Idle|MPos:1,2,3|FS:0,0>");
    EXPECT_EQ(runner.stateRevision(), revision);
    runner.parse("<Idle|MPos:1,2,4|FS:0,0>");
    EXPECT_GT(runner.stateRevision(), revision);
}

INSTANTIATE_TEST_SUITE_P(BothFirmwares, RunnerTest, ::testing::Values(Firmware::Grbl, Firmware::GrblHal),
                         [](const auto& info) { return info.param == Firmware::Grbl ? "Grbl" : "GrblHal"; });

TEST(GrblRunner, EnteringAlarmIsReportedOnce) {
    Runner runner(Firmware::Grbl);
    EXPECT_FALSE(runner.parse("<Idle|MPos:0,0,0|FS:0,0>")->enteredAlarm);
    EXPECT_TRUE(runner.parse("<Alarm|MPos:0,0,0|FS:0,0>")->enteredAlarm);
    EXPECT_FALSE(runner.parse("<Alarm|MPos:0,0,0|FS:0,0>")->enteredAlarm);
    runner.parse("<Idle|MPos:0,0,0|FS:0,0>");
    EXPECT_TRUE(runner.parse("<Alarm|MPos:0,0,0|FS:0,0>")->enteredAlarm);
}

TEST(GrblRunner, StartupBannerSetsVersionAndAlarmCodeDefaultsToHoming) {
    Runner runner(Firmware::Grbl);
    EXPECT_EQ(runner.state().status.alarmCode, "Homing");
    runner.parse("Grbl 1.1h ['$' for help]");
    EXPECT_EQ(runner.settings().version, "1.1h");
}

TEST(GrblRunner, AccessoriesClearWhenOverridesArriveWithoutThem) {
    Runner runner(Firmware::Grbl);
    runner.parse("<Run|MPos:0,0,0|FS:100,1000|Ov:100,100,100|A:S>");
    EXPECT_EQ(runner.state().status.accessoryState, "S");
    runner.parse("<Run|MPos:0,0,0|FS:100,1000>");
    EXPECT_EQ(runner.state().status.accessoryState, "S");  // no Ov: nothing known
    runner.parse("<Idle|MPos:0,0,0|FS:0,0|Ov:100,100,100>");
    EXPECT_EQ(runner.state().status.accessoryState, "");
}

TEST(GrblHalRunner, VersionCarriesBuildDate) {
    Runner runner(Firmware::GrblHal);
    auto event = runner.parse("[VER:1.1f.20240512:]");
    ASSERT_TRUE(event && event->semver);
    EXPECT_EQ(*event->semver, 20240512);
    EXPECT_EQ(runner.settings().semver, 20240512);
}

TEST(GrblHalRunner, AlarmCodeClearsOutsideAlarm) {
    Runner runner(Firmware::GrblHal);
    runner.parse("ALARM:11");
    EXPECT_EQ(runner.state().status.alarmCode, "11");
    runner.parse("<Idle|MPos:0,0,0|FS:0,0>");
    EXPECT_EQ(runner.state().status.alarmCode, "");
}

TEST(GrblHalRunner, CompleteReportSetsAlarmCodeAndSdCard) {
    Runner runner(Firmware::GrblHal);
    runner.parse("<Alarm:11|MPos:0.000,0.000,0.000|Pn:P|T:4|H:1|SD:1|FW:grblHAL>");
    const MachineStatus& s = runner.state().status;
    EXPECT_EQ(s.activeState, "Alarm");
    EXPECT_EQ(s.alarmCode, "11");
    EXPECT_EQ(s.currentTool, 4);
    EXPECT_TRUE(s.hasHomed);
    EXPECT_TRUE(s.sdCard);
    EXPECT_TRUE(s.probeActive);
}

TEST(GrblHalRunner, SettingMetadataAccumulates) {
    Runner runner(Firmware::GrblHal);
    runner.parse("[SETTING:342|9|Tool change probing distance|mm|6|#####0.0|||0|0]");
    runner.parse("342\tTool change probing distance\tmm\t\t\tDistance to probe.");
    runner.parse("[SETTINGGROUP:9|0|Tool change]");
    runner.parse("[ALARMCODE:16||Power on selftest (POS) failed.]");
    runner.parse("[ERRORCODE:62||Directory listing failed.]");
    runner.parse("[NEWOPT:ENUMS,RT+,ATC=1]");
    runner.parse("[AXS:4:XYZA]");
    runner.parse("[T:2|0.000,0.000,-5.000|0.000]");

    const FirmwareSettings& s = runner.settings();
    ASSERT_TRUE(s.descriptions.count(342));
    EXPECT_EQ(s.descriptions.at(342).description, "Tool change probing distance");
    EXPECT_EQ(s.descriptions.at(342).details, "Distance to probe.");
    EXPECT_EQ(s.groups.at(9).label, "Tool change");
    EXPECT_EQ(s.alarms.at(16).description, "Power on selftest (POS) failed.");
    EXPECT_EQ(s.errors.at(62).description, "Directory listing failed.");
    EXPECT_EQ(s.info.at("NEWOPT").option("ATC"), std::optional<std::string>("1"));
    EXPECT_EQ(runner.state().axes.letters, "XYZA");
    EXPECT_DOUBLE_EQ(s.toolTable.at(2).offsets.values[2], -5);
}

TEST(GrblHalRunner, SuccessfulProbeUpdatesCurrentToolOffsets) {
    Runner runner(Firmware::GrblHal);
    runner.parse("[T:3|0.000,0.000,0.000|0.000]");
    runner.parse("<Idle|MPos:0,0,0|FS:0,0|T:3>");
    runner.parse("[PRB:1.000,2.000,-7.500:1]");
    const AxisValues& offsets = runner.settings().toolTable.at(3).offsets;
    EXPECT_DOUBLE_EQ(offsets.values[2], -7.5);
    // A failed probe leaves them alone.
    runner.parse("[PRB:0.000,0.000,-9.000:0]");
    EXPECT_DOUBLE_EQ(runner.settings().toolTable.at(3).offsets.values[2], -7.5);
}

TEST(GrblHalRunner, InfersAxesWhenFirmwareDoesNotReportThem) {
    Runner runner(Firmware::GrblHal);
    runner.parse("<Idle|MPos:0,0,0,0|FS:0,0>");
    EXPECT_EQ(runner.setInferredAxesFromStatus(), std::optional<std::string>("XYZA"));
    EXPECT_TRUE(runner.state().axes.inferred);
    // Firmware-reported letters are authoritative afterwards.
    runner.parse("[AXS:4:XYZB]");
    EXPECT_EQ(runner.state().axes.letters, "XYZB");
    EXPECT_FALSE(runner.state().axes.inferred);
    EXPECT_FALSE(runner.setInferredAxesFromStatus());
}

TEST(GrblHalRunner, SdCardFileListReplacesByName) {
    Runner runner(Firmware::GrblHal);
    runner.parse("[FILE:/a.nc|SIZE:10]");
    runner.parse("[FILE:/b.nc|SIZE:20]");
    runner.parse("[FILE:/a.nc|SIZE:30]");
    const auto& files = runner.state().sdcard.files;
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files[1].name, "a.nc");
    EXPECT_EQ(files[1].size, 30);
}
