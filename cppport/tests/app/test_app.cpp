// The Qt-side application services: event loop adapter, machine service against
// the simulated board (real time, so kept short), configuration, shortcuts,
// diagnostics, console log, and SD card utilities.

#include "accessory_wizards.hpp"
#include "app_settings.hpp"
#include "console_log.hpp"
#include "diagnostics.hpp"
#include "jogger.hpp"
#include "machine.hpp"
#include "notification_center.hpp"
#include "qt_event_loop.hpp"
#include "rotary_actions.hpp"
#include "sd_card_utils.hpp"
#include "shortcuts.hpp"

#include "gs/config/history.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/actions.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include "app_test_support.hpp"

#include <boost/json.hpp>

#include <QGuiApplication>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QKeyEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QTextDocument>
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
using namespace gs::app::test_support;

namespace {

class AppTest : public ::testing::Test {
protected:
    void SetUp() override { application(); }
};

}  // namespace


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
        settings.touchplateTypeSwitcher = true;
        settings.ethernetIp = {10, 0, 0, 42};
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
    EXPECT_TRUE(machine.settings().touchplateTypeSwitcher);
    EXPECT_EQ(machine.settings().ethernetAddress(), "10.0.0.42");

    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] { return machine.isConnected(); }));
    EXPECT_EQ(machine.controller()->toolChangeContext().option, "Code");
    EXPECT_EQ(machine.controller()->toolChangeContext().preHook, "G0 Z20");
}

TEST_F(AppTest, TheSimulatorHasAutoZeroAndBitZeroPlates) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.simulator()->setSpeed(100);
    // Places the plate as the run dialog does, runs the routine, and says
    // where the bit started.
    const auto probeWith = [&](probe::PlateType plate, probe::ProbeType type, double diameter, probe::Axes axes) {
        AppSettings settings = machine.settings();
        settings.probe.plateType = plate;
        machine.setSettings(settings);
        machine.placeSimulatedPlate(type, diameter, probe::kBottomLeft, axes);
        const sim::SimAxes start = machine.simulator()->machinePosition();
        EXPECT_TRUE(machine.runProbe(machine.probeRoutine(axes, type, diameter, probe::kBottomLeft)));
        EXPECT_TRUE(waitFor([&] {
            controller::Controller* c = machine.controller();
            return c->feeder().size() == 0 && !c->feeder().isPending() && machine.simulator()->activeState() == "Idle";
        }, 15000));
        EXPECT_NE(machine.simulator()->activeState(), "Alarm");
        return start;
    };

    // AutoZero, finding its pocket's walls: the pocket's centre is 22.5 mm
    // in from the corner, its floor the plate's 5 mm above the stock.
    const sim::SimAxes autoStart = probeWith(probe::PlateType::AutoZero, probe::ProbeType::Auto, 0, {true, true, true});
    sim::SimAxes offset = machine.simulator()->workOffset();
    EXPECT_NEAR(offset[0], autoStart[0] - 22.5, 1e-3);
    EXPECT_NEAR(offset[1], autoStart[1] - 22.5, 1e-3);
    EXPECT_NEAR(offset[2], autoStart[2] - 15, 1e-3);
    // A V-bit's tip finds the narrower walls near the floor.
    const sim::SimAxes tipStart = probeWith(probe::PlateType::AutoZero, probe::ProbeType::Tip, 0, {true, true, true});
    offset = machine.simulator()->workOffset();
    EXPECT_NEAR(offset[0], tipStart[0] - 22.5, 1e-3);
    EXPECT_NEAR(offset[1], tipStart[1] - 22.5, 1e-3);

    // BitZero: the bore over the stock's corner becomes X0 Y0, the block's
    // top 13 mm above the stock.
    const sim::SimAxes boreStart = probeWith(probe::PlateType::BitZero, probe::ProbeType::Diameter, 6.35, {true, true, true});
    offset = machine.simulator()->workOffset();
    EXPECT_NEAR(offset[0], boreStart[0] - 1, 1e-3);
    EXPECT_NEAR(offset[1], boreStart[1] + 0.5, 1e-3);
    EXPECT_NEAR(offset[2], boreStart[2] - 5, 1e-3);
    // Z alone: on the block's top, its Z-only thickness.
    const sim::SimAxes zStart = probeWith(probe::PlateType::BitZero, probe::ProbeType::Diameter, 6.35, {false, false, true});
    EXPECT_NEAR(machine.simulator()->workOffset()[2], zStart[2] - 25.5, 1e-3);
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
    ShortcutScope scope;
    scope.active = [] { return true; };
    ShortcutManager shortcuts(machine, scope);
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
    config::MacroStore store = machine.macros();
    const std::string text = boost::json::serialize(config::exportMacros(store));
    QFile file(dir.path() + "/macros.json");
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write(text.data(), static_cast<qint64>(text.size()));
    file.close();

    Machine other(loop, (dir.path() + "/other_rc").toStdWString());
    boost::system::error_code ec;
    const boost::json::value parsed = boost::json::parse(text, ec);
    ASSERT_FALSE(ec);
    config::MacroStore otherStore = other.macros();
    const auto res = config::importMacros(otherStore, parsed);
    EXPECT_EQ(res.imported, 1);
    ASSERT_EQ(other.macros().list().size(), 1u);
    EXPECT_EQ(other.macros().list()[0].content, "G0 Z5\nG0 X0 Y0");
    boost::system::error_code junkEc;
    const boost::json::value junk = boost::json::parse("{not json", junkEc);
    EXPECT_TRUE(junkEc);
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
                "probe": {"probeFeedrate": 60, "connectivityTest": false, "touchplateTypeSwitcher": true},
                "axes": {"jog": {"rapid": {"xyStep": 25, "zStep": 12, "aStep": 30, "feedrate": 6000},
                                 "threshold": 300}},
                "connection": {"port": "COM4", "baudrate": 250000, "ip": [192, 168, 1, 77], "ethernetPort": 8023},
                "spindle": {"mode": "laser", "delay": 2, "laser": {"maxPower": 1000}},
                "surfacing": {"width": 250},
                "visualizer": {"showLineWarnings": true, "rotaryDiameterOffsetEnabled": true}
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
    EXPECT_TRUE(s.touchplateTypeSwitcher);
    EXPECT_EQ(s.outlineMode, job::OutlineMode::Square);
    EXPECT_EQ(s.defaultFirmware, protocol::Firmware::GrblHal);
    EXPECT_TRUE(s.preferences.useAaxisForGrbl);
    EXPECT_EQ(s.jog.rapid.xyStep, 25);
    EXPECT_EQ(s.jog.threshold, 300);
    EXPECT_EQ(s.jog.normal.xyStep, 5);  // not in the file: the default
    EXPECT_EQ(s.port, "COM4");
    EXPECT_EQ(s.baudRate, 250000);
    EXPECT_EQ(s.ethernetAddress(), "192.168.1.77");
    EXPECT_EQ(s.networkPort, 8023);
    EXPECT_TRUE(s.spindle.laserMode);
    EXPECT_EQ(s.spindle.laser.maxPower, 1000);
    EXPECT_EQ(s.preferences.spindleDelay, 2);
    EXPECT_EQ(s.surfacing.width, 250);
    EXPECT_TRUE(s.preferences.showLineWarnings);
    EXPECT_TRUE(s.rotary.diameterOffset);
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

