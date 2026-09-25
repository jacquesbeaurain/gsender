// The QML touch UI, loaded headless (the offscreen platform, Qt Quick's
// software renderer) against a real Machine and the simulated board: items
// are found by objectName and driven with taps and touch gestures.
// GS_TEST_SCREENSHOTS=<dir> saves what the tests show.

#include "backend.hpp"
#include "machine.hpp"
#include "notification_center.hpp"
#include "qt_event_loop.hpp"
#include "ui_app.hpp"
#include "ui_shortcuts.hpp"
#include "shortcuts.hpp"

#include "gs/config/history.hpp"
#include "gs/controller/controller.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include <QApplication>
#include <QDeadlineTimer>
#include <QFile>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QtQml/qqmlextensionplugin.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <ostream>
#include <memory>

Q_IMPORT_QML_PLUGIN(GSenderPlugin)

// GoogleTest prints QStrings as text.
QT_BEGIN_NAMESPACE
inline void PrintTo(const QString& text, std::ostream* os) {
    *os << '"' << text.toStdString() << '"';
}
QT_END_NAMESPACE

using namespace gs;

namespace {

QApplication& application() {
    static int argc = 1;
    static char name[] = "gs_ui_tests";
    static char* argv[] = {name, nullptr};
    static QApplication* app = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && !qEnvironmentVariableIsEmpty("WINDIR")) {
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "\\Fonts");
        }
        ui::configureQuick(true);
        return new QApplication(argc, argv);
    }();
    return *app;
}

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

// The item named `name` under `root` (Repeater delegates included, which
// QObject::findChild misses).
QQuickItem* findItem(QQuickItem* root, const QString& name) {
    if (!root) {
        return nullptr;
    }
    if (root->objectName() == name) {
        return root;
    }
    for (QQuickItem* child : root->childItems()) {
        if (QQuickItem* found = findItem(child, name)) {
            return found;
        }
    }
    return nullptr;
}

// The UI over its own configuration and Machine.
class UiTest : public ::testing::Test {
protected:
    void SetUp() override {
        application();
        loop_ = std::make_unique<app::QtEventLoop>();
        machine_ = std::make_unique<app::Machine>(*loop_, (dir_.path() + "/rc").toStdWString());
        backend_ = std::make_unique<ui::UiBackend>(*machine_);
        ui::UiBackend::setInstance(backend_.get());
        engine_ = std::make_unique<QQmlApplicationEngine>();
        window_ = ui::loadMainWindow(*engine_);
        ASSERT_NE(window_, nullptr);
        window_->resize(1400, 900);
        window_->show();
        ASSERT_TRUE(waitFor([&] { return window_->isExposed(); }));
    }

    void TearDown() override {
        engine_.reset();
        // Delegates a Repeater dropped are deleted later; not at exit.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        backend_.reset();
        machine_.reset();
        loop_.reset();
    }

    QQuickItem* item(const QString& name) { return findItem(window_->contentItem(), name); }
    QString text(const QString& name) {
        QQuickItem* found = item(name);
        return found ? found->property("text").toString() : QString("<no %1>").arg(name);
    }
    QPoint centreOf(QQuickItem* target) {
        return target->mapToScene(QPointF(target->width() / 2, target->height() / 2)).toPoint();
    }
    void tap(const QString& name) {
        QQuickItem* target = item(name);
        ASSERT_NE(target, nullptr) << name.toStdString();
        // A tap outside the window still reaches the scene; a person's cannot.
        ASSERT_TRUE(QRect(QPoint(0, 0), window_->size()).contains(centreOf(target)))
            << name.toStdString() << " is off the window";
        QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(target));
        QCoreApplication::processEvents();
    }
    void type(const QString& text) {
        for (const QChar c : text) {
            QTest::keyClick(window_, c.toLatin1());
        }
    }
    // A tool tab, scrolled to with the arrows as a person would.
    void selectTool(const QString& key) {
        // A rebuilt strip (the settings showing a tab) settles first.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QTest::qWait(50);
        const QString name = "toolsTab_" + key;
        QQuickItem* strip = item(name) ? item(name)->parentItem() : nullptr;
        ASSERT_NE(strip, nullptr) << name.toStdString();
        const auto inView = [&] {
            QQuickItem* tab = item(name);
            const QRectF box = tab->mapRectToItem(window_->contentItem(), QRectF(0, 0, tab->width(), tab->height()));
            QQuickItem* flick = strip->parentItem()->parentItem();
            const QRectF view = flick->mapRectToItem(window_->contentItem(), QRectF(0, 0, flick->width(), flick->height()));
            return view.contains(box.center());
        };
        for (const char* arrow : {"toolsScrollLeft", "toolsScrollRight"}) {
            for (int i = 0; i < 10 && !inView() && item(arrow)->property("can").toBool(); ++i) {
                tap(arrow);
                QTest::qWait(200);
            }
        }
        tap(name);
    }
    void connectSimulator() {
        backend_->connectSimulator();
        ASSERT_TRUE(waitFor([&] {
            return machine_->isConnected() && machine_->controller()->state().status.activeState == "Idle";
        }));
    }
    void screenshot(const QString& name) {
        if (const QByteArray out = qgetenv("GS_TEST_SCREENSHOTS"); !out.isEmpty()) {
            window_->grabWindow().save(QString::fromLocal8Bit(out) + "/" + name + ".png");
        }
    }

    QTemporaryDir dir_;
    std::unique_ptr<app::QtEventLoop> loop_;
    std::unique_ptr<app::Machine> machine_;
    std::unique_ptr<ui::UiBackend> backend_;
    std::unique_ptr<QQmlApplicationEngine> engine_;
    QQuickWindow* window_ = nullptr;
};

}  // namespace

TEST_F(UiTest, TheTopBarShowsTheConnectionAndTheMachineState) {
    EXPECT_EQ(text("statusText"), "Disconnected");
    ASSERT_NE(item("connectionButton"), nullptr);
    EXPECT_FALSE(item("connectionPort")->isVisible());

    connectSimulator();
    EXPECT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));
    EXPECT_EQ(text("connectionPort"), "Simulator");
    EXPECT_EQ(text("connectionFirmware"), "Grbl");
    EXPECT_TRUE(item("connectionPort")->isVisible());
    screenshot("ui_shell");

    // An alarm shows its code.
    machine_->simulator()->triggerAlarm(3);
    EXPECT_TRUE(waitFor([&] { return text("statusText") == "Alarm (3)"; }));
}

TEST_F(UiTest, TheConnectionButtonListsPortsConnectsAndDisconnects) {
    tap("connectionButton");
    QObject* ports = window_->findChild<QObject*>("portListings");
    ASSERT_NE(ports, nullptr);
    ASSERT_TRUE(waitFor([&] { return ports->property("opened").toBool(); }));
    // The Ethernet board at the settings' address.
    QQuickItem* ethernet = item("portEthernet");
    ASSERT_NE(ethernet, nullptr);
    EXPECT_EQ(ethernet->property("name").toString(), "192.168.5.1");
    EXPECT_EQ(ethernet->property("detail").toString(), "Ethernet (port 23)");
    // The simulated Grbl board.
    ASSERT_TRUE(waitFor([&] { return item("portSimulator") && item("portSimulator")->height() > 0; }));
    tap("portSimulator");
    ASSERT_TRUE(waitFor([&] { return machine_->isConnected(); }));
    EXPECT_FALSE(ports->property("opened").toBool());
    EXPECT_TRUE(waitFor([&] { return text("connectionPort") == "Simulator"; }));

    // Connected, a tap offers Disconnect.
    tap("connectionButton");
    QObject* menu = window_->findChild<QObject*>("disconnectMenu");
    ASSERT_TRUE(waitFor([&] { return menu->property("opened").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return item("disconnectItem") && item("disconnectItem")->height() > 0; }));
    tap("disconnectItem");
    EXPECT_TRUE(waitFor([&] { return !machine_->isConnected(); }));
    EXPECT_TRUE(waitFor([&] { return text("connectionText") == "Connect to CNC"; }));

    // A board that does not answer: "Unable to connect." (the port does not exist).
    app::AppSettings settings = machine_->settings();
    settings.ethernetIp = {127, 0, 0, 1};
    settings.networkPort = 1;   // nothing listens
    machine_->setSettings(settings);
    tap("connectionButton");
    ASSERT_TRUE(waitFor([&] { return ports->property("opened").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return item("portEthernet")->height() > 0; }));
    tap("portEthernet");
    EXPECT_TRUE(waitFor([&] { return text("connectionText") == "Unable to connect."; }, 8000));
    screenshot("ui_connection_error");
}

TEST_F(UiTest, TheRailSwitchesPages) {
    QQuickItem* pages = item("pages");
    ASSERT_NE(pages, nullptr);
    EXPECT_EQ(pages->property("currentIndex").toInt(), 0);
    EXPECT_TRUE(item("navCarve")->property("active").toBool());
    tap("navStats");
    EXPECT_EQ(pages->property("currentIndex").toInt(), 1);
    EXPECT_TRUE(item("statsPage")->isVisible());
    EXPECT_FALSE(item("carvePage")->isVisible());
    EXPECT_TRUE(item("navStats")->property("active").toBool());
    tap("navConfig");
    EXPECT_EQ(pages->property("currentIndex").toInt(), 3);
    tap("navCarve");
    EXPECT_TRUE(item("carvePage")->isVisible());
}

