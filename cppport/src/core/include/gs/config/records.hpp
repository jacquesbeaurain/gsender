#pragma once

// Macros and event hooks as stored in the config file. Ports of the record
// handling in src/server/api/api.macros.js and api.events.js; the records
// stay JSON objects in the store (unknown members survive), these classes
// read and edit them.

#include "gs/config/config_store.hpp"
#include "gs/controller/controller.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::config {

// Record ids (uuid v4 by default) and modification times (ms since epoch).
using IdGenerator = std::function<std::string()>;
using Clock = std::function<std::int64_t()>;

std::string randomUuid();
std::int64_t systemClockMs();

// ---- macros ("macros": an array) ----

struct MacroRecord {
    std::string id;
    std::int64_t mtime = 0;
    std::string name;
    std::string content;
    std::string description;
    std::string column;  // "column1" / "column2" in the macro panel
    int rowIndex = 0;
    bool operator==(const MacroRecord&) const = default;
};

struct MacroChanges {
    std::optional<std::string> name;
    std::optional<std::string> content;
    std::optional<std::string> description;
    std::optional<std::string> column;
    std::optional<int> rowIndex;
};

class MacroStore {
public:
    explicit MacroStore(ConfigStore& store, IdGenerator ids = randomUuid, Clock clock = systemClockMs);

    // All macros, repaired first: missing ids, descriptions and columns are
    // filled in (kept in memory only, as upstream's silent update).
    std::vector<MacroRecord> list();
    std::optional<MacroRecord> find(std::string_view id);
    // Appends to the shorter column. Name and content must not be empty.
    std::optional<MacroRecord> create(std::string name, std::string content, std::string description = {});
    bool update(std::string_view id, const MacroChanges& changes);
    // Replaces the stored macros that share an id with one of `macros`.
    bool bulkUpdate(const std::vector<MacroRecord>& macros);
    bool remove(std::string_view id);

private:
    boost::json::array sanitized();

    ConfigStore& store_;
    IdGenerator ids_;
    Clock clock_;
};

// ---- event hooks ("events": an object keyed by event, e.g. "gcode:start") ----

struct EventRecord {
    std::string key;  // the member name
    std::string id;
    std::int64_t mtime = 0;
    bool enabled = false;
    std::string event;
    std::string trigger;  // "gcode" or "system"
    std::string commands;
    bool operator==(const EventRecord&) const = default;
};

struct EventChanges {
    std::optional<bool> enabled;
    std::optional<std::string> event;
    std::optional<std::string> trigger;
    std::optional<std::string> commands;
};

class EventStore {
public:
    explicit EventStore(ConfigStore& store, IdGenerator ids = randomUuid, Clock clock = systemClockMs);

    std::vector<EventRecord> list() const;
    std::optional<EventRecord> find(std::string_view key) const;
    // Event, trigger and commands must not be empty.
    std::optional<EventRecord> create(std::string event, std::string trigger, std::string commands,
                                      bool enabled = true);
    // A hook left without commands is disabled.
    bool update(std::string_view key, const EventChanges& changes);
    bool remove(std::string_view key);
    void clear();

    // What EventTrigger.js read: `enabled` defaults to false, `commands` to "".
    std::optional<controller::EventConfig> lookup(std::string_view key) const;

private:
    ConfigStore& store_;
    IdGenerator ids_;
    Clock clock_;
};

// Controller hooks backed by the config file: macros and event hooks.
controller::ControllerHooks makeControllerHooks(ConfigStore& store);

}  // namespace gs::config
