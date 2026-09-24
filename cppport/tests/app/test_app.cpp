// The Qt side: the event loop adapter, the machine service against the
// simulated board (real time, so kept short), and a main-window smoke test on
// the offscreen platform.

#include "calibration_dialogs.hpp"
#include "controls.hpp"
#include "dro_panel.hpp"
#include "gcode_editor_dialog.hpp"
#include "jogger.hpp"
#include "machine.hpp"
#include "main_window.hpp"
#include "notifications.hpp"
#include "panels.hpp"
#include "probe_panel.hpp"
#include "qt_event_loop.hpp"
#include "rotary_panel.hpp"
#include "sd_card_dialog.hpp"
#include "settings_dialog.hpp"
#include "shortcuts.hpp"
#include "shortcuts_dialog.hpp"
#include "start_from_line_dialog.hpp"
#include "step_through_dialog.hpp"
#include "stats_dialog.hpp"
#include "status_area.hpp"
#include "surfacing_dialog.hpp"
#include "toolchange_dialog.hpp"

#include "gs/config/history.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/actions.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include <boost/json.hpp>

#include <QApplication>
#include <QCheckBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QPushButton>
#include <QTableWidget>
#include <QToolButton>
#include <QDeadlineTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <cmath>
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

TEST_F(AppTest, MacrosGetShortcutsThatStartSwitchedOff) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    const auto macro = machine.macros().create("Lift", "G0 Z5", "");
    ASSERT_TRUE(macro.has_value());
    const QString id = QString::fromStdString(macro->id);
    QWidget window;
    ShortcutManager shortcuts(machine, window);
    QStringList ran;
    shortcuts.setMacroHandler([&](const QString& macroId) { ran << macroId; });

    // Listed under Macros, unbound and off - as upstream adds them.
    ShortcutsDialog dialog(machine);
    EXPECT_FALSE(dialog.isActive(id));
    EXPECT_TRUE(dialog.keys(id).isEmpty());
    // Binding keys switches it on; only that is stored.
    ASSERT_TRUE(dialog.setKeys(id, QKeySequence("Ctrl+M")));
    EXPECT_TRUE(dialog.isActive(id));
    dialog.save();
    EXPECT_EQ(machine.settings().shortcuts.size(), 1u);
    EXPECT_EQ(shortcuts.actionFor(QKeySequence("Ctrl+M")[0]), id);
    EXPECT_TRUE(shortcuts.trigger(id));
    EXPECT_EQ(ran, QStringList{id});

    // A macro added later appears too.
    const auto second = machine.macros().create("Park", "G0 X0 Y0", "");
    ASSERT_TRUE(second.has_value());
    Q_EMIT machine.macrosChanged();
    const std::vector<ShortcutAction> actions = shortcutActions(machine);
    const ShortcutAction* listed = findShortcutAction(actions, QString::fromStdString(second->id));
    ASSERT_NE(listed, nullptr);
    EXPECT_EQ(listed->title, "Park");
    EXPECT_EQ(listed->category, kMacroCategory);
    EXPECT_FALSE(shortcuts.isActive(QString::fromStdString(second->id)));
}

TEST_F(AppTest, AutomationsStoreEventHooksThatRunAroundJobs) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    SettingsDialog dialog(machine);
    dialog.setEventHook("gcode:start", "G0 Z7", true);
    dialog.setEventHook("gcode:pause", "", true);  // no commands: nothing stored
    dialog.save();
    const config::EventStore hooks(machine.config());
    ASSERT_TRUE(hooks.find("gcode:start").has_value());
    EXPECT_TRUE(hooks.find("gcode:start")->enabled);
    EXPECT_EQ(hooks.find("gcode:start")->trigger, "gcode");
    EXPECT_FALSE(hooks.find("gcode:pause").has_value());

    // The start hook runs before the job.
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(200);
    machine.loadProgram("job.nc", "G21 G90\nG0 X5\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    controller::runJob(*machine.controller());
    const std::vector<std::string>& received = machine.simulator()->receivedLines();
    ASSERT_TRUE(waitFor([&] { return std::find(received.begin(), received.end(), "G0 X5") != received.end(); }));
    const auto hook = std::find(received.begin(), received.end(), "G0 Z7");
    ASSERT_NE(hook, received.end());
    EXPECT_LT(hook, std::find(received.begin(), received.end(), "G0 X5"));

    // Switching it off keeps its code.
    dialog.setEventHook("gcode:start", "G0 Z7", false);
    dialog.save();
    EXPECT_FALSE(hooks.find("gcode:start")->enabled);
    EXPECT_EQ(hooks.find("gcode:start")->commands, "G0 Z7");
}

TEST_F(AppTest, RecentFilesAndMacroFilesComeAndGo) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    const auto write = [&](const QString& name, const QByteArray& content) {
        QFile file(dir.path() + "/" + name);
        EXPECT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(content);
        return QFileInfo(file).absoluteFilePath();
    };

    // Recent files: newest first, at most 8; loading one again moves it up.
    for (int i = 0; i < 9; ++i) {
        ASSERT_TRUE(machine.loadFile(write(QString("job%1.nc").arg(i), "G0 X1\n")));
    }
    const auto names = [&] {
        QStringList out;
        for (const RecentFile& file : machine.settings().recentFiles) {
            out << QString::fromStdString(file.fileName);
        }
        return out;
    };
    EXPECT_EQ(names(), (QStringList{"job8.nc", "job7.nc", "job6.nc", "job5.nc", "job4.nc", "job3.nc", "job2.nc",
                                    "job1.nc"}));
    ASSERT_TRUE(machine.loadFile(dir.path() + "/job3.nc"));
    EXPECT_EQ(names().first(), "job3.nc");
    EXPECT_EQ(names().size(), 8);
    machine.forgetRecentFile(QFileInfo(dir.path() + "/job3.nc").absoluteFilePath());
    EXPECT_EQ(names().first(), "job8.nc");

    // Macros go out to a file and come back into another configuration.
    ASSERT_TRUE(machine.macros().create("Park", "G0 Z5\nG0 X0 Y0", "Lift").has_value());
    MacrosPanel panel(machine);
    QString message;
    ASSERT_TRUE(panel.exportTo(dir.path() + "/macros.json", &message)) << message.toStdString();
    Machine other(loop, (dir.path() + "/other_rc").toStdWString());
    MacrosPanel otherPanel(other);
    ASSERT_TRUE(otherPanel.importFrom(dir.path() + "/macros.json", &message));
    EXPECT_EQ(message, "Successfully imported 1 macro(s)");
    ASSERT_EQ(other.macros().list().size(), 1u);
    EXPECT_EQ(other.macros().list()[0].content, "G0 Z5\nG0 X0 Y0");
    EXPECT_FALSE(otherPanel.importFrom(write("junk.json", "{not json"), &message));
}

TEST(GSenderSettings, KeysConvertFromMousetrapToQt) {
    EXPECT_EQ(keysFromMousetrap("shift+w"), "Shift+W");
    EXPECT_EQ(keysFromMousetrap("ctrl+alt+command+h"), "Ctrl+Alt+Meta+H");
    EXPECT_EQ(keysFromMousetrap("command+alt+ctrl+shift+left"), "Ctrl+Alt+Shift+Meta+Left");
    EXPECT_EQ(keysFromMousetrap("shift+pageup"), "Shift+PgUp");
    EXPECT_EQ(keysFromMousetrap("~"), "~");
    EXPECT_EQ(keysFromMousetrap("ctrl+4"), "Ctrl+4");
    EXPECT_EQ(keysFromMousetrap("f5"), "F5");
    EXPECT_EQ(keysFromMousetrap("shift++"), "Shift++");
    EXPECT_EQ(keysFromMousetrap("space"), "Space");
    EXPECT_EQ(keysFromMousetrap("del"), "Del");
    EXPECT_EQ(keysFromMousetrap(""), "");  // unbound
    EXPECT_FALSE(keysFromMousetrap("hyper+x").has_value());
    EXPECT_FALSE(keysFromMousetrap("shift+scrolllock").has_value());
}

