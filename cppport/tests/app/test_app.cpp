// The Qt side: the event loop adapter, the machine service against the
// simulated board (real time, so kept short), and a main-window smoke test on
// the offscreen platform.

#include "jogger.hpp"
#include "machine.hpp"
#include "main_window.hpp"
#include "panels.hpp"
#include "probe_panel.hpp"
#include "qt_event_loop.hpp"
#include "settings_dialog.hpp"
#include "shortcuts.hpp"
#include "shortcuts_dialog.hpp"
#include "start_from_line_dialog.hpp"
#include "surfacing_dialog.hpp"
#include "toolchange_dialog.hpp"

#include "gs/controller/actions.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include <QApplication>
#include <QKeyEvent>
#include <QLabel>
#include <QTableWidget>
#include <QDeadlineTimer>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

using namespace gs;
using namespace gs::app;

namespace {

QApplication& application() {
    static int argc = 1;
    static char name[] = "gs_app_tests";
    static char* argv[] = {name, nullptr};
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("WINDIR")) {
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "\\Fonts");
        }
        return new QApplication(argc, argv);
    }();
    return *app;
}

// Runs Qt events until `done` holds; false on timeout.
bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000) {
    const QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

void runFor(int ms) {
    waitFor([] { return false; }, ms);
}

class AppTest : public ::testing::Test {
protected:
    void SetUp() override { application(); }
};

TEST_F(AppTest, EventLoopRunsTimersInOrderAndClearsThem) {
    QtEventLoop loop;
    std::vector<int> order;
    loop.setTimeout(30, [&] { order.push_back(2); });
    loop.setTimeout(10, [&] { order.push_back(1); });
    const runtime::TimerId cancelled = loop.setTimeout(20, [&] { order.push_back(99); });
    loop.clear(cancelled);
    EXPECT_TRUE(waitFor([&] { return order.size() == 2; }));
    runFor(20);
    EXPECT_EQ(order, (std::vector<int>{1, 2}));
    EXPECT_EQ(loop.activeTimers(), 0u);
    EXPECT_GE(loop.nowMs(), 1'000'000);
}

TEST_F(AppTest, IntervalsRepeatUntilTheyClearThemselves) {
    QtEventLoop loop;
    int ticks = 0;
    runtime::TimerId id = 0;
    id = loop.setInterval(5, [&] {
        if (++ticks == 3) {
            loop.clear(id);
        }
    });
    EXPECT_TRUE(waitFor([&] { return ticks >= 3; }));
    runFor(30);
    EXPECT_EQ(ticks, 3);
    EXPECT_EQ(loop.activeTimers(), 0u);
}

TEST_F(AppTest, PostWorksFromOtherThreads) {
    QtEventLoop loop;
    int ran = 0;  // only touched on this thread
    std::thread worker([&] {
        for (int i = 0; i < 100; ++i) {
            loop.post([&] { ++ran; });
        }
    });
    worker.join();
    EXPECT_TRUE(waitFor([&] { return ran == 100; }));
}

TEST_F(AppTest, TheMachineConnectsToTheSimulatorAndAnalysesPrograms) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    std::vector<QString> console;
    QObject::connect(&machine, &Machine::consoleLine,
                     [&](const QString& text, bool) { console.push_back(text.trimmed()); });

    machine.connectTo(Machine::kSimulatorPort);
    EXPECT_TRUE(machine.isConnecting());
    ASSERT_TRUE(waitFor([&] { return machine.isConnected(); }));
    EXPECT_EQ(machine.controller()->firmware(), protocol::Firmware::Grbl);

    machine.sendConsoleLine("$I");
    EXPECT_TRUE(waitFor([&] {
        return std::find(console.begin(), console.end(), "[VER:1.1h.20190825:]") != console.end();
    }));

    machine.loadProgram("square.nc", "G21 G90\nG1 X5 F1200\nG1 Y5\nG2 X0 Y0 I-2.5 J-2.5\n");
    EXPECT_TRUE(machine.isAnalyzing());
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    EXPECT_EQ(machine.analysis().estimates.size(), 4u);
    EXPECT_GT(machine.toolpath().feeds.size(), 6u * 3);  // the arc is tessellated
    EXPECT_TRUE(machine.controller()->sender().hasProgram());

    machine.disconnectFromMachine();
    EXPECT_FALSE(machine.isConnected());
    EXPECT_TRUE(machine.hasProgram());  // the file stays loaded
}

