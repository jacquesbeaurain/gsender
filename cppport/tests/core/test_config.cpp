// Config store (services/configstore/index.js) and the macro/event records of
// api.macros.js / api.events.js.

#include "gs/config/config_store.hpp"
#include "gs/config/json_path.hpp"
#include "gs/config/records.hpp"

#include <boost/json.hpp>
#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::config;
namespace fs = std::filesystem;
namespace json = boost::json;

namespace {

class TempDir {
public:
    TempDir() : path_(fs::temp_directory_path() / ("gs_config_test_" + randomUuid())) { fs::create_directories(path_); }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path_, ec);
    }
    const fs::path& path() const { return path_; }

private:
    fs::path path_;
};

std::string readAll(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

void writeAll(const fs::path& file, std::string_view content) {
    std::ofstream(file, std::ios::binary) << content;
}

json::value readJson(const fs::path& file) {
    return json::parse(readAll(file));
}

// ---- JSON paths ----------------------------------------------------------------------

TEST(JsonPath, ParsesLodashPaths) {
    EXPECT_EQ(toPath("plain"), std::vector<std::string>{"plain"});
    EXPECT_EQ(toPath("a.b[0]['c.d'][\"e\"]"), (std::vector<std::string>{"a", "b", "0", "c.d", "e"}));
    EXPECT_EQ(toPath("events.gcode:start"), (std::vector<std::string>{"events", "gcode:start"}));
}

TEST(JsonPath, SetCreatesContainersAndGetFindsValues) {
    json::value root = json::object();
    setPath(root, "a.b[1].c", 5);
    EXPECT_EQ(json::serialize(root), R"({"a":{"b":[null,{"c":5}]}})");
    ASSERT_NE(findPath(root, "a.b[1].c"), nullptr);
    EXPECT_EQ(findPath(root, "a.b[1].c")->as_int64(), 5);
    EXPECT_TRUE(hasPath(root, "a.b[0]"));  // a null element exists
    EXPECT_FALSE(hasPath(root, "a.x"));
    EXPECT_FALSE(hasPath(root, "a.b[5]"));
}

TEST(JsonPath, SetReplacesScalarsOnTheWay) {
    json::value root = json::parse(R"({"a":1})");
    setPath(root, "a.b", true);
    EXPECT_EQ(json::serialize(root), R"({"a":{"b":true}})");
}

TEST(JsonPath, UnsetRemovesMembersAndLeavesArrayHoles) {
    json::value root = json::parse(R"({"a":{"b":1,"c":2},"list":[1,2,3]})");
    EXPECT_TRUE(unsetPath(root, "a.b"));
    EXPECT_TRUE(unsetPath(root, "list[1]"));
    EXPECT_FALSE(unsetPath(root, "missing.key"));
    EXPECT_EQ(json::serialize(root), R"({"a":{"c":2},"list":[1,null,3]})");
}

// ---- store -------------------------------------------------------------------------------

class ConfigStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        file = dir.path() / ".sender_rc";
        store.onError = [this](const std::string& message) { errors.push_back(message); };
    }

    TempDir dir;
    fs::path file;
    ConfigStore store;
    std::vector<std::string> errors;
};

TEST_F(ConfigStoreTest, LoadingCreatesAMissingFileAndBackfillsDefaultsInMemory) {
    EXPECT_TRUE(store.load(file));
    EXPECT_EQ(readAll(file), "{}");
    EXPECT_EQ(store.get("state.checkForUpdates"), json::value(false));
    EXPECT_EQ(store.get("state.controller.exception.ignoreErrors"), json::value(false));
    EXPECT_EQ(store.get("jobStats.totalJobs"), json::value(0));
    EXPECT_EQ(store.get("maintenance").as_array().size(), 4u);
    EXPECT_EQ(store.get("maintenance[0].name"), json::value("Clean around your CNC"));
    EXPECT_TRUE(errors.empty());
}