TEST_F(AppTest, NotificationsKeepTheLastHundred) {
    NotificationCenter center;
    for (int i = 0; i < 101; ++i) {
        center.add(QString("note %1").arg(i), i % 2 ? NotificationType::Error : NotificationType::Info);
    }
    ASSERT_EQ(center.list().size(), 100u);
    EXPECT_EQ(center.list().front().message, "note 1");  // the oldest went
    EXPECT_EQ(center.unreadErrors(), 50);
    center.clear();
    EXPECT_TRUE(center.list().empty());
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

TEST(GSenderSettings, AccessibilityComesAlong) {
    const boost::json::value file = boost::json::parse(R"({"settings": {"workspace": {"accessibility": {
        "statusAnnouncements": true, "jobProgressAnnouncements": true, "jobProgressIncrement": 20,
        "focusRings": true, "audioCues": {"enabled": true, "alarmTriggered": true},
        "gcodeSummary": {"enabled": true, "showVisually": true}, "showKeyboardMap": true,
        "displayScaleFactor": "150%"}},
        "widgets": {"spindle": {"inputType": "Number"}}}})");
    const std::optional<GSenderSettings> read = readGSenderSettings(file);
    ASSERT_TRUE(read);
    const AccessibilitySettings& a = read->settings.accessibility;
    EXPECT_TRUE(a.statusAnnouncements && a.jobProgressAnnouncements && a.focusRings && a.audioCues && a.cueAlarm);
    EXPECT_FALSE(a.cueJobComplete);
    EXPECT_EQ(a.jobProgressIncrement, 20);
    EXPECT_TRUE(a.gcodeSummary && a.gcodeSummaryVisible && a.showKeyboardMap);
    EXPECT_EQ(a.displayScale, "150%");
    EXPECT_EQ(read->settings.spindle.inputType, "Number");
    // The port stores them the same way.
    const AppSettings again = appSettingsFromJson(appSettingsToJson(read->settings));
    EXPECT_EQ(again.accessibility.jobProgressIncrement, 20);
    EXPECT_TRUE(again.accessibility.cueAlarm && again.accessibility.gcodeSummaryVisible);
    EXPECT_EQ(again.accessibility.displayScale, "150%");
    EXPECT_EQ(again.spindle.inputType, "Number");

    // The display scale is read before Qt starts.
    QTemporaryDir dir;
    const QString path = dir.path() + "/rc";
    EXPECT_EQ(displayScaleFactor(path.toStdWString()), 1.0);  // no file
    QFile rc(path);
    ASSERT_TRUE(rc.open(QIODevice::WriteOnly));
    rc.write(R"({"app": {"accessibility": {"displayScaleFactor": "125%"}}})");
    rc.close();
    EXPECT_EQ(displayScaleFactor(path.toStdWString()), 1.25);
}

