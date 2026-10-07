// The 3D visualizer: touch navigation, its controls, the camera and the
// requests plugins make of it.

#include "ui_test.hpp"

#include "plugin_service.hpp"
#include "toolpath_scene.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QPointingDevice>

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

TEST(ToolpathCameraTest, UnprojectInvertsProjectOnThePlane) {
    for (const bool perspective : {false, true}) {
        app::ToolpathCamera camera;
        camera.setViewport(QSizeF(800, 600));
        camera.setPerspective(perspective);
        camera.setView(app::ToolpathCamera::View::Iso, std::nullopt);
        camera.pan(37, -12);
        for (const app::Point3 p : {app::Point3{0, 0, 0}, app::Point3{42.5, -17, 0}, app::Point3{-80, 60, 5}}) {
            const QPointF screen = camera.project(p);
            const auto back = camera.unproject(screen, p.z);
            ASSERT_TRUE(back.has_value());
            EXPECT_NEAR(back->x, p.x, 1e-6) << perspective;
            EXPECT_NEAR(back->y, p.y, 1e-6) << perspective;
            EXPECT_DOUBLE_EQ(back->z, p.z);
        }
        // Edge-on, the work plane has no point under a pixel.
        camera.setView(app::ToolpathCamera::View::Front, std::nullopt);
        EXPECT_FALSE(camera.unproject(QPointF(400, 300)).has_value());
    }
}

TEST_F(UiTest, TheVisualizerServesPluginViewerRequests) {
    app::PluginBridge& bridge = machine_->pluginService().bridge();
    QQuickItem* view = item("toolpath");
    ASSERT_NE(view, nullptr);
    ASSERT_NE(bridge.viewer(), nullptr);
    app::PluginManifest manifest;
    manifest.id = "com.example.viewer";
    manifest.capabilities.requestTypes = {"viewer:screen-to-world", "viewer:world-to-screen", "viewer:camera:set",
                                          "viewer:camera:lock-rotate", "viewer:pick:arm", "viewer:overlay:set",
                                          "machine:busy:set"};

    ASSERT_TRUE(bridge.execute(manifest, "viewer:camera:set", {{"view", "3d"}}).ok);
    EXPECT_EQ(view->property("view").toString(), "3d");
    const auto screen = bridge.execute(manifest, "viewer:world-to-screen", {{"x", 10}, {"y", 20}});
    ASSERT_TRUE(screen.ok);
    const auto world = bridge.execute(manifest, "viewer:screen-to-world",
                                      {{"px", screen.result.value("x")}, {"py", screen.result.value("y")}});
    ASSERT_TRUE(world.ok);
    EXPECT_NEAR(world.result.value("x").toDouble(), 10, 1e-6);
    EXPECT_NEAR(world.result.value("y").toDouble(), 20, 1e-6);

    // Rotation locked: a one-finger drag no longer orbits.
    ASSERT_TRUE(bridge.execute(manifest, "viewer:camera:lock-rotate", {{"locked", true}}).ok);
    EXPECT_FALSE(view->property("rotateEnabled").toBool());
    const double yaw = view->property("yaw").toDouble();
    QMetaObject::invokeMethod(view, "orbit", Q_ARG(double, 30.0), Q_ARG(double, 0.0));
    EXPECT_DOUBLE_EQ(view->property("yaw").toDouble(), yaw);

    // A click pick, once armed, comes back on the viewer topic.
    connectSimulator();
    QJsonObject picked;
    QObject::connect(&bridge, &app::PluginBridge::pluginEvent,
                     [&](const QString&, const QString& topic, const QJsonObject& data) {
                         if (topic == "viewer" && data.value("kind") == "pick") picked = data;
                     });
    ASSERT_TRUE(bridge.execute(manifest, "viewer:pick:arm", {{"mode", "click"}}).ok);
    EXPECT_EQ(view->property("pickMode").toString(), "click");
    QTest::mouseClick(window_, Qt::LeftButton, Qt::NoModifier, centreOf(view));
    ASSERT_TRUE(waitFor([&] { return !picked.isEmpty(); }));
    EXPECT_TRUE(picked.value("world").isObject());

    // A plugin's busy latch holds the status pill, with its label.
    ASSERT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));
    ASSERT_TRUE(bridge.execute(manifest, "machine:busy:set", {{"busy", true}, {"label", "Drilling"}}).ok);
    EXPECT_TRUE(waitFor([&] { return text("statusText") == "Drilling"; }));
    ASSERT_TRUE(bridge.execute(manifest, "machine:busy:set", {{"busy", false}}).ok);
    EXPECT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));

    ASSERT_TRUE(bridge.execute(manifest, "viewer:overlay:set",
                               {{"markers", QJsonArray{QJsonObject{{"id", "a"}, {"x", 10}, {"y", 20}, {"label", "A"}},
                                                       QJsonObject{{"id", "b"}, {"x", 30}, {"y", 20}, {"shape", "ring"}}}}})
                    .ok);
    screenshot("ui_visualizer_plugin_overlay");
}

