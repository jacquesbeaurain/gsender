// The top bar, the rail, notifications, alarms, the keyboard map and
// the application-wide settings (dark mode, prompt on exit, help links).

#include "ui_test.hpp"

#include "icon_provider.hpp"
#include "remote_service.hpp"
#include "shortcuts.hpp"
#include "ui_shortcuts.hpp"

#include <QImage>
#include <QQmlComponent>
#include <QUrl>

#include <cmath>

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

TEST_F(UiTest, TheSimulatedBoardsAreOnlyOfferedWithTheOption) {
    tap("connectionButton");
    QObject* ports = window_->findChild<QObject*>("portListings");
    ASSERT_NE(ports, nullptr);
    ASSERT_TRUE(waitFor([&] { return ports->property("opened").toBool(); }));
    ASSERT_NE(item("portEthernet"), nullptr);
    ASSERT_NE(item("portSimulator"), nullptr);
    EXPECT_FALSE(backend_->simulatorEnabled());
    EXPECT_FALSE(item("portSimulator")->isVisible());
    EXPECT_FALSE(item("portSimulatorHal")->isVisible());
    // --simulator turns them on.
    backend_->setSimulatorEnabled(true);
    EXPECT_TRUE(waitFor([&] { return item("portSimulator")->isVisible() && item("portSimulatorHal")->isVisible(); }));
}

TEST_F(UiTest, TheConnectionButtonListsPortsConnectsAndDisconnects) {
    backend_->setSimulatorEnabled(true);  // as --simulator does
    tap("connectionButton");
    QObject* ports = window_->findChild<QObject*>("portListings");
    ASSERT_NE(ports, nullptr);
    ASSERT_TRUE(waitFor([&] { return ports->property("opened").toBool(); }));
    // The Ethernet board at the settings' address.
    QQuickItem* ethernet = item("portEthernet");
    ASSERT_NE(ethernet, nullptr);
    EXPECT_EQ(ethernet->property("name").toString(), "192.168.5.1");
    EXPECT_EQ(ethernet->property("detail").toString(), "Ethernet (port 23)");
    // The simulated Grbl board (offered because of --simulator).
    ASSERT_TRUE(waitFor([&] { return item("portSimulator") && item("portSimulator")->height() > 0; }));
    EXPECT_TRUE(item("portSimulatorHal")->isVisible());
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

TEST_F(UiTest, TheKeyboardMapAndTheStatusIconsShowTheShortcuts) {
    // The map: the shortcuts that work now, by category; closing it turns it off.
    EXPECT_FALSE(item("keyboardMap")->isVisible());
    backend_->setKeyboardMap(true);
    ASSERT_TRUE(waitFor([&] { return item("keyboardMap")->isVisible(); }));
    ASSERT_TRUE(waitFor([&] { return item("keyboardMapGroup_Jogging") != nullptr; }));
    const QVariantList groups = backend_->activeShortcuts();
    ASSERT_FALSE(groups.isEmpty());
    EXPECT_EQ(groups.front().toMap()["category"].toString(), "General");
    bool jogRight = false;
    for (const QVariant& group : groups) {
        for (const QVariant& shortcut : group.toMap()["shortcuts"].toList()) {
            jogRight = jogRight || shortcut.toMap()["title"].toString() == "Jog X+ (right)";
        }
    }
    EXPECT_TRUE(jogRight);
    screenshot("ui_keyboard_map");
    tap("closeKeyboardMap");
    EXPECT_FALSE(machine_->settings().accessibility.showKeyboardMap);
    EXPECT_FALSE(item("keyboardMap")->isVisible());

    // The status icons open their tools.
    tap("statusKeyboard");
    ASSERT_TRUE(waitFor([&] { return item("keyboardShortcutsTool") && item("keyboardShortcutsTool")->isVisible(); }));
    tap("statusGamepad");
    ASSERT_TRUE(waitFor([&] { return item("gamepadTool") && item("gamepadTool")->isVisible(); }));
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

TEST_F(UiTest, PromptOnExitAsksBeforeClosing) {
    app::AppSettings settings = machine_->settings();
    settings.promptExit = true;
    machine_->setSettings(settings);
    QObject* prompt = window_->findChild<QObject*>("exitPrompt");
    ASSERT_NE(prompt, nullptr);
    window_->close();
    ASSERT_TRUE(waitFor([&] { return prompt->property("opened").toBool(); }));
    EXPECT_TRUE(window_->isVisible());
    QMetaObject::invokeMethod(prompt, "close");
}

TEST_F(UiTest, IconProviderRendersPiPuzzlePiece) {
    ui::IconProvider provider;
    QSize size;
    QImage img = provider.requestImage("PiPuzzlePiece/111827", &size, QSize(56, 56));
    EXPECT_FALSE(img.isNull());
    EXPECT_EQ(img.width(), 56);
    EXPECT_EQ(img.height(), 56);
    int nonTransparent = 0;
    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(img.pixel(x, y)) > 0) {
                ++nonTransparent;
            }
        }
    }
    EXPECT_GT(nonTransparent, 0) << "Image is completely transparent!";

    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_plugins") && item("toolCard_plugins")->isVisible(); }));
}

