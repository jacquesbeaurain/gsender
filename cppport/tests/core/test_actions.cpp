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

}  // namespace
