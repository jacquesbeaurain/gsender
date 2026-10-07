// The probe and rotary tabs: probing the stock and capturing grids.

#include "ui_test.hpp"

#include "gamepad_backend.hpp"
#include "gamepad_service.hpp"

#include <QFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

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
    ASSERT_TRUE(waitFor([&] { return item("probeTool")->width() > 100; }));
    QTest::qWait(150);  // the layout settles
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

TEST_F(UiTest, TheProbeTabCapturesARectangularGridAndManualPoints) {
    app::AppSettings settings = machine_->settings();
    settings.probe.plateType = probe::PlateType::Probe3D;
    machine_->setSettings(settings);
    connectSimulator();
    machine_->simulator()->setSpeed(200);
    ASSERT_TRUE(waitFor([&] { return item("probeTab") && item("probeTab")->isVisible(); }));
    ASSERT_TRUE(waitFor([&] { return item("gridCaptureButton")->isVisible() && item("gridCaptureButton")->isEnabled(); }));

    tap("gridCaptureButton");
    QObject* dialog = item("probeTab")->findChild<QObject*>("rectangularGrid");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    QObject* model = dialog->property("model").value<QObject*>();
    QQuickItem* start = nullptr;
    ASSERT_TRUE(waitFor([&] { return (start = item("gridStart")) && centreOf(start).y() < window_->height(); }));
    EXPECT_FALSE(start->isEnabled());  // the circuit check first
    tap("gridConfirmProbe");
    ASSERT_TRUE(waitFor([&] { return start->isEnabled(); }));

    // A 3 x 2 grid from where the probe is.
    item("gridNx")->setProperty("text", "3");
    item("gridNy")->setProperty("text", "2");
    int points = 0;
    QMetaObject::invokeMethod(model, "gridPoints", Q_RETURN_ARG(int, points), Q_ARG(QString, "3"), Q_ARG(QString, "2"));
    EXPECT_EQ(points, 6);
    QMetaObject::invokeMethod(model, "gridPoints", Q_RETURN_ARG(int, points), Q_ARG(QString, "999"), Q_ARG(QString, "x"));
    EXPECT_EQ(points, 200);  // each count 1-200
    screenshot("ui_probe_grid");
    const std::array<double, 4> w = machine_->workPositionMm();
    const double z0 = w[2];
    tap("gridStart");
    ASSERT_TRUE(waitFor([&] { return model->property("status").toString() == "done"; }, 20000))
        << model->property("statusText").toString().toStdString();
    EXPECT_EQ(model->property("statusText").toString(), "Captured 6 grid points");
    EXPECT_EQ(model->property("gridCount").toInt(), 6);
    ASSERT_TRUE(waitFor([&] {
        return machine_->simulator()->activeState() == "Idle" && std::abs(machine_->workPositionMm()[2] - z0) < 1e-3;
    }, 10000));  // back at the safe height

    // The simulator's surface: 10 mm down, sloping per 20 mm tile.
    const auto surface = [&](double x, double y) {
        const auto centre = [](double at) { return -5 + 20 * std::floor((at + 15) / 20); };
        return z0 - 10 - 0.02 * centre(x) + 0.01 * centre(y);
    };
    QString csv;
    QMetaObject::invokeMethod(model, "csv", Q_RETURN_ARG(QString, csv));
    QStringList rows = csv.split('\n');
    ASSERT_EQ(rows.size(), 7);
    EXPECT_EQ(rows[0], "X,Y,Z");
    for (int i = 0; i < 6; ++i) {
        const double dx = 10.0 * (i % 3);
        const double dy = 10.0 * (i / 3);
        const QStringList cells = rows[i + 1].split(',');
        ASSERT_EQ(cells.size(), 3);
        EXPECT_NEAR(cells[0].toDouble(), w[0] + dx, 1e-3) << i;
        EXPECT_NEAR(cells[1].toDouble(), w[1] + dy, 1e-3) << i;
        EXPECT_NEAR(cells[2].toDouble(), surface(dx, dy), 1e-3) << i;
    }

    // A manual point where the probe is jogged to: from the grid's last
    // point (0, 10), 3 along and 17 up.
    tap("gridModeManual");
    ASSERT_TRUE(waitFor([&] { return item("gridCapturePoint") && item("gridCapturePoint")->isVisible(); }));
    machine_->controller()->gcode(std::vector<std::string>{"G91 G0 X3 Y17", "G90"});
    ASSERT_TRUE(waitFor([&] {
        const auto at = machine_->workPositionMm();
        return std::abs(at[0] - (w[0] + 3)) < 1e-3 && std::abs(at[1] - (w[1] + 27)) < 1e-3 &&
               machine_->controller()->state().status.activeState == "Idle";
    }, 10000));
    ASSERT_TRUE(waitFor([&] { return item("gridCapturePoint")->isEnabled(); }));
    EXPECT_TRUE(waitFor([&] { return text("gridPosition") == "X3.00 Y27.00 Z0.00"; })) << text("gridPosition").toStdString();
    screenshot("ui_probe_grid_manual");
    tap("gridCapturePoint");
    ASSERT_TRUE(waitFor([&] { return model->property("manualCount").toInt() == 1 && !model->property("running").toBool(); }, 10000));
    QMetaObject::invokeMethod(model, "csv", Q_RETURN_ARG(QString, csv));
    rows = csv.split('\n');
    ASSERT_EQ(rows.size(), 8);
    const QStringList manual = rows[7].split(',');
    EXPECT_NEAR(manual[0].toDouble(), w[0] + 3, 1e-3);
    EXPECT_NEAR(manual[1].toDouble(), w[1] + 27, 1e-3);
    EXPECT_NEAR(manual[2].toDouble(), surface(3, 27), 1e-3);

    // Saved as a CSV.
    QVariantMap result;
    QMetaObject::invokeMethod(model, "save", Q_RETURN_ARG(QVariantMap, result), Q_ARG(QString, dir_.path() + "/grid"));
    EXPECT_TRUE(result.value("ok").toBool()) << result.value("message").toString().toStdString();
    QFile file(dir_.path() + "/grid.csv");
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_EQ(QString::fromUtf8(file.readAll()), csv + "\n");
    tap("gridClose");
    EXPECT_TRUE(waitFor([&] { return !dialog->property("visible").toBool(); }));
}