TEST(AccessoryWizards, TheirCommandsFollowTheFirmware) {
    const auto has = [](const std::vector<std::string>& code, const std::string& line) {
        return std::find(code.begin(), code.end(), line) != code.end();
    };
    // sienciHAL before the ATCi build, grblCore's settings from it.
    const std::vector<std::string> hal = sienciSpindleCommands(20240417);
    EXPECT_TRUE(has(hal, "$392=11") && has(hal, "$395=6"));
    EXPECT_EQ(hal.back(), "$$");
    const std::vector<std::string> core = sienciSpindleCommands(20250701);
    EXPECT_TRUE(has(core, "$394=11") && has(core, "$395=2"));
    EXPECT_EQ(core.back(), "$REBOOT");
    EXPECT_TRUE(has(sienciSpindleCommands(20260515), "$395=7"));
    EXPECT_EQ(modbusCommands(20240417), std::vector<std::string>{"$476=2"});
    EXPECT_EQ(modbusCommands(20250627), (std::vector<std::string>{"$476=2", "$REBOOT"}));
    EXPECT_EQ(autoSpinCommands(false, false),
              (std::vector<std::string>{"G4P0.1", "$31=1", "G4P0.1", "$30=31250", "G4P0.1", "$$"}));
    const std::vector<std::string> slbLite = autoSpinCommands(true, true);
    EXPECT_TRUE(has(slbLite, "$35 = 32") && has(slbLite, "$36 = 96"));
    EXPECT_TRUE(has(autoSpinCommands(true, false), "$35 = 30"));
}

TEST_F(AppTest, TheDiagnosticFileGathersTheReportSettingsAndJob) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    machine.loadProgram("square.nc", "G21 G90\nG1 X10 F600\nG1 Y10\nM30\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    const QDateTime when(QDate(2026, 9, 24), QTime(14, 5, 9));
    EXPECT_EQ(diagnosticsStamp(when), "9-24-2026_14-05-09");

    const QString report = diagnosticsReport(machine, {"$$", "G0 X5"}, when);
    for (const char* part : {"Diagnostics Report", "Environment", "LongMill MK2", "Controller Status",
                             "Firmware Settings", "$110", "Terminal History", "G0 X5", "square.nc", "G1 X10 F600"}) {
        EXPECT_TRUE(report.contains(QString::fromLatin1(part))) << part;
    }
    const QByteArray pdf = diagnosticsPdf(report);
    EXPECT_TRUE(pdf.startsWith("%PDF"));

    const QString path = dir.path() + "/diagnostics.zip";
    QString error;
    ASSERT_TRUE(writeDiagnostics(machine, {"$$"}, path, &error, when)) << error.toStdString();
    QFile zip(path);
    ASSERT_TRUE(zip.open(QIODevice::ReadOnly));
    const QByteArray bytes = zip.readAll();
    EXPECT_TRUE(bytes.startsWith("PK\x03\x04"));
    // The loaded file, the report, and both settings exports.
    for (const char* name : {"square.nc", "diagnostics_9-24-2026_14-05-09.pdf",
                             "gSender-firmware-settings-9-24-2026-14-05-09.json", "gSenderSettings_9-24-2026_14-05-09.json"}) {
        EXPECT_TRUE(bytes.contains(name)) << name;
    }
    EXPECT_TRUE(bytes.contains("\n \"$110\": \"4000.000\""));
    if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
        QFile::remove(QString::fromLocal8Bit(out) + "/diagnostics.zip");
        QFile::copy(path, QString::fromLocal8Bit(out) + "/diagnostics.zip");
        // The report's first screenful, as the PDF lays it out.
        QTextDocument document;
        document.setHtml(report);
        document.setTextWidth(760);
        QImage image(760, 1400, QImage::Format_RGB32);
        image.fill(Qt::white);
        QPainter painter(&image);
        document.drawContents(&painter, QRectF(0, 0, 760, 1400));
        painter.end();
        image.save(QString::fromLocal8Bit(out) + "/diagnostics_report.png");
    }
}