TEST_F(ConfigStoreTest, ChangesAreWrittenAtomicallyAsAWholeDocument) {
    store.load(file);
    store.set("commands", json::parse(R"([{"id":"a"}])"));
    const json::value saved = readJson(file);
    EXPECT_EQ(saved.at("commands").at(0).at("id"), json::value("a"));
    EXPECT_EQ(saved.at("state").at("checkForUpdates"), json::value(false));
    for (const auto& entry : fs::directory_iterator(dir.path())) {
        EXPECT_NE(entry.path().extension().string(), ".tmp") << entry.path().string();
    }
}

TEST_F(ConfigStoreTest, AChangeRereadsTheFileFirst) {
    store.load(file);
    writeAll(file, R"({"external":1})");  // another process wrote meanwhile
    store.set("mine", 2);
    const json::value saved = readJson(file);
    EXPECT_EQ(saved.at("external"), json::value(1));
    EXPECT_EQ(saved.at("mine"), json::value(2));
}

TEST_F(ConfigStoreTest, SilentChangesStayInMemory) {
    store.load(file);
    store.set("x", 1, /*silent=*/true);
    EXPECT_EQ(store.get("x"), json::value(1));
    EXPECT_FALSE(readJson(file).as_object().contains("x"));
}

TEST_F(ConfigStoreTest, UnsetRemovesAndWrites) {
    store.load(file);
    store.set("a", json::parse(R"({"b":1,"c":2})"));
    store.unset("a.b");
    EXPECT_EQ(readJson(file).at("a"), json::parse(R"({"c":2})"));
    EXPECT_FALSE(store.has("a.b"));
}

TEST_F(ConfigStoreTest, LegacyEventListsAreKeyedByEvent) {
    writeAll(file, R"({"events":[{"event":"gcode:start","trigger":"gcode","commands":"M3","enabled":true}]})");
    store.load(file);
    const json::value events = store.get("events");
    ASSERT_TRUE(events.is_object());
    EXPECT_EQ(events.at("gcode:start").at("commands"), json::value("M3"));
    EXPECT_TRUE(readJson(file).at("events").is_object());  // migrated on disk
}

TEST_F(ConfigStoreTest, StateDefaultsAreMergedShallowly) {
    writeAll(file, R"({"state":{"checkForUpdates":true,"custom":1}})");
    store.load(file);
    EXPECT_EQ(store.get("state.checkForUpdates"), json::value(true));
    EXPECT_EQ(store.get("state.custom"), json::value(1));
    EXPECT_EQ(store.get("state.controller.exception.ignoreErrors"), json::value(false));
}

TEST_F(ConfigStoreTest, MaintenanceTasksWithoutIdsAreRenumbered) {
    writeAll(file, R"({"maintenance":[{"name":"a"},{"name":"b","id":7}]})");
    store.load(file);
    EXPECT_EQ(store.get("maintenance[0].id"), json::value(0));
    EXPECT_EQ(store.get("maintenance[1].id"), json::value(1));
}

TEST_F(ConfigStoreTest, ANonObjectDocumentIsReplacedByAnEmptyOne) {
    writeAll(file, "[1,2]");
    EXPECT_TRUE(store.load(file));
    EXPECT_TRUE(store.document().is_object());
    EXPECT_FALSE(store.has("0"));
    EXPECT_EQ(errors.size(), 1u);
}

TEST_F(ConfigStoreTest, AnUnreadableFileIsReportedAndNeverOverwritten) {
    writeAll(file, "not json");
    EXPECT_FALSE(store.load(file));
    EXPECT_EQ(errors.size(), 1u);
    store.set("x", 1);  // the failed reload keeps the change in memory only
    EXPECT_EQ(readAll(file), "not json");
    EXPECT_EQ(store.get("x"), json::value(1));
}