TEST_F(UiTest, TheVisualizerOrbitsPansAndZoomsByTouch) {
    machine_->loadProgram("square.nc", "G21 G90\nG0 X0 Y0\nG1 Z-1 F300\nG1 X50\nG1 Y50\nG1 X0\nG1 Y0\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    QQuickItem* view = item("toolpath");
    ASSERT_NE(view, nullptr);
    tap("view3D");
    EXPECT_EQ(view->property("view").toString(), "3d");
    const double yaw = view->property("yaw").toDouble();
    const double scale = view->property("scale").toDouble();

    QPointingDevice* touch = QTest::createTouchDevice();
    const QPoint centre = centreOf(view);
    // One finger: orbit.
    QTest::touchEvent(window_, touch).press(0, centre);
    for (int step = 1; step <= 10; ++step) {
        QTest::touchEvent(window_, touch).move(0, centre + QPoint(step * 8, 0));
    }
    QTest::touchEvent(window_, touch).release(0, centre + QPoint(80, 0));
    QCoreApplication::processEvents();
    EXPECT_GT(view->property("yaw").toDouble(), yaw + 10);

    // Two fingers spreading, 80 to 320 pixels apart, in fine steps as a touch
    // screen reports them: zoom in (the handler takes over past its drag
    // threshold, so a little less than 4x).
    QTest::touchEvent(window_, touch).press(0, centre - QPoint(40, 0)).press(1, centre + QPoint(40, 0));
    for (int step = 1; step <= 60; ++step) {
        QTest::touchEvent(window_, touch)
            .move(0, centre - QPoint(40 + step * 2, 0))
            .move(1, centre + QPoint(40 + step * 2, 0));
    }
    QTest::touchEvent(window_, touch).release(0, centre - QPoint(160, 0)).release(1, centre + QPoint(160, 0));
    QCoreApplication::processEvents();
    EXPECT_GT(view->property("scale").toDouble(), scale * 2.5);
    EXPECT_LT(view->property("scale").toDouble(), scale * 4.01);

    // Top, and Fit brings the scale back.
    tap("viewTop");
    EXPECT_EQ(view->property("view").toString(), "top");
    EXPECT_DOUBLE_EQ(view->property("pitch").toDouble(), 0);
    screenshot("ui_visualizer_top");
}

TEST_F(UiTest, TheDroZeroesTypesAndGoesTo) {
    connectSimulator();
    machine_->simulator()->setSpeed(50);
    // The wheel's X+ sector steps the Normal preset's 5 mm.
    QQuickItem* wheel = item("jogWheel");
    ASSERT_NE(wheel, nullptr);
    const QPointF xPlus = wheel->mapToScene(QPointF(wheel->width() * 0.92, wheel->height() / 2));
    QTest::mouseClick(window_, Qt::LeftButton, {}, xPlus.toPoint());
    ASSERT_TRUE(waitFor([&] { return text("workX") == "5.00" || item("workX")->property("text") == "5.00"; }));
    EXPECT_EQ(item("machineX")->property("text").toString(), "5.00");

    // X0 zeroes X: the work position reads 0, the machine's stays.
    tap("zeroX");
    ASSERT_TRUE(waitFor([&] { return item("workX")->property("text").toString() == "0.00"; }));
    EXPECT_EQ(item("machineX")->property("text").toString(), "5.00");

    // A typed position makes the current one read it.
    QQuickItem* workY = item("workY");
    tap("workY");
    ASSERT_TRUE(workY->hasActiveFocus());
    type("12.5");
    QTest::keyClick(window_, Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return workY->property("text").toString() == "12.50"; }));

    // Go To: absolute X 10.
    tap("goToButton");
    QObject* popup = window_->findChild<QObject*>("goToPopup");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(waitFor([&] { return popup->property("opened").toBool(); }));
    QQuickItem* goToX = item("goToX");
    ASSERT_NE(goToX, nullptr);
    tap("goToX");
    type("10");
    tap("goToGo");
    ASSERT_TRUE(waitFor([&] { return item("workX")->property("text").toString() == "10.00"; }, 10000));

    // The workspace follows the modal state; G55 selected through the model.
    EXPECT_EQ(item("workspaceSelector")->property("current").toInt(), 0);
}

TEST_F(UiTest, TheJogControlsStepHoldAndChangePresets) {
    connectSimulator();
    machine_->simulator()->setSpeed(50);
    // Presets and the - / + buttons.
    tap("presetRapid");
    EXPECT_EQ(item("jogFieldxy")->property("text").toString(), "20");
    tap("presetNormal");
    tap("jogPlusxy");
    EXPECT_EQ(item("jogFieldxy")->property("text").toString(), "6");
    tap("jogMinusfeedrate");
    EXPECT_EQ(item("jogFieldfeedrate")->property("text").toString(), "2000");

    // Z+ held: a continuous jog until released.
    QQuickItem* z = item("jogZ");
    const QPoint zPlus = z->mapToScene(QPointF(z->width() / 2, z->height() / 4)).toPoint();
    QTest::mousePress(window_, Qt::LeftButton, {}, zPlus);
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->state().status.activeState == "Jog"; }));
    QTest::mouseRelease(window_, Qt::LeftButton, {}, zPlus);
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->state().status.activeState == "Idle"; }));
    EXPECT_GT(machine_->machinePositionMm()[2], 0);

    // The stop button cancels a jog.
    QTest::mousePress(window_, Qt::LeftButton, {}, zPlus);
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->state().status.activeState == "Jog"; }));
    QQuickItem* stop = item("jogStop");
    QTest::mouseRelease(window_, Qt::LeftButton, {}, zPlus);
    QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(stop));
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->state().status.activeState == "Idle"; }));
    screenshot("ui_location");
}

TEST_F(UiTest, ZeroingAsksFirstWhenWarned) {
    app::AppSettings settings = machine_->settings();
    settings.warnZero = true;
    machine_->setSettings(settings);
    connectSimulator();
    machine_->simulator()->setSpeed(50);
    machine_->controller()->gcode("G0 X3");
    ASSERT_TRUE(waitFor([&] { return item("workX")->property("text").toString() == "3.00"; }));
    tap("zeroX");
    QObject* confirm = window_->findChild<QObject*>("confirmZero");
    ASSERT_NE(confirm, nullptr);
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    EXPECT_EQ(confirm->property("title").toString(), "Zero X Axis");
    tap("confirmCancel");
    ASSERT_TRUE(waitFor([&] { return !confirm->property("opened").toBool(); }));
    EXPECT_EQ(item("workX")->property("text").toString(), "3.00");
    tap("zeroX");
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    tap("confirmAction");
    EXPECT_TRUE(waitFor([&] { return item("workX")->property("text").toString() == "0.00"; }));
}

TEST_F(UiTest, TheFilePanelLoadsShowsAndClosesAFile) {
    // Without a file: Load File, and no recent files yet.
    EXPECT_FALSE(item("fileName")->isVisible());
    const QString path = dir_.path() + "/square.nc";
    {
        QFile file(path);
        ASSERT_TRUE(file.open(QIODevice::WriteOnly));
        file.write("G21 G90\nM3 S12000\nG0 X0 Y0\nG1 Z-1 F300\nG1 X50 F800\nG1 Y20\nM5\n");
    }
    QObject* model = item("fileControl")->property("model").value<QObject*>();
    ASSERT_NE(model, nullptr);
    QString error;
    ASSERT_TRUE(QMetaObject::invokeMethod(model, "load", Q_RETURN_ARG(QString, error), Q_ARG(QString, path)));
    EXPECT_EQ(error, "");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing() && item("fileName")->isVisible(); }));
    EXPECT_EQ(text("fileName"), "square");
    EXPECT_EQ(text("fileSize"), "61 Bytes (7 lines)");
    EXPECT_EQ(model->property("feedText").toString(), "300-800 mm/min");
    EXPECT_EQ(model->property("speedText").toString(), "12000-12000 RPM");
    EXPECT_EQ(model->property("recentFiles").toList().size(), 1);
    // Size: the extent.
    const QVariantList extent = model->property("extent").toList();
    ASSERT_EQ(extent.size(), 3);
    EXPECT_EQ(extent[0].toMap()["size"].toString(), "50.00");
    EXPECT_EQ(extent[2].toMap()["min"].toString(), "-1.00");
    screenshot("ui_file");

    // Close asks first.
    tap("closeFile");
    QObject* confirm = window_->findChild<QObject*>("confirmCloseFile");
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    QQuickItem* action = nullptr;
    ASSERT_TRUE(waitFor([&] {
        action = item("confirmAction");
        return action && action->isVisible() && action->width() > 0;
    }));
    ASSERT_NE(action, nullptr);
    QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(action));
    EXPECT_TRUE(waitFor([&] { return !machine_->hasProgram(); }));
    // The recent file is offered again.
    EXPECT_TRUE(item("recentFiles")->isVisible());
}

