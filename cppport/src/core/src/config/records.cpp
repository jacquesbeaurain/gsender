#include "gs/config/records.hpp"

#include "gs/util/jsnumber.hpp"

#include <boost/json.hpp>
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>

#include <chrono>
#include <cmath>

namespace gs::config {
namespace {

namespace json = boost::json;

// JavaScript truthiness of a JSON member (a missing member is undefined).
bool truthy(const json::value* value) {
    if (!value || value->is_null()) {
        return false;
    }
    if (value->is_bool()) {
        return value->as_bool();
    }
    if (value->is_string()) {
        return !value->as_string().empty();
    }
    if (value->is_number()) {
        const double number = value->to_number<double>();
        return number != 0 && !std::isnan(number);
    }
    return true;  // objects and arrays
}

// String(value), with undefined/null as "".
std::string text(const json::value* value) {
    if (!value || value->is_null()) {
        return {};
    }
    if (value->is_string()) {
        return std::string(value->as_string());
    }
    if (value->is_bool()) {
        return value->as_bool() ? "true" : "false";
    }
    if (value->is_number()) {
        return js::numberToString(value->to_number<double>());
    }
    return json::serialize(*value);
}

double number(const json::value* value) {
    return value && value->is_number() ? value->to_number<double>() : 0.0;
}

// String(x || "")
std::string textOrEmpty(const json::value* value) {
    return truthy(value) ? text(value) : std::string();
}

MacroRecord toMacro(const json::object& record) {
    MacroRecord macro;
    macro.id = text(record.if_contains("id"));
    macro.mtime = static_cast<std::int64_t>(number(record.if_contains("mtime")));
    macro.name = text(record.if_contains("name"));
    macro.content = text(record.if_contains("content"));
    macro.description = text(record.if_contains("description"));
    macro.column = text(record.if_contains("column"));
    macro.rowIndex = static_cast<int>(number(record.if_contains("rowIndex")));
    return macro;
}

json::object toJson(const MacroRecord& macro) {
    return json::object{{"id", macro.id},           {"mtime", macro.mtime},
                        {"name", macro.name},       {"content", macro.content},
                        {"description", macro.description}, {"column", macro.column},
                        {"rowIndex", macro.rowIndex}};
}

EventRecord toEvent(std::string_view key, const json::value& value) {
    EventRecord event;
    event.key = std::string(key);
    if (const json::object* record = value.if_object()) {
        event.id = text(record->if_contains("id"));
        event.mtime = static_cast<std::int64_t>(number(record->if_contains("mtime")));
        event.enabled = truthy(record->if_contains("enabled"));
        event.event = text(record->if_contains("event"));
        event.trigger = text(record->if_contains("trigger"));
        event.commands = text(record->if_contains("commands"));
    }
    return event;
}

json::object events(const ConfigStore& store) {
    const json::value value = store.get("events", json::object());
    return value.is_object() ? value.as_object() : json::object();
}

}  // namespace

std::string randomUuid() {
    thread_local boost::uuids::random_generator generator;
    return boost::uuids::to_string(generator());
}

std::int64_t systemClockMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

// ---- macros --------------------------------------------------------------------------

MacroStore::MacroStore(ConfigStore& store, IdGenerator ids, Clock clock)
    : store_(store), ids_(std::move(ids)), clock_(std::move(clock)) {}

json::array MacroStore::sanitized() {
    // castArray(config.get("macros", []))
    json::value value = store_.get("macros", json::array());
    json::array records;
    if (value.is_array()) {
        records = std::move(value.as_array());
    } else {
        records.push_back(std::move(value));
    }
    bool shouldUpdate = false;
    for (std::size_t i = 0; i < records.size(); ++i) {
        if (!records[i].is_object()) {
            records[i] = json::object();
        }
        json::object& record = records[i].as_object();
        if (!truthy(record.if_contains("id"))) {
            record["id"] = ids_();
            shouldUpdate = true;
        }
        if (!truthy(record.if_contains("description"))) {
            record["description"] = " ";
            shouldUpdate = true;
        }
        if (!truthy(record.if_contains("column"))) {
            record["column"] = i % 2 == 0 ? "column1" : "column2";
            shouldUpdate = true;
        }
    }
    if (shouldUpdate) {
        store_.set("macros", records, /*silent=*/true);
    }
    return records;
}

std::vector<MacroRecord> MacroStore::list() {
    std::vector<MacroRecord> out;
    for (const json::value& record : sanitized()) {
        out.push_back(toMacro(record.as_object()));
    }
    return out;
}

std::optional<MacroRecord> MacroStore::find(std::string_view id) {
    for (const json::value& record : sanitized()) {
        const json::value* recordId = record.as_object().if_contains("id");
        if (recordId && recordId->is_string() && recordId->as_string() == id) {
            return toMacro(record.as_object());
        }
    }
    return std::nullopt;
}

std::optional<MacroRecord> MacroStore::create(std::string name, std::string content, std::string description) {
    if (name.empty() || content.empty()) {
        return std::nullopt;
    }
    json::array records = sanitized();
    // The new macro goes to the end of the shorter column.
    int column1 = 0;
    int column2 = 0;
    for (const json::value& record : records) {
        const json::value* column = record.as_object().if_contains("column");
        if (column && column->is_string()) {
            column1 += column->as_string() == "column1" ? 1 : 0;
            column2 += column->as_string() == "column2" ? 1 : 0;
        }
    }
    MacroRecord macro;
    macro.id = ids_();
    macro.mtime = clock_();
    macro.name = std::move(name);
    macro.content = std::move(content);
    macro.description = std::move(description);
    macro.column = column2 >= column1 ? "column1" : "column2";
    macro.rowIndex = column2 >= column1 ? column1 : column2;
    records.push_back(toJson(macro));
    store_.set("macros", std::move(records));
    return macro;
}

bool MacroStore::update(std::string_view id, const MacroChanges& changes) {
    json::array records = sanitized();
    for (json::value& value : records) {
        json::object& record = value.as_object();
        const json::value* recordId = record.if_contains("id");
        if (!recordId || !recordId->is_string() || recordId->as_string() != id) {
            continue;
        }
        // Absent changes keep the stored value; String(value || "").
        const auto apply = [&record](const char* key, const std::optional<std::string>& change) {
            record[key] = change ? *change : textOrEmpty(record.if_contains(key));
        };
        record["mtime"] = clock_();
        apply("name", changes.name);
        apply("content", changes.content);
        apply("description", changes.description);
        apply("column", changes.column);
        record["rowIndex"] = changes.rowIndex ? *changes.rowIndex : static_cast<int>(number(record.if_contains("rowIndex")));
        store_.set("macros", std::move(records));
        return true;
    }
    return false;
}

bool MacroStore::bulkUpdate(const std::vector<MacroRecord>& macros) {
    if (macros.empty()) {
        return false;
    }
    json::array records = sanitized();
    for (json::value& value : records) {
        const json::value* recordId = value.as_object().if_contains("id");
        for (const MacroRecord& macro : macros) {
            if (recordId && recordId->is_string() && recordId->as_string() == macro.id) {
                value = toJson(macro);
                break;
            }
        }
    }
    store_.set("macros", std::move(records));
    return true;
}

bool MacroStore::remove(std::string_view id) {
    json::array records = sanitized();
    json::array kept;
    bool found = false;
    for (json::value& value : records) {
        const json::value* recordId = value.as_object().if_contains("id");
        if (recordId && recordId->is_string() && recordId->as_string() == id) {
            found = true;
        } else {
            kept.push_back(std::move(value));
        }
    }
    if (!found) {
        return false;
    }
    store_.set("macros", std::move(kept));
    return true;
}

// ---- event hooks -----------------------------------------------------------------------

EventStore::EventStore(ConfigStore& store, IdGenerator ids, Clock clock)
    : store_(store), ids_(std::move(ids)), clock_(std::move(clock)) {}

std::vector<EventRecord> EventStore::list() const {
    std::vector<EventRecord> out;
    for (const auto& member : events(store_)) {
        out.push_back(toEvent(member.key(), member.value()));
    }
    return out;
}

std::optional<EventRecord> EventStore::find(std::string_view key) const {
    const json::object records = events(store_);
    if (const json::value* record = records.if_contains(key)) {
        return toEvent(key, *record);
    }
    return std::nullopt;
}

std::optional<EventRecord> EventStore::create(std::string event, std::string trigger, std::string commands,
                                              bool enabled) {
    if (event.empty() || trigger.empty() || commands.empty()) {
        return std::nullopt;
    }
    EventRecord record;
    record.key = event;
    record.id = ids_();
    record.mtime = clock_();
    record.enabled = enabled;
    record.event = std::move(event);
    record.trigger = std::move(trigger);
    record.commands = std::move(commands);
    json::object records = events(store_);
    records[record.key] = json::object{{"id", record.id},           {"mtime", record.mtime},
                                       {"enabled", record.enabled}, {"event", record.event},
                                       {"trigger", record.trigger}, {"commands", record.commands}};
    store_.set("events", std::move(records));
    return record;
}

bool EventStore::update(std::string_view key, const EventChanges& changes) {
    json::object records = events(store_);
    json::value* value = records.if_contains(key);
    if (!value || !value->is_object()) {
        return false;
    }
    json::object& record = value->as_object();
    const bool enabled = changes.enabled ? *changes.enabled : truthy(record.if_contains("enabled"));
    const auto pick = [&record](const char* member, const std::optional<std::string>& change) {
        return change ? *change : textOrEmpty(record.if_contains(member));
    };
    record["mtime"] = clock_();
    record["event"] = pick("event", changes.event);
    record["trigger"] = pick("trigger", changes.trigger);
    const std::string commands = pick("commands", changes.commands);
    record["commands"] = commands;
    record.erase("command");  // deprecated member
    // A hook without commands cannot be enabled.
    record["enabled"] = enabled && !commands.empty();
    store_.set("events", std::move(records));
    return true;
}

bool EventStore::remove(std::string_view key) {
    json::object records = events(store_);
    if (!records.contains(key)) {
        return false;
    }
    records.erase(key);
    store_.set("events", std::move(records));
    return true;
}

void EventStore::clear() {
    store_.set("events", json::object());
}

std::optional<controller::EventConfig> EventStore::lookup(std::string_view key) const {
    // config.get("events") - the in-memory document, no reload.
    const json::value* records = store_.document().as_object().if_contains("events");
    const json::value* record = records && records->is_object() ? records->as_object().if_contains(key) : nullptr;
    if (!record || !record->is_object()) {
        return std::nullopt;
    }
    const json::object& object = record->as_object();
    controller::EventConfig config;
    config.event = text(object.if_contains("event"));
    config.trigger = text(object.if_contains("trigger"));
    config.commands = text(object.if_contains("commands"));
    config.enabled = truthy(object.if_contains("enabled"));
    return config;
}

controller::ControllerHooks makeControllerHooks(ConfigStore& store) {
    controller::ControllerHooks hooks;
    // _.find(config.get("macros"), { id }) - no repair on this path.
    hooks.findMacro = [&store](std::string_view id) -> std::optional<controller::Macro> {
        const json::value* macros = store.document().as_object().if_contains("macros");
        if (!macros || !macros->is_array()) {
            return std::nullopt;
        }
        for (const json::value& value : macros->as_array()) {
            const json::object* record = value.if_object();
            const json::value* recordId = record ? record->if_contains("id") : nullptr;
            if (recordId && recordId->is_string() && recordId->as_string() == id) {
                return controller::Macro{std::string(id), text(record->if_contains("name")),
                                         text(record->if_contains("content"))};
            }
        }
        return std::nullopt;
    };
    hooks.findEvent = [&store](std::string_view key) { return EventStore(store).lookup(key); };
    return hooks;
}

}  // namespace gs::config