TEST_F(ConfigStoreTest, RepairBacksUpACorruptFileAndResetsIt) {
    EXPECT_FALSE(validateAndRepair(file).restored);  // missing: nothing to do
    writeAll(file, R"({"ok":true})");
    EXPECT_FALSE(validateAndRepair(file).restored);
    EXPECT_EQ(readAll(file), R"({"ok":true})");

    const std::string garbage("\0\0\0\0", 4);
    writeAll(file, garbage);
    const RepairResult result = validateAndRepair(file);
    EXPECT_TRUE(result.restored);
    EXPECT_FALSE(result.repairFailed);
    EXPECT_EQ(readAll(file), "{}");
    ASSERT_TRUE(fs::exists(result.backupPath));
    EXPECT_EQ(readAll(result.backupPath), garbage);
    EXPECT_NE(result.backupPath.filename().string().find(".corrupt-"), std::string::npos);
}

TEST_F(ConfigStoreTest, AtomicWritesReplaceTheWholeFile) {
    writeAll(file, "old and longer content");
    ASSERT_TRUE(writeFileAtomic(file, "new"));
    EXPECT_EQ(readAll(file), "new");
}

// ---- records -----------------------------------------------------------------------------

class RecordsTest : public ConfigStoreTest {
protected:
    void SetUp() override {
        ConfigStoreTest::SetUp();
        store.load(file);
    }

    IdGenerator ids() {
        return [this] { return "id-" + std::to_string(++nextId); };
    }
    Clock clock() {
        return [this] { return now; };
    }

    int nextId = 0;
    std::int64_t now = 1'700'000'000'000;
};

TEST_F(RecordsTest, MacrosAreRepairedWhenRead) {
    writeAll(file, R"({"macros":[{"name":"Probe","content":"G38.2 Z-10"},"junk"]})");
    store.reload();
    MacroStore macros(store, ids(), clock());
    const auto list = macros.list();
    ASSERT_EQ(list.size(), 2u);
    EXPECT_EQ(list[0].id, "id-1");
    EXPECT_EQ(list[0].name, "Probe");
    EXPECT_EQ(list[0].description, " ");
    EXPECT_EQ(list[0].column, "column1");
    EXPECT_EQ(list[1].id, "id-2");
    EXPECT_EQ(list[1].column, "column2");
    // The repair is silent: nothing is written.
    EXPECT_EQ(readJson(file).at("macros").at(1), json::value("junk"));
}

TEST_F(RecordsTest, NewMacrosFillTheShorterColumn) {
    MacroStore macros(store, ids(), clock());
    EXPECT_FALSE(macros.create("", "G0").has_value());
    EXPECT_FALSE(macros.create("Name", "").has_value());
    const auto a = macros.create("A", "G0 X0", "first");
    const auto b = macros.create("B", "G0 X1");
    const auto c = macros.create("C", "G0 X2");
    ASSERT_TRUE(a && b && c);
    EXPECT_EQ(std::make_pair(a->column, a->rowIndex), std::make_pair(std::string("column1"), 0));
    EXPECT_EQ(std::make_pair(b->column, b->rowIndex), std::make_pair(std::string("column2"), 0));
    EXPECT_EQ(std::make_pair(c->column, c->rowIndex), std::make_pair(std::string("column1"), 1));
    EXPECT_EQ(a->mtime, now);
    EXPECT_EQ(macros.find(a->id), a);
    EXPECT_EQ(readJson(file).at("macros").as_array().size(), 3u);
}

TEST_F(RecordsTest, MacroUpdatesChangeOnlyTheGivenFields) {
    MacroStore macros(store, ids(), clock());
    const auto created = macros.create("A", "G0 X0", "first");
    now += 5;
    EXPECT_TRUE(macros.update(created->id, MacroChanges{.name = "Renamed", .rowIndex = 4}));
    const auto updated = macros.find(created->id);
    ASSERT_TRUE(updated);
    EXPECT_EQ(updated->name, "Renamed");
    EXPECT_EQ(updated->content, "G0 X0");
    EXPECT_EQ(updated->description, "first");
    EXPECT_EQ(updated->rowIndex, 4);
    EXPECT_EQ(updated->mtime, now);
    EXPECT_FALSE(macros.update("missing", MacroChanges{.name = "x"}));
}