TEST(GSenderSettings, AnExportIsReadOverTheDefaults) {
    const boost::json::value file = boost::json::parse(R"({
        "settings": {
            "workspace": {
                "units": "in", "safeRetractHeight": 5, "shouldWarnZero": true,
                "park": {"x": -10, "y": -20, "z": -1},
                "toolChangeOption": "Fixed Tool Sensor", "toolChangeHooks": {"preHook": "G0 Z10"},
                "toolChange": {"passthrough": true, "firstToolBehaviour": "Prompt for first tool"},
                "toolChangePosition": {"x": -5, "y": -6, "z": -7},
                "probeProfile": {"touchplateType": "AutoZero Touchplate", "xyThickness": 9,
                                 "zThickness": {"standardBlock": 14}},
                "outlineMode": "Square", "defaultFirmware": "grblHAL",
                "rotaryAxis": {"useAaxisForGrbl": true}
            },
            "widgets": {
                "probe": {"probeFeedrate": 60, "connectivityTest": false},
                "axes": {"jog": {"rapid": {"xyStep": 25, "zStep": 12, "aStep": 30, "feedrate": 6000},
                                 "threshold": 300}},
                "connection": {"port": "COM4", "baudrate": 250000},
                "spindle": {"mode": "laser", "delay": 2, "laser": {"maxPower": 1000}},
                "surfacing": {"width": 250},
                "visualizer": {"showLineWarnings": true}
            },
            "commandKeys": {
                "START_JOB": {"keys": "f9", "isActive": true},
                "STOP_JOB": {"keys": "@", "isActive": false},
                "JOG_X_P": {"keys": "hyper+x", "isActive": true}
            }
        },
        "events": {"gcode:start": {"event": "gcode:start", "trigger": "gcode", "commands": "G0 Z5", "enabled": true}}
    })");
    const std::optional<GSenderSettings> read = readGSenderSettings(file);
    ASSERT_TRUE(read.has_value());
    const AppSettings& s = read->settings;
    EXPECT_FALSE(s.metric);
    EXPECT_EQ(s.safeRetractHeight, 5);
    EXPECT_TRUE(s.warnZero);
    EXPECT_EQ(s.park.y, -20);
    EXPECT_EQ(s.toolChange.option, "Fixed Tool Sensor");
    EXPECT_EQ(s.toolChange.preHook, "G0 Z10");
    EXPECT_TRUE(s.toolChange.passthrough);
    EXPECT_EQ(s.firstToolBehaviour, "Prompt for first tool");
    EXPECT_EQ(s.toolChangePosition.z, -7);
    EXPECT_EQ(s.probe.plateType, probe::PlateType::AutoZero);  // the old name
    EXPECT_EQ(s.probe.xyThickness, 9);
    EXPECT_EQ(s.probe.zThickness.standardBlock, 14);
    EXPECT_EQ(s.probe.probeFeedrate, 60);
    EXPECT_FALSE(s.probe.connectivityTest);
    EXPECT_EQ(s.outlineMode, job::OutlineMode::Square);
    EXPECT_EQ(s.defaultFirmware, protocol::Firmware::GrblHal);
    EXPECT_TRUE(s.preferences.useAaxisForGrbl);
    EXPECT_EQ(s.jog.rapid.xyStep, 25);
    EXPECT_EQ(s.jog.threshold, 300);
    EXPECT_EQ(s.jog.normal.xyStep, 5);  // not in the file: the default
    EXPECT_EQ(s.port, "COM4");
    EXPECT_EQ(s.baudRate, 250000);
    EXPECT_TRUE(s.spindle.laserMode);
    EXPECT_EQ(s.spindle.laser.maxPower, 1000);
    EXPECT_EQ(s.preferences.spindleDelay, 2);
    EXPECT_EQ(s.surfacing.width, 250);
    EXPECT_TRUE(s.preferences.showLineWarnings);
    EXPECT_EQ(s.shortcuts.at("START_JOB").keys, "F9");
    EXPECT_FALSE(s.shortcuts.at("STOP_JOB").active);
    EXPECT_EQ(read->unreadableShortcuts, std::vector<std::string>{"JOG_X_P"});
    ASSERT_TRUE(read->events.has_value());
    EXPECT_TRUE(read->events->contains("gcode:start"));

    // gSender's store file carries the same under "state"; anything else is no settings file.
    EXPECT_TRUE(readGSenderSettings(boost::json::parse(R"({"version": "1.5", "state": {"workspace": {}}})")));
    EXPECT_FALSE(readGSenderSettings(boost::json::parse(R"({"macros": []})")));
}

TEST_F(AppTest, SettingsFilesExportImportAndRestore) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.safeRetractHeight = 7;
    settings.shortcuts["START_JOB"] = {"F9", true};
    machine.setSettings(settings);
    config::EventStore(machine.config()).create("gcode:stop", "gcode", "M5", true);

    // The port's own file: everything back as it was.
    QString message;
    ASSERT_TRUE(machine.exportSettings(dir.path() + "/settings.json", &message)) << message.toStdString();
    machine.restoreDefaultSettings();
    EXPECT_EQ(machine.settings().safeRetractHeight, 0);
    EXPECT_FALSE(config::EventStore(machine.config()).find("gcode:stop").has_value());
    ASSERT_TRUE(machine.importSettings(dir.path() + "/settings.json", &message)) << message.toStdString();
    EXPECT_EQ(machine.settings().safeRetractHeight, 7);
    EXPECT_EQ(machine.settings().shortcuts.at("START_JOB").keys, "F9");
    EXPECT_EQ(config::EventStore(machine.config()).find("gcode:stop")->commands, "M5");

    // A gSender export: only the shortcuts that differ from the defaults stay.
    QFile gsender(dir.path() + "/gsender.json");
    ASSERT_TRUE(gsender.open(QIODevice::WriteOnly));
    gsender.write(R"({"settings": {"workspace": {"units": "in"}, "widgets": {},
        "commandKeys": {"PAUSE_JOB": {"keys": "!", "isActive": true},
                        "STOP_JOB": {"keys": "shift+f12", "isActive": true},
                        "SOMETHING_ELSE": {"keys": "f2", "isActive": true}}},
        "events": {}})");
    gsender.close();
    ASSERT_TRUE(machine.importSettings(gsender.fileName(), &message)) << message.toStdString();
    EXPECT_FALSE(machine.settings().metric);
    ASSERT_EQ(machine.settings().shortcuts.size(), 1u);
    EXPECT_EQ(machine.settings().shortcuts.at("STOP_JOB").keys, "Shift+F12");

    // Anything else is refused and changes nothing.
    QFile junk(dir.path() + "/junk.json");
    ASSERT_TRUE(junk.open(QIODevice::WriteOnly));
    junk.write(R"({"macros": []})");
    junk.close();
    EXPECT_FALSE(machine.importSettings(junk.fileName(), &message));
    EXPECT_FALSE(machine.settings().metric);
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

