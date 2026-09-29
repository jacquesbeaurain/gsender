#include "wasm_engine.hpp"
#include "plugin_wasm_host.hpp"
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

TEST(PluginServiceTest, MirroredReferencePluginsSuiteValidation) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QtEventLoop loop;
    Machine machine(loop, (tempDir.path() + "/rc").toStdWString());
    PluginService service(machine, tempDir.path());

    // Point to repo plugins/
    const QString repoPluginsPath = QStringLiteral(GS_SOURCE_DIR "/plugins");
    service.addSearchPath(repoPluginsPath);
    service.scanPlugins();

    // Verify all 6 mirrored plugins exist
    const QStringList expectedPluginIds = {
        "com.sienci.example-hello",
        "com.sienci.storage-test",
        "com.sienci.controller-events-demo",
        "com.sienci.basic-cam",
        "com.sienci.corner-finder",
        "com.sienci.parser-demo"
    };

    for (const QString& id : expectedPluginIds) {
        const auto* p = service.findPlugin(id);
        ASSERT_NE(p, nullptr) << "Missing plugin: " << id.toStdString();
        EXPECT_TRUE(p->enabled);
        EXPECT_FALSE(p->manifest.name.isEmpty());
        EXPECT_FALSE(p->manifest.uiEntry.isEmpty());
        // Verify UI entry file exists on disk
        const QString uiFile = QDir(p->directory).filePath(p->manifest.uiEntry);
        EXPECT_TRUE(QFile::exists(uiFile)) << "Missing UI entry: " << uiFile.toStdString();
    }

    // Check specific capabilities & slot contributions
    const auto* cam = service.findPlugin("com.sienci.basic-cam");
    ASSERT_NE(cam, nullptr);
    EXPECT_TRUE(cam->manifest.capabilities.requestTypes.contains("gcode:load:to:visualizer"));

    const auto* corner = service.findPlugin("com.sienci.corner-finder");
    ASSERT_NE(corner, nullptr);
    EXPECT_TRUE(corner->manifest.capabilities.requestTypes.contains("viewer:camera:set"));

    const auto* parser = service.findPlugin("com.sienci.parser-demo");
    ASSERT_NE(parser, nullptr);
    EXPECT_GE(parser->manifest.parsers.size(), 2u);

    // Verify slot contributions across suite
    const auto toolsPageContribs = service.contributionsForSlot("tools-page");
    EXPECT_GE(toolsPageContribs.size(), 4u);

    const auto toolsTabContribs = service.contributionsForSlot("tools-tab");
    EXPECT_GE(toolsTabContribs.size(), 2u);

    const auto overlayContribs = service.contributionsForSlot("visualizer-overlay");
    EXPECT_GE(overlayContribs.size(), 1u);
}

