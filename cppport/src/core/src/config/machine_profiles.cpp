#include "gs/config/machine_profiles.hpp"

#include "gs/core/resources.hpp"
#include "gs/util/jsnumber.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <set>

namespace gs::config {
namespace {

namespace json = boost::json;

// `${value}` in JavaScript.
std::string interpolate(const json::value& value) {
    switch (value.kind()) {
        case json::kind::string: return std::string(value.as_string());
        case json::kind::int64: return std::to_string(value.as_int64());
        case json::kind::uint64: return std::to_string(value.as_uint64());
        case json::kind::double_: return js::numberToString(value.as_double());
        case json::kind::bool_: return value.as_bool() ? "true" : "false";
        case json::kind::null: return "null";
        default: return "[object Object]";
    }
}

// JavaScript truthiness of a JSON value.
bool truthy(const json::value& value) {
    switch (value.kind()) {
        case json::kind::string: return !value.as_string().empty();
        case json::kind::int64: return value.as_int64() != 0;
        case json::kind::uint64: return value.as_uint64() != 0;
        case json::kind::double_: return value.as_double() != 0 && !std::isnan(value.as_double());
        case json::kind::bool_: return value.as_bool();
        case json::kind::null: return false;
        default: return true;
    }
}

std::string text(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    return v && v->is_string() ? std::string(v->as_string()) : std::string();
}

double number(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    return v && v->is_number() ? v->to_number<double>() : 0;
}

std::optional<protocol::OrderedMap> settingsObject(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    if (!v || !v->is_object()) {
        return std::nullopt;
    }
    protocol::OrderedMap map;
    for (const auto& [name, value] : v->as_object()) {
        map.set(name, interpolate(value));
    }
    return map;
}

struct Migration {
    long long cutoffSemver = 0;
    SettingPairs keyRemaps;                                        // old -> new, in order
    std::vector<std::pair<std::string, std::optional<std::string>>> valueOverrides;  // nullopt: removed
    std::set<std::string> skippedBoards;
};

struct Data {
    std::vector<MachineProfile> profiles;
    Migration migration;
};

const Data& data() {
    static const Data loaded = [] {
        Data d;
        const auto bytes = resources::find("data/machine_profiles.json");
        if (!bytes) {
            return d;
        }
        const json::value root = json::parse(*bytes);
        const json::object& top = root.as_object();
        for (const json::value& entry : top.at("profiles").as_array()) {
            const json::object& o = entry.as_object();
            MachineProfile p;
            p.id = static_cast<int>(number(o, "id"));
            p.company = text(o, "company");
            p.name = text(o, "name");
            p.type = text(o, "type");
            p.version = text(o, "version");
            if (const json::value* mm = o.if_contains("mm"); mm && mm->is_object()) {
                p.width = number(mm->as_object(), "width");
                p.depth = number(mm->as_object(), "depth");
                p.height = number(mm->as_object(), "height");
            }
            p.eepromSettings = settingsObject(o, "eepromSettings");
            p.grblHalEepromSettings = settingsObject(o, "grblHALeepromSettings");
            if (const json::value* ordered = o.if_contains("orderedSettings"); ordered && ordered->is_array()) {
                SettingPairs pairs;
                for (const json::value& pair : ordered->as_array()) {
                    const json::array& kv = pair.as_array();
                    pairs.emplace_back(interpolate(kv.at(0)), interpolate(kv.at(1)));
                }
                p.orderedSettings = std::move(pairs);
            }
            d.profiles.push_back(std::move(p));
        }
        const json::object& migration = top.at("grblCore").as_object().at("GRBLCORE_MIGRATION").as_object();
        d.migration.cutoffSemver = migration.at("cutoffSemver").to_number<long long>();
        for (const auto& [from, to] : migration.at("keyRemaps").as_object()) {
            d.migration.keyRemaps.emplace_back(std::string(from), interpolate(to));
        }
        for (const auto& [key, value] : migration.at("valueOverrides").as_object()) {
            d.migration.valueOverrides.emplace_back(
                std::string(key), value.is_null() ? std::nullopt : std::optional<std::string>(interpolate(value)));
        }
        for (const json::value& board : top.at("boardProfiles").as_array()) {
            const json::object& b = board.as_object();
            if (const json::value* skip = b.if_contains("skipGrblCoreMigration"); skip && skip->is_bool() &&
                                                                                  skip->as_bool()) {
                d.migration.skippedBoards.insert(text(b, "boardId"));
            }
        }
        return d;
    }();
    return loaded;
}

// An insertion-ordered map with removal, for the migration's rewrites.
class Editable {
public:
    explicit Editable(const protocol::OrderedMap& map) : items_(map.items()) {}
    const std::string* find(const std::string& key) const {
        const auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& kv) { return kv.first == key; });
        return it == items_.end() ? nullptr : &it->second;
    }
    void erase(const std::string& key) {
        std::erase_if(items_, [&](const auto& kv) { return kv.first == key; });
    }
    void set(const std::string& key, std::string value) {
        const auto it = std::find_if(items_.begin(), items_.end(), [&](const auto& kv) { return kv.first == key; });
        if (it != items_.end()) {
            it->second = std::move(value);
        } else {
            items_.emplace_back(key, std::move(value));
        }
    }
    protocol::OrderedMap map() const {
        protocol::OrderedMap out;
        for (const auto& [key, value] : items_) {
            out.set(key, value);
        }
        return out;
    }

private:
    SettingPairs items_;
};

