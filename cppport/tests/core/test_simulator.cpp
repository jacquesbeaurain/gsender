// The simulated Grbl board, and the whole stack running a job against it.

#include "gs/controller/session.hpp"
#include "gs/probe/probing.hpp"
#include "gs/toolchange/wizards.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::sim;

namespace {

class SimulatorTest : public ::testing::Test {
protected:
    void SetUp() override {
        sim.onData = [this](std::string_view bytes) { output += bytes; };
        sim.open();
        loop.advance(100);  // the banner
        output.clear();
    }

    void send(std::string_view text) {
        sim.send(text, controller::SendKind::Write);
        loop.advance(0);
    }

    // Output since the last call.
    std::string take() {
        std::string out = std::move(output);
        output.clear();
        return out;
    }

    runtime::ManualEventLoop loop;
    GrblSimulator sim{loop};
    std::string output;
};

TEST_F(SimulatorTest, BootsWithTheGrblBanner) {
    GrblSimulator fresh(loop);
    std::string banner;
    fresh.onData = [&](std::string_view bytes) { banner += bytes; };
    fresh.open();
    loop.advance(99);
    EXPECT_EQ(banner, "");
    loop.advance(1);
    EXPECT_EQ(banner, "\r\nGrbl 1.1h ['$' for help]\r\n");
}

TEST_F(SimulatorTest, ReportsStatusParserStateAndSettings) {
    send("?");
    EXPECT_EQ(take(), "<Idle|MPos:0.000,0.000,0.000|FS:0,0|Ov:100,100,100|WCO:0.000,0.000,0.000>\r\n");
    send("$G\n");
    EXPECT_EQ(take(), "[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]\r\nok\r\n");
    send("$I\n");
    EXPECT_EQ(take(), "[VER:1.1h.20190825:]\r\n[OPT:V,15,128]\r\nok\r\n");
    send("$$\n");
    const std::string settings = take();
    EXPECT_NE(settings.find("$110=4000.000\r\n"), std::string::npos);
    EXPECT_TRUE(settings.ends_with("ok\r\n"));
    send("$110=2000\n");
    EXPECT_EQ(take(), "ok\r\n");
    send("$999=1\n");
    EXPECT_EQ(take(), "error:3\r\n");
}

TEST_F(SimulatorTest, MovesTakeTheTimeTheirFeedGives) {
    send("G1 X10 F600\n");  // 10 mm/s
    EXPECT_EQ(take(), "ok\r\n");
    EXPECT_EQ(sim.activeState(), "Run");
    loop.advance(500);
    EXPECT_NEAR(sim.machinePosition()[0], 5.0, 0.3);
    loop.advance(600);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 10.0);
    EXPECT_EQ(sim.activeState(), "Idle");

    send("G0 X110\n");  // a rapid at $110 = 4000 mm/min takes 1.5 s
    loop.advance(1400);
    EXPECT_LT(sim.machinePosition()[0], 110.0);
    loop.advance(200);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 110.0);
}

TEST_F(SimulatorTest, FeedHoldFreezesMotionUntilCycleStart) {
    send("G1 X10 F600\n");
    loop.advance(300);
    send("!");
    const double held = sim.machinePosition()[0];
    loop.advance(1000);
    EXPECT_EQ(sim.activeState(), "Hold:0");
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], held);
    send("~");
    EXPECT_EQ(sim.activeState(), "Run");
    loop.advance(1000);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 10.0);
}

TEST_F(SimulatorTest, AFullPlannerDelaysTheOk) {
    std::string lines;
    for (int i = 1; i <= 20; ++i) {
        lines += "G1 X" + std::to_string(i) + " F600\n";
    }
    send(lines);
    const std::string first = take();
    EXPECT_EQ(std::count(first.begin(), first.end(), 'k'), 15);  // one "ok" per planned block
    loop.advance(250);  // two blocks done
    const std::string later = take();
    EXPECT_GE(std::count(later.begin(), later.end(), 'k'), 2);
}

TEST_F(SimulatorTest, WorkCoordinatesFollowG10AndG92) {
    send("G0 X10 Y5\n");
    loop.advance(1000);
    send("G10 L20 P0 X0 Y0\n");
    take();
    EXPECT_DOUBLE_EQ(sim.workPosition()[0], 0.0);
    send("$#\n");
    EXPECT_NE(take().find("[G54:10.000,5.000,0.000]"), std::string::npos);
    send("G92 X1\n");
    EXPECT_DOUBLE_EQ(sim.workPosition()[0], 1.0);
    send("G92.1\nG0 X0\n");
    loop.advance(1000);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 10.0);  // G54 X0
}