namespace {

static const uint8_t kTestWasmBytes[] = {
    0x00, 0x61, 0x73, 0x6D, 0x01, 0x00, 0x00, 0x00, 0x01, 0x19, 0x05, 0x60, 0x00, 0x01, 0x7F, 0x60,
    0x00, 0x00, 0x60, 0x03, 0x7F, 0x7F, 0x7F, 0x01, 0x7F, 0x60, 0x02, 0x7F, 0x7F, 0x00, 0x60, 0x01,
    0x7F, 0x01, 0x7F, 0x02, 0x2C, 0x02, 0x03, 0x65, 0x6E, 0x76, 0x12, 0x67, 0x73, 0x5F, 0x68, 0x6F,
    0x73, 0x74, 0x5F, 0x65, 0x6D, 0x69, 0x74, 0x5F, 0x67, 0x63, 0x6F, 0x64, 0x65, 0x00, 0x04, 0x03,
    0x65, 0x6E, 0x76, 0x0B, 0x67, 0x73, 0x5F, 0x68, 0x6F, 0x73, 0x74, 0x5F, 0x6C, 0x6F, 0x67, 0x00,
    0x03, 0x03, 0x09, 0x08, 0x00, 0x01, 0x02, 0x03, 0x00, 0x00, 0x00, 0x04, 0x05, 0x04, 0x01, 0x01,
    0x01, 0x10, 0x07, 0xBA, 0x01, 0x09, 0x13, 0x67, 0x73, 0x65, 0x6E, 0x64, 0x65, 0x72, 0x5F, 0x70,
    0x6C, 0x75, 0x67, 0x69, 0x6E, 0x5F, 0x69, 0x6E, 0x69, 0x74, 0x00, 0x02, 0x17, 0x67, 0x73, 0x65,
    0x6E, 0x64, 0x65, 0x72, 0x5F, 0x70, 0x6C, 0x75, 0x67, 0x69, 0x6E, 0x5F, 0x73, 0x68, 0x75, 0x74,
    0x64, 0x6F, 0x77, 0x6E, 0x00, 0x03, 0x1D, 0x67, 0x73, 0x65, 0x6E, 0x64, 0x65, 0x72, 0x5F, 0x70,
    0x6C, 0x75, 0x67, 0x69, 0x6E, 0x5F, 0x68, 0x61, 0x6E, 0x64, 0x6C, 0x65, 0x5F, 0x72, 0x65, 0x71,
    0x75, 0x65, 0x73, 0x74, 0x00, 0x04, 0x1D, 0x67, 0x73, 0x65, 0x6E, 0x64, 0x65, 0x72, 0x5F, 0x70,
    0x6C, 0x75, 0x67, 0x69, 0x6E, 0x5F, 0x6F, 0x6E, 0x5F, 0x74, 0x6F, 0x70, 0x69, 0x63, 0x5F, 0x65,
    0x76, 0x65, 0x6E, 0x74, 0x00, 0x05, 0x10, 0x74, 0x72, 0x61, 0x70, 0x5F, 0x75, 0x6E, 0x72, 0x65,
    0x61, 0x63, 0x68, 0x61, 0x62, 0x6C, 0x65, 0x00, 0x06, 0x0D, 0x74, 0x72, 0x61, 0x70, 0x5F, 0x64,
    0x69, 0x76, 0x5F, 0x7A, 0x65, 0x72, 0x6F, 0x00, 0x07, 0x0F, 0x74, 0x72, 0x61, 0x70, 0x5F, 0x6D,
    0x65, 0x6D, 0x6F, 0x72, 0x79, 0x5F, 0x6F, 0x6F, 0x62, 0x00, 0x08, 0x08, 0x61, 0x64, 0x64, 0x5F,
    0x66, 0x69, 0x76, 0x65, 0x00, 0x09, 0x06, 0x6D, 0x65, 0x6D, 0x6F, 0x72, 0x79, 0x02, 0x00, 0x0A,
    0x5B, 0x08, 0x04, 0x00, 0x41, 0x00, 0x0B, 0x02, 0x00, 0x0B, 0x26, 0x00, 0x20, 0x00, 0x10, 0x00,
    0x1A, 0x20, 0x01, 0x41, 0xFB, 0x00, 0x3A, 0x00, 0x00, 0x20, 0x01, 0x41, 0x01, 0x6A, 0x41, 0xFD,
    0x00, 0x3A, 0x00, 0x00, 0x20, 0x01, 0x41, 0x02, 0x6A, 0x41, 0x00, 0x3A, 0x00, 0x00, 0x41, 0x02,
    0x0B, 0x08, 0x00, 0x41, 0x01, 0x20, 0x00, 0x10, 0x01, 0x0B, 0x05, 0x00, 0x00, 0x41, 0x00, 0x0B,
    0x07, 0x00, 0x41, 0x2A, 0x41, 0x00, 0x6D, 0x0B, 0x0B, 0x00, 0x41, 0xFF, 0xFF, 0xFF, 0xFF, 0x07,
    0x28, 0x00, 0x00, 0x0B, 0x07, 0x00, 0x20, 0x00, 0x41, 0x05, 0x6A, 0x0B
};
static const size_t kTestWasmBytesLen = sizeof(kTestWasmBytes);

} // namespace

TEST(WasmEngineTest, InstantiatesAndExecutesArithmetic) {
    std::string err;
    auto module = WasmModule::loadFromBytes(kTestWasmBytes, kTestWasmBytesLen, &err);
    ASSERT_NE(module, nullptr) << err;

    WasmInstance inst(module);
    inst.linkHostFunction("env", "gs_host_emit_gcode", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> {
        return int32_t(0);
    });
    inst.linkHostFunction("env", "gs_host_log", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> {
        return std::nullopt;
    });

    ASSERT_TRUE(inst.instantiate(&err)) << err;

    // Test add_five(37) -> 42
    auto res = inst.invoke("add_five", {int32_t(37)});
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(std::get<int32_t>(*res), 42);

    // Test add_five(-10) -> -5
    res = inst.invoke("add_five", {int32_t(-10)});
    ASSERT_TRUE(res.has_value());
    EXPECT_EQ(std::get<int32_t>(*res), -5);
}

TEST(WasmEngineTest, EnforcesLinearMemoryBoundsAndTraps) {
    std::string err;
    auto module = WasmModule::loadFromBytes(kTestWasmBytes, kTestWasmBytesLen, &err);
    ASSERT_NE(module, nullptr) << err;

    WasmInstance inst(module);
    inst.linkHostFunction("env", "gs_host_emit_gcode", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> { return int32_t(0); });
    inst.linkHostFunction("env", "gs_host_log", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> { return std::nullopt; });
    ASSERT_TRUE(inst.instantiate(&err)) << err;

    // Linear memory bounds checks on WasmMemory API
    EXPECT_GT(inst.memory().sizeBytes(), 0u);
    uint8_t buf[16];
    EXPECT_FALSE(inst.memory().read(0xFFFFFFFF, buf, sizeof(buf)));
    EXPECT_FALSE(inst.memory().write(0xFFFFFFFF, buf, sizeof(buf)));

    // Executing out of bounds memory load traps without crashing host process
    auto res = inst.invoke("trap_memory_oob");
    EXPECT_FALSE(res.has_value());
    EXPECT_EQ(inst.lastTrap(), WasmTrap::OutOfBoundsMemoryAccess);
}