TEST_F(AppTest, TheDroSelectsWorkspacesAndGoesToPlaces) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.park = {-100, -200, -10};
    machine.setSettings(settings);
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(200);
    controller::Controller& c = *machine.controller();
    // Positions as the controller last heard them (what the DRO shows).
    const auto near = [&](const std::array<double, 4>& p, double x, double y, double z) {
        return c.state().status.activeState == "Idle" && std::fabs(p[0] - x) < 1e-6 && std::fabs(p[1] - y) < 1e-6 &&
               std::fabs(p[2] - z) < 1e-6;
    };
    const auto machineAt = [&](double x, double y, double z) { return near(machine.machinePositionMm(), x, y, z); };
    const auto workAt = [&](double x, double y, double z) { return near(machine.workPositionMm(), x, y, z); };
    PositionPanel panel(machine);

    // G55, and a typed work position in it.
    machine.selectWorkspace("G55");
    ASSERT_TRUE(waitFor([&] { return c.runner().modal().wcs == "G55"; }));
    panel.enterWorkPosition(0, "12.5");
    ASSERT_TRUE(waitFor([&] { return workAt(12.5, 0, 0); }));
    panel.enterWorkPosition(1, "");  // ignored
    EXPECT_EQ(machine.workPositionMm()[1], 0);

    // The corners and park appear with homing enabled, and work once homed:
    // the board homes back right, so the far corner is max travel less the
    // pull-off away, below the top of Z by the pull-off.
    const auto corners = panel.findChildren<QToolButton*>();
    ASSERT_EQ(corners.size(), 4);
    EXPECT_TRUE(corners[0]->isVisibleTo(&panel));
    EXPECT_FALSE(corners[0]->isEnabled());
    c.home();
    ASSERT_TRUE(waitFor([&] { return c.hasHomed() && machineAt(0, 0, 0); }));
    ASSERT_TRUE(waitFor([&] { return corners[2]->isEnabled(); }));
    corners[2]->click();  // front left
    ASSERT_TRUE(waitFor([&] { return machineAt(-799, -799, -1); }));
    machine.goToPark();
    ASSERT_TRUE(waitFor([&] { return machineAt(-100, -200, -10); }));

    // Go To Location: absolute and incremental work coordinates, then
    // machine coordinates (which leave Z where it is).
    GoToDialog goTo(machine);
    goTo.setMode(controller::GoToMode::Absolute);
    EXPECT_EQ(goTo.value(1), -200);  // filled with the work position
    goTo.setTarget(10, 20, -5, 0);
    goTo.go();
    ASSERT_TRUE(waitFor([&] { return workAt(10, 20, -5); }));
    goTo.setMode(controller::GoToMode::Incremental);
    EXPECT_EQ(goTo.value(0), 0);
    goTo.setTarget(1, 2, 0, 0);
    goTo.go();
    ASSERT_TRUE(waitFor([&] { return workAt(11, 22, -5); }));
    goTo.setMode(controller::GoToMode::Machine);
    ASSERT_EQ(goTo.mode(), controller::GoToMode::Machine);
    goTo.setTarget(-50, -60, 0, 0);
    goTo.go();
    // Z stays (G55 has no Z offset).
    ASSERT_TRUE(waitFor([&] { return machineAt(-50, -60, -5); }));

    // Single-axis homing ($22 bit 1): the axis buttons turn into "HX"...
    machine.sendConsoleLine("$22=3");
    machine.sendConsoleLine("$$");
    ASSERT_TRUE(waitFor([&] { return machine.singleAxisHoming(); }));
    panel.setHomingMode(true);
    QPushButton* homeX = nullptr;
    for (QPushButton* button : panel.findChildren<QPushButton*>()) {
        if (button->text() == "HX") {
            homeX = button;
        }
    }
    ASSERT_NE(homeX, nullptr);
    ASSERT_TRUE(waitFor([&] { return homeX->isEnabled(); }));
    homeX->click();
    ASSERT_TRUE(waitFor([&] { return machineAt(0, -60, -5); }));
}

TEST_F(AppTest, TheStatusAreaUnlocksAlarmsAndShowsMachineInformation) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    QWidget view;
    view.resize(800, 600);
    StatusArea status(machine, &view);
    EXPECT_EQ(status.stateText(), "Disconnected");
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(200);
    controller::Controller& c = *machine.controller();
    EXPECT_TRUE(waitFor([&] { return status.stateText() == "Idle"; }));
    EXPECT_FALSE(status.alarmButtonShown());

    // An alarm shows its code and description; its button unlocks.
    machine.simulator()->triggerAlarm(3);
    ASSERT_TRUE(waitFor([&] { return status.stateText() == "Alarm (3)"; }));
    EXPECT_TRUE(status.alarmButtonShown());
    EXPECT_EQ(status.alarmButtonText(), "Click to Unlock Machine");
    EXPECT_TRUE(machine.alarmDescription("3").startsWith("Reset while in motion."));
    status.clickAlarmButton();
    ASSERT_TRUE(waitFor([&] { return status.stateText() == "Idle"; }));

    // A failed homing cycle asks first: cancelled nothing happens, "Rehome" homes.
    QStringList asked;
    StatusArea::HomingFailureChoice choice = StatusArea::HomingFailureChoice::Cancel;
    status.setHomingFailureChooser([&](const QString& code) {
        asked << code;
        return choice;
    });
    machine.simulator()->triggerAlarm(9);
    ASSERT_TRUE(waitFor([&] { return status.stateText() == "Alarm (9)"; }));
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        view.show();
        view.grab().save(QString::fromLocal8Bit(out) + "/status_alarm.png");
    }
    status.clickLockIcon();
    runFor(100);
    EXPECT_EQ(c.state().status.activeState, "Alarm");
    choice = StatusArea::HomingFailureChoice::Rehome;
    status.clickAlarmButton();
    EXPECT_EQ(asked, (QStringList{"9", "9"}));
    ASSERT_TRUE(waitFor([&] { return c.hasHomed() && status.stateText() == "Idle"; }));

    // Machine Information: modals, pins, and the stepper lock ($1=255, the
    // idle delay restored afterwards).
    MachineInfoDialog info(machine);
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        info.show();
        info.grab().save(QString::fromLocal8Bit(out) + "/machine_info.png");
    }
    EXPECT_EQ(info.row("Coordinate system"), "G54");
    EXPECT_EQ(info.row("Units"), "G21");
    EXPECT_EQ(info.row("X limit"), "Off");
    EXPECT_FALSE(machine.stepperLocked());
    machine.setStepperLock(true);
    ASSERT_TRUE(waitFor([&] { return machine.stepperLocked(); }));
    EXPECT_EQ(machine.settings().stepperRestoreValue, "25");
    machine.setStepperLock(false);
    ASSERT_TRUE(waitFor([&] { return c.runner().setting("$1") == "25"; }));
    EXPECT_TRUE(machine.settings().stepperRestoreValue.empty());
}

TEST_F(AppTest, TheCalibrationWizardsMoveMeasureAndRewriteStepsPerMm) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(200);
    controller::Controller& c = *machine.controller();
    const auto idleAt = [&](double x, double y) {
        const std::array<double, 4> p = machine.machinePositionMm();
        return c.state().status.activeState == "Idle" && std::fabs(p[0] - x) < 1e-6 && std::fabs(p[1] - y) < 1e-6;
    };
    const QByteArray shots = qgetenv("GS_TEST_SCREENSHOTS");

    // Movement Tuning: X told to move 100 mm, measured 102: $100 = 200 x 100/102.
    MovementTuningDialog tuning(machine);
    EXPECT_FALSE(tuning.moveAxis());  // not started
    ASSERT_TRUE(tuning.start());
    tuning.markLocation();
    ASSERT_TRUE(tuning.moveAxis());
    ASSERT_TRUE(waitFor([&] { return idleAt(100, 0); }));
    tuning.setTravelled(102);
    tuning.confirmTravelled();
    EXPECT_EQ(tuning.step(), MovementTuningDialog::Result);
    EXPECT_TRUE(tuning.resultText().contains("off by <b>-2 mm.</b>")) << tuning.resultText().toStdString();
    EXPECT_EQ(tuning.recommendedStepsPerMm(), 196.08);
    if (!shots.isEmpty()) {
        tuning.show();
        tuning.grab().save(QString::fromLocal8Bit(shots) + "/movement_tuning.png");
    }
    tuning.updateFirmware();
    ASSERT_TRUE(waitFor([&] { return c.runner().setting("$100") == "196.08"; }));

    // XY Squaring: mark, move X, mark, move Y, mark; measure the triangle.
    SquaringDialog squaring(machine);
    ASSERT_TRUE(squaring.canGoNext());
    squaring.next();
    EXPECT_TRUE(squaring.completeRow(0));
    EXPECT_FALSE(squaring.completeRow(2));  // the X move comes first
    squaring.setRowValue(1, 50);
    EXPECT_TRUE(squaring.completeRow(1));
    ASSERT_TRUE(waitFor([&] { return idleAt(150, 0); }));
    EXPECT_TRUE(squaring.completeRow(2));
    squaring.setRowValue(3, 50);
    if (!shots.isEmpty()) {
        squaring.show();
        squaring.grab().save(QString::fromLocal8Bit(shots) + "/squaring_marking.png");
    }
    EXPECT_TRUE(squaring.completeRow(3));
    ASSERT_TRUE(waitFor([&] { return idleAt(150, 50); }));
    EXPECT_TRUE(squaring.completeRow(4));
    squaring.next();
    EXPECT_FALSE(squaring.completeRow(0));  // nothing measured yet
    squaring.setRowValue(0, 49);
    EXPECT_TRUE(squaring.completeRow(0));
    squaring.setRowValue(1, 50);
    EXPECT_TRUE(squaring.completeRow(1));
    squaring.setRowValue(2, 71);
    EXPECT_TRUE(squaring.completeRow(2));
    squaring.next();
    ASSERT_EQ(squaring.mainStep(), 3);
    // 49 x 50 with a 71 diagonal: 1.6 degrees out, 0.99 mm on the diagonal.
    EXPECT_EQ(squaring.result().verdict, calibration::Squareness::SlightlyOut);
    EXPECT_EQ(squaring.result().diagonalError, "0.99");
    const calibration::StepsAdjustment adjustment = squaring.adjustment();
    EXPECT_TRUE(adjustment.x.needed);  // X moved 50 but measured 49
    EXPECT_FALSE(adjustment.y.needed);
    if (!shots.isEmpty()) {
        squaring.grab().save(QString::fromLocal8Bit(shots) + "/squaring_results.png");
    }
    squaring.updateFirmware();
    ASSERT_TRUE(waitFor([&] {
        return c.runner().setting("$100") == "200.082" && c.runner().setting("$101") == "200.000";
    }));
}