TEST_F(SimulatorTest, ErrorsAlarmsAndUnlocking) {
    send("G1 X1\n");  // no feed rate yet
    EXPECT_EQ(take(), "error:22\r\n");
    send("G87\n");
    EXPECT_EQ(take(), "error:20\r\n");
    send("G1 X50 F600\n");
    loop.advance(100);
    take();
    send("\x18");  // a reset while moving loses the position
    EXPECT_EQ(take(), "ALARM:3\r\n\r\nGrbl 1.1h ['$' for help]\r\n[MSG:'$H'|'$X' to unlock]\r\n");
    EXPECT_EQ(sim.activeState(), "Alarm");
    send("G0 X0\n");
    EXPECT_EQ(take(), "error:9\r\n");
    send("$X\n");
    EXPECT_EQ(take(), "[MSG:Caution: Unlocked]\r\nok\r\n");
    EXPECT_EQ(sim.activeState(), "Idle");
}

TEST_F(SimulatorTest, HomingEndsAtMachineZero) {
    send("G0 X10\n");
    loop.advance(1000);
    take();
    send("$H\n");
    EXPECT_EQ(sim.activeState(), "Home");
    EXPECT_EQ(take(), "");  // the ok comes when homing is done
    loop.advance(1500);
    EXPECT_EQ(take(), "ok\r\n");
    EXPECT_EQ(sim.activeState(), "Idle");
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 0.0);
}

TEST_F(SimulatorTest, JogsRunUntilCancelled) {
    send("$J=G21G91X10F600\n");
    EXPECT_EQ(take(), "ok\r\n");
    EXPECT_EQ(sim.activeState(), "Jog");
    loop.advance(300);
    send("\x85");
    EXPECT_EQ(sim.activeState(), "Idle");
    const double stopped = sim.machinePosition()[0];
    EXPECT_GT(stopped, 0.0);
    EXPECT_LT(stopped, 10.0);
    loop.advance(1000);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], stopped);
    send("$J=G91 X1\n");  // a jog needs its own feed rate
    EXPECT_EQ(take(), "error:22\r\n");
}

TEST_F(SimulatorTest, FeedOverridesChangeTheSpeed) {
    send("\x91");
    EXPECT_EQ(sim.feedOverride(), 110);
    for (int i = 0; i < 10; ++i) {
        send("\x91");
    }
    EXPECT_EQ(sim.feedOverride(), 200);  // capped
    send("G1 X10 F600\n");
    loop.advance(520);  // twice as fast
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 10.0);
}

TEST_F(SimulatorTest, ADwellAnswersOnceThePlannerEmptiesAndTheTimeIsUp) {
    send("G1 X10 F600\n");  // 1 s
    EXPECT_EQ(take(), "ok\r\n");
    send("G4 P0.5\n");
    send("G0 X0\n");  // waits behind the dwell, like Grbl's serial buffer
    loop.advance(1400);
    EXPECT_EQ(take(), "");
    loop.advance(200);
    EXPECT_EQ(take(), "ok\r\nok\r\n");
}

TEST_F(SimulatorTest, ProbesStopWhereTheBitTouchesThePlate) {
    sim.setProbeSolids({Solid{{-10, -10, -20}, {40, 40, -5}}});
    sim.setToolRadius(3);
    send("G91 G38.2 Z-10 F600\n");  // the plate top is 5 below
    EXPECT_EQ(take(), "");           // answered when the probe completes
    loop.advance(600);
    EXPECT_EQ(take(), "[PRB:0.000,0.000,-5.000:1]\r\nok\r\n");
    EXPECT_DOUBLE_EQ(sim.machinePosition()[2], -5.0);
    send("?");
    EXPECT_NE(take().find("|Pn:P|"), std::string::npos);

    send("G38.2 Z-1\n");  // already touching
    loop.advance(50);
    EXPECT_EQ(take(), "ALARM:4\r\nok\r\n");
    send("$X\n");
    take();

    // The bit's radius counts: a side probe stops 3 mm short of the face.
    send("G0 Z2\n");
    send("G0 X-20\n");
    send("G0 Z-3\n");
    loop.advance(2000);
    take();
    send("G38.2 X15 F600\n");
    loop.advance(1000);
    EXPECT_EQ(take(), "[PRB:-13.000,0.000,-6.000:1]\r\nok\r\n");
}

