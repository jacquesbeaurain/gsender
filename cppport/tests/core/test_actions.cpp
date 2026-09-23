// The widget rules shared by buttons and shortcuts: job control
// (JobControl/ControlButton.tsx), zeroing and go-to-zero (DRO/utils/DRO.ts)
// and the workspace's controller shortcuts (workspace/index.tsx).

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::controller;
using protocol::Firmware;

namespace {

class FakeLink final : public DeviceLink {
public:
    std::vector<std::string> sends;
    bool isOpen() const override { return true; }
    void send(std::string_view bytes, SendKind) override { sends.emplace_back(bytes); }
};

class ActionsTest : public ::testing::Test {
protected:
    void make(Firmware firmware = Firmware::Grbl) {
        controller_ = std::make_unique<Controller>(loop, link, firmware, ControllerHooks{},
                                                   [](const ControllerEvent&) {});
        controller_->setPollingEnabled(false);
        link.sends.clear();
    }
    Controller& c() { return *controller_; }
    bool sent(std::string_view bytes) const {
        return std::find(link.sends.begin(), link.sends.end(), bytes) != link.sends.end();
    }

    runtime::ManualEventLoop loop;
    FakeLink link;
    std::unique_ptr<Controller> controller_;
};

TEST(JobRules, FollowTheControlButtons) {
    EXPECT_TRUE(canRun("Idle", WorkflowState::Idle));
    EXPECT_TRUE(canRun("Hold", WorkflowState::Paused));
    EXPECT_TRUE(canRun("Check", WorkflowState::Idle));
    EXPECT_FALSE(canRun("Idle", WorkflowState::Running));
    EXPECT_FALSE(canRun("Alarm", WorkflowState::Idle));
    EXPECT_TRUE(canPause("Run", WorkflowState::Running));
    EXPECT_FALSE(canPause("Hold", WorkflowState::Running));
    EXPECT_TRUE(canStop(WorkflowState::Paused));
    EXPECT_FALSE(canStop(WorkflowState::Idle));
}

TEST_F(ActionsTest, RunStartsAnIdleJobAndResumesAPausedOne) {
    make();
    c().receiveLine("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    ASSERT_TRUE(c().loadProgram("job.nc", "G0 X1\nG0 X2\n").ok);
    runJob(c());
    EXPECT_TRUE(c().workflow().isRunning());
    pauseJob(c());
    EXPECT_TRUE(c().workflow().isPaused());
    link.sends.clear();
    runJob(c());
    EXPECT_TRUE(sent("~"));  // cycle start
    loop.advance(1000);      // the workflow resumes a second later, as upstream
    EXPECT_TRUE(c().workflow().isRunning());
}

TEST_F(ActionsTest, StopLeavesCheckMode) {
    make();
    c().receiveLine("<Check|MPos:0.000,0.000,0.000|FS:0,0>");
    ASSERT_TRUE(c().loadProgram("job.nc", "G0 X1\n").ok);
    runJob(c());
    EXPECT_TRUE(c().workflow().isRunning());
    stopJob(c());
    EXPECT_TRUE(c().workflow().isIdle());
    EXPECT_TRUE(sent("$C\n"));
}

TEST_F(ActionsTest, TheStopShortcutWithoutAJobCancelsJogsOrResets) {
    make();
    c().receiveLine("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    link.sends.clear();  // (state changes query the parser state)
    stopWithoutJob(c());
    EXPECT_TRUE(link.sends.empty());
    c().receiveLine("<Jog|MPos:0.000,0.000,0.000|FS:0,0>");
    stopWithoutJob(c());
    EXPECT_TRUE(sent("\x85"));
    c().receiveLine("<Run|MPos:0.000,0.000,0.000|FS:0,0>");
    stopWithoutJob(c());
    EXPECT_TRUE(sent("\x18"));
}

TEST(Positions, ZeroingCommands) {
    EXPECT_EQ(zeroAxisCommand('x'), "G10 L20 P0 X0");
    EXPECT_EQ(zeroAxisCommand('Z', 1.5), "G10 L20 P0 Z1.5");
    EXPECT_EQ(zeroAllCommands(false, true), (std::vector<std::string>{"G10 L20 P0 X0 Y0 Z0"}));
    EXPECT_EQ(zeroAllCommands(true, true), (std::vector<std::string>{"G10 L20 P0 X0 Y0 Z0", "G10 L20 P0 A0"}));
}

TEST(Positions, GoToZeroLiftsToTheSafeHeightFirst) {
    // gSender's default: no retract height.
    EXPECT_EQ(goToZeroCommands("XY", true, 0, -50), (std::vector<std::string>{"G90 G0 X0 Y0"}));
    // With homing: machine Z, only when below it.
    EXPECT_EQ(goToZeroCommands("XY", true, 5, -20), (std::vector<std::string>{"G53 G0 Z-5", "G90 G0 X0 Y0"}));
    EXPECT_EQ(goToZeroCommands("X", true, -5, -1), (std::vector<std::string>{"G90 G0 X0"}));
    // Without homing: up by the height and back down.
    EXPECT_EQ(goToZeroCommands("Y", false, 3, 0),
              (std::vector<std::string>{"G91", "G0Z3", "G90 G0 Y0", "G91 G0 Z-3", "G90"}));
    // Z itself never lifts first.
    EXPECT_EQ(goToZeroCommands("Z", false, 3, 0), (std::vector<std::string>{"G90 G0 Z0"}));
}

TEST_F(ActionsTest, ControllerShortcutsOnlyRunWhereUpstreamAllowsThem) {
    make();
    c().receiveLine("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    EXPECT_FALSE(runControllerCommand(c(), ControllerCommand::ResetLimit));  // nothing to unlock
    EXPECT_TRUE(runControllerCommand(c(), ControllerCommand::Homing));
    EXPECT_TRUE(sent("$H\n"));

    c().receiveLine("<Run|MPos:0.000,0.000,0.000|FS:0,0>");
    EXPECT_FALSE(runControllerCommand(c(), ControllerCommand::Reset));

    // A limit alarm gets the command itself...
    c().receiveLine("ALARM:1");
    c().receiveLine("<Alarm|MPos:0.000,0.000,0.000|FS:0,0>");
    link.sends.clear();
    EXPECT_TRUE(runControllerCommand(c(), ControllerCommand::Reset));
    EXPECT_TRUE(sent("\x18"));
    EXPECT_FALSE(sent("$X\n"));

    // ...any other alarm is just unlocked, whatever was asked.
    c().receiveLine("ALARM:9");
    c().receiveLine("<Alarm|MPos:0.000,0.000,0.000|FS:0,0>");
    link.sends.clear();
    EXPECT_TRUE(runControllerCommand(c(), ControllerCommand::Homing));
    EXPECT_TRUE(sent("$X\n"));
    EXPECT_FALSE(sent("$H\n"));
}

TEST_F(ActionsTest, ToolChangeAcknowledgementNeedsTheToolState) {
    make(Firmware::GrblHal);
    c().receiveLine("<Run|MPos:0.000,0.000,0.000|FS:0,0>");
    EXPECT_FALSE(runControllerCommand(c(), ControllerCommand::ToolChangeAcknowledge));
    c().receiveLine("<Tool|MPos:0.000,0.000,0.000|FS:0,0>");
    EXPECT_TRUE(runControllerCommand(c(), ControllerCommand::ToolChangeAcknowledge));
}


// ---- the status area (MachineStatus, UnlockButton) ----

TEST(StatusArea, NamesTheStatesAsUpstream) {
    EXPECT_EQ(statusLabel(""), "Disconnected");
    EXPECT_EQ(statusLabel("Run"), "Running");
    EXPECT_EQ(statusLabel("Jog"), "Jogging");
    EXPECT_EQ(statusLabel("Home"), "Homing");
    EXPECT_EQ(statusLabel("Tool"), "Tool Change");
    for (const char* same : {"Idle", "Hold", "Check", "Sleep", "Alarm", "Door"}) {
        EXPECT_EQ(statusLabel(same), same);
    }
}

// UnlockButton/__tests__/isHomingFailureAlarm.test.ts and the
// isLimitSwitchFaultAlarm cases of confirmUnlockAfterHomingFailure.test.tsx.
TEST(StatusArea, HomingFailuresAreAlarmsSixToNine) {
    for (const char* code : {"6", "7", "8", "9"}) {
        EXPECT_TRUE(isHomingFailureAlarm(code)) << code;
    }
    for (const char* code : {"1", "10", "17", "Homing", ""}) {
        EXPECT_FALSE(isHomingFailureAlarm(code)) << code;
    }
    EXPECT_TRUE(isLimitSwitchFaultAlarm("8"));
    EXPECT_TRUE(isLimitSwitchFaultAlarm("9"));
    EXPECT_FALSE(isLimitSwitchFaultAlarm("6"));
    EXPECT_FALSE(isLimitSwitchFaultAlarm("7"));
    EXPECT_FALSE(isLimitSwitchFaultAlarm("Homing"));
}

TEST(StatusArea, TheAlarmButtonResetsHomesAsksOrUnlocks) {
    for (const char* code : {"1", "2", "10", "14", "17"}) {
        EXPECT_EQ(alarmButtonAction("Alarm", code), UnlockAction::ResetLimit) << code;
    }
    EXPECT_EQ(alarmButtonAction("Alarm", "Homing"), UnlockAction::Home);
    EXPECT_EQ(alarmButtonAction("Alarm", "11"), UnlockAction::Home);
    EXPECT_TRUE(alarmButtonHomes("Alarm", "11"));
    EXPECT_FALSE(alarmButtonHomes("Alarm", "3"));
    EXPECT_EQ(alarmButtonAction("Alarm", "6"), UnlockAction::ConfirmHomingFailure);
    EXPECT_EQ(alarmButtonAction("Alarm", "3"), UnlockAction::Unlock);
    EXPECT_EQ(alarmButtonAction("Hold", ""), UnlockAction::CycleStart);
    EXPECT_EQ(alarmButtonAction("Idle", ""), UnlockAction::Unlock);
}

TEST(StatusArea, TheLockIconDiffersFromTheAlarmButton) {
    // Only 10 and 17 reset; the homing lock just unlocks (and re-reads the
    // configuration); outside an alarm it resumes, even when idle.
    EXPECT_EQ(lockIconAction("Alarm", "10"), UnlockAction::ResetLimit);
    EXPECT_EQ(lockIconAction("Alarm", "17"), UnlockAction::ResetLimit);
    EXPECT_EQ(lockIconAction("Alarm", "1"), UnlockAction::Unlock);
    EXPECT_EQ(lockIconAction("Alarm", "8"), UnlockAction::ConfirmHomingFailure);
    EXPECT_EQ(lockIconAction("Alarm", "Homing"), UnlockAction::Unlock);
    EXPECT_TRUE(lockIconRepopulates("Alarm", "Homing"));
    EXPECT_TRUE(lockIconRepopulates("Alarm", "11"));
    EXPECT_FALSE(lockIconRepopulates("Alarm", "1"));
    EXPECT_EQ(lockIconAction("Hold", ""), UnlockAction::CycleStart);
    EXPECT_EQ(lockIconAction("Idle", ""), UnlockAction::CycleStart);
}

TEST_F(ActionsTest, UnlockActionsSendTheirCommands) {
    make();
    runUnlockAction(c(), UnlockAction::Unlock);
    EXPECT_TRUE(sent("$X\n"));
    link.sends.clear();
    runUnlockAction(c(), UnlockAction::ResetLimit);  // soft reset, then $X
    EXPECT_TRUE(sent("\x18"));
    EXPECT_TRUE(sent("$X\n"));
    link.sends.clear();
    runUnlockAction(c(), UnlockAction::CycleStart);
    EXPECT_TRUE(sent("~"));
    link.sends.clear();
    runUnlockAction(c(), UnlockAction::ConfirmHomingFailure);
    EXPECT_TRUE(link.sends.empty());
    runUnlockAction(c(), UnlockAction::Home);
    EXPECT_TRUE(sent("$H\n"));
}

}  // namespace