TEST_F(AppTest, TheSpindleTabSwitchesToLaserModeAndBack) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.spindle.laser.xOffset = 10;
    settings.spindle.laser.yOffset = 5;
    machine.setSettings(settings);
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    controller::Controller& c = *machine.controller();
    const std::vector<std::string>& received = machine.simulator()->receivedLines();
    const auto sent = [&](const std::string& line) {
        return std::find(received.begin(), received.end(), line) != received.end();
    };
    const auto workAt = [&](double x, double y) {
        const std::array<double, 4> w = machine.workPositionMm();
        return std::fabs(w[0] - x) < 1e-6 && std::fabs(w[1] - y) < 1e-6;
    };
    SpindlePanel panel(machine);
    ASSERT_FALSE(machine.laserMode());

    // To the laser: the work position shifts by the laser's offset, the
    // laser's range replaces the spindle's (kept for later), $32=1.
    panel.toggleMode();
    ASSERT_TRUE(waitFor([&] { return sent("$32=1"); }));
    for (const char* line : {"G10 L20 P1 X10 Y5", "$30=255", "$31=0"}) {
        EXPECT_TRUE(sent(line)) << line;
    }
    EXPECT_TRUE(machine.laserMode());
    EXPECT_EQ(machine.settings().spindle.spindleMax, 1000);  // the board's $30
    ASSERT_TRUE(waitFor([&] { return workAt(10, 5) && c.state().status.activeState == "Idle"; }));

    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        panel.resize(460, 380);
        panel.show();
        panel.grab().save(QString::fromLocal8Bit(out) + "/spindle_laser.png");
    }

    // Focus at the set power, then a live power change.
    panel.startClockwise();
    EXPECT_TRUE(panel.isLaserOn());
    ASSERT_TRUE(waitFor([&] { return sent("G1F1 M3 S255"); }));
    panel.setLaserPower(50);
    ASSERT_TRUE(waitFor([&] { return sent("S127.5"); }));
    EXPECT_EQ(machine.settings().spindle.laser.power, 50);
    panel.stopSpindle();
    ASSERT_TRUE(waitFor([&] { return sent("M5 S0"); }));

    // Back to the spindle: the shift undone, the spindle's range back.
    ASSERT_TRUE(waitFor([&] { return c.state().status.activeState == "Idle"; }));
    panel.toggleMode();
    ASSERT_TRUE(waitFor([&] { return sent("$32=0"); }));
    EXPECT_TRUE(sent("G10 L20 P1 X0 Y0"));
    EXPECT_TRUE(sent("$30=1000"));
    EXPECT_FALSE(machine.laserMode());
    EXPECT_EQ(machine.settings().spindle.laser.maxPower, 255);
    ASSERT_TRUE(waitFor([&] { return workAt(0, 0); }));
}

TEST_F(AppTest, JobsMaintenanceHoursAndAlarmsAreRecordedForTheStatistics) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(200);
    int recorded = 0;
    QObject::connect(&machine, &Machine::historyChanged, [&] { ++recorded; });

    machine.loadProgram("square.nc", "G21 G90\nG1 X5 F1200\nG1 Y5\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    controller::runJob(*machine.controller());
    ASSERT_TRUE(waitFor([&] { return recorded == 1; }));  // the job's end

    StatsDialog stats(machine);
    EXPECT_TRUE(stats.totalsText().startsWith("Jobs: 1 - completed 1, stopped 0")) << stats.totalsText().toStdString();
    ASSERT_EQ(stats.jobsTable()->rowCount(), 1);
    EXPECT_EQ(stats.jobsTable()->item(0, 0)->text(), "square.nc");
    EXPECT_EQ(stats.jobsTable()->item(0, 3)->text(), "4");  // the sender's lines: the last one is empty
    EXPECT_EQ(stats.jobsTable()->item(0, 5)->text(), "Complete");
    const std::vector<config::MaintenanceTask> tasks = config::MaintenanceStore(machine.config()).list();
    ASSERT_FALSE(tasks.empty());
    EXPECT_GT(tasks[0].currentTime, 0);  // the job's running time
    EXPECT_EQ(stats.tasksTable()->rowCount(), static_cast<int>(tasks.size()));

    // Alarms land in the log, which the open dialog follows.
    machine.simulator()->triggerAlarm(9);
    ASSERT_TRUE(waitFor([&] { return stats.alarmsTable()->rowCount() == 1; }));
    EXPECT_EQ(stats.alarmsTable()->item(0, 1)->text(), "Alarm");
    EXPECT_EQ(stats.alarmsTable()->item(0, 2)->text(), "9");
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        stats.show();
        stats.grab().save(QString::fromLocal8Bit(out) + "/stats.png");
    }
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
    EXPECT_NE(table->item(0, 4)->text().indexOf("Step pulse"), -1);  // described from the Grbl tables
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        dialog.grab().save(QString::fromLocal8Bit(out) + "/settings_firmware.png");
    }

    // Typed editors: a switch writes 0/1, an axis mask's bits add up.
    int softLimits = -1;
    int directions = -1;
    for (int row = 0; row < table->rowCount(); ++row) {
        softLimits = table->item(row, 0)->text() == "$20" ? row : softLimits;
        directions = table->item(row, 0)->text() == "$3" ? row : directions;
    }
    ASSERT_GE(softLimits, 0);
    ASSERT_GE(directions, 0);
    auto* toggle = qobject_cast<QCheckBox*>(table->cellWidget(softLimits, 1));
    ASSERT_NE(toggle, nullptr);
    toggle->setChecked(!toggle->isChecked());
    EXPECT_EQ(table->item(softLimits, 1)->text(), toggle->isChecked() ? "1" : "0");
    auto* mask = qobject_cast<QToolButton*>(table->cellWidget(directions, 1));
    ASSERT_NE(mask, nullptr);
    ASSERT_GE(mask->menu()->actions().size(), 3);  // X Y Z
    mask->menu()->actions()[1]->setChecked(!mask->menu()->actions()[1]->isChecked());
    EXPECT_EQ((table->item(directions, 1)->text().toInt() & 2) != 0, mask->menu()->actions()[1]->isChecked());

    // Against the machine profile's defaults (the default LongMill MK2
    // 30x30 - the simulator is no LongMill): the changed ones filter out.
    auto* firmware = dialog.findChild<FirmwareSettingsTable*>();
    ASSERT_NE(firmware, nullptr);
    const int changed = firmware->modifiedCount();
    ASSERT_GT(changed, 0);
    firmware->setOnlyModified(true);
    EXPECT_EQ(firmware->visibleRows(), changed);
    firmware->setOnlyModified(false);
    firmware->setFilter("step pulse time");
    EXPECT_EQ(firmware->visibleRows(), 1);
    firmware->setFilter("");
    EXPECT_EQ(firmware->visibleRows(), expected);

    // One changed setting back to its default.
    QString setting;
    for (int row = 0; row < table->rowCount() && setting.isEmpty(); ++row) {
        if (table->cellWidget(row, 5)) {
            setting = table->item(row, 0)->text();
        }
    }
    ASSERT_FALSE(setting.isEmpty());
    const std::string fallback = *config::defaultValue(machine.machineProfile(), machine.boardContext(),
                                                       setting.toStdString());
    ASSERT_TRUE(firmware->restoreSetting(setting));
    ASSERT_TRUE(waitFor([&] { return machine.controller()->settings().settings.get(setting.toStdString()) == fallback; }));
    ASSERT_TRUE(waitFor([&] { return firmware->modifiedCount() == changed - 1; }));

    // EEPROM files: exported as the board has them, imported back.
    const QString file = dir.path() + "/eeprom.json";
    ASSERT_TRUE(firmware->exportFile(file));
    QFile exported(file);
    ASSERT_TRUE(exported.open(QIODevice::ReadOnly));
    EXPECT_TRUE(exported.readAll().startsWith("{\"$0\":"));
    exported.close();
    QFile edited(dir.path() + "/import.json");
    ASSERT_TRUE(edited.open(QIODevice::WriteOnly));
    edited.write(R"({"$1":"42"})");
    edited.close();
    ASSERT_TRUE(firmware->importFile(edited.fileName()));
    ASSERT_TRUE(waitFor([&] { return machine.controller()->settings().settings.get("$1") == "42"; }));
    QFile broken(dir.path() + "/broken.json");
    ASSERT_TRUE(broken.open(QIODevice::WriteOnly));
    broken.write(R"({"one":1})");
    broken.close();
    EXPECT_FALSE(firmware->importFile(broken.fileName()));

    // The live pins: the limit switches (Firmware) and the probe (Probe).
    const QList<PinIndicators*> pins = dialog.findChildren<PinIndicators*>();
    ASSERT_EQ(pins.size(), 2);
    EXPECT_FALSE(pins[0]->lit('X'));
}

