// Plugin response parsers (the chain on its own, then through the bridge
// against the simulated board): manifest and runtime parsers, machine:query
// and the busy latch.

#include "app_test_support.hpp"
#include "machine.hpp"
#include "plugin_bridge.hpp"
#include "plugin_parsers.hpp"
#include "plugin_service.hpp"
#include "plugin_storage.hpp"
#include "plugin_wasm_host.hpp"
#include "qt_event_loop.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <vector>

using namespace gs::app;
using namespace gs::app::test_support;

namespace {

struct Delivered {
    QString pluginId;
    QString topic;
    QJsonObject data;
};

QJsonArray specs(const char* json) {
    return QJsonDocument::fromJson(json).array();
}

class ParserChainTest : public ::testing::Test {
protected:
    void SetUp() override { application(); }

    std::vector<Delivered> delivered;
    bool idle = true;
    PluginParserChain chain{[this](const QString& id, const QString& topic, const QJsonObject& data) {
                                delivered.push_back({id, topic, data});
                            },
                            [this] { return idle; }};

    std::vector<QJsonObject> matches() const {
        std::vector<QJsonObject> out;
        for (const auto& d : delivered) {
            if (!d.data.value("error").toBool()) out.push_back(d.data);
        }
        return out;
    }
    std::vector<QJsonObject> problems() const {
        std::vector<QJsonObject> out;
        for (const auto& d : delivered) {
            if (d.data.value("error").toBool()) out.push_back(d.data);
        }
        return out;
    }
};

}  // namespace

TEST(PluginParserRegexTest, ScreensCatastrophicPatterns) {
    EXPECT_TRUE(assessParserRegexRisk(R"(^\[PRB:([-\d.]+),([-\d.]+)\])").isEmpty());
    EXPECT_FALSE(assessParserRegexRisk("(a+)+$").isEmpty());
    EXPECT_FALSE(assessParserRegexRisk("(a|a)*b").isEmpty());
    EXPECT_FALSE(assessParserRegexRisk("x{1,5000}").isEmpty());
    EXPECT_FALSE(assessParserRegexRisk("(unclosed").isEmpty());
    EXPECT_FALSE(compilePluginRegex(QJsonValue(42)).has_value());
    const auto re = compilePluginRegex(QJsonObject{{"source", "^ok$"}, {"flags", "gi"}});
    ASSERT_TRUE(re.has_value());
    EXPECT_TRUE(re->match("OK").hasMatch());
}

TEST_F(ParserChainTest, LineParsersDeliverMatchesToTheirPluginOnly) {
    const auto rejected = chain.setManifestParsers(
        "com.example.a", specs(R"([{"id":"probe","match":"^\\[PRB:(?<x>[-\\d.]+),(?<y>[-\\d.]+),(?<z>[-\\d.]+):(?<ok>[01])\\]"}])"));
    EXPECT_TRUE(rejected.empty());
    chain.setManifestParsers("com.example.b", specs(R"([{"id":"msg","match":"^\\[MSG:(.*)\\]"}])"));

    chain.feed("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    chain.feed("[PRB:1.000,2.000,-3.500:1]");
    chain.feed("ok");

    const auto got = matches();
    ASSERT_EQ(got.size(), 1u);
    EXPECT_EQ(delivered[0].pluginId, "com.example.a");
    EXPECT_EQ(delivered[0].topic, "parser");
    EXPECT_EQ(got[0].value("parserId").toString(), "probe");
    EXPECT_EQ(got[0].value("line").toString(), "[PRB:1.000,2.000,-3.500:1]");
    EXPECT_EQ(got[0].value("groups").toObject().value("z").toString(), "-3.500");
    EXPECT_EQ(got[0].value("seq").toInt(), 1);
}

TEST_F(ParserChainTest, BlockParsersGatherLinesUntilTheirEnd) {
    chain.setManifestParsers("com.example.a", specs(R"([{
        "id": "settings", "mode": "block", "begin": "^\\$0=", "match": "^\\$(?<key>\\d+)=(?<value>.*)$",
        "until": "ok", "whenWorkflow": "idle"
    }])"));
    chain.feed("$0=10");
    chain.feed("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");  // status reports are ignored inside a block
    chain.feed("$1=25");
    chain.feed("ok");

    const auto got = matches();
    ASSERT_EQ(got.size(), 1u);
    EXPECT_TRUE(got[0].value("complete").toBool());
    EXPECT_EQ(got[0].value("reason").toString(), "until");
    EXPECT_EQ(got[0].value("lines").toArray().size(), 3);
    const QJsonArray entries = got[0].value("entries").toArray();
    ASSERT_EQ(entries.size(), 2);
    EXPECT_EQ(entries[1].toObject().value("groups").toObject().value("value").toString(), "25");

    // "idle" parsers sit out a running job.
    idle = false;
    chain.feed("$0=10");
    chain.feed("ok");
    EXPECT_EQ(matches().size(), 1u);
}

