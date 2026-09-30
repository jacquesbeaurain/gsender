// Jobs: loading a file, running it, stepping through it, editing it, its
// summary and tool changes.

#include "ui_test.hpp"

#include <QFile>

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
    // The fields' checks.
    QObject* job = popup->property("model").value<QObject*>();
    const auto startLine = [&](const QString& text) {
        int line = 0;
        QMetaObject::invokeMethod(job, "startLine", Q_RETURN_ARG(int, line), Q_ARG(QString, text));
        return line;
    };
    EXPECT_EQ(startLine("3"), 3);
    EXPECT_EQ(startLine("0"), 1);
    EXPECT_EQ(startLine("999999"), job->property("totalLines").toInt());
    EXPECT_EQ(startLine("abc"), job->property("suggestedStartLine").toInt());
    double height = -1;
    QMetaObject::invokeMethod(job, "safeHeight", Q_RETURN_ARG(double, height), Q_ARG(QString, "-3"));
    EXPECT_EQ(height, 0);
    QQuickItem* startButton = nullptr;
    ASSERT_TRUE(waitFor([&] {
        startButton = item("startFromLineStart");  // the overlay is under the root item too
        return startButton && startButton->width() > 0;
    }));
    QTest::mouseClick(window_, Qt::LeftButton, {}, centreOf(startButton));
    EXPECT_TRUE(waitFor([&] { return machine_->controller()->workflow().isRunning(); }));
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
    const auto jumpRow = [&](const QString& text) {
        int row = 0;
        QMetaObject::invokeMethod(model, "jumpRow", Q_RETURN_ARG(int, row), Q_ARG(QString, text));
        return row;
    };
    EXPECT_EQ(jumpRow("3"), 2);
    EXPECT_EQ(jumpRow("2.7"), 1);
    EXPECT_EQ(jumpRow("99"), 4);  // past the end: the last line
    EXPECT_EQ(jumpRow("x"), -1);

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