TEST_F(AppTest, RotaryModePutsTheRotaryOnGrblsYAndBack) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    MainWindow window(machine);
    window.setDialogsEnabled(false);
    EXPECT_FALSE(window.rotaryTabVisible());  // until the Rotary controls are on
    AppSettings settings = machine.settings();
    settings.rotary.showControls = true;
    machine.setSettings(settings);
    EXPECT_TRUE(window.rotaryTabVisible());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    controller::Controller& c = *machine.controller();
    const std::vector<std::string>& received = machine.simulator()->receivedLines();
    const auto sent = [&](const std::string& line) {
        return std::find(received.begin(), received.end(), line) != received.end();
    };
    const std::string resolution = c.runner().setting("$101");
    const std::string maxRate = c.runner().setting("$111");
    RotaryPanel& panel = window.rotaryPanel();
    QStringList asked;
    bool answer = false;
    panel.setConfirmer([&](const QString& title, const QString&, const QString&) {
        asked << title;
        return answer;
    });
    const auto enabled = [&](const char* name) { return panel.findChild<QPushButton*>(name)->isEnabled(); };

    // Grbl has the rotary only in rotary mode; the mounting setup and the Y
    // alignment are for the machine outside it.
    ASSERT_TRUE(waitFor([&] { return enabled("mountingSetup"); }));
    EXPECT_TRUE(enabled("alignYAxis"));
    EXPECT_FALSE(enabled("rotarySurfacing"));
    EXPECT_FALSE(enabled("probeRotaryZ"));
    MountingSetupDialog mounting(machine);
    rotary::MountingSetup setup;
    setup.linesUp = true;
    setup.holes = 10;
    mounting.setSetup(setup);
    const QByteArray screenshots = qgetenv("GS_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        mounting.resize(820, 520);
        mounting.show();
        mounting.grab().save(QString::fromLocal8Bit(screenshots) + "/rotary_mounting.png");
    }
    ASSERT_TRUE(mounting.loadIntoMachine());
    EXPECT_EQ(machine.programName(), "gSender_Rotary_Mounting_Setup");
    EXPECT_EQ(machine.programText(), *rotary::mountingProgram(setup));
    EXPECT_FALSE(panel.alignYAxis());  // asked, declined: nothing runs
    EXPECT_EQ(asked, QStringList{"Y-Axis Alignment probing"});

    // Declined, rotary mode stays off.
    asked.clear();
    EXPECT_FALSE(panel.setRotaryMode(true));
    EXPECT_EQ(asked, QStringList{"Enable Rotary Mode"});
    EXPECT_FALSE(machine.rotaryMode());
    EXPECT_FALSE(sent("G10 L20 P1 Y0"));

    // Entering (the shortcut runs the switch): Y zeroed, the rotary's values
    // written to Y, the board's own kept to restore.
    answer = true;
    ASSERT_TRUE(window.shortcuts().trigger("SWITCH_WORKSPACE_MODE"));
    ASSERT_TRUE(waitFor([&] { return c.runner().setting("$101") == "19.75308642"; }));
    for (const char* line : {"G10 L20 P1 Y0", "$111=8000.00", "$20=0", "$21=0", "G04 P0.5"}) {
        EXPECT_TRUE(sent(line)) << line;
    }
    EXPECT_TRUE(machine.rotaryMode());
    const auto stored = [&](const rotary::FirmwareValues& values, const char* key) {
        for (const auto& [name, value] : values) {
            if (name == key) {
                return value;
            }
        }
        return std::string();
    };
    EXPECT_EQ(stored(machine.settings().rotary.defaults, "$101"), resolution);
    EXPECT_EQ(stored(machine.settings().rotary.defaults, "$111"), maxRate);
    ASSERT_TRUE(waitFor([&] { return c.state().status.activeState == "Idle" && enabled("probeRotaryZ"); }));
    EXPECT_TRUE(enabled("rotarySurfacing"));
    EXPECT_FALSE(enabled("alignYAxis"));
    EXPECT_FALSE(enabled("mountingSetup"));

    // The A buttons jog the rotary on Y, in degrees whatever the units.
    Jogger jogger(machine);
    jogger.pressRotary(1);
    jogger.release();
    ASSERT_TRUE(waitFor([&] { return sent("$J=G21 G91 Y5 F3000"); }));
    ASSERT_TRUE(waitFor([&] {
        return c.state().status.activeState == "Idle" && std::fabs(machine.machinePositionMm()[1] - 5) < 1e-6;
    })) << c.state().status.activeState << " Y " << machine.machinePositionMm()[1] << " sim Y "
        << machine.simulator()->machinePosition()[1];

    // The surfacing program turns the stock with A (translated to Y for
    // Grbl) and becomes the job; its options are kept in mm.
    RotarySurfacingDialog surfacing(machine);
    rotary::StockTurningOptions options = surfacing.options();
    options.stockLength = 120;
    surfacing.setOptions(options);
    surfacing.generate();
    EXPECT_TRUE(surfacing.program().contains("(*** Layer 1 ***)"));
    if (!screenshots.isEmpty()) {
        surfacing.show();
        surfacing.grab().save(QString::fromLocal8Bit(screenshots) + "/rotary_surfacing.png");
        panel.resize(420, 200);
        panel.show();
        panel.grab().save(QString::fromLocal8Bit(screenshots) + "/rotary_panel.png");
    }
    ASSERT_TRUE(surfacing.loadIntoMachine());
    EXPECT_EQ(machine.programName(), "gSender_Rotary_Surfacing");
    surfacing.reject();
    EXPECT_EQ(machine.settings().rotary.stockTurning.stockLength, 120);

    // Leaving asks nothing and puts Y's own values back.
    asked.clear();
    ASSERT_TRUE(panel.setRotaryMode(false));
    EXPECT_TRUE(asked.isEmpty());
    ASSERT_TRUE(waitFor([&] { return c.runner().setting("$101") == resolution; }));
    EXPECT_TRUE(sent("$111=" + maxRate));
    EXPECT_FALSE(machine.rotaryMode());

    // Turning the Rotary controls off hides the tab.
    settings = machine.settings();
    settings.rotary.showControls = false;
    machine.setSettings(settings);
    EXPECT_FALSE(window.rotaryTabVisible());
}

