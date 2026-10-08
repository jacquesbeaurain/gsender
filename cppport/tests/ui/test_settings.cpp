// The config page and the tools that rebind input: keyboard shortcuts and
// gamepads.

#include "ui_test.hpp"

#include "gamepad_backend.hpp"
#include "gamepad_service.hpp"
#include "shortcuts.hpp"
#include "ui_shortcuts.hpp"

#include <QUrl>

#include <string>

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

// The toolbar under the shortcuts: Delete, Disable All / Enable All, and an
// Export that Import reads back.
TEST_F(UiTest, TheShortcutsToolbarDeletesTogglesAllAndRoundTripsAFile) {
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_shortcuts") && item("toolCard_shortcuts")->isVisible(); }));
    tap("toolCard_shortcuts");
    ASSERT_TRUE(waitFor([&] { return item("keyboardShortcutsTool") && item("keyboardShortcutsTool")->isVisible(); }));
    QObject* model = item("keyboardShortcutsTool")->property("model").value<QObject*>();
    ASSERT_NE(model, nullptr);

    // Enable All has nothing to do at first; Disable All switches every action off.
    EXPECT_FALSE(item("shortcutsEnableAll")->isEnabled());
    ASSERT_TRUE(item("shortcutsDisableAll")->isEnabled());
    tap("shortcutsDisableAll");
    EXPECT_TRUE(model->property("noneActive").toBool());
    EXPECT_TRUE(item("shortcutsEnableAll")->isEnabled());
    EXPECT_FALSE(item("shortcutsDisableAll")->isEnabled());

    // Export, then Enable All, then Import: back to everything off.
    const QUrl file = QUrl::fromLocalFile(dir_.path() + "/shortcuts.json");
    QString failure;
    QMetaObject::invokeMethod(model, "exportTo", Q_RETURN_ARG(QString, failure), Q_ARG(QUrl, file));
    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    tap("shortcutsEnableAll");
    EXPECT_TRUE(model->property("allActive").toBool());
    QMetaObject::invokeMethod(model, "importFrom", Q_RETURN_ARG(QString, failure), Q_ARG(QUrl, file));
    EXPECT_TRUE(failure.isEmpty()) << failure.toStdString();
    EXPECT_TRUE(model->property("noneActive").toBool());
    QMetaObject::invokeMethod(model, "importFrom", Q_RETURN_ARG(QString, failure),
                              Q_ARG(QUrl, QUrl::fromLocalFile(dir_.path() + "/missing.json")));
    EXPECT_FALSE(failure.isEmpty());

    // Delete clears a shortcut's keys; the action stays.
    tap("shortcutsEnableAll");
    item("shortcutsSearch")->forceActiveFocus();
    type("jog x");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    ASSERT_TRUE(waitFor([&] { return item("shortcutDelete_JOG_X_P") && item("shortcutDelete_JOG_X_P")->isVisible(); }));
    tap("shortcutDelete_JOG_X_P");
    EXPECT_EQ(machine_->settings().shortcuts.at("JOG_X_P").keys, "");
    ASSERT_TRUE(waitFor([&] { return item("shortcutAssign_JOG_X_P") && item("shortcutAssign_JOG_X_P")->isVisible(); }));
}