TEST(WasmEngineTest, TrapsUnreachableAndDivideByZero) {
    std::string err;
    auto module = WasmModule::loadFromBytes(kTestWasmBytes, kTestWasmBytesLen, &err);
    ASSERT_NE(module, nullptr) << err;

    WasmInstance inst(module);
    inst.linkHostFunction("env", "gs_host_emit_gcode", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> { return int32_t(0); });
    inst.linkHostFunction("env", "gs_host_log", [](WasmInstance&, const std::vector<WasmVal>&) -> std::optional<WasmVal> { return std::nullopt; });
    ASSERT_TRUE(inst.instantiate(&err)) << err;

    // Test unreachable opcode trap
    auto resUnreach = inst.invoke("trap_unreachable");
    EXPECT_FALSE(resUnreach.has_value());
    EXPECT_EQ(inst.lastTrap(), WasmTrap::Unreachable);

    // Test division by zero trap
    auto resDivZero = inst.invoke("trap_div_zero");
    EXPECT_FALSE(resDivZero.has_value());
    EXPECT_EQ(inst.lastTrap(), WasmTrap::DivisionByZero);
}

TEST(WasmEngineTest, PluginWasmHostLifecycleAndRpc) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QtEventLoop loop;
    Machine machine(loop, (tempDir.path() + "/rc").toStdWString());
    PluginStorage storage(tempDir.path());
    PluginBridge bridge(machine, storage);

    PluginManifest manifest;
    manifest.id = "com.sienci.wasm-test";
    manifest.name = "Wasm Test Plugin";
    manifest.version = "1.0.0";
    manifest.capabilities.requestTypes.insert("machine:command");
    manifest.capabilities.topics.insert("workspace");

    PluginWasmHost host(machine, bridge, storage, manifest);
    QString err;
    ASSERT_TRUE(host.loadBinary(kTestWasmBytes, kTestWasmBytesLen, &err)) << err.toStdString();
    EXPECT_TRUE(host.isLoaded());

    // Test initialization lifecycle
    EXPECT_TRUE(host.init());

    // Test RPC request handling
    const QString resp = host.handleRequest("{\"cmd\":\"test\"}");
    EXPECT_FALSE(resp.isEmpty());
    EXPECT_TRUE(resp.startsWith("{"));

    // Test topic event dispatch
    QJsonObject eventData;
    eventData.insert("x", 123.456);
    host.onTopicEvent("workspace", eventData);

    // Test shutdown lifecycle
    host.shutdown();
}

TEST(PluginServiceTest, WasmModuleAutoDiscoveryAndRpc) {
    QTemporaryDir tempDir;
    ASSERT_TRUE(tempDir.isValid());

    QtEventLoop loop;
    Machine machine(loop, (tempDir.path() + "/rc").toStdWString());
    PluginService service(machine, tempDir.path());

    // Create a temporary plugin folder with manifest and wasm binary
    const QString pdir = tempDir.filePath("test-wasm-plugin");
    ASSERT_TRUE(QDir().mkpath(pdir + "/bin"));

    const QString manifestJson = QStringLiteral(R"json({
        "id": "com.sienci.test-wasm-auto",
        "name": "Auto Wasm Test",
        "version": "1.0.0",
        "wasm": { "entry": "bin/plugin.wasm" },
        "capabilities": {
            "requestTypes": ["machine:command"],
            "topics": ["workspace"]
        }
    })json");

    QFile manifestFile(pdir + "/gsender-plugin.json");
    ASSERT_TRUE(manifestFile.open(QIODevice::WriteOnly | QIODevice::Text));
    manifestFile.write(manifestJson.toUtf8());
    manifestFile.close();

    QFile wasmFile(pdir + "/bin/plugin.wasm");
    ASSERT_TRUE(wasmFile.open(QIODevice::WriteOnly));
    wasmFile.write(reinterpret_cast<const char*>(kTestWasmBytes), kTestWasmBytesLen);
    wasmFile.close();

    service.addSearchPath(tempDir.path());
    service.scanPlugins();

    // Verify Wasm host was instantiated and active
    PluginWasmHost* host = service.wasmHost("com.sienci.test-wasm-auto");
    ASSERT_NE(host, nullptr);
    EXPECT_TRUE(host->isLoaded());

    // Execute RPC request via service
    const QString reply = service.executeWasmRequest("com.sienci.test-wasm-auto", "{\"ping\":true}");
    EXPECT_FALSE(reply.isEmpty());
    EXPECT_TRUE(reply.startsWith("{"));

    // Disable plugin: Wasm host should be unloaded
    service.setPluginEnabled("com.sienci.test-wasm-auto", false);
    EXPECT_EQ(service.wasmHost("com.sienci.test-wasm-auto"), nullptr);

    // Re-enable plugin: Wasm host should be reloaded
    service.setPluginEnabled("com.sienci.test-wasm-auto", true);
    EXPECT_NE(service.wasmHost("com.sienci.test-wasm-auto"), nullptr);
}