TEST_F(AppTest, TheStepThroughFollowsTheFileLineByLine) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.loadProgram("step.nc",
                        "G21 G90\n"              // 1
                        "M6 T1 (6mm endmill)\n"  // 2
                        "M3 S12000\n"            // 3
                        "G0 X10 Y0\n"            // 4
                        "G1 Z-1 F300\n"          // 5
                        "\n"                     // 6
                        "G1 X20\n"               // 7
                        "M6 T2\n"                // 8
                        "G1 Y10 F600\n");        // 9
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    StepThroughDialog dialog(machine);
    ASSERT_TRUE(waitFor([&] { return dialog.indexReady(); }));
    EXPECT_EQ(dialog.totalLines(), 9u);
    EXPECT_EQ(dialog.currentLine(), 1u);
    ASSERT_EQ(dialog.tools().size(), 2u);
    EXPECT_EQ(dialog.tools()[0].endLine, 7u);
    EXPECT_EQ(dialog.tools()[0].diameter, 6);
    EXPECT_EQ(dialog.activeTool(), -1);  // before the first tool change

    // A line: where it leaves the cutter, the modes it runs in, its tool.
    dialog.goToLine(5);
    EXPECT_EQ(dialog.positionText(), "X 10.00   Y 0.00   Z -1.00");
    const job::StepModalReadout modals = dialog.modalReadout();
    EXPECT_EQ(modals[0], "G1");
    EXPECT_EQ(modals[8], "T1");
    EXPECT_EQ(modals[9], "F300");
    EXPECT_EQ(modals[10], "S12000");
    EXPECT_EQ(dialog.activeTool(), 0);
    dialog.goToLine(9);
    EXPECT_EQ(dialog.activeTool(), 1);

    // Steps stay in the file.
    dialog.step(-100);
    EXPECT_EQ(dialog.currentLine(), 1u);
    dialog.step(1000);
    EXPECT_EQ(dialog.currentLine(), 9u);

    // Search: Enter goes to the next match after the line, wrapping.
    dialog.setSearch(" g1 ");
    EXPECT_EQ(dialog.matchCount(), 3u);
    dialog.goToLine(5);
    dialog.goToNextMatch();
    EXPECT_EQ(dialog.currentLine(), 7u);
    dialog.goToNextMatch();
    dialog.goToNextMatch();
    EXPECT_EQ(dialog.currentLine(), 5u);

    // Playback from the end starts over and stops at the end.
    dialog.goToLine(9);
    dialog.setPlaybackSpeed(100);
    dialog.togglePlay();
    EXPECT_TRUE(dialog.isPlaying());
    EXPECT_LT(dialog.currentLine(), 9u);
    ASSERT_TRUE(waitFor([&] { return !dialog.isPlaying(); }));
    EXPECT_EQ(dialog.currentLine(), 9u);

    dialog.toggleTool(1);
    EXPECT_TRUE(dialog.toolHidden(1));
    dialog.setHideProcessed(true);
    dialog.goToLine(7);
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        dialog.setHideProcessed(false);
        dialog.toggleTool(1);
        dialog.resize(1280, 820);
        dialog.show();
        dialog.goToLine(7);
        runFor(50);
        dialog.grab().save(QString::fromLocal8Bit(out) + "/step_through.png");
    }

    // Unloading the file closes it.
    dialog.show();
    machine.unloadProgram();
    EXPECT_FALSE(dialog.isVisible());
}

TEST_F(AppTest, TheGcodeEditorChangesTheJobAndFollowsItRunning) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.loadProgram("edit.nc", "G21 G90\r\nG0 X5\r\n\r\nG1 X10 F500\r\nG1 Y5\r\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    GcodeEditorDialog editor(machine);
    const QString original = "G21 G90\nG0 X5\n\nG1 X10 F500\nG1 Y5";
    EXPECT_EQ(editor.text(), original);  // one line per line, whatever the endings
    EXPECT_FALSE(editor.hasChanges());

    // Search: any case, wrapping both ways.
    editor.setSearch("g1");
    EXPECT_EQ(editor.matchCount(), 2);
    EXPECT_EQ(editor.currentMatch(), 0);
    editor.nextMatch();
    editor.nextMatch();
    EXPECT_EQ(editor.currentMatch(), 0);
    editor.previousMatch();
    EXPECT_EQ(editor.currentMatch(), 1);
    editor.setSearch("");

    // Selected lines: copied, deleted, reverted.
    editor.selectLines(2, 3);
    EXPECT_EQ(editor.selectedLineCount(), 2);
    EXPECT_EQ(editor.copyText(), "G0 X5\n");
    editor.deleteSelectedLines();
    EXPECT_EQ(editor.text(), "G21 G90\nG1 X10 F500\nG1 Y5");
    EXPECT_TRUE(editor.hasChanges());
    ASSERT_TRUE(editor.revert());
    EXPECT_EQ(editor.text(), original);
    EXPECT_FALSE(editor.hasChanges());

    // The last line goes with the break before it; saved, it is the job.
    editor.selectLines(5, 5);
    editor.deleteSelectedLines();
    const QString edited = "G21 G90\nG0 X5\n\nG1 X10 F500";
    EXPECT_EQ(editor.text(), edited);
    ASSERT_TRUE(editor.save());
    EXPECT_EQ(machine.programText(), edited.toStdString());
    EXPECT_EQ(machine.programName(), "edit.nc");
    EXPECT_FALSE(editor.hasChanges());
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    EXPECT_EQ(editor.text(), edited);

    // While the job runs the text is read-only and follows the running line
    // (the blank line 3 is not sent: the sender's lines map back to the
    // file's).
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->state().status.activeState == "Idle";
    }));
    controller::runJob(*machine.controller());
    ASSERT_TRUE(waitFor([&] { return editor.jobRunning() && editor.runningLine() == 4; }));
    EXPECT_TRUE(editor.editor().isReadOnly());
    EXPECT_FALSE(editor.save());
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        editor.resize(760, 420);
        editor.show();
        editor.grab().save(QString::fromLocal8Bit(out) + "/gcode_editor.png");
    }
    ASSERT_TRUE(waitFor([&] { return !editor.jobRunning(); }));
    EXPECT_EQ(editor.runningLine(), 0u);
    EXPECT_FALSE(editor.editor().isReadOnly());

    // Unloading the file closes the editor.
    editor.show();
    machine.unloadProgram();
    EXPECT_FALSE(editor.isVisible());
}

TEST_F(AppTest, NotificationsKeepTheLastHundredAndPopUpForTheirTime) {
    NotificationCenter center;
    for (int i = 0; i < 101; ++i) {
        center.add(QString("note %1").arg(i), i % 2 ? NotificationType::Error : NotificationType::Info);
    }
    ASSERT_EQ(center.list().size(), 100u);
    EXPECT_EQ(center.list().front().message, "note 1");  // the oldest went
    EXPECT_EQ(center.unreadErrors(), 50);

    // The bell's list filters by type; opening it reads everything.
    NotificationButton bell(center);
    bell.panel().setTab(1);
    EXPECT_EQ(bell.panel().shownCount(), 50);
    bell.panel().setTab(0);
    EXPECT_EQ(bell.panel().shownCount(), 100);
    bell.togglePanel();
    EXPECT_EQ(center.unreadErrors(), 0);
    bell.togglePanel();
    center.clear();
    EXPECT_TRUE(center.list().empty());

    // Pop-ups: none when switched off, at most three, gone after their time.
    QWidget host;
    host.resize(800, 600);
    ToastArea toasts(&host);
    toasts.showToast("off", NotificationType::Info, kToastDisabled);
    EXPECT_EQ(toasts.count(), 0);
    for (const char* text : {"one", "two", "three", "four"}) {
        toasts.showToast(text, NotificationType::Info, kToastUntilClose);
    }
    EXPECT_EQ(toasts.texts(), (QStringList{"two", "three", "four"}));
    toasts.showToast("brief", NotificationType::Success, 50);
    EXPECT_EQ(toasts.texts(), (QStringList{"three", "four", "brief"}));
    ASSERT_TRUE(waitFor([&] { return toasts.count() == 2; }));
    EXPECT_EQ(toasts.texts(), (QStringList{"three", "four"}));
}