TEST_F(UiTest, TheConfigPageStagesAndAppliesSettingsAndTheBoards) {
    tap("navConfig");
    ASSERT_TRUE(waitFor([&] { return item("configPage") && item("configPage")->isVisible(); }));
    QObject* model = item("configPage")->property("model").value<QObject*>();
    ASSERT_TRUE(waitFor([&] { return item("config_units") != nullptr; }));
    screenshot("ui_config");
    const auto search = [&](const QString& text) {
        QQuickItem* field = item("configSearch");
        field->forceActiveFocus();
        QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
        if (text.isEmpty()) {
            QTest::keyClick(window_, Qt::Key_Delete);
        } else {
            type(text);
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    };
    const auto apply = [&] {
        item("toastArea")->setProperty("toasts", QVariantList());
        ASSERT_TRUE(item("configApply")->property("active").toBool());
        tap("configApply");
    };

    // A switch, staged until applied.
    search("dark mode");
    ASSERT_TRUE(waitFor([&] { return item("configValue_darkMode") && item("configValue_darkMode")->isVisible(); }));
    tap("configValue_darkMode");
    EXPECT_EQ(model->property("pendingChanges").toInt(), 1);
    EXPECT_TRUE(item("configApply")->property("active").toBool());
    EXPECT_FALSE(machine_->settings().darkMode);
    apply();
    EXPECT_TRUE(machine_->settings().darkMode);
    EXPECT_EQ(model->property("pendingChanges").toInt(), 0);
    EXPECT_TRUE(item("configReset_darkMode")->isVisible());  // away from the default
    tap("configReset_darkMode");
    apply();
    EXPECT_FALSE(machine_->settings().darkMode);

    // Lengths follow the staged units: 1 in is 25.4 mm.
    search("safe height");
    ASSERT_TRUE(waitFor([&] { return item("configValue_safeRetractHeight") != nullptr; }));
    QMetaObject::invokeMethod(model, "setValue", Q_ARG(QString, "units"), Q_ARG(QVariant, QString("in")));
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QQuickItem* height = item("configValue_safeRetractHeight");
    height->forceActiveFocus();
    QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
    type("1");
    QTest::keyClick(window_, Qt::Key_Return);
    EXPECT_EQ(model->property("pendingChanges").toInt(), 2);
    apply();
    EXPECT_FALSE(machine_->settings().metric);
    EXPECT_NEAR(machine_->settings().safeRetractHeight, 25.4, 1e-9);

    // The editors hand their text to the model, which checks it.
    const auto value = [&](const QString& key) {
        QVariant entry;
        QMetaObject::invokeMethod(model, "valueOf", Q_RETURN_ARG(QVariant, entry), Q_ARG(QString, key));
        return entry;
    };
    QMetaObject::invokeMethod(model, "setNumber", Q_ARG(QString, "jogThreshold"), Q_ARG(QString, "99999"));
    EXPECT_EQ(value("jogThreshold").toInt(), 10000);  // kept within its range
    QMetaObject::invokeMethod(model, "setNumber", Q_ARG(QString, "jogThreshold"), Q_ARG(QString, "abc"));
    EXPECT_EQ(value("jogThreshold").toInt(), 10000);
    QMetaObject::invokeMethod(model, "setPart", Q_ARG(QString, "ethernetIp"), Q_ARG(QVariant, 3),
                              Q_ARG(QString, "300"));
    EXPECT_EQ(value("ethernetIp").toList()[3].toInt(), 255);
    QMetaObject::invokeMethod(model, "setPart", Q_ARG(QString, "park"), Q_ARG(QVariant, 2), Q_ARG(QString, "-7.5"));
    EXPECT_EQ(value("park").toList()[2].toDouble(), -7.5);
    const double feed = value("jog1").toMap()["feedrate"].toDouble();
    QMetaObject::invokeMethod(model, "setPart", Q_ARG(QString, "jog1"), Q_ARG(QVariant, QString("feedrate")),
                              Q_ARG(QString, "-1"));
    EXPECT_EQ(value("jog1").toMap()["feedrate"].toDouble(), feed);  // never negative
    QMetaObject::invokeMethod(model, "setFolder", Q_ARG(QString, "backupLocation"),
                              Q_ARG(QUrl, QUrl::fromLocalFile("/tmp/my backups")));
    EXPECT_EQ(value("backupLocation").toString(), "/tmp/my backups");
    QMetaObject::invokeMethod(model, "revert");

    // The board's settings, in their sections.
    connectSimulator();
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->runner().hasSettings(); }));
    search("$110");
    ASSERT_TRUE(waitFor([&] { return item("configValue_$110") != nullptr; }));
    EXPECT_EQ(item("config_$110")->property("entry").toMap()["section"].toString(), "Motors");
    QQuickItem* rate = item("configValue_$110");
    ASSERT_TRUE(waitFor([&] { return rate->isEnabled(); }));
    rate->forceActiveFocus();
    QTest::keyClick(window_, Qt::Key_A, Qt::ControlModifier);
    type("4321");
    QTest::keyClick(window_, Qt::Key_Return);
    EXPECT_EQ(model->property("pendingChanges").toInt(), 1);
    screenshot("ui_config_firmware");
    apply();
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->runner().setting("$110") == "4321"; }));
    ASSERT_TRUE(waitFor([&] { return model->property("pendingChanges").toInt() == 0; }));
    ASSERT_TRUE(waitFor([&] { return item("configReset_$110") && item("configReset_$110")->isVisible(); }));
    tap("configReset_$110");
    apply();
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->runner().setting("$110") != "4321"; }));

    // Bits: $3's X direction.
    search("$3");
    ASSERT_TRUE(waitFor([&] { return item("configBit_$3_0") != nullptr; }));
    const QString before = QString::fromStdString(machine_->controller()->runner().setting("$3"));
    tap("configBit_$3_0");
    apply();
    ASSERT_TRUE(waitFor([&] {
        return QString::fromStdString(machine_->controller()->runner().setting("$3")).toInt() == (before.toInt() ^ 1);
    }));

    // The menu scrolls to a section.
    search("");
    ASSERT_TRUE(waitFor([&] { return item("configSection_Probe") != nullptr; }));
    tap("configSection_Probe");
    EXPECT_EQ(item("configPage")->property("currentSection").toString(), "Probe");
    ASSERT_TRUE(waitFor([&] { return item("config_plateType") && item("config_plateType")->isVisible(); }));
    screenshot("ui_config_probe");
}

