// The Qt side: the event loop adapter, the machine service against the
// simulated board (real time, so kept short), and a main-window smoke test on
// the offscreen platform.

#include "machine.hpp"
#include "main_window.hpp"
#include "probe_panel.hpp"
#include "qt_event_loop.hpp"
#include "settings_dialog.hpp"

#include "gs/sim/grbl_simulator.hpp"

#include <QApplication>
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
