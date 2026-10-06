// The tools: stats, surfacing, calibration, the SD card and the accessory
// installer.

#include "ui_test.hpp"

#include "notification_center.hpp"

#include "gs/config/history.hpp"

#include <QFile>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

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
    ASSERT_TRUE(waitFor([&] {
        auto* card = item("toolCard_movementTuning");
        return card && card->isVisible() && card->width() > 0 && card->height() > 0;
    }));
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
    enter("squaringValue_0", "48");
    tap("squaringRow_0");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_FALSE(item("squaringValue_2")->isEnabled());  // a later measurement
    // A past measurement can be corrected and confirmed again.
    ASSERT_TRUE(item("squaringValue_0")->isEnabled());
    enter("squaringValue_0", "49");
    ASSERT_TRUE(waitFor([&] { return item("squaringRow_0") && item("squaringRow_0")->isEnabled(); }));
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
    // Shown, enabled and laid out: a page that just opened can place its
    // buttons before its layout has the final width (off the window on a
    // slower machine).
    const auto waitForTap = [&](const QString& name) {
        ASSERT_TRUE(waitFor([&] {
            QQuickItem* target = item(name);
            return target && target->isVisible() && target->isEnabled() &&
                   QRect(QPoint(0, 0), window_->size()).contains(centreOf(target));
        })) << name.toStdString();
        tap(name);
    };

    // Vacuum Table: zero at the table's corner, its size, the mounting holes
    // loaded as the job - which goes to the Carve page.
    tap("sub-wizard-mounting-setup");
    EXPECT_EQ(text("wizardStepTitle"), "Zero Position");
    EXPECT_EQ(text("wizardProgress"), "Step 1 of 3");
    EXPECT_FALSE(item("wizardNext")->isEnabled());
    EXPECT_FALSE(item("wizardPrevious")->isEnabled());
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
    // Restart: from the first step again; Exit: back to the landing page.
    tap("wizardRestart");
    EXPECT_EQ(text("wizardStepTitle"), "Tool Change Options");
    EXPECT_FALSE(tool->property("atCompletion").toBool());
    QMetaObject::invokeMethod(tool->property("model").value<QObject*>(), "exitSubWizard");
    EXPECT_EQ(tool->property("screen").toString(), "landing");
}