TEST_F(AppTest, AJobsEndIsSummedUpWithItsErrorsAndTheMaintenanceDue) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    MainWindow window(machine);
    window.setDialogsEnabled(false);
    config::MaintenanceTask task;
    task.name = "Clean the rails";
    task.rangeStart = 0;  // due after any job
    task.rangeEnd = 10;
    task.currentTime = 5;
    config::MaintenanceStore(machine.config()).add(task);
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->state().status.activeState == "Idle";
    }));
    controller::Controller& c = *machine.controller();
    // G99 is error 20; the moves after it are more than the planner holds,
    // so the job is still sending when it is stopped.
    std::string program = "G21\nG99\n";
    for (int i = 1; i <= 40; ++i) {
        program += "G1 X" + std::to_string(i) + " F300\n";
    }
    machine.loadProgram("errors.nc", program);
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    bool ended = false;
    bool completed = true;
    QStringList errors;
    QObject::connect(&machine, &Machine::jobEnded, [&](bool done, double, const QStringList& seen) {
        ended = true;
        completed = done;
        errors = seen;
    });

    // Grbl pauses on the error; stopped, the job ends with it.
    controller::runJob(c);
    ASSERT_TRUE(waitFor([&] { return c.workflow().isPaused(); }));
    // The error pops up and waits, unread, in the bell's list.
    EXPECT_EQ(window.notifications().unreadErrors(), 1);
    const QStringList popped = window.toasts().texts();
    EXPECT_TRUE(std::any_of(popped.begin(), popped.end(), [](const QString& t) { return t.startsWith("Error 20: "); }))
        << popped.join(" | ").toStdString();
    controller::stopJob(c);
    ASSERT_TRUE(waitFor([&] { return ended; }));
    EXPECT_FALSE(completed);
    ASSERT_EQ(errors.size(), 1);
    EXPECT_TRUE(errors[0].startsWith("Error 20 on line")) << errors[0].toStdString();
    const JobEndDialog summary(completed, 2500, errors);
    EXPECT_TRUE(summary.summary().contains("Status: STOPPED"));
    EXPECT_TRUE(summary.summary().contains("Time: 00:00:02"));
    EXPECT_TRUE(summary.summary().contains("- Error 20"));

    // The task is due: the alert lists it and resets its hours.
    std::vector<config::MaintenanceTask> due = machine.dueMaintenanceTasks();
    const auto rails = std::find_if(due.begin(), due.end(), [](const auto& t) { return t.name == "Clean the rails"; });
    ASSERT_NE(rails, due.end());
    const int id = rails->id;
    MaintenanceAlertDialog alert(machine, due);
    EXPECT_TRUE(alert.taskNames().contains("Clean the rails"));
    alert.resetTimers();
    for (const config::MaintenanceTask& after : config::MaintenanceStore(machine.config()).list()) {
        if (after.id == id) {
            EXPECT_EQ(after.currentTime, 0);
        }
    }
}

TEST_F(AppTest, AJobsWorkspaceComesBackAndBadFilesAreReported) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.warnBadFile = true;
    machine.setSettings(settings);
    int invalid = 0;
    QStringList sample;
    QObject::connect(&machine, &Machine::invalidLinesFound, [&](int count, const QStringList& lines) {
        invalid = count;
        sample = lines;
    });
    machine.loadProgram("bad.nc", "G1 X5 E0.2 F100\nG1 X6\n");
    ASSERT_TRUE(waitFor([&] { return invalid > 0; }));
    EXPECT_EQ(invalid, 1);
    EXPECT_EQ(sample, QStringList{"G1 X5 E0.2 F100"});

    // A job that moves to G55 leaves the machine in the workspace it started
    // in (G54) - unless M2/M30 may reset it (workspace.revertWorkspace).
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    controller::Controller& c = *machine.controller();
    machine.loadProgram("wcs.nc", "G55\nG0 X1\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    controller::runJob(c);
    ASSERT_TRUE(waitFor([&] { return c.workflow().isRunning(); }));
    ASSERT_TRUE(waitFor([&] { return c.workflow().isIdle() && c.runner().modal().wcs == "G54"; }, 8000))
        << c.runner().modal().wcs;
    settings = machine.settings();
    settings.revertWorkspace = true;
    machine.setSettings(settings);
    controller::runJob(c);
    ASSERT_TRUE(waitFor([&] { return c.workflow().isRunning(); }));
    ASSERT_TRUE(waitFor([&] { return c.workflow().isIdle(); }, 8000));
    runFor(600);
    EXPECT_EQ(c.runner().modal().wcs, "G55");
}

TEST_F(AppTest, SettingsAreBackedUpWhenDue) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.backupLocation = dir.path().toStdString();
    machine.setSettings(settings);
    const std::int64_t now = 1'700'000'000'000;
    // On Update: once per version.
    const QString first = machine.backupSettingsIfDue("1.0.0", now);
    ASSERT_FALSE(first.isEmpty());
    EXPECT_TRUE(QFileInfo(first).fileName().startsWith("preferences-backup-2023-11-14T22-13-20.000Z"));
    QFile copy(first);
    ASSERT_TRUE(copy.open(QIODevice::ReadOnly));
    EXPECT_TRUE(copy.readAll().contains("backupLocation"));
    EXPECT_TRUE(machine.backupSettingsIfDue("1.0.0", now + 1000).isEmpty());
    EXPECT_FALSE(machine.backupSettingsIfDue("1.1.0", now + 2000).isEmpty());
    // Daily: after a day.
    settings = machine.settings();
    settings.backupFrequency = "Daily";
    machine.setSettings(settings);
    EXPECT_TRUE(machine.backupSettingsIfDue("1.1.0", now + 3000).isEmpty());
    EXPECT_FALSE(machine.backupSettingsIfDue("1.1.0", now + 2000 + 24LL * 3600 * 1000).isEmpty());
}

TEST(SdCard, FilesAreCheckedAndSizedAsUpstreamDoes) {
    EXPECT_EQ(formatSdFileSize(0), "0 B");
    EXPECT_EQ(formatSdFileSize(1023), "1023 B");
    EXPECT_EQ(formatSdFileSize(1024), "1.0 KB");
    EXPECT_EQ(formatSdFileSize(1536), "1.5 KB");
    EXPECT_EQ(formatSdFileSize(5LL * 1024 * 1024), "5.0 MB");
    EXPECT_EQ(formatSdFileSize(3LL * 1024 * 1024 * 1024), "3.0 GB");
    EXPECT_TRUE(isAcceptedSdFile("JOB.GCODE"));
    EXPECT_TRUE(isAcceptedSdFile("tool.macro"));
    EXPECT_FALSE(isAcceptedSdFile("notes.md"));
    EXPECT_FALSE(isAcceptedSdFile("readme"));
    EXPECT_TRUE(isAcceptedSdFile("nc"));  // no dot: the whole name is the "extension"
    EXPECT_EQ(sdFilenameProblem(QString(41, 'a') + ".nc").value_or(""), "Filename too long (max 40 characters)");
    EXPECT_EQ(sdFilenameProblem("why?.nc").value_or(""), "Filename contains invalid character: ?");
    EXPECT_FALSE(sdFilenameProblem("fine.nc"));
    const SdFileCheck check = checkSdFiles({"C:/jobs/a.nc", "C:/jobs/notes.md", "C:/jobs/b~1.nc"});
    EXPECT_EQ(check.accepted, QStringList{"C:/jobs/a.nc"});
    EXPECT_EQ(check.refused, (QStringList{"notes.md: Invalid file type",
                                          "b~1.nc: Filename contains invalid character: ~"}));
    EXPECT_TRUE(isAtciFile("ATCI.macro"));
    EXPECT_FALSE(isAtciFile("atci.macro"));
}