protocol::OrderedMap profileDefaults(const MachineProfile& profile, const BoardContext& board) {
    if (board.grblHal) {
        return resolveGrblCoreDefaults(board.semver, profile.grblHalEepromSettings.value_or(protocol::OrderedMap{}),
                                       std::nullopt, board.boardId)
            .defaults;
    }
    return profile.eepromSettings.value_or(protocol::OrderedMap{});
}

}  // namespace

// ---- profiles --------------------------------------------------------------------------------

const std::vector<MachineProfile>& machineProfiles() {
    return data().profiles;
}

const MachineProfile* findMachineProfile(int id) {
    for (const MachineProfile& profile : machineProfiles()) {
        if (profile.id == id) {
            return &profile;
        }
    }
    return nullptr;
}

int defaultMachineProfileId() {
    const auto& all = machineProfiles();
    return all.size() > 6 ? all[6].id : (all.empty() ? 0 : all.front().id);
}

std::string machineProfileName(const MachineProfile& profile) {
    std::string name = profile.name;
    if (!profile.type.empty()) {
        name += " " + profile.type;
    }
    if (!profile.version.empty()) {
        name += " (" + profile.version + ")";
    }
    return name;
}

bool canRestoreDefaults(const MachineProfile& profile) {
    return profile.company == "Sienci Labs" || profile.grblHalEepromSettings || profile.eepromSettings;
}

// ---- migration -------------------------------------------------------------------------------

bool usesGrblCoreMigration(long long firmwareSemver, std::string_view boardId) {
    const Migration& m = data().migration;
    if (m.skippedBoards.count(std::string(boardId)) != 0 || firmwareSemver < 0) {
        return false;
    }
    return firmwareSemver >= m.cutoffSemver;
}

std::string translateGrblCoreKey(const std::string& key, long long firmwareSemver, std::string_view boardId) {
    if (!usesGrblCoreMigration(firmwareSemver, boardId)) {
        return key;
    }
    for (const auto& [from, to] : data().migration.keyRemaps) {
        if (from == key) {
            return to;
        }
    }
    return key;
}

ResolvedDefaults resolveGrblCoreDefaults(long long firmwareSemver, const protocol::OrderedMap& baseDefaults,
                                         const std::optional<SettingPairs>& ordered, std::string_view boardId) {
    if (!usesGrblCoreMigration(firmwareSemver, boardId)) {
        return {baseDefaults, ordered};
    }
    const Migration& m = data().migration;
    // translateGrblCoreDefaults(): old keys renamed (their values moving to
    // the end), then the overrides set or removed.
    Editable defaults(baseDefaults);
    for (const auto& [from, to] : m.keyRemaps) {
        if (const std::string* value = baseDefaults.find(from)) {
            const std::string moved = *value;
            defaults.erase(from);
            defaults.set(to, moved);
        }
    }
    for (const auto& [key, value] : m.valueOverrides) {
        if (value) {
            defaults.set(key, *value);
        } else {
            defaults.erase(key);
        }
    }
    // translateGrblCoreOrderedSettings(): renamed, removed ones dropped, a
    // repeated key moving to its last place.
    std::optional<SettingPairs> translated;
    if (ordered) {
        std::set<std::string> removed;
        for (const auto& [key, value] : m.valueOverrides) {
            if (!value) {
                removed.insert(key);
            }
        }
        SettingPairs out;
        for (const auto& [key, value] : *ordered) {
            std::string renamed = key;
            for (const auto& [from, to] : m.keyRemaps) {
                if (from == key) {
                    renamed = to;
                }
            }
            if (removed.count(renamed) != 0) {
                continue;
            }
            std::erase_if(out, [&](const auto& kv) { return kv.first == renamed; });
            out.emplace_back(renamed, value);
        }
        translated = std::move(out);
    }
    return {defaults.map(), translated};
}