TEST(Console, LinesAreClassifiedAsUpstreamClassifiesThem) {
    EXPECT_EQ(classifyRead("ok"), ConsoleType::Response);
    EXPECT_EQ(classifyRead("<Idle|MPos:0.000,0.000,0.000>"), ConsoleType::Response);
    EXPECT_EQ(classifyRead("$130=800.000"), ConsoleType::Response);
    EXPECT_EQ(classifyRead("ALARM:10 (EStop asserted. Clear and reset)"), ConsoleType::Alarm);
    EXPECT_EQ(classifyRead("error:20"), ConsoleType::Error);
    EXPECT_EQ(classifyRead("[MSG:Emergency stop - clear, then reset to continue]"), ConsoleType::System);
    EXPECT_EQ(classifyRead("[MSG:WARN: Spindle at max]"), ConsoleType::Warning);
    EXPECT_EQ(classifyRead("[MSG:ALARM:1 triggered]"), ConsoleType::Alarm);
    for (const auto source : {controller::WriteSource::Client, controller::WriteSource::Feeder,
                              controller::WriteSource::Sender}) {
        EXPECT_EQ(classifyWrite(source), ConsoleType::Gcode);
    }
    EXPECT_EQ(classifyWrite(controller::WriteSource::Server), ConsoleType::System);

    const auto message = [](ConsoleType type) { return ConsoleMessage{1, "x", type}; };
    for (const ConsoleType type : {ConsoleType::Gcode, ConsoleType::Response, ConsoleType::System,
                                   ConsoleType::Warning, ConsoleType::Error, ConsoleType::Alarm}) {
        EXPECT_TRUE(matchesFilter(message(type), ConsoleFilter::All));
    }
    for (const ConsoleType type : {ConsoleType::Warning, ConsoleType::Error, ConsoleType::Alarm}) {
        EXPECT_TRUE(matchesFilter(message(type), ConsoleFilter::Faults));
    }
    for (const ConsoleType type : {ConsoleType::Gcode, ConsoleType::Response, ConsoleType::System}) {
        EXPECT_FALSE(matchesFilter(message(type), ConsoleFilter::Faults));
    }
    EXPECT_TRUE(matchesFilter(message(ConsoleType::Gcode), ConsoleFilter::Gcode));
    EXPECT_FALSE(matchesFilter(message(ConsoleType::Response), ConsoleFilter::Gcode));
    EXPECT_TRUE(matchesFilter(message(ConsoleType::System), ConsoleFilter::System));
}