TEST_F(UiTest, TheJobControlsRunPauseStopAndOverride) {
    connectSimulator();
    machine_->loadProgram("job.nc", "G21 G90\nG1 Z-1 F200\nG1 X20 F200\nG1 Y20\nG1 X0\nG1 Y0\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    QQuickItem* start = item("startJob");
    ASSERT_TRUE(waitFor([&] { return start->isEnabled(); }));
    EXPECT_FALSE(item("pauseJob")->isEnabled());
    EXPECT_FALSE(item("stopJob")->isEnabled());
    EXPECT_TRUE(item("outlineJob")->isVisible());

    tap("startJob");
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->workflow().isRunning(); }));
    ASSERT_TRUE(waitFor([&] { return item("progressArea")->isVisible(); }));
    EXPECT_FALSE(item("outlineJob")->isVisible());
    ASSERT_TRUE(waitFor([&] { return item("pauseJob")->isEnabled(); }));
    screenshot("ui_job_running");

    // Overrides: + raises the feed by 10 %.
    tap("feedOverridePlus");
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->state().status.overrides[0] == 110; }));
    EXPECT_TRUE(waitFor([&] { return text("feedOverridePercent") == "110%"; }));
    tap("feedOverrideReset");
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->state().status.overrides[0] == 100; }));

    // Pause, resume with Start, stop.
    tap("pauseJob");
    ASSERT_TRUE(waitFor([&] {
        return machine_->controller()->workflow().state() == controller::WorkflowState::Paused;
    }));
    ASSERT_TRUE(waitFor([&] { return item("startJob")->isEnabled(); }));  // once the board holds
    tap("startJob");
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->workflow().isRunning(); }));
    ASSERT_TRUE(waitFor([&] { return item("stopJob")->isEnabled(); }));
    tap("stopJob");
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->workflow().isIdle(); }, 8000));
    QObject* summary = window_->findChild<QObject*>("jobEndSummary");
    ASSERT_TRUE(waitFor([&] { return summary->property("opened").toBool(); }));
    tap("jobEndClose");
    ASSERT_TRUE(waitFor([&] { return !summary->property("visible").toBool(); }));

    // Start From Line: the popup, then the job from line 4.
    ASSERT_TRUE(waitFor([&] { return item("startFromLine")->isVisible(); }, 8000));
    tap("startFromLine");
    QObject* popup = window_->findChild<QObject*>("startFromLinePopup");

    ASSERT_TRUE(waitFor([&] { return popup->property("opened").toBool(); }));
    QQuickItem* startButton = nullptr;
    ASSERT_TRUE(waitFor([&] {
        startButton = item("startFromLineStart");  // the overlay is under the root item too
        return startButton && startButton->width() > 0;
    }));
    QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(startButton));
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->workflow().isRunning(); }));
}

TEST_F(UiTest, AnAlarmExplainsItselfAndUnlocks) {
    connectSimulator();
    EXPECT_FALSE(item("unlockButton")->isVisible());
    machine_->simulator()->triggerAlarm(3);
    ASSERT_TRUE(waitFor([&] { return text("statusText") == "Alarm (3)"; }));
    ASSERT_TRUE(waitFor([&] { return item("unlockButton")->isVisible(); }));

    // The ? opens the Helper with the alarm's description.
    EXPECT_FALSE(item("helperPanel")->isVisible());
    tap("alarmHelp");
    ASSERT_TRUE(waitFor([&] { return item("helperPanel")->isVisible(); }));
    EXPECT_TRUE(text("helperTitle").contains("Alarm"));
    EXPECT_FALSE(text("helperText").isEmpty());
    screenshot("ui_alarm");
    tap("helperClose");
    EXPECT_TRUE(waitFor([&] { return !item("helperPanel")->isVisible(); }));

    tap("unlockButton");
    EXPECT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));
}

TEST_F(UiTest, MachineInformationShowsTheFirmwareAndPins) {
    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));
    tap("machineInfoButton");
    QObject* popup = window_->findChild<QObject*>("machineInfo");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(waitFor([&] { return popup->property("opened").toBool(); }));
    EXPECT_NE(item("stepperLock"), nullptr);
    screenshot("ui_machine_info");
}

TEST_F(UiTest, NotificationsPopUpAndCollectInTheBell) {
    EXPECT_FALSE(item("unreadErrors")->isVisible());
    backend_->notify("Something went wrong", "error");
    backend_->notify("All good", "success");
    QCoreApplication::processEvents();
    QQuickItem* toasts = item("toastArea");
    ASSERT_TRUE(waitFor([&] { return toasts->property("count").toInt() == 2; }));
    EXPECT_TRUE(waitFor([&] { return item("unreadErrors")->isVisible(); }));
    screenshot("ui_toasts");

    // At most three at once.
    backend_->notify("Three", "info");
    backend_->notify("Four", "info");
    EXPECT_TRUE(waitFor([&] { return toasts->property("count").toInt() == 3; }));

    // The bell lists them and reads them on opening.
    tap("notificationBell");
    QObject* panel = window_->findChild<QObject*>("notificationPanel");
    ASSERT_TRUE(waitFor([&] { return panel->property("opened").toBool(); }));
    EXPECT_FALSE(item("unreadErrors")->isVisible());
    QQuickItem* list = nullptr;
    ASSERT_TRUE(waitFor([&] { return (list = item("notificationList")) != nullptr; }));
    EXPECT_EQ(list->property("count").toInt(), 4);
    tap("notificationTab_error");
    EXPECT_TRUE(waitFor([&] { return list->property("count").toInt() == 1; }));
    tap("clearNotifications");
    EXPECT_TRUE(waitFor([&] { return list->property("count").toInt() == 0; }));
}

TEST_F(UiTest, AJobsEndShowsItsSummary) {
    connectSimulator();
    machine_->simulator()->setSpeed(50);
    machine_->loadProgram("job.nc", "G21 G90\nG1 X2 F500\nG1 X0\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    ASSERT_TRUE(waitFor([&] { return item("startJob")->isEnabled(); }));
    tap("startJob");
    QObject* summary = window_->findChild<QObject*>("jobEndSummary");
    ASSERT_NE(summary, nullptr);
    ASSERT_TRUE(waitFor([&] { return summary->property("opened").toBool(); }, 10000));
    EXPECT_EQ(text("jobEndStatus"), "COMPLETE");
    screenshot("ui_job_end");
    tap("jobEndClose");
    EXPECT_TRUE(waitFor([&] { return !summary->property("visible").toBool(); }));
}

TEST_F(UiTest, TheConsoleShowsFiltersAndSendsCommands) {
    selectTool("console");
    ASSERT_TRUE(waitFor([&] { return item("consoleTab") && item("consoleTab")->isVisible(); }));
    EXPECT_TRUE(item("consoleDisconnected")->isVisible());
    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return !item("consoleDisconnected")->isVisible(); }));
    QObject* model = item("consoleTab")->property("model").value<QObject*>();
    ASSERT_NE(model, nullptr);
    const auto shown = [&] {
        QStringList lines;
        QMetaObject::invokeMethod(model, "shownLines", Q_RETURN_ARG(QStringList, lines));
        return lines;
    };

    // A command typed and run shows as sent, and its reply follows.
    item("consoleInput")->forceActiveFocus();
    type("$I");
    tap("consoleSend");
    ASSERT_TRUE(waitFor([&] { return shown().contains("$I"); }));
    EXPECT_TRUE(waitFor([&] { return shown().contains("ok"); }));
    EXPECT_TRUE(item("consoleInput")->property("text").toString().isEmpty());

    // Up brings it back.
    item("consoleInput")->forceActiveFocus();
    QTest::keyClick(window_, Qt::Key_Up);
    EXPECT_EQ(item("consoleInput")->property("text").toString(), "$I");
    QTest::keyClick(window_, Qt::Key_Down);
    EXPECT_TRUE(item("consoleInput")->property("text").toString().isEmpty());
    screenshot("ui_console");

    // G-code only: what was sent.
    tap("consoleFilter_gcode");
    ASSERT_TRUE(waitFor([&] { return !shown().contains("ok"); }));
    EXPECT_TRUE(shown().contains("$I"));
    tap("consoleFilter_all");
    ASSERT_TRUE(waitFor([&] { return shown().contains("ok"); }));

    tap("consoleClear");
    EXPECT_TRUE(waitFor([&] { return shown().isEmpty(); }));
}

TEST_F(UiTest, CoolantTurnsOnAndOff) {
    connectSimulator();
    selectTool("coolant");
    ASSERT_TRUE(waitFor([&] { return item("coolantTab") && item("coolantTab")->isVisible(); }));
    const auto coolant = [&] { return machine_->controller()->state().parserState.modal.coolant; };
    tap("coolantMist");
    ASSERT_TRUE(waitFor([&] { return item("coolantMist")->property("active").toBool(); }));
    tap("coolantFlood");
    ASSERT_TRUE(waitFor([&] { return item("coolantFlood")->property("active").toBool(); }));
    EXPECT_EQ(coolant().size(), 2u);
    screenshot("ui_coolant");
    tap("coolantOff");
    EXPECT_TRUE(waitFor([&] { return !item("coolantMist")->property("active").toBool(); }));
    EXPECT_FALSE(item("coolantFlood")->property("active").toBool());
}

