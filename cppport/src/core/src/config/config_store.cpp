#include "gs/config/config_store.hpp"

#include "gs/config/json_path.hpp"
#include "gs/core/resources.hpp"

#include <boost/json.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <system_error>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#else
#include <unistd.h>
#endif

namespace gs::config {
namespace {

namespace json = boost::json;
namespace fs = std::filesystem;

std::optional<std::string> readFile(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream text;
    text << in.rdbuf();
    if (in.bad()) {
        return std::nullopt;
    }
    return std::move(text).str();
}

std::int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
        .count();
}

int processId() {
#ifdef _WIN32
    return _getpid();
#else
    return static_cast<int>(getpid());
#endif
}

std::FILE* openForWrite(const fs::path& file) {
#ifdef _WIN32
    return _wfopen(file.c_str(), L"wb");
#else
    return std::fopen(file.c_str(), "wb");
#endif
}

// Flushes the C stream and the OS buffers behind it.
bool flushToDisk(std::FILE* stream) {
    if (std::fflush(stream) != 0) {
        return false;
    }
#ifdef _WIN32
    return _commit(_fileno(stream)) == 0;
#else
    return fsync(fileno(stream)) == 0;
#endif
}

// Defaults backfilled on every load (configstore/index.js), extracted from the
// JavaScript into resources/data/config_defaults.json.
const json::object& defaults() {
    static const json::object table = [] {
        const std::optional<std::string_view> text = resources::find("data/config_defaults.json");
        return text ? json::parse(*text).as_object() : json::object();
    }();
    return table;
}

}  // namespace

bool writeFileAtomic(const fs::path& file, std::string_view content, std::string* error) {
    const fs::path temp = fs::path(file).concat("." + std::to_string(processId()) + ".tmp");
    std::FILE* stream = openForWrite(temp);
    if (!stream) {
        if (error) {
            *error = "cannot create " + temp.string();
        }
        return false;
    }
    const bool written = std::fwrite(content.data(), 1, content.size(), stream) == content.size();
    const bool flushed = written && flushToDisk(stream);
    const bool closed = std::fclose(stream) == 0;
    std::error_code ec;
    if (!(written && flushed && closed)) {
        fs::remove(temp, ec);
        if (error) {
            *error = "cannot write " + temp.string();
        }
        return false;
    }
    // Same-volume rename replaces the target in one step.
    fs::rename(temp, file, ec);
    if (ec) {
        fs::remove(temp, ec);
        if (error) {
            *error = "cannot replace " + file.string() + ": " + ec.message();
        }
        return false;
    }
    return true;
}

RepairResult validateAndRepair(const fs::path& file) {
    RepairResult result;
    std::error_code ec;
    if (!fs::exists(file, ec)) {
        return result;
    }
    const std::optional<std::string> text = readFile(file);
    boost::system::error_code parseError;
    if (text) {
        json::parse(*text, parseError);
        if (!parseError) {
            return result;
        }
    }
    // Back the damaged file up beside itself, then start from an empty object;
    // the next load backfills the defaults.
    const fs::path backup = fs::path(file).concat(".corrupt-" + std::to_string(nowMs()) + ".bak");
    fs::copy_file(file, backup, fs::copy_options::overwrite_existing, ec);
    if (ec || !writeFileAtomic(file, "{}")) {
        result.repairFailed = true;
        return result;
    }
    result.restored = true;
    result.backupPath = backup;
    return result;
}

void ConfigStore::error(const std::string& message) const {
    if (onError) {
        onError(message);
    }
}

bool ConfigStore::load(fs::path file) {
    file_ = std::move(file);
    const bool ok = reload();
    std::error_code ec;
    if (!fs::exists(file_, ec)) {
        std::string message;
        if (!writeFileAtomic(file_, "{}", &message)) {
            error(message);
        }
    }
    return ok;
}