TEST_F(UiTest, TheGamepadToolAddsAProfileAndSetsItsButtons) {
    auto fake = std::make_unique<app::FakeGamepadBackend>();
    app::FakeGamepadBackend* pads = fake.get();
    backend_->gamepad().setBackend(std::move(fake));
    const auto profileCount = [&] { return machine_->settings().gamepadProfiles.size(); };
    const std::size_t defaults = profileCount();
    ASSERT_EQ(defaults, 2U);  // Logitech F710 and Xbox

    backend_->openTool("gamepad");
    ASSERT_TRUE(waitFor([&] { return item("gamepadTool") && item("gamepadProfile_1"); }));
    EXPECT_TRUE(backend_->gamepad().capturing());
    screenshot("ui_gamepad_profiles");
    const auto popup = [&](const char* name) { return item("gamepadTool")->findChild<QObject*>(name); };
    const auto isOpen = [&](const char* name) { return popup(name)->property("visible").toBool(); };

    // Adding: the pad whose button is pressed.
    tap("gamepadAdd");
    ASSERT_TRUE(waitFor([&] { return isOpen("gamepadAddPopup"); }));
    EXPECT_EQ(text("gamepadAvailabilityText"), "Connect your device and press any button on it");
    const std::string id = "Test Pad (STANDARD GAMEPAD Vendor: 1234 Product: 5678)";
    gamepad::PadState pad;
    pad.id = id;
    pad.standard = true;
    pad.buttons.assign(17, false);
    pad.axes.assign(4, 0.0);
    pads->pads[0] = pad;
    backend_->gamepad().poll();
    pads->pads[0]->buttons[0] = true;
    backend_->gamepad().poll();
    ASSERT_TRUE(waitFor([&] { return text("gamepadAvailabilityText") == "Profile Is Available"; }));
    EXPECT_EQ(text("gamepadAddName"), QString::fromStdString(id));
    tap("gamepadAddConfirm");
    ASSERT_EQ(profileCount(), defaults + 1);
    EXPECT_EQ(machine_->settings().gamepadProfiles.back().buttons.size(), 17U);
    EXPECT_TRUE(waitFor([&] { return !isOpen("gamepadAddPopup"); }));

    // The profile: connected, button 0 lit while it is down.
    ASSERT_TRUE(waitFor([&] { return item("gamepadProfile_2") != nullptr; }));
    tap("gamepadProfile_2");
    ASSERT_TRUE(waitFor([&] { return item("gamepadStatus") && item("gamepadStatus")->isVisible(); }));
    QObject* model = item("gamepadTool")->property("model").value<QObject*>();
    ASSERT_NE(model, nullptr);
    EXPECT_TRUE(model->property("padConnected").toBool());
    EXPECT_TRUE(model->property("pressed").toList().value(0).toBool());
    pads->pads[0]->buttons[0] = false;
    backend_->gamepad().poll();
    EXPECT_FALSE(model->property("pressed").toList().value(0).toBool());

    // Button A jogs X+.
    ASSERT_TRUE(waitFor([&] { return item("gamepadPrimary_0") != nullptr; }));
    tap("gamepadPrimary_0");
    ASSERT_TRUE(waitFor([&] { return isOpen("gamepadActionPopup"); }));
    ASSERT_TRUE(waitFor([&] {
        QQuickItem* action = item("gamepadAction_JOG_X_P");
        return action && QRect(QPoint(0, 0), window_->size()).contains(centreOf(action));
    }));
    tap("gamepadAction_JOG_X_P");
    EXPECT_EQ(text("gamepadSelectedAction"), "Jog X+ (right)");
    screenshot("ui_gamepad_set_action");
    tap("gamepadSetShortcut");
    EXPECT_EQ(machine_->settings().gamepadProfiles.back().buttons[0].primaryAction, "JOG_X_P");

    // Button B as the 2nd-action one: its row says so.
    tap("gamepadSecondary_1");
    ASSERT_TRUE(waitFor([&] { return isOpen("gamepadActionPopup"); }));
    tap("gamepadModifierSwitch");
    EXPECT_EQ(machine_->settings().gamepadProfiles.back().modifier, std::optional<int>(1));
    tap("gamepadCancelShortcut");
    ASSERT_TRUE(waitFor([&] { return text("gamepadRole_1") == "Activate 2nd Actions"; }));

    // Joystick options.
    tap("gamepadFixedSpeed");
    EXPECT_TRUE(machine_->settings().gamepadProfiles.back().joystickOptions.fixedSpeedMode);
    screenshot("ui_gamepad_profile");

    // Back, and delete it (asked first).
    tap("gamepadBack");
    ASSERT_TRUE(waitFor([&] { return item("gamepadDelete_2") && item("gamepadDelete_2")->isVisible(); }));
    tap("gamepadDelete_2");
    ASSERT_TRUE(waitFor([&] { return isOpen("gamepadDeleteConfirm"); }));
    QMetaObject::invokeMethod(popup("gamepadDeleteConfirm"), "close");
    QMetaObject::invokeMethod(popup("gamepadDeleteConfirm"), "accepted");
    EXPECT_EQ(profileCount(), defaults);

    // Leaving the tool lets the pads act again.
    tap("toolGoBack");
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    EXPECT_TRUE(waitFor([&] { return !backend_->gamepad().capturing(); }));
}