TEST_F(UiTest, TheSpindleRunsAndTheLaserTakesItsPlace) {
    // Hidden until the settings show it.
    EXPECT_EQ(item("toolsTab_spindle"), nullptr);
    app::AppSettings settings = machine_->settings();
    settings.spindleFunctions = true;
    machine_->setSettings(settings);
    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return item("toolsTab_spindle") != nullptr; }));
    selectTool("spindle");
    ASSERT_TRUE(waitFor([&] { return item("spindleTab") && item("spindleTab")->isVisible(); }));
    const auto spindle = [&] { return machine_->controller()->state().parserState.modal.spindle; };

    tap("spindleForward");
    ASSERT_TRUE(waitFor([&] { return spindle() == "M3"; }));
    EXPECT_TRUE(waitFor([&] { return item("spindleForward")->property("active").toBool(); }));
    screenshot("ui_spindle");
    tap("spindleStop");
    ASSERT_TRUE(waitFor([&] { return spindle() == "M5"; }));

    // The speed: kept in the settings once it settles.
    QObject* model = item("spindleTab")->property("model").value<QObject*>();
    QMetaObject::invokeMethod(model, "setSpeed", Q_ARG(double, 500));
    QMetaObject::invokeMethod(model, "applyNow");
    EXPECT_EQ(machine_->settings().spindle.speed, 500);
    EXPECT_TRUE(waitFor([&] { return text("spindleSpeedText") == "500 RPM"; }));
    // Within the board's $31..$30 (Grbl's default $30 is 1000).
    QMetaObject::invokeMethod(model, "setSpeed", Q_ARG(double, 12000));
    EXPECT_TRUE(waitFor([&] { return text("spindleSpeedText") == "1000 RPM"; }));

    // Laser mode: the laser's controls.
    EXPECT_FALSE(item("laserOn")->isVisible());
    tap("laserModeSwitch");
    ASSERT_TRUE(waitFor([&] { return machine_->laserMode(); }));
    ASSERT_TRUE(waitFor([&] { return item("laserOn")->isVisible(); }));
    EXPECT_FALSE(item("spindleForward")->isVisible());
    ASSERT_TRUE(waitFor([&] { return item("laserOn")->isEnabled(); }));
    tap("laserOn");
    EXPECT_TRUE(waitFor([&] { return item("laserOn")->property("active").toBool(); }));
    screenshot("ui_laser");
    tap("laserOff");
    EXPECT_TRUE(waitFor([&] { return spindle() == "M5"; }));
}

TEST_F(UiTest, MacrosAreAddedRunMovedAndDeleted) {
    connectSimulator();
    selectTool("macros");
    ASSERT_TRUE(waitFor([&] { return item("macrosTab") && item("macrosTab")->isVisible(); }));

    // Add one through the form.
    tap("macroAdd");
    QObject* form = item("macrosTab")->findChild<QObject*>("macroForm");
    ASSERT_NE(form, nullptr);
    ASSERT_TRUE(waitFor([&] { return form->property("opened").toBool(); }));
    item("macroName")->forceActiveFocus();
    type("Probe corner");
    item("macroContent")->setProperty("text", "G91\nG0 X1\nG90");
    tap("macroSubmit");
    ASSERT_TRUE(waitFor([&] { return !form->property("visible").toBool(); }));
    ASSERT_EQ(machine_->macros().list().size(), 1u);
    EXPECT_EQ(machine_->macros().list()[0].name, "Probe corner");
    ASSERT_TRUE(waitFor([&] { return item("macro_Probe corner") != nullptr; }));
    machine_->macros().create("Second", "G0 X0");
    Q_EMIT machine_->macrosChanged();
    ASSERT_TRUE(waitFor([&] { return item("macro_Second") != nullptr; }));
    screenshot("ui_macros");

    // Run it: the moves reach the machine.
    tap("macro_Probe corner");
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->state().status.mpos[0] > 0.5; }, 8000));

    // Moved to the other column.
    QObject* model = item("macrosTab")->property("model").value<QObject*>();
    const std::string id = machine_->macros().list()[0].id;
    const std::string from = machine_->macros().find(id)->column;
    const QString to = from == "column1" ? "column2" : "column1";
    QMetaObject::invokeMethod(model, "move", Q_ARG(QString, QString::fromStdString(id)), Q_ARG(QString, to),
                              Q_ARG(int, 0));
    EXPECT_EQ(machine_->macros().find(id)->column, to.toStdString());
    EXPECT_EQ(machine_->macros().find(id)->rowIndex, 0);
    // The old column's delegates go later; find the new ones.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

    // Deleted after confirming.
    // The pop-ups sit over the tool area's right edge.
    item("toastArea")->setProperty("toasts", QVariantList());
    tap("macroMenu_Probe corner");
    QQuickItem* remove = nullptr;
    ASSERT_TRUE(waitFor([&] { return (remove = item("macroDelete")) && remove->isVisible(); }));
    QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(remove));
    QObject* confirm = item("macrosTab")->findChild<QObject*>("confirmDeleteMacro");
    ASSERT_NE(confirm, nullptr);
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    QMetaObject::invokeMethod(confirm, "accepted");
    EXPECT_TRUE(waitFor([&] { return machine_->macros().list().size() == 1u; }));
}

TEST_F(UiTest, TheProbeTabZeroesTheCornerOfTheSimulatedStock) {
    connectSimulator();
    machine_->simulator()->setSpeed(200);
    ASSERT_TRUE(waitFor([&] { return item("probeTab") && item("probeTab")->isVisible(); }));
    QObject* model = item("probeTab")->property("model").value<QObject*>();

    // XYZ needs the tool: 6.35 mm to start with; a custom one is kept.
    EXPECT_FALSE(item("probeTool")->isVisible());
    tap("probeRoutine_XYZ");
    ASSERT_TRUE(waitFor([&] { return model->property("commandId").toString() == "XYZ Touch"; }));
    ASSERT_TRUE(waitFor([&] { return item("probeTool")->isVisible(); }));
    EXPECT_EQ(model->property("tool").toString(), "6.35");
    tap("probeTool");
    ASSERT_TRUE(waitFor([&] { return item("probeCustomTool") && item("probeCustomTool")->isVisible(); }));
    item("probeCustomTool")->forceActiveFocus();
    type("4");
    tap("probeAddTool");
    ASSERT_TRUE(waitFor([&] { return model->property("tool").toString() == "4"; }));
    const auto& tools = machine_->settings().probeTools;
    EXPECT_TRUE(std::any_of(tools.begin(), tools.end(), [](const auto& t) { return t.metric == 4; }));
    QMetaObject::invokeMethod(model, "selectTool", Q_ARG(QString, "6.35"));
    EXPECT_EQ(model->property("cornerName").toString(), "Bottom left");
    screenshot("ui_probe");

    // The run step: waits for the circuit, then zeroes the corner.
    const sim::SimAxes start = machine_->simulator()->machinePosition();
    tap("probeButton");
    QObject* run = item("probeTab")->findChild<QObject*>("runProbe");
    ASSERT_NE(run, nullptr);
    ASSERT_TRUE(waitFor([&] { return run->property("opened").toBool(); }));
    QQuickItem* startButton = nullptr;
    ASSERT_TRUE(waitFor([&] { return (startButton = item("startProbe")) && startButton->width() > 0; }));
    EXPECT_FALSE(startButton->isEnabled());
    // Wrapped text settles on the second layout pass.
    ASSERT_TRUE(waitFor([&] { return centreOf(startButton).y() < window_->height(); }));
    screenshot("ui_probe_run");
    tap("confirmProbe");
    ASSERT_TRUE(waitFor([&] { return startButton->isEnabled(); }));
    tap("startProbe");
    ASSERT_TRUE(waitFor([&] { return !run->property("visible").toBool(); }));
    ASSERT_TRUE(waitFor([&] {
        controller::Controller* c = machine_->controller();
        return c->feeder().size() == 0 && !c->feeder().isPending() && machine_->simulator()->activeState() == "Idle";
    }, 10000));
    const sim::SimAxes offset = machine_->simulator()->workOffset();
    EXPECT_NEAR(offset[0], start[0] + 5, 1e-6);
    EXPECT_NEAR(offset[1], start[1] + 5, 1e-6);
    EXPECT_NEAR(offset[2], start[2] - 25, 1e-6);
}

TEST_F(UiTest, TheRotaryTabSwitchesModeAndLoadsTheMountingSetup) {
    app::AppSettings settings = machine_->settings();
    settings.rotary.showControls = true;
    machine_->setSettings(settings);
    connectSimulator();
    selectTool("rotary");
    ASSERT_TRUE(waitFor([&] { return item("rotaryTab") && item("rotaryTab")->isVisible(); }));

    // Grbl: the rotary is only there in rotary mode.
    EXPECT_FALSE(item("probeRotaryZ")->isEnabled());
    ASSERT_TRUE(waitFor([&] { return item("mountingSetupButton")->isEnabled(); }));
    screenshot("ui_rotary");

    // Mounting Setup: the choices, then the program as the job.
    tap("mountingSetupButton");
    QObject* mounting = item("rotaryTab")->findChild<QObject*>("mountingSetup");
    ASSERT_TRUE(waitFor([&] { return mounting->property("opened").toBool(); }));
    QQuickItem* linesUp = nullptr;
    ASSERT_TRUE(waitFor([&] {
        linesUp = item("mounting_Lines up");
        return linesUp && centreOf(linesUp).y() < window_->height();
    }));
    tap("mounting_Lines up");
    EXPECT_TRUE(item("mountingIllustration")->property("source").toString().endsWith("standard-track-top-view.png"));
    tap("mounting_10");
    EXPECT_TRUE(item("mountingIllustration")->property("source").toString().endsWith("extension-track-top-view.png"));
    screenshot("ui_mounting_setup");
    tap("mountingLoad");
    ASSERT_TRUE(waitFor([&] { return !mounting->property("visible").toBool(); }));
    EXPECT_TRUE(waitFor([&] { return machine_->programName() == "gSender_Rotary_Mounting_Setup"; }));

    // Rotary mode: asked first.
    tap("rotaryModeSwitch");
    QObject* confirm = item("rotaryTab")->findChild<QObject*>("confirmRotaryMode");
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    EXPECT_FALSE(machine_->rotaryMode());
    EXPECT_FALSE(item("rotaryModeSwitch")->property("checked").toBool());
    QMetaObject::invokeMethod(confirm, "accepted");
    ASSERT_TRUE(waitFor([&] { return machine_->rotaryMode(); }));
    EXPECT_TRUE(waitFor([&] { return item("rotaryModeSwitch")->property("checked").toBool(); }));
    EXPECT_FALSE(item("mountingSetupButton")->isEnabled());
    EXPECT_TRUE(waitFor([&] { return item("probeRotaryZ")->isEnabled(); }, 8000));
}