TEST_F(UiTest, TheRectangularGridStopsGracefully) {
    app::AppSettings settings = machine_->settings();
    settings.probe.plateType = probe::PlateType::Probe3D;
    machine_->setSettings(settings);
    connectSimulator();
    machine_->simulator()->setSpeed(20);
    ASSERT_TRUE(waitFor([&] { return item("gridCaptureButton") && item("gridCaptureButton")->isEnabled(); }));
    tap("gridCaptureButton");
    QObject* dialog = item("probeTab")->findChild<QObject*>("rectangularGrid");
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    QObject* model = dialog->property("model").value<QObject*>();
    ASSERT_TRUE(waitFor([&] { return item("gridConfirmProbe") && centreOf(item("gridConfirmProbe")).y() < window_->height(); }));
    tap("gridConfirmProbe");
    ASSERT_TRUE(waitFor([&] { return item("gridStart")->isEnabled(); }));
    tap("gridStart");
    ASSERT_TRUE(waitFor([&] { return model->property("captured").toInt() >= 2; }, 20000));
    // Stop: the point in flight is kept, then the probe retracts.
    EXPECT_FALSE(item("gridClose")->isEnabled());
    tap("gridStart");
    ASSERT_TRUE(waitFor([&] { return model->property("status").toString() == "stopped"; }, 20000));
    const int captured = model->property("captured").toInt();
    EXPECT_GE(captured, 3);
    EXPECT_LT(captured, 16);
    EXPECT_EQ(model->property("statusText").toString(), QString("Stopped after %1 of 16 points").arg(captured));
    EXPECT_EQ(model->property("pointCount").toInt(), captured);
    EXPECT_TRUE(waitFor([&] { return machine_->simulator()->activeState() == "Idle"; }, 10000));
    EXPECT_TRUE(item("gridSave")->isEnabled());
}

TEST_F(UiTest, TheRectangularGridsManualPointsJogWithAGamepad) {
    auto fake = std::make_unique<app::FakeGamepadBackend>();
    app::FakeGamepadBackend* pads = fake.get();
    backend_->gamepad().setBackend(std::move(fake));
    app::AppSettings settings = machine_->settings();
    settings.probe.plateType = probe::PlateType::Probe3D;
    gamepad::Profile profile;
    profile.ids = {"Test Pad (STANDARD GAMEPAD Vendor: 1234 Product: 5678)"};
    profile.name = "Test Pad";
    profile.buttons.push_back({"A", 0, "JOG_X_P", ""});
    settings.gamepadProfiles.push_back(profile);
    machine_->setSettings(settings);
    connectSimulator();
    machine_->simulator()->setSpeed(50);

    ASSERT_TRUE(waitFor([&] { return item("gridCaptureButton") && item("gridCaptureButton")->isEnabled(); }));
    tap("gridCaptureButton");
    QObject* dialog = item("probeTab")->findChild<QObject*>("rectangularGrid");
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    ASSERT_TRUE(waitFor([&] { return item("gridModeManual") && centreOf(item("gridModeManual")).y() > 0; }));
    tap("gridModeManual");
    ASSERT_TRUE(waitFor([&] { return item("gridCapturePoint") && item("gridCapturePoint")->isVisible(); }));

    // With the dialog open, a tap of the pad's A steps X+ by the jog step.
    gamepad::PadState pad;
    pad.id = profile.ids[0];
    pad.standard = true;
    pad.buttons.assign(17, false);
    pad.axes.assign(4, 0.0);
    pads->pads[0] = pad;
    backend_->gamepad().poll();
    const double x0 = machine_->workPositionMm()[0];
    pads->pads[0]->buttons[0] = true;
    backend_->gamepad().poll();
    pads->pads[0]->buttons[0] = false;
    backend_->gamepad().poll();
    const double step = machine_->settings().jog.normal.xyStep;
    ASSERT_GT(step, 0);
    EXPECT_TRUE(waitFor([&] {
        return std::abs(machine_->workPositionMm()[0] - (x0 + step)) < 1e-3 &&
               machine_->controller()->state().status.activeState == "Idle";
    }, 10000)) << machine_->workPositionMm()[0];
    EXPECT_TRUE(waitFor([&] { return text("gridPosition").startsWith("X" + QString::number(x0 + step, 'f', 2)); }))
        << text("gridPosition").toStdString();
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
    EXPECT_TRUE(item("mountingIllustration")->property("source").toString().endsWith("Vortex_Standard_Track_01.png"));
    tap("mounting_10");
    EXPECT_TRUE(item("mountingIllustration")->property("source").toString().endsWith("Vortex_Extension_Track_01.png"));
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