// ---- the board -------------------------------------------------------------------------------

std::optional<std::string> defaultValue(const MachineProfile& profile, const BoardContext& board,
                                        const std::string& setting) {
    const std::string key = board.grblHal
                                ? translateGrblCoreKey(setting, board.semver, board.boardId)
                                : setting;
    const protocol::OrderedMap defaults = profileDefaults(profile, board);
    if (const std::string* value = defaults.find(key)) {
        return *value;
    }
    return std::nullopt;
}

bool isDefaultValue(const std::string& value, const std::optional<std::string>& fallback, int dataType) {
    if (!fallback) {
        return true;  // unknown defaults count as default
    }
    const double current = js::stringToNumber(value);
    const double expected = js::stringToNumber(*fallback);
    if (dataType == 5 || dataType == 6) {  // integer, decimal: to 3 decimals
        const auto fixed = [](double v) { return std::isnan(v) ? std::string("NaN") : js::toFixed(v, 3); };
        return fixed(current) == fixed(expected);
    }
    if (!std::isnan(current) && !std::isnan(expected)) {
        return current == expected;
    }
    return value == *fallback;
}

std::vector<std::string> restoreDefaultsCommands(const MachineProfile& profile, const BoardContext& board) {
    const bool hal = board.grblHal;
    protocol::OrderedMap defaults;
    std::optional<SettingPairs> ordered;
    if (hal) {
        ResolvedDefaults resolved = resolveGrblCoreDefaults(
            board.semver, profile.grblHalEepromSettings.value_or(protocol::OrderedMap{}), profile.orderedSettings,
            board.boardId);
        defaults = std::move(resolved.defaults);
        ordered = std::move(resolved.ordered);
    } else {
        defaults = profile.eepromSettings.value_or(protocol::OrderedMap{});
        ordered = profile.orderedSettings;
    }
    const auto isOrdered = [&ordered](const std::string& key) {
        return ordered && std::any_of(ordered->begin(), ordered->end(), [&](const auto& kv) { return kv.first == key; });
    };
    std::vector<std::string> commands;
    for (const auto& [key, value] : defaults.items()) {
        if (!isOrdered(key)) {
            commands.push_back(key + "=" + value);
        }
    }
    if (ordered) {
        for (const auto& [key, value] : *ordered) {
            commands.push_back(key + "=" + value);
        }
    }
    commands.emplace_back("$$");
    if (hal) {
        commands.emplace_back("$ES");
        commands.emplace_back("$ESH");
    }
    return commands;
}

std::optional<std::vector<std::string>> importEepromCommands(std::string_view text, const MachineProfile* profile) {
    json::value root;
    try {
        root = json::parse(text);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    if (!root.is_object()) {
        return std::nullopt;
    }
    const json::object& uploaded = root.as_object();
    for (const auto& [key, value] : uploaded) {
        if (key.empty() || key[0] != '$' || std::isnan(js::stringToNumber(std::string_view(key).substr(1)))) {
            return std::nullopt;  // 'Invalid firmware settings file format'
        }
    }
    // The profile's ordered settings first (those set), then the rest.
    SettingPairs formatted;
    const auto has = [&formatted](std::string_view key) {
        return std::any_of(formatted.begin(), formatted.end(), [&](const auto& kv) { return kv.first == key; });
    };
    if (profile && profile->orderedSettings) {
        for (const auto& [key, _] : *profile->orderedSettings) {
            if (const json::value* value = uploaded.if_contains(key); value && truthy(*value)) {
                formatted.emplace_back(key, interpolate(*value));
            }
        }
    }
    for (const auto& [key, value] : uploaded) {
        if (!has(key)) {
            formatted.emplace_back(std::string(key), interpolate(value));
        }
    }
    std::vector<std::string> commands;
    for (const auto& [key, value] : formatted) {
        commands.push_back(key + "=" + value);
    }
    commands.emplace_back("$$");
    return commands;
}

std::string exportEeprom(const protocol::OrderedMap& settings) {
    json::object out;
    for (const auto& [key, value] : settings.items()) {
        out[key] = value;
    }
    return json::serialize(out);
}

}  // namespace gs::config