TEST_F(UiTest, AToolChangeWizardCarriesTheJobThroughItsToolChange) {
    app::AppSettings settings = machine_->settings();
    settings.toolChange.option = "Standard Re-zero";
    machine_->setSettings(settings);
    connectSimulator();
    machine_->simulator()->setSpeed(20);
    machine_->loadProgram("tools.nc", "G21 G90\nG0 X5 Z-2\nM6 T2\nG0 X10\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    ASSERT_TRUE(waitFor([&] { return item("startJob")->isEnabled(); }));
    tap("startJob");

    // The M6 pauses the job and opens the wizard.
    QQuickItem* wizard = item("toolChangeWizard");
    ASSERT_TRUE(waitFor([&] { return wizard->isVisible(); }, 8000));
    EXPECT_TRUE(machine_->controller()->workflow().isPaused());
    QObject* model = item("toolChange")->property("model").value<QObject*>();
    const auto step = [&] { return model->property("step").toInt(); };
    ASSERT_TRUE(waitFor([&] { return model->property("ready").toBool(); }, 8000));

    // Minimised to a pill and back.
    tap("toolChangeMinimize");
    ASSERT_TRUE(waitFor([&] { return item("toolChangePill")->isVisible() && !wizard->isVisible(); }));
    tap("toolChangeRestore");
    ASSERT_TRUE(waitFor([&] { return wizard->isVisible(); }));

    // Safety First, then Change Bit: an instruction with actions passes
    // only once one ran.
    ASSERT_TRUE(waitFor([&] { return item("toolChangeNext")->isEnabled(); }));
    tap("toolChangeNext");
    ASSERT_TRUE(waitFor([&] { return item("toolChangeNext")->isEnabled(); }));
    tap("toolChangeNext");
    ASSERT_TRUE(waitFor([&] { return step() == 1; }));
    EXPECT_FALSE(item("toolChangeNext")->isEnabled());
    EXPECT_EQ(text("toolBannerTool"), "T2");
    screenshot("ui_toolchange");
    tap("toolChangeAction_1");  // Set Z0 (paper method)
    ASSERT_TRUE(waitFor([&] { return step() == 2; }, 8000));
    EXPECT_NEAR(machine_->simulator()->workOffset()[2], -2, 1e-9);

    // Resume Job: the wizard closes and the job runs on.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_TRUE(waitFor([&] { return item("toolChangeAction_0") && item("toolChangeAction_0")->isEnabled(); }));
    tap("toolChangeAction_0");
    ASSERT_TRUE(waitFor([&] { return !model->property("active").toBool(); }, 8000));
    ASSERT_TRUE(waitFor([&] {
        return machine_->controller()->workflow().isIdle() && machine_->simulator()->machinePosition()[0] > 9.99;
    }, 8000));
}

TEST_F(UiTest, StepThroughWalksTheFileLineByLine) {
    QString program = "G21 G90\nT1 M6 (6mm endmill)\nS12000 M3\n";
    for (int i = 0; i < 150; ++i) {
        program += QString("G1 X%1 Y%2 F800\n").arg(i % 20).arg(i / 10);
    }
    program += "T2 M6\nG1 Z-1\nG0 X0 Y0\nM5\n";
    machine_->loadProgram("steps.nc", program.toStdString());
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    ASSERT_TRUE(waitFor([&] { return item("openStepThrough") && item("openStepThrough")->isVisible(); }));
    tap("openStepThrough");
    QObject* dialog = window_->findChild<QObject*>("stepThrough");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    QObject* model = dialog->property("model").value<QObject*>();
    bool ready = false;
    QMetaObject::invokeMethod(model, "waitForIndex", Q_RETURN_ARG(bool, ready), Q_ARG(int, 5000));
    ASSERT_TRUE(ready);
    EXPECT_EQ(model->property("total").toInt(), 157);
    EXPECT_EQ(model->property("tools").toList().size(), 2);
    const auto line = [&] { return model->property("line").toInt(); };

    // +100 lines, then back to the start.
    ASSERT_TRUE(waitFor([&] { return item("stepBy_100") && centreOf(item("stepBy_100")).y() < window_->height(); }));
    tap("stepBy_100");
    EXPECT_EQ(line(), 101);
    EXPECT_TRUE(waitFor([&] { return text("stepLineText") == "G1 X17 Y9 F800"; }));
    EXPECT_TRUE(model->property("positionText").toString().startsWith("X 17.00   Y 9.00"));
    screenshot("ui_step_through");
    tap("stepGoToStart");
    EXPECT_EQ(line(), 1);

    // Search: Enter goes to the next match.
    tap("stepSearchToggle");
    type("T2");
    EXPECT_EQ(model->property("matchCount").toInt(), 1);
    QTest::keyClick(window_, Qt::Key_Return);
    EXPECT_EQ(line(), 154);

    // A tool's eye; the second tool is where the line is.
    EXPECT_EQ(model->property("activeTool").toInt(), 1);
    tap("stepToolEye_0");
    EXPECT_TRUE(model->property("tools").toList()[0].toMap()["hidden"].toBool());

    // Play from the start runs on by itself.
    tap("stepGoToStart");
    tap("stepSpeed_100");
    tap("stepPlay");
    ASSERT_TRUE(model->property("playing").toBool());
    EXPECT_TRUE(waitFor([&] { return line() > 1; }));
    tap("stepPlay");
    EXPECT_FALSE(model->property("playing").toBool());
    tap("stepThroughClose");
    EXPECT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
}

TEST_F(UiTest, TheEditorEditsSearchesAndSavesTheJob) {
    machine_->loadProgram("edit.nc", "G21 G90\nG0 X1\nG1 X2 F100\nG1 X3\nM30\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    ASSERT_TRUE(waitFor([&] { return item("openEditor") && item("openEditor")->isVisible(); }));
    tap("openEditor");
    QQuickItem* editor = item("gcodeEditor");
    ASSERT_TRUE(waitFor([&] { return editor->isVisible(); }));
    QObject* model = editor->property("model").value<QObject*>();
    EXPECT_EQ(model->property("count").toInt(), 5);
    ASSERT_TRUE(waitFor([&] { return item("editorLine_1") != nullptr; }));
    screenshot("ui_editor");

    // Tap a line's text to edit it.
    QQuickItem* line = item("editorLine_1");
    QTest::mouseClick(window_, Qt::LeftButton, {}, line->mapToScene(QPointF(line->width() / 2, line->height() / 2)).toPoint());
    ASSERT_TRUE(waitFor([&] { return editor->property("editingRow").toInt() == 1; }));
    type("G0 X9");
    QTest::keyClick(window_, Qt::Key_Return);
    ASSERT_TRUE(waitFor([&] { return editor->property("editingRow").toInt() == -1; }));
    EXPECT_TRUE(model->property("hasChanges").toBool());

    // Select two lines (the second with Shift: the range) and delete them.
    tap("editorSelect_3");
    QQuickItem* box = item("editorSelect_4");
    QTest::mouseClick(window_, Qt::LeftButton, Qt::ShiftModifier, centreOf(box));
    EXPECT_EQ(model->property("selectedCount").toInt(), 2);
    tap("editorDelete");
    EXPECT_EQ(model->property("count").toInt(), 3);

    // Search.
    tap("editorSearchButton");
    type("x");
    EXPECT_EQ(model->property("matchCount").toInt(), 2);
    EXPECT_EQ(text("editorMatches"), "1/2");
    tap("editorNextMatch");
    EXPECT_EQ(text("editorMatches"), "2/2");

    // Save: the edited text is the job now.
    tap("editorSave");
    EXPECT_TRUE(waitFor([&] { return machine_->programText() == "G21 G90\nG0 X9\nG1 X2 F100"; }))
        << machine_->programText();
    EXPECT_FALSE(model->property("hasChanges").toBool());
    tap("editorClose");
    EXPECT_FALSE(editor->isVisible());
}

TEST_F(UiTest, KeyboardShortcutsJogZeroAndReachTheScreens) {
    connectSimulator();
    machine_->simulator()->setSpeed(50);
    window_->requestActivate();
    ASSERT_TRUE(waitFor([&] { return window_->isActive(); }));
    const auto mpos = [&] { return machine_->controller()->state().status.mpos; };

    // Shift+Right held jogs X+, released stops.
    QTest::keyPress(window_, Qt::Key_Right, Qt::ShiftModifier);
    ASSERT_TRUE(waitFor([&] { return mpos()[0] > 0.5; }));
    QTest::keyRelease(window_, Qt::Key_Right, Qt::ShiftModifier);
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->state().status.activeState == "Idle"; }, 8000));

    // Shift+W zeroes X.
    QTest::keyClick(window_, Qt::Key_W, Qt::ShiftModifier);
    EXPECT_TRUE(waitFor([&] { return std::abs(machine_->controller()->state().status.wpos[0]) < 1e-6; }));

    // Not while typing into a field.
    selectTool("console");
    ASSERT_TRUE(waitFor([&] { return item("consoleInput") && item("consoleInput")->isVisible(); }));
    const double x = mpos()[0];
    item("consoleInput")->forceActiveFocus();
    QTest::keyClick(window_, Qt::Key_Right, Qt::ShiftModifier);
    QTest::qWait(300);
    EXPECT_EQ(mpos()[0], x);
    window_->contentItem()->forceActiveFocus();

    // A screen's shortcut: Machine Information.
    auto* shortcuts = window_->findChild<ui::UiShortcuts*>();
    ASSERT_NE(shortcuts, nullptr);
    QObject* info = window_->findChild<QObject*>("machineInfo");
    EXPECT_TRUE(shortcuts->manager().trigger("DISPLAY_MACHINE_INFO"));
    EXPECT_TRUE(waitFor([&] { return info->property("opened").toBool(); }));
}