TEST_F(UiTest, MoveToHereArmsTheTopViewAndRapidsToTheHeldSpot) {
    QQuickItem* view = item("toolpath");
    ASSERT_NE(view, nullptr);
    // Disconnected: the crosshair is hidden and arming refuses.
    EXPECT_FALSE(view->property("moveToHereAvailable").toBool());
    QMetaObject::invokeMethod(view, "toggleMoveToHere");
    EXPECT_FALSE(view->property("moveToHere").toBool());

    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return text("statusText") == "Idle"; }));
    ASSERT_TRUE(waitFor([&] { return view->property("moveToHereAvailable").toBool(); }));
    QMetaObject::invokeMethod(view, "setView", Q_ARG(QString, QStringLiteral("3d")));
    QMetaObject::invokeMethod(view, "toggleMoveToHere");
    EXPECT_TRUE(view->property("moveToHere").toBool());
    EXPECT_EQ(view->property("view").toString(), "top");
    EXPECT_FALSE(view->property("rotateEnabled").toBool());
    EXPECT_EQ(view->property("pickMode").toString(), "hold");

    // A completed hold at the viewport's middle moves there and disarms.
    const QPointF middle(view->width() / 2 + 40, view->height() / 2 - 30);
    QMetaObject::invokeMethod(view, "pickAt", Q_ARG(double, middle.x()), Q_ARG(double, middle.y()));
    EXPECT_FALSE(view->property("moveToHere").toBool());
    EXPECT_TRUE(view->property("rotateEnabled").toBool());
    EXPECT_EQ(view->property("view").toString(), "3d");  // the camera is back
    EXPECT_TRUE(waitFor([&] {
        const auto work = machine_->workPositionMm();
        return std::abs(work[0]) > 1 && std::abs(work[1]) > 1;
    }));

    // Arming waits for the move to finish; the button toggles it off again.
    ASSERT_TRUE(waitFor([&] { return machine_->canMoveToHere(); }));
    QMetaObject::invokeMethod(view, "toggleMoveToHere");
    EXPECT_TRUE(view->property("moveToHere").toBool());
    QMetaObject::invokeMethod(view, "toggleMoveToHere");
    EXPECT_FALSE(view->property("moveToHere").toBool());
}

TEST_F(UiTest, TheJobSummaryShowsOverTheVisualizer) {
    app::AppSettings settings = machine_->settings();
    settings.accessibility.gcodeSummary = settings.accessibility.gcodeSummaryVisible = true;
    machine_->setSettings(settings);
    machine_->loadProgram("square.nc", "G21 G90\nG1 X5 F1200\nG1 Y5\nG1 X0\nG1 Y0\n");
    ASSERT_TRUE(waitFor([&] { return item("jobSummary") && item("jobSummary")->isVisible(); }));
    EXPECT_TRUE(backend_->property("jobSummary").toString().startsWith("File loaded: square.nc."))
        << backend_->property("jobSummary").toString().toStdString();
    screenshot("ui_job_summary");

    settings.accessibility.gcodeSummaryVisible = false;
    machine_->setSettings(settings);
    EXPECT_TRUE(waitFor([&] { return !item("jobSummary")->isVisible(); }));
}