TEST_F(ParserChainTest, RejectsBadSpecsAndReportsThem) {
    const auto rejected = chain.setManifestParsers("com.example.a", specs(R"([
        {"id": "risky", "match": "(a+)+$"},
        {"id": "no-match"},
        {"id": "bad id!", "match": "x"},
        {"id": "block", "mode": "block", "begin": "^x"},
        {"id": "fine", "match": "^ok$"}
    ])"));
    EXPECT_EQ(rejected.size(), 4u);
    EXPECT_EQ(chain.parserCount("com.example.a"), 1);
    EXPECT_EQ(problems().size(), 4u);
    EXPECT_EQ(problems()[0].value("reason").toString(), "invalid-spec");
}

TEST_F(ParserChainTest, RateLimitsAParserAndCapsParsersPerPlugin) {
    chain.setManifestParsers("com.example.a", specs(R"([{"id":"any","match":"^line"}])"));
    for (int i = 0; i < 50; ++i) chain.feed(QStringLiteral("line %1").arg(i));
    EXPECT_EQ(matches().size(), static_cast<size_t>(PluginParserChain::MaxEmitsPerSecond));
    ASSERT_EQ(problems().size(), 1u);
    EXPECT_EQ(problems()[0].value("reason").toString(), "rate-limited");

    QJsonArray many;
    for (int i = 0; i < 20; ++i) many.append(QJsonObject{{"id", QStringLiteral("p%1").arg(i)}, {"match", "^x"}});
    const QJsonObject outcome = chain.registerRuntime("com.example.a", many);
    EXPECT_EQ(chain.parserCount("com.example.a"), PluginParserChain::MaxParsersPerPlugin);
    EXPECT_FALSE(outcome.value("errors").toArray().isEmpty());

    chain.unregisterRuntime("com.example.a");
    EXPECT_EQ(chain.parserCount("com.example.a"), 1);  // the manifest's stays
    chain.removePlugin("com.example.a");
    EXPECT_EQ(chain.parserCount("com.example.a"), 0);
}

TEST_F(ParserChainTest, CapturesAQueryReply) {
    QJsonObject result;
    PluginParserChain::QueryOptions options;
    ASSERT_TRUE(chain.beginCapture(options, [&](const QJsonObject& r) { result = r; }));
    EXPECT_FALSE(chain.beginCapture(options, {}));  // one at a time
    chain.feed("[GC:G0 G54 G17 G21 G90 G94 M5 M9 T0 F0 S0]");
    chain.feed("<Idle|MPos:0.000,0.000,0.000|FS:0,0>");
    chain.feed("ok");
    EXPECT_FALSE(chain.captureActive());
    EXPECT_TRUE(result.value("ok").toBool());
    EXPECT_TRUE(result.value("complete").toBool());
    EXPECT_EQ(result.value("lines").toArray().size(), 2);  // no status report

    // A connection that closes ends the capture.
    result = {};
    ASSERT_TRUE(chain.beginCapture(options, [&](const QJsonObject& r) { result = r; }));
    chain.feed("[MSG:partial]");
    chain.reset();
    EXPECT_FALSE(result.value("complete").toBool());
    EXPECT_FALSE(chain.captureActive());
}

// ---- through the bridge, against the simulated board ----------------------------------

namespace {

class PluginBridgeLiveTest : public ::testing::Test {
protected:
    void SetUp() override {
        application();
        manifest.id = "com.example.live";
        manifest.capabilities.requestTypes = {"machine:query", "machine:parser:register", "machine:parser:unregister",
                                              "machine:busy:set", "machine:command"};
        QObject::connect(&bridge, &PluginBridge::pluginEvent,
                         [this](const QString& id, const QString& topic, const QJsonObject& data) {
                             if (topic == "parser" || topic == "query") events.push_back({id, topic, data});
                         });
    }
    void connectMachine() {
        machine.connectTo(Machine::kSimulatorPort);
        ASSERT_TRUE(waitFor([&] { return machine.isConnected(); }));
    }
    const Delivered* find(const QString& topic) const {
        for (const auto& e : events) {
            if (e.topic == topic) return &e;
        }
        return nullptr;
    }

    QTemporaryDir dir;
    QtEventLoop loop;
    Machine machine{loop, (dir.path() + "/rc").toStdWString()};
    PluginStorage storage{dir.path()};
    PluginBridge bridge{machine, storage};
    PluginManifest manifest;
    std::vector<Delivered> events;
};

}  // namespace

