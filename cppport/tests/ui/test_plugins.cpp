// Plugins: discovering and toggling them, and the QML they add.

#include "ui_test.hpp"

#include "plugin_qml_context.hpp"
#include "plugin_service.hpp"
#include "plugins_model.hpp"

#include <QDir>
#include <QFile>

TEST_F(UiTest, ThePluginsToolDiscoversAndTogglesPlugins) {
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_plugins") && item("toolCard_plugins")->isVisible(); }));
    tap("toolCard_plugins");
    ASSERT_TRUE(waitFor([&] { return item("pluginsTool") && item("pluginsTool")->isVisible(); }));
    screenshot("ui_plugins_manager");

    // Test PluginsModel API directly with real Machine instance
    ui::PluginsModel model(*machine_);
    EXPECT_GE(model.count(), 0);
    EXPECT_EQ(model.filter(), "all");
    model.setFilter("official");
    EXPECT_EQ(model.filter(), "official");
    model.setFilter("all");

    const int initialCount = model.count();
    const int initialEnabled = model.enabledCount();

    // Create a temporary plugin to test live discovery and toggling
    QTemporaryDir pluginDir;
    const QString subDir = pluginDir.filePath("dro-enhancer");
    ASSERT_TRUE(QDir().mkpath(subDir));
    const QString manifestJson = QStringLiteral(R"json({
        "id": "com.sienci.dro-enhancer",
        "name": "DRO Enhancer",
        "version": "1.2.0",
        "description": "Custom DRO readouts and styling",
        "author": "Sienci Labs",
        "capabilities": {
            "requests": ["machine:command", "storage:get"],
            "topics": ["workspace"]
        },
        "ui": {
            "entry": "ui/Main.qml",
            "contributions": [
                {
                    "slot": "tools-page",
                    "label": "DRO Tools",
                    "icon": "PiPuzzlePiece",
                    "route": "dro-tools"
                }
            ]
        }
    })json");

    QFile manifestFile(subDir + "/gsender-plugin.json");
    ASSERT_TRUE(manifestFile.open(QIODevice::WriteOnly | QIODevice::Text));
    manifestFile.write(manifestJson.toUtf8());
    manifestFile.close();

    machine_->pluginService().addSearchPath(pluginDir.path());
    model.scan();

    ASSERT_EQ(model.count(), initialCount + 1);
    EXPECT_EQ(model.enabledCount(), initialEnabled + 1);
    EXPECT_TRUE(model.isEnabled("com.sienci.dro-enhancer"));

    const QVariantMap info = model.getPlugin("com.sienci.dro-enhancer");
    EXPECT_EQ(info["name"].toString(), "DRO Enhancer");
    EXPECT_TRUE(info["official"].toBool());

    const QVariantList contribs = model.contributions("tools-page");
    bool foundDroTools = false;
    for (const auto& item : contribs) {
        if (item.toMap()["label"].toString() == "DRO Tools") {
            foundDroTools = true;
            break;
        }
    }
    EXPECT_TRUE(foundDroTools);

    // Disable plugin
    model.setEnabled("com.sienci.dro-enhancer", false);
    EXPECT_FALSE(model.isEnabled("com.sienci.dro-enhancer"));
    EXPECT_EQ(model.enabledCount(), initialEnabled);

    // Filter testing by searching for the temporary plugin
    model.setSearch("DRO Enhancer");
    EXPECT_EQ(model.count(), 1);
    model.setFilter("enabled");
    EXPECT_EQ(model.count(), 0);
    model.setFilter("all");
    EXPECT_EQ(model.count(), 1);
    model.setSearch("");

    // Go back from Plugins tool to Tools page
    tap("toolGoBack");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_plugins") && item("toolCard_plugins")->isVisible(); }));
}