TEST_F(UiTest, TheVisualizerTakesTheKeysWhenAsked) {
    app::AppSettings settings = machine_->settings();
    settings.accessibility.visualizerKeyboardControl = true;
    machine_->setSettings(settings);
    QQuickItem* toolpath = item("toolpath");
    ASSERT_NE(toolpath, nullptr);
    EXPECT_TRUE(toolpath->activeFocusOnTab());
    QMetaObject::invokeMethod(toolpath, "setView", Q_ARG(QString, "3d"));
    toolpath->forceActiveFocus();
    const double yaw = toolpath->property("yaw").toDouble();
    QTest::keyClick(window_, Qt::Key_Left);
    EXPECT_NEAR(toolpath->property("yaw").toDouble(), yaw - 15, 1e-9);

    settings.accessibility.visualizerKeyboardControl = false;
    machine_->setSettings(settings);
    EXPECT_FALSE(toolpath->activeFocusOnTab());
    EXPECT_FALSE(toolpath->hasActiveFocus());
}

TEST_F(UiTest, VisualizerModernControlsLightweightOrthoAndNavCube) {
    machine_->loadProgram("square.nc", "G21 G90\nG0 X0 Y0\nG1 Z-1 F300\nG1 X50\nG1 Y50\nG1 X0\nG1 Y0\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));

    QQuickItem* toolpath = item("toolpath");
    ASSERT_NE(toolpath, nullptr);

    QQuickItem* btnLightweight = item("btnLightweight");
    ASSERT_NE(btnLightweight, nullptr);

    QQuickItem* btnOrtho = item("viewOrtho");
    ASSERT_NE(btnOrtho, nullptr);

    QQuickItem* btnIso = item("view3D");
    ASSERT_NE(btnIso, nullptr);

    QQuickItem* navCube = item("navCube");
    ASSERT_NE(navCube, nullptr);

    // 1. Test Lightweight Mode Toggle
    EXPECT_FALSE(machine_->settings().liteMode);
    EXPECT_FALSE(btnLightweight->property("isLite").toBool());

    tap("btnLightweight");
    EXPECT_TRUE(machine_->settings().liteMode);
    EXPECT_TRUE(btnLightweight->property("isLite").toBool());
    EXPECT_TRUE(toolpath->property("flat").toBool());

    tap("btnLightweight");
    EXPECT_FALSE(machine_->settings().liteMode);
    EXPECT_FALSE(btnLightweight->property("isLite").toBool());
    EXPECT_FALSE(toolpath->property("flat").toBool());

    // 2. Test Ortho / Perspective Toggle
    EXPECT_TRUE(machine_->settings().perspective);
    EXPECT_FALSE(btnOrtho->property("isOrtho").toBool());

    tap("viewOrtho");
    EXPECT_FALSE(machine_->settings().perspective);
    EXPECT_TRUE(btnOrtho->property("isOrtho").toBool());

    tap("viewOrtho");
    EXPECT_TRUE(machine_->settings().perspective);
    EXPECT_FALSE(btnOrtho->property("isOrtho").toBool());

    // 3. Test Iso View Button
    tap("view3D");
    EXPECT_EQ(toolpath->property("view").toString(), "3d");
    // gviewer's 3D preset: a true isometric view.
    EXPECT_NEAR(toolpath->property("yaw").toDouble(), 45.0, 1.0);
    EXPECT_NEAR(toolpath->property("pitch").toDouble(), 54.7, 1.0);

    // 4. Test Navigation Cube Properties and View Snapping
    EXPECT_NE(navCube->property("view").value<QObject*>(), nullptr);
    EXPECT_NEAR(navCube->property("yaw").toDouble(), 45.0, 1.0);
    EXPECT_NEAR(navCube->property("pitch").toDouble(), 54.7, 1.0);

    // Test dragging the NavCube to orbit
    const QPoint cubeCentre = centreOf(navCube);
    QTest::mousePress(window_, Qt::LeftButton, {}, cubeCentre);
    for (int step = 1; step <= 8; ++step) {
        QTest::mouseMove(window_, cubeCentre + QPoint(step * 5, 0));
    }
    QTest::mouseRelease(window_, Qt::LeftButton, {}, cubeCentre + QPoint(40, 0));
    QCoreApplication::processEvents();

    EXPECT_GT(toolpath->property("yaw").toDouble(), 35.0 + 5.0);
}