TEST_F(AppTest, MacrosAreStoredAndRunOnTheMachine) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    const auto macro = machine.macros().create("Park", "G0 Z5\nG0 X0 Y0", "Lift and go home");
    ASSERT_TRUE(macro.has_value());
    EXPECT_EQ(machine.macros().list().size(), 1u);

    std::vector<QString> sent;
    QObject::connect(&machine, &Machine::consoleLine, [&](const QString& text, bool fromHost) {
        if (fromHost) {
            sent.push_back(text.trimmed());
        }
    });
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected(); }));
    ASSERT_TRUE(machine.controller()->runMacro(macro->id));
    EXPECT_TRUE(waitFor([&] {
        return std::find(sent.begin(), sent.end(), "G0 X0 Y0") != sent.end();
    }));
    EXPECT_NE(std::find(sent.begin(), sent.end(), "G0 Z5"), sent.end());
}

TEST_F(AppTest, SettingsPersistAndReachTheController) {
    QTemporaryDir dir;
    QtEventLoop loop;
    const std::wstring file = (dir.path() + "/rc").toStdWString();
    {
        Machine machine(loop, file);
        AppSettings settings = machine.settings();
        EXPECT_EQ(settings.toolChange.option, "Ignore");  // gSender's default
        settings.toolChange.option = "Code";
        settings.toolChange.preHook = "G0 Z20";
        settings.preferences.spindleDelay = 1.5;
        settings.port = "COM7";
        settings.probe.plateType = probe::PlateType::AutoZero;
        settings.probe.xyThickness = 9.5;
        settings.probe.connectivityTest = false;
        settings.probe.direction = probe::kTopRight;
        machine.setSettings(settings);
    }
    Machine machine(loop, file);
    EXPECT_EQ(machine.settings().toolChange.option, "Code");
    EXPECT_EQ(machine.settings().toolChange.preHook, "G0 Z20");
    EXPECT_EQ(machine.settings().preferences.spindleDelay, 1.5);
    EXPECT_EQ(machine.preferences().spindleDelay, 1.5);
    EXPECT_EQ(machine.settings().port, "COM7");
    EXPECT_EQ(machine.settings().probe.plateType, probe::PlateType::AutoZero);
    EXPECT_EQ(machine.settings().probe.xyThickness, 9.5);
    EXPECT_FALSE(machine.settings().probe.connectivityTest);
    EXPECT_EQ(machine.settings().probe.direction, probe::kTopRight);

    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected(); }));
    EXPECT_EQ(machine.controller()->toolChangeContext().option, "Code");
    EXPECT_EQ(machine.controller()->toolChangeContext().preHook, "G0 Z20");
}

TEST_F(AppTest, TheProbeTabZeroesTheCornerOfTheSimulatedStock) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(200);

    ProbePanel panel(machine);
    panel.selectCommand(1);
    ASSERT_EQ(panel.command().id, "XYZ Touch");
    EXPECT_EQ(panel.probeType(), probe::ProbeType::Diameter);
    EXPECT_EQ(panel.toolDiameter(), 6.35);
    EXPECT_EQ(panel.corner(), probe::kBottomLeft);

    RunProbeDialog* dialog = panel.openRunDialog();  // puts the simulated plate under the bit
    ASSERT_NE(dialog, nullptr);
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        panel.resize(520, 220);
        panel.show();
        panel.grab().save(QString::fromLocal8Bit(out) + "/probe_panel.png");
        dialog->grab().save(QString::fromLocal8Bit(out) + "/probe_run.png");
    }
    EXPECT_FALSE(dialog->start());  // the circuit is not checked yet
    dialog->confirmCircuit();
    ASSERT_TRUE(dialog->start());
    const sim::SimAxes start = machine.simulator()->machinePosition();
    ASSERT_TRUE(waitFor([&] {
        controller::Controller* c = machine.controller();
        return c->feeder().size() == 0 && !c->feeder().isPending() && machine.simulator()->activeState() == "Idle";
    }, 10000));
    // The plate's inner corner sat 5 mm beyond the bit (towards +X+Y), its
    // top 10 mm below and 15 mm above the stock.
    const sim::SimAxes offset = machine.simulator()->workOffset();
    EXPECT_NEAR(offset[0], start[0] + 5, 1e-6);
    EXPECT_NEAR(offset[1], start[1] + 5, 1e-6);
    EXPECT_NEAR(offset[2], start[2] - 25, 1e-6);
}