TEST_F(AppTest, TheSdCardToolUploadsRunsAndDeletesFilesOnAGrblHalCard) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    NotificationCenter notifications;
    SdCardDialog dialog(machine, notifications);
    EXPECT_EQ(dialog.status(), "Disconnected");
    EXPECT_EQ(dialog.message(), "Must be connected to use SD card functionality.");
    EXPECT_FALSE(dialog.refreshButton()->isEnabled());
    EXPECT_FALSE(dialog.uploadButton()->isEnabled());

    // Grbl has no card.
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected() && machine.controller()->runner().hasSettings(); }));
    EXPECT_EQ(dialog.message(), "SD card tools are only available for grblHAL devices.");
    machine.disconnectFromMachine();

    machine.connectTo(Machine::kSimulatorHalPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle" && dialog.status() == "Mounted";
    }));
    ASSERT_TRUE(machine.simulator()->isGrblHal());
    EXPECT_TRUE(dialog.message().contains("No files found"));
    EXPECT_TRUE(dialog.refreshButton()->isEnabled());
    EXPECT_TRUE(dialog.uploadButton()->isEnabled());

    // The modal keeps the files it can send and reports the others.
    const QString square = dir.path() + "/square.nc";
    const QString macro = dir.path() + "/ATCI.macro";
    for (const auto& [path, text] : {std::pair{square, "G21 G90\nG1 X10 F1200\nG1 Y10\nG1 X0\nG1 Y0\n"},
                                     std::pair{macro, "(tool changer)\n"}}) {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write(text);
    }
    QStringList refused;
    SdUploadDialog modal([&](const QString& text) { refused << text; });
    modal.addFiles({square, dir.path() + "/notes.md"});
    EXPECT_EQ(modal.files(), QStringList{square});
    EXPECT_EQ(refused, QStringList{"Some files were rejected:\nnotes.md: Invalid file type"});
    EXPECT_EQ(modal.findChild<QPushButton*>("upload")->text(), "Upload (1)");
    modal.addFiles({macro});
    const QByteArray screenshots = qgetenv("GS_TEST_SCREENSHOTS");
    if (!screenshots.isEmpty()) {
        modal.show();
        modal.grab().save(QString::fromLocal8Bit(screenshots) + "/sd_upload.png");
    }
    modal.removeFile(0);
    EXPECT_EQ(modal.files(), QStringList{macro});

    // Upload: the refused are reported, the rest go up over YMODEM.
    dialog.setFilePicker([&] { return QStringList{square, macro, dir.path() + "/oops!.nc"}; });
    dialog.uploadButton()->click();
    ASSERT_FALSE(notifications.list().empty());
    EXPECT_EQ(notifications.list().front().message,
              "Some files were rejected:\noops!.nc: Filename contains invalid character: !");
    ASSERT_TRUE(waitFor([&] { return dialog.uploadState() == "uploading"; }));
    ASSERT_TRUE(waitFor([&] { return dialog.uploadState() == "complete"; }, 10000));
    EXPECT_EQ(dialog.uploadProgress(), 100);
    EXPECT_EQ(machine.simulator()->sdFiles().at("square.nc"), "G21 G90\nG1 X10 F1200\nG1 Y10\nG1 X0\nG1 Y0\n");
    // Listed again once done; the tool changer's macro is not to be run.
    ASSERT_TRUE(waitFor([&] { return dialog.fileNames() == QStringList({"ATCI.macro", "square.nc"}); }));
    ASSERT_TRUE(waitFor([&] { return dialog.uploadState() == "idle"; }));
    EXPECT_FALSE(dialog.canRun("ATCI.macro"));
    EXPECT_TRUE(dialog.canDelete("ATCI.macro"));
    EXPECT_TRUE(dialog.canRun("square.nc"));
    if (!screenshots.isEmpty()) {
        dialog.show();  // which lists the card again
        ASSERT_TRUE(waitFor([&] { return dialog.canRun("square.nc") && dialog.message().isEmpty(); }));
        dialog.grab().save(QString::fromLocal8Bit(screenshots) + "/sd_card.png");
    }

    // Run: the board streams it from the card; nothing else runs meanwhile.
    dialog.runFile("square.nc");
    ASSERT_TRUE(waitFor([&] { return !dialog.canDelete("square.nc"); }));
    EXPECT_TRUE(machine.simulator()->isRunningSdFile());
    EXPECT_TRUE(machine.isRunningSdFile());  // no job starts meanwhile
    EXPECT_FALSE(dialog.canRun("square.nc"));
    ASSERT_TRUE(waitFor([&] { return dialog.canRun("square.nc"); }, 10000));
    EXPECT_FALSE(machine.isRunningSdFile());
    EXPECT_DOUBLE_EQ(machine.simulator()->machinePosition()[0], 0.0);
    EXPECT_EQ(machine.controller()->state().status.activeState, "Idle");

    // Delete asks, and the file leaves the list at once.
    QStringList asked;
    dialog.setConfirmer([&](const QString& title, const QString& text) {
        asked << title + ": " + text;
        return true;
    });
    dialog.deleteFile("square.nc");
    EXPECT_EQ(asked, QStringList{"Delete File: Are you sure you want to delete square.nc?"});
    EXPECT_EQ(dialog.fileNames(), QStringList{"ATCI.macro"});
    ASSERT_TRUE(waitFor([&] { return machine.simulator()->sdFiles().count("square.nc") == 0; }));
    dialog.refreshButton()->click();
    ASSERT_TRUE(waitFor([&] { return dialog.fileNames() == QStringList{"ATCI.macro"}; }));

    // A board that reports no card fails the upload, with a toast.
    dialog.upload({square});
    machine.controller()->receiveLine("<Idle|MPos:0.000,0.000,0.000|FS:0,0|FW:grblHAL>");
    ASSERT_TRUE(waitFor([&] { return notifications.list().size() == 2; }, 5000));
    EXPECT_EQ(notifications.list().back().message,
              "Error uploading file - SD Card not detected, please insert an SD Card in FAT32 format, 32 GB or "
              "under, and try again.");
    EXPECT_EQ(notifications.list().back().type, NotificationType::Error);
    EXPECT_EQ(dialog.status(), "Unmounted");
    EXPECT_FALSE(dialog.uploadButton()->isEnabled());
}

TEST_F(AppTest, AccessoriesComingAndGoingPopUp) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    MainWindow window(machine);
    window.setDialogsEnabled(false);
    QStringList changes;
    QObject::connect(&machine, &Machine::accessoryConnectivityChanged, [&](const QString& name, bool connected) {
        changes << name + (connected ? " on" : " off");
    });
    machine.connectTo(Machine::kSimulatorHalPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected() && machine.controller()->runner().hasSettings(); }));
    controller::Controller& c = *machine.controller();
    // [NEWOPT:] says what is there; Autoconfig messages then tell changes.
    c.receiveLine("[NEWOPT:ENUMS,TLS=1,ATCEXP=0,PROBE]");
    c.receiveLine("[MSG:Info: Autoconfig: TLS=0, ATCEXP=1, PROBE=0, SD=0]");
    EXPECT_EQ(changes, (QStringList{"TLS off", "ATCEXP on"}));
    const QStringList toasts = window.toasts().texts();
    EXPECT_TRUE(toasts.contains("TLS disconnected\nConnection lost")) << toasts.join(" | ").toStdString();
    EXPECT_TRUE(toasts.contains("ATCEXP connected\nReady to use - device available"));
    c.receiveLine("[MSG:Info: Autoconfig: TLS=0]");  // no change
    // A key without a value counts as there.
    c.receiveLine("[MSG:Info: Autoconfig: TLS]");
    EXPECT_EQ(changes, (QStringList{"TLS off", "ATCEXP on", "TLS on"}));
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

    // Lightweight mode (Shift+M): Light holds the view flat, from above.
    window.toolpathView().set3dView();
    ASSERT_TRUE(window.shortcuts().trigger("LIGHTWEIGHT_MODE"));
    EXPECT_TRUE(machine.settings().liteMode);
    EXPECT_TRUE(window.toolpathView().flat());
    EXPECT_EQ(window.toolpathView().view(), ToolpathView::View::Top);
    window.toolpathView().set3dView();
    EXPECT_EQ(window.toolpathView().view(), ToolpathView::View::Top);
    ASSERT_TRUE(window.shortcuts().trigger("LIGHTWEIGHT_MODE"));
    EXPECT_FALSE(window.toolpathView().flat());
}

}  // namespace