TEST_F(UiTest, TheStatsPageShowsTheRecordAndManagesMaintenance) {
    // A record to show: a job, a task, an alarm.
    config::ConfigStore& store = machine_->config();
    config::JobRecord job;
    job.file = "sign.nc";
    job.totalLines = 1200;
    job.port = "Simulator";
    job.startTime = 1'790'000'000'000;
    job.duration = 65'000;
    job.completed = true;
    config::JobStatsStore(store).record(job, 65'000);
    config::AlarmRecord alarm;
    alarm.alarm = true;
    alarm.code = "2";
    alarm.source = "Console";
    alarm.time = 1'790'000'100'000;
    alarm.message = "Soft limit";
    config::AlarmHistory(store).record(alarm);
    Q_EMIT machine_->historyChanged();
    connectSimulator();

    tap("navStats");
    ASSERT_TRUE(waitFor([&] { return item("statsOverview") && item("statsOverview")->isVisible(); }));
    QObject* model = item("statsPage")->property("model").value<QObject*>();
    ASSERT_TRUE(waitFor([&] { return model->property("recentJobs").toList().size() == 1; }));
    EXPECT_EQ(model->property("alarmPreview").toList().size(), 1);
    EXPECT_FALSE(model->property("releases").toList().isEmpty());
    screenshot("ui_stats");

    // Jobs: searched.
    tap("statMenu_jobs");
    ASSERT_TRUE(waitFor([&] { return item("statsJobs")->isVisible(); }));
    QQuickItem* jobs = item("jobHistory");
    EXPECT_EQ(jobs->property("count").toInt(), 1);
    item("jobSearch")->forceActiveFocus();
    type("nothing");
    EXPECT_EQ(jobs->property("count").toInt(), 0);

    // Maintenance: add a task through the form, then reset it (asked first).
    tap("statMenu_maintenance");
    ASSERT_TRUE(waitFor([&] { return item("statsMaintenance")->isVisible(); }));
    const auto tasks = [&] { return config::MaintenanceStore(store).list(); };
    const std::size_t before = tasks().size();
    tap("addTask");
    QObject* dialog = item("statsPage")->findChild<QObject*>("maintenanceTaskDialog");
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    tap("submitTask");  // empty: refused
    EXPECT_TRUE(dialog->property("opened").toBool());
    item("taskName")->forceActiveFocus();
    type("Oil rails");
    item("taskRangeStart")->forceActiveFocus();
    type("10");
    item("taskRangeEnd")->forceActiveFocus();
    type("20");
    tap("submitTask");
    ASSERT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
    ASSERT_EQ(tasks().size(), before + 1);
    const int id = tasks().back().id;
    config::MaintenanceStore(store).addRunTime(5 * 3'600'000);
    QMetaObject::invokeMethod(model, "reload");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_TRUE(waitFor([&] { return item(QString("resetTask_%1").arg(id)) != nullptr; }));
    tap(QString("resetTask_%1").arg(id));
    QObject* confirm = item("statsPage")->findChild<QObject*>("statsConfirm");
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    QMetaObject::invokeMethod(confirm, "close");  // as its button does
    QMetaObject::invokeMethod(confirm, "accepted");
    EXPECT_EQ(tasks().back().currentTime, 0);
    screenshot("ui_stats_maintenance");

    // Alarms: cleared.
    tap("statMenu_alarms");
    ASSERT_TRUE(waitFor([&] { return item("statsAlarms")->isVisible(); }));
    tap("clearAlarms");
    ASSERT_TRUE(waitFor([&] { return confirm->property("opened").toBool(); }));
    QMetaObject::invokeMethod(confirm, "close");
    QMetaObject::invokeMethod(confirm, "accepted");
    EXPECT_TRUE(config::AlarmHistory(store).list().empty());
    tap("statMenu_about");
    ASSERT_TRUE(waitFor([&] { return item("statsAbout")->isVisible(); }));
}

TEST_F(UiTest, TheSurfacingToolGeneratesAndLoadsItsProgram) {
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_surfacing") && item("toolCard_surfacing")->isVisible(); }));
    screenshot("ui_tools");
    tap("toolCard_surfacing");
    ASSERT_TRUE(waitFor([&] { return item("surfacingTool") && item("surfacingTool")->isVisible(); }));
    QObject* model = item("surfacingTool")->property("model").value<QObject*>();
    // Whole numbers keep their zeros.
    EXPECT_EQ(text("surfacing_stepover"), "40");
    EXPECT_EQ(text("surfacing_spindleRPM"), "17000");

    // The stock: 200 wide, zig-zag from the front left.
    QQuickItem* width = item("surfacing_width");
    width->forceActiveFocus();
    QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
    type("200");
    QTest::keyClick(window_, Qt::Key_Return);
    EXPECT_EQ(model->property("options").toMap()["width"].toDouble(), 200);
    tap("surfacingPattern_zigzag");
    tap("surfacingStart_frontLeft");
    EXPECT_EQ(model->property("options").toMap()["pattern"].toString(), "zigzag");
    EXPECT_EQ(model->property("options").toMap()["startPosition"].toString(), "frontLeft");

    tap("surfacingGenerate");
    ASSERT_TRUE(waitFor([&] { return model->property("lines").toInt() > 10; }));
    EXPECT_FALSE(item("surfacingPreview")->property("empty").toBool());
    EXPECT_EQ(machine_->settings().surfacing.width, 200);  // kept
    screenshot("ui_surfacing");

    // Loaded as the job, back on the Carve page.
    tap("surfacingLoad");
    EXPECT_TRUE(waitFor([&] { return machine_->programName() == "gSender_Surfacing.gcode"; }));
    EXPECT_TRUE(waitFor([&] { return item("carvePage")->isVisible(); }));
}

TEST_F(UiTest, RotarySurfacingOpensFromTheRotaryTabAndGenerates) {
    app::AppSettings settings = machine_->settings();
    settings.rotary.showControls = true;
    settings.defaultFirmware = protocol::Firmware::GrblHal;  // the rotary is there outside rotary mode
    machine_->setSettings(settings);
    selectTool("rotary");
    ASSERT_TRUE(waitFor([&] { return item("rotarySurfacing") && item("rotarySurfacing")->isEnabled(); }));
    tap("rotarySurfacing");
    ASSERT_TRUE(waitFor([&] { return item("rotarySurfacingTool") && item("rotarySurfacingTool")->isVisible(); }));
    EXPECT_TRUE(item("toolsPage")->isVisible());
    QObject* model = item("rotarySurfacingTool")->property("model").value<QObject*>();
    tap("rotarySurfacingGenerate");
    ASSERT_TRUE(waitFor([&] { return model->property("lines").toInt() > 10; }));
    EXPECT_FALSE(item("rotarySurfacingPreview")->property("empty").toBool());
    screenshot("ui_rotary_surfacing");
    tap("toolGoBack");
    EXPECT_TRUE(waitFor([&] { return item("toolCard_surfacing") && item("toolCard_surfacing")->isVisible(); }));
}

TEST_F(UiTest, TheCalibrationToolsTuneAndSquareTheMachine) {
    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->runner().hasSettings(); }));
    machine_->simulator()->setSpeed(200);
    controller::Controller& c = *machine_->controller();
    const auto idleAt = [&](double x, double y) {
        const std::array<double, 4> p = machine_->machinePositionMm();
        return c.state().status.activeState == "Idle" && std::fabs(p[0] - x) < 1e-6 && std::fabs(p[1] - y) < 1e-6;
    };
    const auto enter = [&](const QString& name, const QString& value) {
        QQuickItem* field = item(name);
        ASSERT_NE(field, nullptr) << name.toStdString();
        field->forceActiveFocus();
        QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
        type(value);
        QTest::keyClick(window_, Qt::Key_Return);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    const auto confirm = [&](const QString& name) {
        QObject* dialog = item("toolsPage")->findChild<QObject*>(name);
        ASSERT_NE(dialog, nullptr) << name.toStdString();
        QMetaObject::invokeMethod(dialog, "close");  // as its button does
        QMetaObject::invokeMethod(dialog, "accepted");
    };

    // Movement Tuning: X told to move 100 mm, measured 102: $100 = 200 x 100/102.
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_movementTuning") && item("toolCard_movementTuning")->isVisible(); }));
    tap("toolCard_movementTuning");
    ASSERT_TRUE(waitFor([&] { return item("movementTuningTool") && item("movementTuningTool")->isVisible(); }));
    QObject* tuning = item("movementTuningTool")->property("model").value<QObject*>();
    ASSERT_TRUE(waitFor([&] { return item("tuningStart")->isEnabled(); }));
    screenshot("ui_movement_tuning_intro");
    tap("tuningStart");
    EXPECT_EQ(tuning->property("step").toString(), "mark");
    tap("tuningMark");
    ASSERT_TRUE(waitFor([&] { return item("tuningMove")->isEnabled(); }));
    tap("tuningMove");
    ASSERT_TRUE(waitFor([&] { return idleAt(100, 0); }));
    enter("tuningMeasured", "102");
    EXPECT_EQ(tuning->property("travelled").toDouble(), 102);
    tap("tuningTravelled");
    EXPECT_EQ(tuning->property("step").toString(), "result");
    EXPECT_TRUE(text("tuningResult").contains("off by <b>-2 mm.</b>")) << text("tuningResult").toStdString();
    ASSERT_TRUE(waitFor([&] { return item("tuningUpdate")->isVisible() && item("tuningUpdate")->isEnabled(); }));
    screenshot("ui_movement_tuning_result");
    tap("tuningUpdate");
    confirm("tuningConfirm");
    ASSERT_TRUE(waitFor([&] { return c.runner().setting("$100") == "196.08"; }));
    tap("toolGoBack");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_squaring") && item("toolCard_squaring")->isVisible(); }));
    item("toastArea")->setProperty("toasts", QVariantList());

    // XY Squaring: mark, move X, mark, move Y, mark; measure the triangle.
    tap("toolCard_squaring");
    ASSERT_TRUE(waitFor([&] { return item("squaringTool") && item("squaringTool")->isVisible(); }));
    QObject* squaring = item("squaringTool")->property("model").value<QObject*>();
    tap("squaringNext");
    EXPECT_EQ(text("squaringTitle"), "Mark Reference Points");
    tap("squaringRow_0");
    EXPECT_FALSE(item("squaringRow_2")->isEnabled());  // the X move comes first
    enter("squaringValue_1", "50");
    ASSERT_TRUE(waitFor([&] { return item("squaringRow_1") && item("squaringRow_1")->isEnabled(); }));
    EXPECT_TRUE(item("squaringArrow")->isVisible());
    screenshot("ui_squaring_marking");
    tap("squaringRow_1");
    ASSERT_TRUE(waitFor([&] { return idleAt(150, 0); }));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    tap("squaringRow_2");
    enter("squaringValue_3", "50");
    ASSERT_TRUE(waitFor([&] { return item("squaringRow_3") && item("squaringRow_3")->isEnabled(); }));
    tap("squaringRow_3");
    ASSERT_TRUE(waitFor([&] { return idleAt(150, 50); }));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    tap("squaringRow_4");
    EXPECT_EQ(squaring->property("markedPoints").toInt(), 3);
    tap("squaringNext");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_FALSE(item("squaringRow_0")->isEnabled());  // nothing measured yet
    enter("squaringValue_0", "49");
    tap("squaringRow_0");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    enter("squaringValue_1", "50");
    tap("squaringRow_1");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    enter("squaringValue_2", "71");
    tap("squaringRow_2");
    EXPECT_EQ(text("squaringNext"), "See Results");
    tap("squaringNext");
    ASSERT_EQ(squaring->property("mainStep").toInt(), 3);
    // 49 x 50 with a 71 diagonal: slightly out, 0.99 mm on the diagonal.
    EXPECT_TRUE(text("squaringResult").contains("slightly out of square")) << text("squaringResult").toStdString();
    EXPECT_TRUE(text("squaringResult").contains("0.99mm"));
    EXPECT_TRUE(item("squaringSide_2")->isVisible());
    ASSERT_TRUE(item("squaringUpdate")->isVisible());  // X moved 50 but measured 49
    screenshot("ui_squaring_results");
    tap("squaringUpdate");
    confirm("squaringConfirm");
    ASSERT_TRUE(waitFor([&] {
        return c.runner().setting("$100") == "200.082" && c.runner().setting("$101") == "200.000";
    }));
    item("toastArea")->setProperty("toasts", QVariantList());  // over Back
    tap("squaringBack");
    EXPECT_EQ(squaring->property("mainStep").toInt(), 2);
    tap("squaringRestart");
    EXPECT_EQ(squaring->property("mainStep").toInt(), 0);
}