TEST_F(AppTest, TheSurfacingToolGeneratesAndLoadsAJob) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    SurfacingDialog dialog(machine);
    surfacing::Options options = dialog.options();
    EXPECT_EQ(options.width, 100);  // gSender's defaults
    EXPECT_EQ(options.bitDiameter, 22);
    options.width = 150;
    options.type = surfacing::Pattern::ZigZag;
    options.startPosition = surfacing::StartPosition::Center;
    dialog.setOptions(options);
    EXPECT_FALSE(dialog.loadIntoMachine());  // nothing generated yet
    dialog.generate();
    EXPECT_EQ(dialog.program().toStdString(), surfacing::generate(options, true));
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        dialog.show();
        dialog.grab().save(QString::fromLocal8Bit(out) + "/surfacing.png");
    }
    ASSERT_TRUE(dialog.loadIntoMachine());
    EXPECT_EQ(machine.programName(), "gSender_Surfacing.gcode");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    EXPECT_FALSE(machine.toolpath().feeds.empty());
    dialog.reject();  // closing keeps the settings
    EXPECT_EQ(machine.settings().surfacing.width, 150);
    EXPECT_EQ(machine.settings().surfacing.type, surfacing::Pattern::ZigZag);
}

TEST_F(AppTest, ShortcutKeysBindSymbolsWithoutShift) {
    const QKeyEvent tilde(QEvent::KeyPress, Qt::Key_AsciiTilde, Qt::ShiftModifier, "~");
    EXPECT_EQ(QKeySequence(shortcutKey(tilde)), QKeySequence("~"));
    const QKeyEvent zero(QEvent::KeyPress, Qt::Key_W, Qt::ShiftModifier, "W");
    EXPECT_EQ(QKeySequence(shortcutKey(zero)), QKeySequence("Shift+W"));
    const QKeyEvent jog(QEvent::KeyPress, Qt::Key_Right, Qt::ShiftModifier | Qt::KeypadModifier);
    EXPECT_EQ(QKeySequence(shortcutKey(jog)), QKeySequence("Shift+Right"));
}

TEST_F(AppTest, ShortcutsUseTheUsersKeysOverTheDefaults) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    QWidget window;
    ShortcutManager shortcuts(machine, window);
    EXPECT_EQ(shortcuts.actionFor(QKeySequence("~")[0]), "START_JOB");
    EXPECT_EQ(shortcuts.actionFor(QKeySequence("Shift+PgUp")[0]), "JOG_Z_P");

    AppSettings settings = machine.settings();
    settings.shortcuts["START_JOB"] = {"F9", true};
    settings.shortcuts["STOP_JOB"] = {"@", false};  // switched off
    machine.setSettings(settings);
    EXPECT_EQ(shortcuts.actionFor(QKeySequence("F9")[0]), "START_JOB");
    EXPECT_TRUE(shortcuts.actionFor(QKeySequence("~")[0]).isEmpty());
    EXPECT_TRUE(shortcuts.actionFor(QKeySequence("@")[0]).isEmpty());

    int toggles = 0;
    shortcuts.setHandler("TOGGLE_SHORTCUTS", [&] { ++toggles; });
    shortcuts.setHandler("START_JOB", [] {});
    settings.shortcutsEnabled = false;
    machine.setSettings(settings);
    EXPECT_FALSE(shortcuts.trigger("START_JOB"));  // all off...
    EXPECT_TRUE(shortcuts.trigger("TOGGLE_SHORTCUTS"));  // ...but the switch itself
    EXPECT_EQ(toggles, 1);
}