TEST_F(PluginBridgeLiveTest, QueryCapturesTheBoardsReply) {
    EXPECT_FALSE(bridge.execute(manifest, "machine:query", {{"cmd", "$G"}}).ok);  // not connected
    connectMachine();
    EXPECT_FALSE(bridge.execute(manifest, "machine:query", {}).ok);
    EXPECT_FALSE(bridge.execute(manifest, "machine:query", {{"cmd", "$G"}, {"opts", QJsonObject{{"until", "never"}}}}).ok);

    const auto started = bridge.execute(manifest, "machine:query", {{"cmd", "$G"}});
    ASSERT_TRUE(started.ok) << started.error.toStdString();
    // One at a time.
    EXPECT_FALSE(bridge.execute(manifest, "machine:query", {{"cmd", "$G"}}).ok);
    ASSERT_TRUE(waitFor([&] { return find("query") != nullptr; }));
    const Delivered& reply = *find("query");
    EXPECT_EQ(reply.pluginId, manifest.id);
    EXPECT_EQ(reply.data.value("queryId").toInt(), started.result.value("queryId").toInt());
    EXPECT_TRUE(reply.data.value("ok").toBool()) << QJsonDocument(reply.data).toJson().toStdString();
    EXPECT_TRUE(reply.data.value("lines").toArray().first().toString().startsWith("[GC:"));
}

TEST_F(PluginBridgeLiveTest, RuntimeParsersSeeTheBoardsLines) {
    connectMachine();
    EXPECT_FALSE(bridge.execute(manifest, "machine:parser:register", {}).ok);
    EXPECT_FALSE(bridge.execute(manifest, "machine:parser:register",
                                {{"spec", QJsonObject{{"id", "bad"}, {"match", "(a+)+$"}}}})
                     .ok);
    const auto registered = bridge.execute(
        manifest, "machine:parser:register",
        {{"spec", QJsonObject{{"id", "modal"}, {"match", "^\\[GC:(?<modes>[^\\]]+)\\]"}}}});
    ASSERT_TRUE(registered.ok) << registered.error.toStdString();

    ASSERT_TRUE(bridge.execute(manifest, "machine:command", {{"command", "$G"}}).ok);
    ASSERT_TRUE(waitFor([&] { return find("parser") != nullptr; }));
    EXPECT_EQ(find("parser")->pluginId, manifest.id);
    EXPECT_TRUE(find("parser")->data.value("groups").toObject().value("modes").toString().contains("G54"));

    ASSERT_TRUE(bridge.execute(manifest, "machine:parser:unregister", {{"id", "modal"}}).ok);
    EXPECT_EQ(bridge.parsers().parserCount(manifest.id), 0);
}

TEST_F(PluginBridgeLiveTest, BusyLatchReleasesWhenMotionNeverStarts) {
    EXPECT_FALSE(bridge.execute(manifest, "machine:busy:set", {{"busy", true}}).ok);  // not connected
    connectMachine();
    int changes = 0;
    QObject::connect(&bridge, &PluginBridge::busyChanged, [&] { ++changes; });
    ASSERT_TRUE(bridge.execute(manifest, "machine:busy:set", {{"busy", true}, {"label", "Drilling"}}).ok);
    EXPECT_TRUE(bridge.busy());
    EXPECT_EQ(bridge.busyLabel(), "Drilling");

    // Only its owner (or the host) clears it.
    PluginManifest other = manifest;
    other.id = "com.example.other";
    ASSERT_TRUE(bridge.execute(other, "machine:busy:set", {{"busy", false}}).ok);
    EXPECT_TRUE(bridge.busy());
    ASSERT_TRUE(bridge.execute(manifest, "machine:busy:set", {{"busy", false}}).ok);
    EXPECT_FALSE(bridge.busy());

    // Disconnecting releases it.
    ASSERT_TRUE(bridge.execute(manifest, "machine:busy:set", {{"busy", true}}).ok);
    machine.disconnectFromMachine();
    ASSERT_TRUE(waitFor([&] { return !bridge.busy(); }));
    EXPECT_EQ(changes, 4);
}

TEST_F(PluginBridgeLiveTest, ManifestParsersReachThePluginsWasm) {
    // The parser-demo example counts its parser matches.
    PluginService service(machine, dir.path() + "/storage");
    service.addSearchPath(QStringLiteral(GS_SOURCE_DIR "/plugins"));
    service.scanPlugins();
    ASSERT_NE(service.wasmHost("com.sienci.parser-demo"), nullptr);
    EXPECT_EQ(service.bridge().parsers().parserCount("com.sienci.parser-demo"), 2);

    connectMachine();
    machine.sendConsoleLine("$G");
    ASSERT_TRUE(waitFor([&] {
        const QJsonObject r = QJsonDocument::fromJson(
            service.executeWasmRequest("com.sienci.parser-demo", "{}").toUtf8()).object();
        return r.value("matches").toInt() >= 1;
    }));
    const QJsonObject r =
        QJsonDocument::fromJson(service.executeWasmRequest("com.sienci.parser-demo", "{}").toUtf8()).object();
    EXPECT_EQ(r.value("lastParser").toString(), "modal-state");
    EXPECT_TRUE(r.value("lastLine").toString().startsWith("[GC:"));

    // Disabled, its parsers go.
    service.setPluginEnabled("com.sienci.parser-demo", false);
    EXPECT_EQ(service.bridge().parsers().parserCount("com.sienci.parser-demo"), 0);
}