TEST_F(UiTest, TheShortcutsToolRebindsAndResetsTheKeys) {
    window_->requestActivate();
    ASSERT_TRUE(waitFor([&] { return window_->isActive(); }));
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_shortcuts") && item("toolCard_shortcuts")->isVisible(); }));
    tap("toolCard_shortcuts");
    ASSERT_TRUE(waitFor([&] { return item("keyboardShortcutsTool") && item("keyboardShortcutsTool")->isVisible(); }));
    screenshot("ui_shortcuts");
    item("shortcutsSearch")->forceActiveFocus();
    type("jog x");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_TRUE(waitFor([&] { return item("shortcutKeys_JOG_X_P") && item("shortcutKeys_JOG_X_P")->isVisible(); }));

    // Shift+Left is Jog X-'s: refused. Alt+Right is free.
    tap("shortcutKeys_JOG_X_P");
    QObject* editor = item("keyboardShortcutsTool")->findChild<QObject*>("shortcutEditor");
    ASSERT_TRUE(waitFor([&] { return editor->property("opened").toBool() && item("shortcutCapture")->hasActiveFocus(); }));
    QTest::keyClick(window_, Qt::Key_Left, Qt::ShiftModifier);
    EXPECT_EQ(editor->property("keys").toString(), "Shift+Left");
    tap("shortcutSave");
    EXPECT_EQ(editor->property("conflict").toString(), "Jog X- (left)");
    EXPECT_TRUE(item("shortcutConflict")->isVisible());
    screenshot("ui_shortcut_conflict");
    QTest::keyClick(window_, Qt::Key_Right, Qt::AltModifier);
    EXPECT_EQ(editor->property("keys").toString(), "Alt+Right");
    tap("shortcutSave");
    ASSERT_TRUE(waitFor([&] { return !editor->property("visible").toBool(); }));
    EXPECT_EQ(machine_->settings().shortcuts.at("JOG_X_P").keys, "Alt+Right");
    auto* shortcuts = window_->findChild<ui::UiShortcuts*>();
    EXPECT_EQ(shortcuts->manager().actionFor(QKeyCombination(Qt::AltModifier, Qt::Key_Right)), "JOG_X_P");

    // Switched off, then everything back to the defaults.
    tap("shortcutActive_JOG_X_M");
    EXPECT_FALSE(machine_->settings().shortcuts.at("JOG_X_M").active);
    tap("shortcutsReset");
    QObject* reset = item("keyboardShortcutsTool")->findChild<QObject*>("shortcutsResetConfirm");
    QMetaObject::invokeMethod(reset, "close");
    QMetaObject::invokeMethod(reset, "accepted");
    EXPECT_TRUE(machine_->settings().shortcuts.empty());
    EXPECT_TRUE(item("shortcutActive_JOG_X_M")->property("checked").toBool());
    EXPECT_EQ(shortcuts->manager().actionFor(QKeyCombination(Qt::ShiftModifier, Qt::Key_Right)), "JOG_X_P");
}

TEST_F(UiTest, TheSdCardToolUploadsAndDeletesFilesOnAGrblHalCard) {
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_sd") && item("toolCard_sd")->isVisible(); }));
    tap("toolCard_sd");
    ASSERT_TRUE(waitFor([&] { return item("sdCardTool") && item("sdCardTool")->isVisible(); }));
    QObject* model = item("sdCardTool")->property("model").value<QObject*>();
    EXPECT_EQ(text("sdStatus"), "Disconnected");
    EXPECT_EQ(text("sdMessage"), "Must be connected to use SD card functionality.");
    EXPECT_FALSE(item("sdUpload")->isEnabled());

    backend_->connectSimulator(true);
    ASSERT_TRUE(waitFor([&] {
        return machine_->isConnected() && machine_->controller()->runner().hasSettings() && text("sdStatus") == "Mounted";
    }));
    EXPECT_EQ(text("sdMessage"), "No files found");
    ASSERT_TRUE(waitFor([&] { return item("sdUpload")->isEnabled(); }));

    // The modal keeps the files it can send and reports the others.
    const QString square = dir_.path() + "/square.nc";
    QFile file(square);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("G21 G90\nG1 X10 F1200\nG1 Y10\nG1 X0\nG1 Y0\n");
    file.close();
    tap("sdUpload");
    QObject* modal = item("sdCardTool")->findChild<QObject*>("sdUploadModal");
    ASSERT_TRUE(waitFor([&] { return modal->property("opened").toBool(); }));
    QMetaObject::invokeMethod(model, "addPending",
                              Q_ARG(QVariantList, (QVariantList{QUrl::fromLocalFile(square), dir_.path() + "/notes.md"})));
    ASSERT_EQ(model->property("pending").toList().size(), 1);
    ASSERT_FALSE(backend_->notificationCenter().list().empty());
    EXPECT_EQ(backend_->notificationCenter().list().front().message,
              "Some files were rejected:\nnotes.md: Invalid file type");
    ASSERT_TRUE(waitFor([&] { return item("sdUploadPending") && item("sdUploadPending")->isVisible(); }));
    EXPECT_EQ(text("sdUploadPending"), "Upload (1)");
    screenshot("ui_sd_upload");
    tap("sdUploadPending");

    // Up over YMODEM, then listed.
    ASSERT_TRUE(waitFor([&] { return model->property("uploadState").toString() != "idle"; }));
    ASSERT_TRUE(waitFor([&] { return machine_->simulator()->sdFiles().count("square.nc") == 1; }, 10000));
    ASSERT_TRUE(waitFor([&] { return item("sdFile_square.nc") != nullptr; }, 10000));
    ASSERT_TRUE(waitFor([&] { return model->property("uploadState").toString() == "idle"; }));
    EXPECT_EQ(text("sdFilesTitle"), "Files (1)");
    ASSERT_TRUE(waitFor([&] { return item("sdDelete_square.nc")->isEnabled(); }));
    screenshot("ui_sd_card");

    // Delete asks, and the file leaves the list at once.
    item("toastArea")->setProperty("toasts", QVariantList());
    tap("sdDelete_square.nc");
    QObject* confirm = item("sdCardTool")->findChild<QObject*>("sdDeleteConfirm");
    EXPECT_EQ(confirm->property("message").toString(), "Are you sure you want to delete square.nc?");
    QMetaObject::invokeMethod(confirm, "close");
    QMetaObject::invokeMethod(confirm, "accepted");
    EXPECT_EQ(text("sdMessage"), "No files found");
    ASSERT_TRUE(waitFor([&] { return machine_->simulator()->sdFiles().count("square.nc") == 0; }));
}