TEST_F(AppTest, HeldJogKeysJogUntilReleased) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    MainWindow window(machine);
    window.setDialogsEnabled(false);
    window.show();
    window.activateWindow();
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->state().status.activeState == "Idle";
    }));
    ASSERT_TRUE(waitFor([&] { return QApplication::activeWindow() == &window; }, 2000));
    QWidget* target = QApplication::focusWidget() ? QApplication::focusWidget() : &window;
    const auto key = [&](QEvent::Type type, bool autoRepeat = false) {
        QKeyEvent event(type, Qt::Key_Right, Qt::ShiftModifier, QString(), autoRepeat);
        QCoreApplication::sendEvent(target, &event);
        return event.isAccepted();
    };

    // A tap steps by the Normal preset's 5 mm.
    key(QEvent::KeyPress);
    key(QEvent::KeyRelease);
    ASSERT_TRUE(waitFor([&] { return machine.simulator()->activeState() == "Idle" &&
                                     machine.simulator()->machinePosition()[0] > 4.99; }));
    EXPECT_NEAR(machine.simulator()->machinePosition()[0], 5.0, 1e-9);

    // A hold jogs continuously; auto-repeat is swallowed; release stops.
    key(QEvent::KeyPress);
    runFor(350);
    key(QEvent::KeyPress, true);
    EXPECT_EQ(machine.simulator()->activeState(), "Jog");
    key(QEvent::KeyRelease);
    ASSERT_TRUE(waitFor([&] { return machine.simulator()->activeState() == "Idle"; }));
    EXPECT_GT(machine.simulator()->machinePosition()[0], 5.0);
}

TEST_F(AppTest, TheShortcutEditorRefusesTakenKeysAndStoresOnlyChanges) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    ShortcutsDialog dialog(machine);
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        dialog.show();
        dialog.grab().save(QString::fromLocal8Bit(out) + "/shortcuts.png");
    }
    QString conflict;
    EXPECT_FALSE(dialog.setKeys("START_JOB", QKeySequence("@"), &conflict));
    EXPECT_EQ(conflict, "Global Stop");
    EXPECT_TRUE(dialog.setKeys("START_JOB", QKeySequence("F9")));
    EXPECT_TRUE(dialog.setKeys("PAUSE_JOB", QKeySequence("!")));  // its default: no change
    dialog.setActive("STOP_JOB", false);
    dialog.save();
    const auto& stored = machine.settings().shortcuts;
    EXPECT_EQ(stored.size(), 2u);
    EXPECT_EQ(stored.at("START_JOB").keys, "F9");
    EXPECT_FALSE(stored.at("STOP_JOB").active);
    dialog.resetAll();
    dialog.save();
    EXPECT_TRUE(machine.settings().shortcuts.empty());
}

TEST_F(AppTest, StartFromLineResumesAStoppedJob) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->state().status.activeState == "Idle";
    }));
    // 30 moves of 1 mm at 600 mm/min: 0.1 s each.
    std::string program = "G21 G90\nG1 F600\n";
    for (int i = 1; i <= 30; ++i) {
        program += "G1 X" + std::to_string(i) + "\n";
    }
    machine.loadProgram("job.nc", program);
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    controller::Controller& c = *machine.controller();
    controller::runJob(c);
    ASSERT_TRUE(waitFor([&] { return c.sender().currentLineRunning() >= 6; }));
    controller::stopJob(c);
    ASSERT_TRUE(waitFor([&] { return c.workflow().isIdle() && c.state().status.activeState == "Idle"; }));
    EXPECT_GE(machine.lastLine(), 6);  // where the job stopped

    StartFromLineDialog dialog(machine);
    EXPECT_EQ(dialog.line(), std::max<int>(static_cast<int>(machine.lastLine()) - 10, 1));
    EXPECT_EQ(dialog.safeHeight(), 10);  // no safe retract height set
    dialog.setLine(20);
    machine.simulator()->setSpeed(20);
    ASSERT_TRUE(dialog.start());
    ASSERT_TRUE(waitFor([&] { return machine.simulator()->machinePosition()[0] > 29.99; }));
    const std::vector<std::string>& lines = machine.simulator()->receivedLines();
    const auto rise = std::find(lines.begin(), lines.end(), "G0 G90 G21 Z10");  // file top (0) + safe height
    ASSERT_NE(rise, lines.end());
    const auto resumed = std::find(rise, lines.end(), "G1 X19");  // line 21 of the file
    EXPECT_NE(resumed, lines.end());
    EXPECT_EQ(std::find(rise, lines.end(), "G1 X18"), lines.end());  // earlier lines are skipped
}