bool ConfigStore::reload() {
    std::error_code ec;
    if (fs::exists(file_, ec)) {
        const std::optional<std::string> text = readFile(file_);
        if (!text) {
            error("Unable to read " + file_.string());
            return false;
        }
        boost::system::error_code parseError;
        json::value parsed = json::parse(*text, parseError);
        if (parseError) {
            error("Unable to load data from " + file_.string() + ": " + parseError.message());
            return false;
        }
        config_ = std::move(parsed);
        if (!config_.is_object()) {
            error(file_.string() + " does not contain valid JSON");
            config_ = json::object();
        }
        json::object& root = config_.as_object();
        json::value* events = root.if_contains("events");
        if (events && events->is_array()) {
            // Migration: events used to be a list; they are keyed by event now.
            json::object keyed;
            for (const json::value& item : events->as_array()) {
                if (const json::object* record = item.if_object()) {
                    const json::value* name = record->if_contains("event");
                    keyed[name && name->is_string() ? std::string_view(name->as_string()) : std::string_view("undefined")] =
                        *record;
                }
            }
            *events = std::move(keyed);
            sync();  // as upstream: before the defaults are backfilled
        } else if (!events || !events->is_object()) {
            root["events"] = json::object();
        }
    }
    applyDefaults();
    return true;
}

void ConfigStore::applyDefaults() {
    json::object& root = config_.as_object();
    const json::object& table = defaults();

    // state: { ...defaultState, ...state } - a shallow merge.
    json::object state = table.contains("state") ? table.at("state").as_object() : json::object();
    if (const json::value* current = root.if_contains("state"); current && current->is_object()) {
        for (const auto& member : current->as_object()) {
            state[member.key()] = member.value();
        }
    }
    root["state"] = std::move(state);

    // A missing (or falsy) maintenance list gets the defaults; old lists
    // without ids are renumbered.
    const json::value* maintenance = root.if_contains("maintenance");
    const bool missing = !maintenance || maintenance->is_null() ||
                         (maintenance->is_bool() && !maintenance->as_bool()) ||
                         (maintenance->is_string() && maintenance->as_string().empty()) ||
                         (maintenance->is_number() && maintenance->to_number<double>() == 0);
    if (missing) {
        root["maintenance"] = table.contains("maintenance") ? table.at("maintenance") : json::array();
    } else if (json::array* tasks = root["maintenance"].if_array()) {
        bool renumber = false;
        for (const json::value& task : *tasks) {
            renumber = renumber || !task.is_object() || !task.as_object().contains("id");
        }
        if (renumber) {
            std::int64_t id = 0;
            for (json::value& task : *tasks) {
                if (json::object* object = task.if_object()) {
                    (*object)["id"] = id;
                }
                ++id;
            }
        }
    }

    if (const json::value* stats = root.if_contains("jobStats"); !stats || stats->is_null()) {
        root["jobStats"] = table.contains("jobStats") ? table.at("jobStats") : json::object();
    }
}

bool ConfigStore::sync() {
    const std::string content = json::serialize(config_);
    // Never overwrite the live file with blank content.
    if (content.empty()) {
        error("Refusing to write empty config to " + file_.string());
        return false;
    }
    std::string message;
    if (!writeFileAtomic(file_, content, &message)) {
        error("Unable to write data to " + file_.string() + ": " + message);
        return false;
    }
    return true;
}

bool ConfigStore::has(std::string_view path) const {
    return hasPath(config_, path);
}

json::value ConfigStore::get(std::string_view path, json::value fallback) const {
    const json::value* value = findPath(config_, path);
    return value ? *value : std::move(fallback);
}

void ConfigStore::set(std::string_view path, json::value value, bool silent) {
    const bool ok = reload();  // reload before making changes
    setPath(config_, path, std::move(value));
    if (ok && !silent) {
        sync();
    }
}

void ConfigStore::unset(std::string_view path) {
    const bool ok = reload();
    unsetPath(config_, path);
    if (ok) {
        sync();
    }
}

}  // namespace gs::config
