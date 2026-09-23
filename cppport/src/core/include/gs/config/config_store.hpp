#pragma once

// The persistent configuration file (gSender's ~/.sender_rc): one JSON
// document holding macros, event hooks, commands, job statistics and more.
// Port of src/server/services/configstore/index.js.
//
// Like the original, every change re-reads the file first and then writes the
// whole document back atomically (temp file + flush + rename), so a crash
// never leaves a half-written file and changes made by another process in the
// meantime survive. Members this port does not know are preserved.

#include <boost/json/value.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace gs::config {

// Writes `content` to `file` through `<file>.<pid>.tmp`, flushed to disk and
// renamed over the target. Returns false (with `error` set) on failure.
bool writeFileAtomic(const std::filesystem::path& file, std::string_view content, std::string* error = nullptr);

// Startup safeguard (validateAndRepairConfigFile): a file that exists but is
// not valid JSON is copied to "<file>.corrupt-<ms>.bak" and reset to "{}".
struct RepairResult {
    bool restored = false;
    bool repairFailed = false;
    std::filesystem::path backupPath;
};
RepairResult validateAndRepair(const std::filesystem::path& file);

class ConfigStore {
public:
    // Reported for unreadable/unwritable files.
    std::function<void(const std::string& message)> onError;

    // Loads `file`, creating it as "{}" when missing. Returns reload()'s result.
    bool load(std::filesystem::path file);
    // Re-reads the file and applies defaults and migrations. False when the
    // file exists but cannot be read or parsed (the document is kept).
    bool reload();
    // Writes the document to the file.
    bool sync();

    const std::filesystem::path& file() const noexcept { return file_; }
    const boost::json::value& document() const noexcept { return config_; }

    bool has(std::string_view path) const;
    // A copy of the value at `path`, or `fallback` when it is missing.
    boost::json::value get(std::string_view path, boost::json::value fallback = nullptr) const;
    // Reloads, changes the document and writes it - unless the reload failed
    // or `silent` is set, in which case only the in-memory document changes.
    void set(std::string_view path, boost::json::value value, bool silent = false);
    void unset(std::string_view path);

private:
    void applyDefaults();
    void error(const std::string& message) const;

    std::filesystem::path file_;
    boost::json::value config_ = boost::json::object();
};

}  // namespace gs::config