TEST_F(AppTest, TheOutlineTracesTheJobAndMacrosSeeTheFileBox) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(20);
    machine.loadProgram("rect.nc", "G21 G90\nG0 X10 Y5\nG1 X30 F2000\nG1 Y15\nG1 X10\nG1 Y5\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    EXPECT_EQ(machine.fileContext().get("xmax").asNumber(), 30);
    EXPECT_EQ(machine.fileContext().get("xmin").asNumber(), 0);  // the rapid from the origin counts

    // With homing ($22=1) the lift is what is left above the machine Z, less
    // 1 mm, at most 5 (getZUpTravel): start 10 mm down.
    machine.sendConsoleLine("G53 G0 Z-10");
    ASSERT_TRUE(waitFor([&] {  // as the controller last heard it
        return machine.controller()->runner().machinePosition()[2] < -9.99 &&
               machine.controller()->state().status.activeState == "Idle";
    }));

    ASSERT_TRUE(machine.runOutline());
    const std::vector<std::string>& lines = machine.simulator()->receivedLines();
    ASSERT_TRUE(waitFor([&] {
        return std::find(lines.begin(), lines.end(), "G21 G91 G0 Z-5") != lines.end() &&
               machine.simulator()->activeState() == "Idle";
    })) << [&] {
        std::string all;
        for (const std::string& line : lines) {
            all += line + " | ";
        }
        return all;
    }();
    const auto lift = std::find(lines.begin(), lines.end(), "G21 G91 G0 Z5");
    ASSERT_NE(lift, lines.end());
    // The hull of the rectangle and the rapid in from the origin, from the
    // origin round, and back.
    const std::vector<std::string> hull{"X0 Y0", "X30 Y5", "X30 Y15", "X10 Y15", "X0 Y0"};
    EXPECT_NE(std::search(lift, lines.end(), hull.begin(), hull.end()), lines.end());
    EXPECT_NEAR(machine.simulator()->machinePosition()[2], -10, 1e-9);  // lowered again

    // Macros see the same box.
    const auto macro = machine.macros().create("Right edge", "G0 X[xmax]", "");
    ASSERT_TRUE(macro.has_value());
    ASSERT_TRUE(machine.controller()->runMacro(macro->id, machine.fileContext()));
    EXPECT_TRUE(waitFor([&] { return std::find(lines.begin(), lines.end(), "G0 X30") != lines.end(); }));
}

TEST_F(AppTest, AnInchWorkspaceShowsAndJogsInInches) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.metric = false;
    machine.setSettings(settings);
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));

    // Jogging: the Normal preset (5 mm at 3000 mm/min) converted, sent in G20.
    Jogger jogger(machine);
    EXPECT_EQ(jogger.speeds().xyStep, 0.197);
    EXPECT_EQ(jogger.speeds().feedrate, 118.11);
    std::vector<QString> sent;
    QObject::connect(&machine, &Machine::consoleLine, [&](const QString& text, bool fromHost) {
        if (fromHost) {
            sent.push_back(text.trimmed());
        }
    });
    jogger.press({{'X', 1}});
    jogger.release();
    EXPECT_TRUE(waitFor([&] {
        return std::find(sent.begin(), sent.end(), "$J=G20 G91 X0.197 F118.11") != sent.end();
    }));
    ASSERT_TRUE(waitFor([&] { return machine.simulator()->machinePosition()[0] > 0.197 * 25.4 - 1e-6 &&
                                     machine.simulator()->activeState() == "Idle"; }));
    EXPECT_NEAR(machine.simulator()->machinePosition()[0], 0.197 * 25.4, 1e-9);

    // The probe routine and the surfacing program are written in inches.
    const std::vector<std::string> routine = machine.probeRoutine({false, false, true}, probe::ProbeType::Diameter,
                                                                  0.25, probe::kBottomLeft);
    EXPECT_NE(std::find(routine.begin(), routine.end(), "G91 G20"), routine.end());
    EXPECT_NE(std::find(routine.begin(), routine.end(), "%Z_THICKNESS=0.591"), routine.end());  // 15 mm
    SurfacingDialog surfacing(machine);
    EXPECT_EQ(surfacing.options().width, 3.937);  // 100 mm
    surfacing.generate();
    EXPECT_TRUE(surfacing.program().contains("G20 ;inches"));
    surfacing.reject();
    EXPECT_EQ(machine.settings().surfacing.width, 100);  // stored in mm again

    // The position panel shows inches (3 decimals).
    PositionPanel panel(machine);
    ASSERT_TRUE(waitFor([&] {
        const auto labels = panel.findChildren<QLabel*>();
        return std::any_of(labels.begin(), labels.end(), [](QLabel* l) { return l->text() == "0.197"; });
    }));
}