TEST_F(RecordsTest, BulkUpdateReplacesMacrosWithTheSameId) {
    MacroStore macros(store, ids(), clock());
    // (A macro stored without a description reads back as " ".)
    auto a = *macros.create("A", "G0 X0", "first");
    const auto b = *macros.create("B", "G0 X1", "second");
    EXPECT_FALSE(macros.bulkUpdate({}));
    a.content = "G0 X9";
    a.column = "column2";
    EXPECT_TRUE(macros.bulkUpdate({a}));
    EXPECT_EQ(macros.find(a.id), a);
    EXPECT_EQ(macros.find(b.id), b);
}

TEST_F(RecordsTest, MacrosCanBeRemoved) {
    MacroStore macros(store, ids(), clock());
    const auto a = *macros.create("A", "G0 X0");
    EXPECT_TRUE(macros.remove(a.id));
    EXPECT_FALSE(macros.remove(a.id));
    EXPECT_TRUE(macros.list().empty());
}

TEST_F(RecordsTest, EventHooksAreKeyedByEvent) {
    EventStore events(store, ids(), clock());
    EXPECT_FALSE(events.create("gcode:start", "gcode", "").has_value());
    const auto hook = events.create("gcode:start", "gcode", "M3 S1000");
    ASSERT_TRUE(hook);
    EXPECT_EQ(events.find("gcode:start"), hook);
    EXPECT_EQ(readJson(file).at("events").at("gcode:start").at("commands"), json::value("M3 S1000"));

    now += 1;
    EXPECT_TRUE(events.update("gcode:start", EventChanges{.commands = std::string()}));
    const auto updated = events.find("gcode:start");
    EXPECT_FALSE(updated->enabled);  // no commands, no hook
    EXPECT_EQ(updated->mtime, now);
    EXPECT_FALSE(events.update("missing", EventChanges{}));

    EXPECT_TRUE(events.remove("gcode:start"));
    EXPECT_FALSE(events.find("gcode:start").has_value());
    events.create("feedhold", "gcode", "M5");
    events.clear();
    EXPECT_TRUE(events.list().empty());
}

TEST_F(RecordsTest, LookupsReadLikeEventTrigger) {
    writeAll(file, R"({"events":{"a":{"event":"a","trigger":"gcode"},"b":{"event":"b","enabled":1,"commands":"M5"}}})");
    store.reload();
    EventStore events(store);
    const auto a = events.lookup("a");
    ASSERT_TRUE(a);
    EXPECT_FALSE(a->enabled);  // missing: disabled
    EXPECT_EQ(a->commands, "");
    const auto b = events.lookup("b");
    ASSERT_TRUE(b);
    EXPECT_TRUE(b->enabled);
    EXPECT_EQ(b->commands, "M5");
    EXPECT_FALSE(events.lookup("c").has_value());
}

class RecordingLink final : public controller::DeviceLink {
public:
    std::vector<std::string> writes;
    bool isOpen() const override { return true; }
    void send(std::string_view bytes, controller::SendKind kind) override {
        if (kind == controller::SendKind::Write) {
            writes.emplace_back(bytes);
        }
    }
};

TEST_F(RecordsTest, ControllerHooksServeMacrosAndEventHooks) {
    MacroStore macros(store, ids(), clock());
    const auto macro = *macros.create("Park", "G0 Z10");
    EventStore(store, ids(), clock()).create("feedhold", "gcode", "M5");

    runtime::ManualEventLoop loop;
    RecordingLink link;
    controller::Controller c(loop, link, protocol::Firmware::Grbl, makeControllerHooks(store), {});
    c.setPollingEnabled(false);

    EXPECT_TRUE(c.runMacro(macro.id));
    EXPECT_FALSE(c.runMacro("missing"));
    EXPECT_EQ(link.writes, std::vector<std::string>{"G0 Z10\n"});

    link.writes.clear();
    c.receiveLine("ok");  // the macro line is done
    c.feedHold();
    EXPECT_EQ(link.writes, (std::vector<std::string>{"M5\n", "!"}));
}

}  // namespace
