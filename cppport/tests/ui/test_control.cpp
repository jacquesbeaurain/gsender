// Machine control: the DRO, jogging, zeroing, the console, coolant, the
// spindle and laser, and macros.

#include "ui_test.hpp"

#include "jogger.hpp"

#include <string>

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

TEST_F(UiTest, TheCustomJogPresetKeepsTheValuesTypedInIt) {
    app::Jogger& jogger = backend_->jogger();
    EXPECT_EQ(jogger.preset(), controller::JogPreset::Normal);
    tap("presetCustom");
    ASSERT_TRUE(waitFor([&] { return jogger.preset() == controller::JogPreset::Custom; }));
    EXPECT_EQ(jogger.speeds(), (controller::JogSpeeds{5, 2, 5, 3000}));

    // Edits made while Custom is chosen are kept, in mm, as its values.
    QObject* model = item("jogPanel")->property("model").value<QObject*>();
    QMetaObject::invokeMethod(model, "setField", Q_ARG(QString, "xy"), Q_ARG(double, 7.5));
    EXPECT_EQ(machine_->settings().jog.custom.xyStep, 7.5);
    // The other presets are left alone, and their own values come back.
    tap("presetRapid");
    ASSERT_TRUE(waitFor([&] { return jogger.preset() == controller::JogPreset::Rapid; }));
    EXPECT_EQ(jogger.speeds().xyStep, 20);
    EXPECT_EQ(machine_->settings().jog.rapid.xyStep, 20);
    tap("presetCustom");
    ASSERT_TRUE(waitFor([&] { return jogger.preset() == controller::JogPreset::Custom; }));
    EXPECT_EQ(jogger.speeds().xyStep, 7.5);

    // Edits to a standard preset are not kept.
    tap("presetNormal");
    ASSERT_TRUE(waitFor([&] { return jogger.preset() == controller::JogPreset::Normal; }));
    QMetaObject::invokeMethod(model, "setField", Q_ARG(QString, "xy"), Q_ARG(double, 9));
    EXPECT_EQ(machine_->settings().jog.normal.xyStep, 5);
    EXPECT_EQ(machine_->settings().jog.custom.xyStep, 7.5);
}