TEST_F(SimulatorTest, AMissedProbeAlarmsUnlessItOnlyReports) {
    send("G91 G38.3 Z-2 F600\n");
    loop.advance(300);
    EXPECT_EQ(take(), "[PRB:0.000,0.000,-2.000:0]\r\nok\r\n");
    send("G38.2 Z-2\n");
    loop.advance(300);
    EXPECT_EQ(take(), "ALARM:5\r\n[PRB:0.000,0.000,-2.000:0]\r\nok\r\n");
    send("G0 X1\n");
    EXPECT_EQ(take(), "error:9\r\n");
}

TEST_F(SimulatorTest, CheckModeValidatesWithoutMoving) {
    send("$C\n");
    EXPECT_EQ(take(), "[MSG:Enabled]\r\nok\r\n");
    send("G1 X10 F600\n");
    EXPECT_EQ(take(), "ok\r\n");
    loop.advance(2000);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 0.0);
    send("?");
    EXPECT_TRUE(take().starts_with("<Check|"));
}

// ---- the whole stack ------------------------------------------------------------------

TEST(EndToEnd, AJobRunsToCompletionOnTheSimulatedBoard) {
    runtime::ManualEventLoop loop;
    GrblSimulator sim(loop);
    std::vector<controller::ControllerEvent> events;
    controller::Session session(loop, sim, {}, {},
                                [&](const controller::ControllerEvent& e) { events.push_back(e); });
    sim.onData = [&](std::string_view bytes) { session.receive(bytes); };

    sim.open();
    session.opened();
    for (int i = 0; i < 100 && (!session.controller() || !session.controller()->runner().hasSettings()); ++i) {
        loop.advance(50);
    }
    ASSERT_NE(session.controller(), nullptr);
    controller::Controller& c = *session.controller();
    EXPECT_EQ(c.firmware(), protocol::Firmware::Grbl);
    ASSERT_TRUE(c.runner().hasSettings());  // the controller initialized the board
    EXPECT_EQ(c.runner().setting("$110"), "4000.000");

    ASSERT_TRUE(c.loadProgram("square.nc", "G21 G90\nG1 X10 F1200\nG1 Y10\nG1 X0\nG1 Y0\nM30\n").ok);
    c.start();
    EXPECT_TRUE(c.workflow().isRunning());
    for (int i = 0; i < 400 && !c.workflow().isIdle(); ++i) {
        loop.advance(50);
    }
    EXPECT_TRUE(c.workflow().isIdle());
    // (Ending the job rewinds the sender, so its counters read 0 again.)
    const bool stopped = std::any_of(events.begin(), events.end(), [](const controller::ControllerEvent& e) {
        return std::holds_alternative<controller::JobStopped>(e);
    });
    EXPECT_TRUE(stopped);
    EXPECT_EQ(c.state().status.activeState, "Idle");
    EXPECT_DOUBLE_EQ(sim.machinePosition()[0], 0.0);
    EXPECT_DOUBLE_EQ(sim.machinePosition()[1], 0.0);
    // Every program line reached the board, in order.
    const std::vector<std::string>& lines = sim.receivedLines();
    const auto first = std::find(lines.begin(), lines.end(), "G1 X10 F1200");
    ASSERT_NE(first, lines.end());
    const std::vector<std::string> job(first, first + 5);
    EXPECT_EQ(job, (std::vector<std::string>{"G1 X10 F1200", "G1 Y10", "G1 X0", "G1 Y0", "M30"}));
}

