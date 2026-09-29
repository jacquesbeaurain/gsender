#include "plugin_bridge.hpp"
#include "plugin_manifest.hpp"
#include "plugin_service.hpp"
#include "plugin_storage.hpp"
#include "machine.hpp"
#include "qt_event_loop.hpp"

#include <gtest/gtest.h>
#include <QTemporaryDir>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>

using namespace gs::app;

TEST(PluginManifestTest, ParsesValidManifest) {
    const QString json = QString::fromUtf8(R"json({
        "id": "com.sienci.basic-cam",
        "name": "Basic CAM",
        "version": "1.2.3",
        "description": "Reference 2D/2.5D CAM toolpath generator",
        "author": "Sienci Labs",
        "engine": ">=1.5.0",
        "wasm": { "entry": "bin/cam.wasm" },
        "ui": {
            "entry": "qml/Main.qml",
            "contributions": [
                { "slot": "tools-page", "label": "Basic CAM", "icon": "tool" },
                { "slot": "visualizer-overlay", "requiresIdle": true }
            ]
        },
        "capabilities": {
            "requestTypes": ["machine:command", "gcode:load:to:visualizer"],
            "topics": ["workspace", "controller"]
        },
        "parsers": [
            { "pattern": "^;TOOL:(\\d+)", "type": "line" }
        ]
    })json");

    PluginManifest m;
    QString err;
    ASSERT_TRUE(parsePluginManifest(json, &m, &err)) << err.toStdString();
    EXPECT_EQ(m.id, "com.sienci.basic-cam");
    EXPECT_EQ(m.name, "Basic CAM");
    EXPECT_EQ(m.version, "1.2.3");
    EXPECT_TRUE(m.isOfficial());
    EXPECT_EQ(m.wasmEntry, "bin/cam.wasm");
    EXPECT_EQ(m.uiEntry, "qml/Main.qml");
    ASSERT_EQ(m.contributions.size(), 2);
    EXPECT_EQ(m.contributions[0].slot, "tools-page");
    EXPECT_EQ(m.contributions[0].label, "Basic CAM");
    EXPECT_FALSE(m.contributions[0].requiresIdle);
    EXPECT_EQ(m.contributions[1].slot, "visualizer-overlay");
    EXPECT_TRUE(m.contributions[1].requiresIdle);
    EXPECT_TRUE(m.capabilities.requestTypes.contains("machine:command"));
    EXPECT_TRUE(m.capabilities.requestTypes.contains("gcode:load:to:visualizer"));
    EXPECT_FALSE(m.capabilities.requestTypes.contains("storage:get"));
    EXPECT_TRUE(m.capabilities.topics.contains("workspace"));
    EXPECT_TRUE(m.capabilities.topics.contains("controller"));
    ASSERT_EQ(m.parsers.size(), 1);
    EXPECT_EQ(m.parsers[0].pattern, "^;TOOL:(\\d+)");
}

TEST(PluginManifestTest, RejectsMissingRequiredFields) {
    PluginManifest m;
    QString err;
    EXPECT_FALSE(parsePluginManifest("{}", &m, &err));
    EXPECT_FALSE(parsePluginManifest("{\"name\":\"Test\"}", &m, &err));
    EXPECT_FALSE(parsePluginManifest("{\"id\":\"com.test\"}", &m, &err));
}

TEST(PluginManifestTest, EvaluatesEngineCompatibility) {
    EXPECT_TRUE(isEngineCompatible("", "1.5.0"));
    EXPECT_TRUE(isEngineCompatible(">=1.5.0", "1.5.0"));
    EXPECT_TRUE(isEngineCompatible(">=1.5.0", "1.6.2"));
    EXPECT_FALSE(isEngineCompatible(">=1.5.0", "1.4.9"));
    EXPECT_TRUE(isEngineCompatible("^1.2.0", "1.5.0"));
    EXPECT_FALSE(isEngineCompatible("^1.2.0", "2.0.0"));
}