TEST_F(AppTest, AStandardReZeroWizardCarriesAJobThroughItsToolChange) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.toolChange.option = "Standard Re-zero";
    machine.setSettings(settings);
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(20);
    QString requested;
    int requestedCount = 0;
    QObject::connect(&machine, &Machine::toolChangeWizardRequested,
                     [&](const QString& option, int count, const QString&) {
                         requested = option;
                         requestedCount = count;
                     });
    machine.loadProgram("tools.nc", "G21 G90\nG0 X5 Z-2\nM6 T2\nG0 X10\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    controller::runJob(*machine.controller());
    ASSERT_TRUE(waitFor([&] { return !requested.isEmpty(); }));
    EXPECT_EQ(requested, "Standard Re-zero");
    EXPECT_EQ(requestedCount, 1);
    EXPECT_TRUE(machine.controller()->workflow().isPaused());

    const auto wizard = machine.startToolChangeWizard(requested.toStdString(), requestedCount);
    ASSERT_TRUE(wizard.has_value());
    ToolChangeWizardDialog dialog(machine, *wizard);
    // Actions wait for the start-up G-code, which goes once the board is idle.
    dialog.next();
    dialog.next();
    dialog.runAction(1);
    EXPECT_FALSE(dialog.isRunning());
    ASSERT_TRUE(waitFor([&] { return machine.isToolChangeWizardReady(); }));
    const std::vector<std::string>& received = machine.simulator()->receivedLines();
    EXPECT_TRUE(waitFor([&] { return std::find(received.begin(), received.end(), "G91 G21") != received.end(); }));
    dialog.back();
    dialog.back();
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        dialog.next();
        dialog.show();
        dialog.grab().save(QString::fromLocal8Bit(out) + "/toolchange_wizard.png");
        dialog.back();
    }
    dialog.next();  // Safety First
    dialog.next();  // Change Bit
    ASSERT_EQ(dialog.step(), 1);
    dialog.next();  // an instruction with actions only passes once one ran
    EXPECT_EQ(dialog.step(), 1);
    dialog.runAction(1);  // Set Z0 (paper method)
    EXPECT_TRUE(dialog.isRunning());
    ASSERT_TRUE(waitFor([&] { return dialog.step() == 2; }));
    // Z0 was set where the bit is (the job's Z-2 in the first work offset).
    EXPECT_NEAR(machine.simulator()->workOffset()[2], -2, 1e-9);

    dialog.runAction(0);  // Resume Job: back to the stored position, %toolchange_complete
    ASSERT_TRUE(waitFor([&] { return dialog.result() == QDialog::Accepted; })) << [&] {
        std::string all;
        for (const std::string& line : machine.simulator()->receivedLines()) {
            all += line + " | ";
        }
        return all + " running=" + std::to_string(dialog.isRunning()) + " step=" + std::to_string(dialog.step()) +
               " state=" + machine.controller()->state().status.activeState +
               " sim=" + machine.simulator()->activeState();
    }();
    ASSERT_TRUE(waitFor([&] { return machine.controller()->workflow().isIdle() &&
                                     machine.simulator()->machinePosition()[0] > 9.99; }, 8000));
    EXPECT_NEAR(machine.simulator()->machinePosition()[0], 10, 1e-9);
}

TEST_F(AppTest, TheSettingsDialogListsTheFirmwareSettings) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected() && machine.controller()->runner().hasSettings(); }, 5000));
    SettingsDialog dialog(machine);
    dialog.showPage(SettingsDialog::Page::Firmware);
    dialog.show();
    auto* table = dialog.findChild<QTableWidget*>();
    ASSERT_NE(table, nullptr);
    const int expected = static_cast<int>(machine.controller()->settings().settings.size());
    EXPECT_TRUE(waitFor([&] { return table->rowCount() == expected; }));  // the next poll broadcasts them
    EXPECT_EQ(table->item(0, 0)->text(), "$0");
    EXPECT_NE(table->item(0, 3)->text().indexOf("Step pulse"), -1);  // described from the Grbl tables
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        dialog.grab().save(QString::fromLocal8Bit(out) + "/settings_firmware.png");
    }
}

TEST_F(AppTest, TheMainWindowShowsTheConnectedMachine) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    MainWindow window(machine);
    window.setDialogsEnabled(false);
    window.resize(1200, 800);
    window.show();
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected(); }));
    const QImage image = window.grab().toImage();
    EXPECT_EQ(image.width(), 1200);
    EXPECT_FALSE(image.isNull());
}

}  // namespace