TEST_F(UiTest, WirelessControlServesThePendantFromTheTopBar) {
    app::RemoteService& remote = backend_->remote();
    // A port nothing else uses.
    ASSERT_EQ(remote.startServer("127.0.0.1", 0), "");
    const int port = remote.port();
    remote.stopServer();

    tap("statusRemote");
    QObject* dialog = window_->findChild<QObject*>("remoteDialog");
    ASSERT_NE(dialog, nullptr);
    ASSERT_TRUE(waitFor([&] { return dialog->property("opened").toBool(); }));
    EXPECT_EQ(dialog->property("port").toInt(), 8000);  // the default
    EXPECT_FALSE(item("remoteSave")->isEnabled());      // nothing changed yet
    EXPECT_EQ(text("remoteStatus"), "Wireless control is off.");

    dialog->setProperty("ip", "127.0.0.1");
    dialog->setProperty("port", port);
    tap("remoteSwitch");
    ASSERT_TRUE(waitFor([&] { return item("remoteSave")->isEnabled(); }));
    tap("remoteSave");
    ASSERT_TRUE(waitFor([&] { return remote.running(); }));
    EXPECT_EQ(remote.port(), port);
    EXPECT_TRUE(remote.settings().headlessStatus);
    const QString url = QString("http://127.0.0.1:%1/#/remote").arg(port);
    EXPECT_TRUE(waitFor([&] { return text("remoteStatus").startsWith("Serving the pendant at " + url); }));
    // The QR code holds the phone's address.
    QQuickItem* qr = item("remoteQr");
    ASSERT_NE(qr, nullptr);
    EXPECT_EQ(qr->property("text").toString(), url);
    QQuickItem* image = findItem(qr, "qrImage");
    ASSERT_TRUE(waitFor([&] { return image->property("status").toInt() == 1; }));  // Image.Ready
    screenshot("remote_dialog");

    // Off again.
    tap("remoteSwitch");
    tap("remoteSave");
    ASSERT_TRUE(waitFor([&] { return !remote.running(); }));
    EXPECT_FALSE(remote.settings().headlessStatus);
}

TEST_F(UiTest, HelpLinksOfferTheirQrCode) {
    backend_->showHelper("Alarm 1", "<p>Hard limit</p>",
                         "https://resources.sienci.com/view/gs-gsender-grbl-alarm-error-codes/#alarms");
    ASSERT_TRUE(waitFor([&] { return item("helperQr") && item("helperQr")->isVisible(); }));
    tap("helperQr");
    QObject* popup = window_->findChild<QObject*>("qrPopup");
    ASSERT_NE(popup, nullptr);
    ASSERT_TRUE(waitFor([&] { return popup->property("opened").toBool(); }));
    screenshot("helper_qr");

    backend_->showHelper("No link", "<p>text</p>");
    EXPECT_TRUE(waitFor([&] { return !item("helperQr")->isVisible(); }));
}

TEST_F(UiTest, QmlWarningsFailTheTest) {
    // A binding that throws is reported; the fixture fails the test on it.
    QQmlComponent component(engine_.get());
    component.setData("import QtQuick\nItem { width: nothing.x }", QUrl("qrc:/qml_warning_check.qml"));
    std::unique_ptr<QObject> object(component.create());
    ASSERT_NE(object, nullptr);
    ASSERT_TRUE(waitFor([] { return !qmlWarnings().isEmpty(); }));
    EXPECT_TRUE(qmlWarnings().front().contains("ReferenceError")) << qmlWarnings().front().toStdString();
    qmlWarnings().clear();  // expected here
}

TEST_F(UiTest, TheRailsHelperToggleHidesAndShowsTheHelperOnOffer) {
    // Nothing on offer: the toggle is there but does nothing.
    ASSERT_NE(item("helperToggle"), nullptr);
    EXPECT_FALSE(backend_->helperActive());
    tap("helperToggle");
    EXPECT_FALSE(backend_->helperVisible());

    backend_->showHelper("Alarm 1", "<p>Hard limit</p>");
    EXPECT_TRUE(backend_->helperActive());
    EXPECT_TRUE(backend_->helperVisible());
    // A tap minimizes it (the helper stays on offer), another brings it back.
    tap("helperToggle");
    EXPECT_TRUE(backend_->helperActive());
    EXPECT_FALSE(backend_->helperVisible());
    tap("helperToggle");
    EXPECT_TRUE(backend_->helperVisible());
    // Closing it takes the offer away.
    backend_->closeHelper();
    EXPECT_FALSE(backend_->helperActive());
    tap("helperToggle");
    EXPECT_FALSE(backend_->helperVisible());
}
