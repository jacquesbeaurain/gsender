#pragma once

// Static firmware reference tables (error/alarm descriptions, setting
// metadata), loaded from the JSON extracted out of gSender's constants.

#include "gs/protocol/runner.hpp"

#include <boost/json/object.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::protocol {

struct CodeInfo {
    std::string code;  // "9", or "Homing" for the Grbl homing alarm
    std::string message;
    std::string description;
};

struct SettingInfo {
    std::string setting;  // "$110"
    std::string message;
    std::string category;
    std::string units;
    std::string description;
    std::string inputType;
    boost::json::object raw;  // every field, for UI metadata (min/max/values...)
};

class FirmwareTables {
public:
    // Parsed on first use from the embedded resources; thread-safe.
    static const FirmwareTables& get(Firmware firmware);

    const std::vector<CodeInfo>& errors() const noexcept { return errors_; }
    const std::vector<CodeInfo>& alarms() const noexcept { return alarms_; }
    const std::vector<SettingInfo>& settings() const noexcept { return settings_; }

    const CodeInfo* error(std::string_view code) const;
    const CodeInfo* alarm(std::string_view code) const;
    const SettingInfo* setting(std::string_view name) const;

private:
    explicit FirmwareTables(std::string_view resourceName);

    std::vector<CodeInfo> errors_;
    std::vector<CodeInfo> alarms_;
    std::vector<SettingInfo> settings_;
};

}  // namespace gs::protocol