// A job reaching M6 with a wizard strategy: the wizard's start-up G-code runs
// once the board is idle, its actions run on the paused job, and
// %toolchange_complete resumes it.
TEST(EndToEnd, AToolChangeWizardCarriesTheJobOn) {
    runtime::ManualEventLoop loop;
    GrblSimulator sim(loop);
    std::vector<controller::ControllerEvent> events;
    controller::Session session(loop, sim, {}, {},
                                [&](const controller::ControllerEvent& e) { events.push_back(e); });
    sim.onData = [&](std::string_view bytes) { session.receive(bytes); };
    sim.open();
    session.opened();
    for (int i = 0; i < 100 && (!session.controller() || !session.controller()->runner().hasSettings()); ++i) {
        loop.advance(50);
    }
    ASSERT_NE(session.controller(), nullptr);
    controller::Controller& c = *session.controller();
    c.setToolChangeContext({"Standard Re-zero"});
    ASSERT_TRUE(c.loadProgram("tools.nc", "G21 G90\nG0 X5 Z-2\nM6 T2\nG0 X10\n").ok);
    c.start();
    const auto requested = [&] {
        return std::any_of(events.begin(), events.end(), [](const controller::ControllerEvent& e) {
            return std::holds_alternative<controller::ToolChangeRequested>(e);
        });
    };
    for (int i = 0; i < 100 && !requested(); ++i) {
        loop.advance(50);
    }
    ASSERT_TRUE(requested());
    const toolchange::Wizard wizard = toolchange::standardRezero({}, {});
    std::string start;
    for (const std::string& line : wizard.start) {
        start += line + "\n";
    }
    c.wizardStart(start);
    const std::vector<std::string>& lines = sim.receivedLines();
    for (int i = 0; i < 100 && std::find(lines.begin(), lines.end(), "G91 G21") == lines.end(); ++i) {
        loop.advance(50);
    }
    ASSERT_NE(std::find(lines.begin(), lines.end(), "G91 G21"), lines.end());

    // Paper method, then resume.
    c.gcode(wizard.steps[1].substeps[0].actions[1].gcode);
    c.wizardStep(2, 0);
    c.gcode(wizard.steps[2].substeps[0].actions[0].gcode);
    for (int i = 0; i < 400 && !(c.workflow().isIdle() && sim.machinePosition()[0] > 9.99); ++i) {
        loop.advance(50);
    }
    EXPECT_TRUE(c.workflow().isIdle());
    EXPECT_NEAR(sim.machinePosition()[0], 10, 1e-9);
    // Back to the stored spot, the modals restored, then the rest of the job.
    const auto back = std::find(lines.begin(), lines.end(), "G90 G21 G0 X5 Y0");
    ASSERT_NE(back, lines.end());
    EXPECT_NE(std::find(back, lines.end(), "G0 X10"), lines.end());
}

// gSender's standard block routine (XYZ, 1/4" bit) against a simulated plate
// on each corner of the stock: afterwards work zero is the stock corner.
class StandardBlockProbing : public ::testing::TestWithParam<int> {};

TEST_P(StandardBlockProbing, ZeroesTheStockCorner) {
    const int corner = GetParam();
    runtime::ManualEventLoop loop;
    GrblSimulator sim(loop);
    std::vector<std::string> alarms;
    controller::Session session(loop, sim, {}, {}, [&](const controller::ControllerEvent& e) {
        if (const auto* line = std::get_if<controller::ConsoleOutput>(&e); line && line->text.starts_with("ALARM")) {
            alarms.push_back(line->text);
        }
    });
    sim.onData = [&](std::string_view bytes) { session.receive(bytes); };
    sim.open();
    session.opened();
    for (int i = 0; i < 100 && (!session.controller() || !session.controller()->runner().hasSettings()); ++i) {
        loop.advance(50);
    }
    ASSERT_NE(session.controller(), nullptr);
    controller::Controller& c = *session.controller();

    // The bit starts 10 mm above the plate, 5 mm in from its outer faces.
    const double sx = corner == probe::kBottomLeft || corner == probe::kTopLeft ? 1 : -1;
    const double sy = corner == probe::kBottomLeft || corner == probe::kBottomRight ? 1 : -1;
    const double cornerX = 5 * sx;
    const double cornerY = 5 * sy;
    const double stockTop = -25;
    sim.setProbeSolids(touchPlateOnCorner(corner, cornerX, cornerY, stockTop));
    sim.setToolRadius(6.35 / 2);

    probe::MachineFacts facts;
    facts.homing = c.runner().setting("$22");
    facts.zMaxTravel = c.runner().setting("$132");
    const probe::ProbingOptions options = probe::makeProbingOptions(
        probe::ProbeSettings{}, true, {true, true, true}, probe::ProbeType::Diameter, 6.35, facts);
    std::vector<std::string> code = probe::probeCode(options, corner);
    code.emplace_back("G90");  // the widget restores the distance mode
    c.gcodeSafe(code, "G21");
    for (int i = 0; i < 2000 && (c.feeder().size() > 0 || c.feeder().isPending() || sim.activeState() != "Idle");
         ++i) {
        loop.advance(50);
    }
    EXPECT_TRUE(alarms.empty()) << alarms.front();
    EXPECT_EQ(sim.activeState(), "Idle");
    const SimAxes offset = sim.workOffset();
    EXPECT_NEAR(offset[0], cornerX, 1e-9);
    EXPECT_NEAR(offset[1], cornerY, 1e-9);
    EXPECT_NEAR(offset[2], stockTop, 1e-9);
    // The bit ends over the corner, clear of the plate.
    const SimAxes work = sim.workPosition();
    EXPECT_NEAR(work[0], 0, 1e-9);
    EXPECT_NEAR(work[1], 0, 1e-9);
    EXPECT_NEAR(work[2], 19, 1e-9);
}

INSTANTIATE_TEST_SUITE_P(Corners, StandardBlockProbing, ::testing::Values(0, 1, 2, 3));

}  // namespace