TEST_F(UiTest, TheAccessoryInstallerWalksTheVacuumTableAndTlsWizards) {
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_accessoryInstall") && item("toolCard_accessoryInstall")->isVisible(); }));
    tap("toolCard_accessoryInstall");
    ASSERT_TRUE(waitFor([&] { return item("wizard-vacuum-table") && item("wizard-vacuum-table")->isVisible(); }));
    screenshot("ui_accessories_hub");
    // Not connected: the landing page says why and keeps its configurations shut.
    tap("wizard-vacuum-table");
    ASSERT_TRUE(waitFor([&] { return item("wizardChecks") && item("wizardChecks")->isVisible(); }));
    EXPECT_EQ(text("wizardChecks"), "Your controller is not connected.  Connect to your CNC to configure this accessory.");
    EXPECT_FALSE(item("sub-wizard-mounting-setup")->isEnabled());
    screenshot("ui_accessories_landing");

    backend_->connectSimulator(true);
    ASSERT_TRUE(waitFor([&] {
        return machine_->isConnected() && machine_->controller()->runner().hasSettings() &&
               machine_->controller()->state().status.activeState == "Idle";
    }));
    machine_->simulator()->setSpeed(50);
    ASSERT_TRUE(waitFor([&] {
        return text("wizardChecks") == "Machine not homed. Please home your machine before proceeding with accessory configuration.";
    }));
    machine_->controller()->home();
    ASSERT_TRUE(waitFor([&] { return !item("wizardChecks")->isVisible(); }, 8000));

    const std::vector<std::string>& received = machine_->simulator()->receivedLines();
    const auto sent = [&](const std::string& line) {
        return std::find(received.begin(), received.end(), line) != received.end();
    };
    QQuickItem* tool = item("accessoryInstallerTool");
    const auto stepTitle = [&] { return tool->property("steps").toList().value(tool->property("step").toInt()).toMap()["title"].toString(); };
    const auto next = [&] {
        item("toastArea")->setProperty("toasts", QVariantList());
        ASSERT_TRUE(item("wizardNext")->isEnabled()) << stepTitle().toStdString();
        tap("wizardNext");
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    const auto waitForTap = [&](const QString& name) {
        ASSERT_TRUE(waitFor([&] { return item(name) && item(name)->isVisible() && item(name)->isEnabled(); })) << name.toStdString();
        tap(name);
    };

    // Vacuum Table: zero at the table's corner, its size, the mounting holes
    // loaded as the job - which goes to the Carve page.
    tap("sub-wizard-mounting-setup");
    EXPECT_EQ(text("wizardStepTitle"), "Zero Position");
    EXPECT_EQ(text("wizardProgress"), "Step 1 of 3");
    EXPECT_FALSE(item("wizardNext")->isEnabled());
    EXPECT_TRUE(item("wizardJog")->isVisible());
    waitForTap("zeroXY");
    ASSERT_TRUE(waitFor([&] { return sent("G10 L20 P0 X0 Y0"); }));
    next();
    EXPECT_EQ(text("wizardStepTitle"), "Table Size");
    next();  // a size is always chosen
    EXPECT_EQ(text("wizardStepTitle"), "Load to Carve");
    tap("wizardPrevious");
    EXPECT_EQ(text("wizardStepTitle"), "Table Size");
    next();
    waitForTap("loadToVisualizer");
    EXPECT_EQ(machine_->programName(), "gSender_Vacuum_Table_Mounting_4x8");
    EXPECT_TRUE(waitFor([&] { return item("carvePage")->isVisible(); }));

    // Sienci TLS: one configuration and nothing failing - straight in.
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_accessoryInstall") && item("toolCard_accessoryInstall")->isVisible(); }));
    tap("toolCard_accessoryInstall");
    waitForTap("wizard-sienci-tls");
    tool = item("accessoryInstallerTool");
    EXPECT_EQ(text("wizardStepTitle"), "Tool Change Options");
    EXPECT_EQ(text("wizardProgress"), "Step 1 of 4");
    ASSERT_TRUE(waitFor([&] { return item("firstToolBehaviour") != nullptr; }));
    item("firstToolBehaviour")->setProperty("currentIndex", 2);
    waitForTap("applyOptions");
    EXPECT_EQ(machine_->settings().toolChange.option, "Fixed Tool Sensor");
    EXPECT_EQ(machine_->settings().firstToolBehaviour, "Always probe length only");
    EXPECT_TRUE(machine_->settings().moveToManualPosition);
    EXPECT_EQ(machine_->settings().probe.probeFastFeedrate, 1000);
    ASSERT_TRUE(waitFor([&] { return sent("$6=1") && sent("$668=0"); }));  // an older build: no legacy sensor
    screenshot("ui_accessories_tls_options");
    next();

    // The sensor's position: where the machine is; moving away undoes it.
    EXPECT_EQ(text("wizardStepTitle"), "Set TLS Location");
    machine_->controller()->gcode("G53 G0 X-100 Y-50 Z-10");
    ASSERT_TRUE(waitFor([&] { return text("positionX") == "-100.00" && text("positionZ") == "-10.00"; }, 8000));
    waitForTap("setPosition");
    EXPECT_EQ(machine_->settings().toolChangePosition.x, -100);
    EXPECT_EQ(machine_->settings().toolChangePosition.y, -50);
    EXPECT_EQ(machine_->settings().toolChangePosition.z, -10);
    ASSERT_TRUE(waitFor([&] { return sent("G21 G10 L2 P9 X-100 Y-50") && sent("$#"); }));
    EXPECT_TRUE(item("wizardNext")->isEnabled());
    screenshot("ui_accessories_tls_location");
    machine_->controller()->gcode("G53 G0 X-90");
    ASSERT_TRUE(waitFor([&] { return !item("wizardNext")->isEnabled(); }, 8000));
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->state().status.activeState == "Idle"; }));
    waitForTap("setPosition");
    next();

    // The manual tool change position: a recommendation to go to.
    EXPECT_EQ(text("wizardStepTitle"), "Set Tool Change Location");
    EXPECT_EQ(text("positionX"), "-266.67");
    EXPECT_EQ(text("positionY"), "-533.33");
    waitForTap("goToPosition");
    ASSERT_TRUE(waitFor([&] { return sent("G53 G21 G0 Z-1"); }));
    ASSERT_TRUE(waitFor([&] {
        return machine_->simulator()->activeState() == "Idle" &&
               std::abs(machine_->simulator()->machinePosition()[0] + 800.0 / 3) < 0.01;
    }, 10000));
    waitForTap("setPosition");
    EXPECT_NEAR(machine_->settings().manualPosition.x, -266.67, 0.01);
    EXPECT_NEAR(machine_->settings().manualPosition.y, -533.33, 0.01);
    next();

    // Continuity: pressing the sensor passes, 1.5 s later.
    EXPECT_EQ(text("wizardStepTitle"), "Verify TLS Continuity");
    EXPECT_EQ(text("continuityText"), "Waiting for probe contact…");
    EXPECT_FALSE(item("wizardNext")->isEnabled());
    screenshot("ui_accessories_continuity");
    const sim::SimAxes at = machine_->simulator()->machinePosition();
    machine_->simulator()->setProbeSolids({sim::Solid{{at[0] - 1, at[1] - 1, at[2] - 1}, {at[0] + 1, at[1] + 1, at[2] + 1}}});
    ASSERT_TRUE(waitFor([&] { return text("continuityText") == "Continuity confirmed"; }));
    ASSERT_TRUE(waitFor([&] { return item("wizardNext")->isEnabled(); }));
    next();
    EXPECT_TRUE(item("wizardComplete")->isVisible());
    EXPECT_EQ(text("wizardProgress"), "All Steps Complete");
    screenshot("ui_accessories_complete");
    tap("wizardExit");
    EXPECT_EQ(tool->property("screen").toString(), "landing");
}

TEST_F(UiTest, DarkModeSwitchesTheTokens) {
    const QColor light = window_->color();
    backend_->setDarkMode(true);
    QCoreApplication::processEvents();
    EXPECT_EQ(window_->color(), QColor("#151B23"));  // surface-base, Workshop dark
    EXPECT_NE(window_->color(), light);
    EXPECT_TRUE(machine_->settings().darkMode);
    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));
    screenshot("ui_shell_dark");
}

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