TEST_F(UiTest, PluginQmlContextAndDynamicSlotHosting) {
    window_->resize(1400, 1100);
    ASSERT_TRUE(waitFor([&] { return window_->height() == 1100; }));

    QTemporaryDir pluginDir;
    const QString subDir = pluginDir.filePath("host-test");
    ASSERT_TRUE(QDir().mkpath(subDir + "/ui"));

    const QString manifestJson = QStringLiteral(R"json({
        "id": "com.sienci.host-test",
        "name": "Host Test Plugin",
        "version": "1.0.0",
        "description": "Tests QML context and slot hosting",
        "author": "Sienci Labs",
        "capabilities": {
            "requestTypes": ["machine:command", "storage:get", "storage:set"],
            "topics": ["workspace"]
        },
        "ui": {
            "entry": "ui/Main.qml",
            "contributions": [
                {
                    "slot": "tools-page",
                    "label": "Host Tool",
                    "icon": "PiPuzzlePiece",
                    "route": "host-tool"
                },
                {
                    "slot": "tools-tab",
                    "label": "Host Tab",
                    "icon": "PiPuzzlePiece",
                    "route": "host-tab"
                },
                {
                    "slot": "visualizer-overlay",
                    "label": "Host Overlay",
                    "icon": "PiPuzzlePiece",
                    "route": "host-overlay"
                }
            ]
        }
    })json");

    QFile manifestFile(subDir + "/gsender-plugin.json");
    ASSERT_TRUE(manifestFile.open(QIODevice::WriteOnly | QIODevice::Text));
    manifestFile.write(manifestJson.toUtf8());
    manifestFile.close();

    const QString qmlContent = QStringLiteral(R"qml(
import QtQuick
import QtQuick.Controls.Basic

Rectangle {
    id: pluginRoot
    objectName: "hostTestPluginRoot"
    property var gsender: null
    width: 200; height: 100
    color: "lightblue"
    Text {
        id: label
        objectName: "hostTestPluginText"
        text: pluginRoot.gsender ? pluginRoot.gsender.pluginName : "no-context"
    }
}
)qml");

    QFile qmlFile(subDir + "/ui/Main.qml");
    ASSERT_TRUE(qmlFile.open(QIODevice::WriteOnly | QIODevice::Text));
    qmlFile.write(qmlContent.toUtf8());
    qmlFile.close();

    machine_->pluginService().addSearchPath(pluginDir.path());
    ui::PluginsModel model(*machine_);
    model.scan();

    // Verify context creation and properties
    QObject* ctxObj = model.createContext("com.sienci.host-test");
    ASSERT_NE(ctxObj, nullptr);
    auto* ctx = qobject_cast<ui::PluginQmlContext*>(ctxObj);
    ASSERT_NE(ctx, nullptr);
    EXPECT_EQ(ctx->pluginId(), "com.sienci.host-test");
    EXPECT_EQ(ctx->pluginName(), "Host Test Plugin");
    EXPECT_EQ(ctx->version(), "1.0.0");
    EXPECT_TRUE(ctx->official());

    // Test storage capability through context
    ctx->storageSet("testSetting", "42");
    EXPECT_EQ(ctx->storageGet("testSetting").toString(), "42");

    // Test unauthorized request rejection
    QVariantMap unauth = ctx->send("gcode:load:to:visualizer", {});
    EXPECT_FALSE(unauth["ok"].toBool());

    // Test authorized request
    connectSimulator();
    QVariantMap auth = ctx->send("machine:command", {{"command", "$$"}});
    EXPECT_TRUE(auth["ok"].toBool());

    // Test slot contribution queries
    const QVariantList pageContribs = model.contributions("tools-page");
    EXPECT_FALSE(pageContribs.isEmpty());
    const QVariantList tabContribs = model.contributions("tools-tab");
    EXPECT_FALSE(tabContribs.isEmpty());
    const QVariantList overlayContribs = model.contributions("visualizer-overlay");
    EXPECT_FALSE(overlayContribs.isEmpty());
    // The model makes the cards and tabs of them.
    const QVariantList cards = model.toolCards();
    ASSERT_FALSE(cards.isEmpty());
    EXPECT_EQ(cards.back().toMap()["key"].toString(), "plugin:com.sienci.host-test:host-tool");
    EXPECT_EQ(cards.back().toMap()["title"].toString(), "Host Tool");
    const QVariantList tabs = model.toolTabs();
    ASSERT_FALSE(tabs.isEmpty());
    EXPECT_EQ(tabs.back().toMap()["key"].toString(), "plugin:com.sienci.host-test:host-tab");
    ASSERT_TRUE(waitFor([&] { return item("toolsTab_plugin:com.sienci.host-test:host-tab") != nullptr; }));

    // Navigate to Tools and verify dynamic card
    tap("navTools");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_plugin:com.sienci.host-test:host-tool"); }));
    QTest::qWait(100);

    // Scroll flickable so bottom card is in view
    auto* flickable = item("toolsFlickable");
    auto* card = item("toolCard_plugin:com.sienci.host-test:host-tool");
    if (flickable && card) {
        flickable->setProperty("contentY", card->y());
        QCoreApplication::processEvents();
        QTest::qWait(50);
    }

    tap("toolCard_plugin:com.sienci.host-test:host-tool");
    QTest::qWait(100);

    // Inside loaded tool page
    ASSERT_TRUE(waitFor([&] { return item("toolGoBack"); }));
    EXPECT_EQ(text("hostTestPluginText"), "Host Test Plugin");

    // Return to Tools hub
    tap("toolGoBack");
    ASSERT_TRUE(waitFor([&] { return item("toolCard_plugin:com.sienci.host-test:host-tool"); }));

    window_->resize(1400, 900);
}