TEST(PluginStorageTest, HandlesIsolatedPersistentStorage) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    PluginStorage storage(tempDir.path());
    const QString p1 = "com.sienci.plugin1";
    const QString p2 = "com.community.plugin2";

    EXPECT_FALSE(storage.get(p1, "key1").has_value());
    EXPECT_TRUE(storage.set(p1, "key1", "val1"));
    EXPECT_EQ(storage.get(p1, "key1").value_or(""), "val1");

    // Plugin 2 cannot read or overwrite Plugin 1's key
    EXPECT_FALSE(storage.get(p2, "key1").has_value());
    EXPECT_TRUE(storage.set(p2, "key1", "val2"));
    EXPECT_EQ(storage.get(p1, "key1").value_or(""), "val1");
    EXPECT_EQ(storage.get(p2, "key1").value_or(""), "val2");

    // Bulk set and get
    QMap<QString, QString> batch{{"a", "1"}, {"b", "2"}};
    EXPECT_TRUE(storage.setAll(p1, batch));
    const auto all = storage.getAll(p1);
    EXPECT_EQ(all.size(), 3);
    EXPECT_EQ(all.value("key1"), "val1");
    EXPECT_EQ(all.value("a"), "1");
    EXPECT_EQ(all.value("b"), "2");

    // Delete single key
    EXPECT_TRUE(storage.deleteKey(p1, "a"));
    EXPECT_FALSE(storage.get(p1, "a").has_value());

    // Clear all
    EXPECT_TRUE(storage.clear(p1));
    EXPECT_TRUE(storage.getAll(p1).isEmpty());

    // Persistence across storage reload
    PluginStorage storage2(tempDir.path());
    EXPECT_EQ(storage2.get(p2, "key1").value_or(""), "val2");
}

TEST(PluginBridgeTest, EnforcesCapabilities) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QtEventLoop loop;
    Machine machine(loop, (tempDir.path() + "/rc").toStdWString());
    PluginStorage storage(tempDir.path());
    PluginBridge bridge(machine, storage);

    PluginManifest manifest;
    manifest.id = "com.sienci.test";
    manifest.name = "Test";
    manifest.version = "1.0.0";
    manifest.capabilities.requestTypes.insert("storage:get");
    manifest.capabilities.requestTypes.insert("storage:set");
    manifest.capabilities.requestTypes.insert("workspace:get:state");

    // Denied capability
    QJsonObject cmdPayload;
    cmdPayload.insert("command", "$H");
    BridgeResponse resp = bridge.execute(manifest, "machine:command", cmdPayload);
    EXPECT_FALSE(resp.ok);
    EXPECT_TRUE(resp.error.contains("Permission denied"));

    // Permitted capability: storage:set & storage:get
    QJsonObject setPayload;
    setPayload.insert("key", "testKey");
    setPayload.insert("value", "helloValue");
    resp = bridge.execute(manifest, "storage:set", setPayload);
    EXPECT_TRUE(resp.ok) << resp.error.toStdString();

    QJsonObject getPayload;
    getPayload.insert("key", "testKey");
    resp = bridge.execute(manifest, "storage:get", getPayload);
    ASSERT_TRUE(resp.ok) << resp.error.toStdString();
    EXPECT_TRUE(resp.result.value("found").toBool());
    EXPECT_EQ(resp.result.value("value").toString(), "helloValue");

    // Permitted capability: workspace:get:state
    resp = bridge.execute(manifest, "workspace:get:state", {});
    ASSERT_TRUE(resp.ok) << resp.error.toStdString();
    EXPECT_EQ(resp.result.value("units").toString(), "mm");
}

TEST(PluginServiceTest, DiscoversAndManagesPlugins) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QtEventLoop loop;
    Machine machine(loop, (tempDir.path() + "/rc").toStdWString());

    PluginService service(machine, tempDir.path());

    // Create a mock plugin directory with manifest
    const QString mockDir = tempDir.filePath("mock-plugin");
    ASSERT_TRUE(QDir().mkpath(mockDir));
    QFile manifestFile(mockDir + "/gsender-plugin.json");
    ASSERT_TRUE(manifestFile.open(QIODevice::WriteOnly));
    manifestFile.write(R"json({
        "id": "com.sienci.mock-tool",
        "name": "Mock Tool",
        "version": "1.0.0",
        "ui": {
            "entry": "Main.qml",
            "contributions": [
                { "slot": "tools-page", "label": "Mock Tool" }
            ]
        }
    })json");
    manifestFile.close();

    service.addSearchPath(tempDir.path());
    service.scanPlugins();

    ASSERT_EQ(service.plugins().size(), 1);
    const auto* p = service.findPlugin("com.sienci.mock-tool");
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->manifest.name, "Mock Tool");
    EXPECT_TRUE(p->enabled);

    // Contribution querying
    auto contribs = service.contributionsForSlot("tools-page");
    ASSERT_EQ(contribs.size(), 1);
    EXPECT_EQ(contribs[0].second.label, "Mock Tool");

    // Enable / disable
    bool changed = false;
    QObject::connect(&service, &PluginService::pluginsChanged, [&] { changed = true; });

    service.setPluginEnabled("com.sienci.mock-tool", false);
    EXPECT_TRUE(changed);
    EXPECT_FALSE(service.isPluginEnabled("com.sienci.mock-tool"));
    EXPECT_TRUE(service.contributionsForSlot("tools-page").empty());

    service.setPluginEnabled("com.sienci.mock-tool", true);
    EXPECT_TRUE(service.isPluginEnabled("com.sienci.mock-tool"));
    EXPECT_EQ(service.contributionsForSlot("tools-page").size(), 1);
}
