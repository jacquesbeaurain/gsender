// The QML touch UI, loaded headless (the offscreen platform, Qt Quick's
// software renderer) against a real Machine and the simulated board: items
// are found by objectName and driven with taps and touch gestures.
// GS_TEST_SCREENSHOTS=<dir> saves what the tests show.

#include "backend.hpp"
#include "machine.hpp"
#include "qt_event_loop.hpp"
#include "ui_app.hpp"

#include "gs/sim/grbl_simulator.hpp"

#include <QApplication>
#include <QDeadlineTimer>
#include <QPointingDevice>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtQml/qqmlextensionplugin.h>

#include <gtest/gtest.h>

#include <functional>
#include <memory>

Q_IMPORT_QML_PLUGIN(GSenderPlugin)

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
        QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(target));
        QCoreApplication::processEvents();
    }
    void type(const QString& text) {
        for (const QChar c : text) {
            QTest::keyClick(window_, c.toLatin1());
        }
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

TEST_F(UiTest, TheConnectionButtonConnectsAndDisconnects) {
    tap("connectionButton");
    QObject* menu = window_->findChild<QObject*>("connectionMenu");
    ASSERT_NE(menu, nullptr);
    ASSERT_TRUE(waitFor([&] { return menu->property("opened").toBool(); }));
    // The menu's first entry: the simulated Grbl board.
    QQuickItem* entry = nullptr;
    ASSERT_TRUE(QMetaObject::invokeMethod(menu, "itemAt", Qt::DirectConnection, Q_RETURN_ARG(QQuickItem*, entry),
                                          Q_ARG(int, 0)));
    ASSERT_NE(entry, nullptr);
    ASSERT_TRUE(waitFor([&] { return entry->isVisible() && entry->width() > 0; }));
    QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(entry));
    EXPECT_TRUE(waitFor([&] { return machine_->isConnected(); }));
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
    EXPECT_GT(view->property("scale").toDouble(), scale * 3);
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