TEST_F(AppTest, TheConsoleLogKeepsTheLastThousandLinesInBatches) {
    ConsoleLog log;
    std::vector<std::pair<int, int>> batches;
    QObject::connect(&log, &ConsoleLog::appended, [&](int count, int dropped) { batches.emplace_back(count, dropped); });
    log.write("");  // nothing
    for (int i = 0; i < 5; ++i) {
        log.write(QString("first %1").arg(i), ConsoleType::Gcode);
    }
    EXPECT_TRUE(log.messages().empty());  // until the 30 ms flush
    ASSERT_TRUE(waitFor([&] { return !batches.empty(); }));
    EXPECT_EQ(batches, (std::vector<std::pair<int, int>>{{5, 0}}));
    EXPECT_EQ(log.messages().front().type, ConsoleType::Gcode);
    // A burst past the limit: the waiting lines are capped, the log trimmed.
    for (int i = 0; i < 1100; ++i) {
        log.write(QString("line %1").arg(i));
    }
    log.flush();
    ASSERT_EQ(batches.size(), 2u);
    EXPECT_EQ(batches[1], (std::pair<int, int>{1000, 5}));
    ASSERT_EQ(log.messages().size(), 1000u);
    EXPECT_EQ(log.messages().front().text, "line 100");
    EXPECT_EQ(log.messages().back().text, "line 1099");
    EXPECT_EQ(log.messages().back().type, ConsoleType::Response);  // untyped
    const QStringList last = log.lastTexts(50);
    ASSERT_EQ(last.size(), 50);
    EXPECT_EQ(last.front(), "line 1050");
    // Clearing drops what waits too.
    log.write("late");
    log.clear();
    log.flush();
    EXPECT_TRUE(log.messages().empty());
    EXPECT_EQ(batches.size(), 2u);
    for (int i = 0; i < 310; ++i) {
        log.addInput(QString("G0 X%1").arg(i));
    }
    EXPECT_EQ(log.inputHistory().size(), 300);
    EXPECT_EQ(log.inputHistory().front(), "G0 X10");
}

TEST_F(AppTest, WarnOnBadLineNamesTheLineTheBoardRefused) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    AppSettings settings = machine.settings();
    settings.preferences.showLineWarnings = true;
    machine.setSettings(settings);
    machine.connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine.isConnected() && machine.controller()->runner().hasSettings() &&
               machine.controller()->state().status.activeState == "Idle";
    }));
    QStringList warnings;
    int errors = 0;
    QObject::connect(&machine, &Machine::lineWarning,
                     [&](const QString& code, const QString& line) { warnings << code + " " + line; });
    QObject::connect(&machine, &Machine::errorReported, [&] { ++errors; });
    machine.sendConsoleLine("G99");
    ASSERT_TRUE(waitFor([&] { return errors == 1; }));
    EXPECT_EQ(warnings, QStringList{"20 G99"});
    // Off: the error alone.
    settings.preferences.showLineWarnings = false;
    machine.setSettings(settings);
    machine.sendConsoleLine("G99");
    ASSERT_TRUE(waitFor([&] { return errors == 2; }));
    EXPECT_EQ(warnings.size(), 1);
}

TEST_F(AppTest, EthernetAddressFollowsSettings) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    EXPECT_EQ(machine.settings().ethernetAddress(), "192.168.5.1");
    AppSettings settings = machine.settings();
    settings.ethernetIp = {10, 0, 0, 42};
    machine.setSettings(settings);
    EXPECT_EQ(machine.settings().ethernetAddress(), "10.0.0.42");
}

TEST_F(AppTest, RotaryToolpathsWrapAsUpstreamDrawsThem) {
    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine(loop, (dir.path() + "/rc").toStdWString());
    const std::string program = "(Cylinder Dia: 50)\nG21 G90\nG0 X0 Z5 A0\nG1 A90 F500\n";
    const auto endOf = [](const std::vector<float>& segments) {
        const std::size_t n = segments.size();
        return std::array<float, 3>{segments[n - 3], segments[n - 2], segments[n - 1]};
    };
    // Turned by -A: a quarter turn brings the top of the stock round to +Y.
    machine.loadProgram("rotary.nc", program);
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    ASSERT_FALSE(machine.toolpath().feeds.empty());
    std::array<float, 3> end = endOf(machine.toolpath().feeds);
    EXPECT_NEAR(end[1], 5, 1e-4);
    EXPECT_NEAR(end[2], 0, 1e-4);
    EXPECT_NEAR(endOf(machine.toolpath().rapids)[2], 5, 1e-4);

    // Visualize non-center zeros: zeroed on the surface, drawn about the axis.
    AppSettings settings = machine.settings();
    settings.rotary.diameterOffset = true;
    machine.setSettings(settings);
    machine.loadProgram("rotary.nc", program);
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    end = endOf(machine.toolpath().feeds);
    EXPECT_NEAR(end[1], 30, 1e-4);  // Z 5 plus the 25 mm radius
    EXPECT_NEAR(end[2], 0, 1e-4);
    EXPECT_NEAR(endOf(machine.toolpath().rapids)[2], 30, 1e-4);
    // Not for a file that moves Y.
    machine.loadProgram("rotary.nc", program + "G1 Y1\n");
    ASSERT_TRUE(waitFor([&] { return !machine.isAnalyzing(); }));
    EXPECT_NEAR(endOf(machine.toolpath().rapids)[2], 5, 1e-4);
}
