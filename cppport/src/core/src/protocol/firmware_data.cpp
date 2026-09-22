#include "gs/protocol/firmware_data.hpp"

#include "gs/core/resources.hpp"
#include "gs/util/jsnumber.hpp"

#include <boost/json.hpp>

#include <stdexcept>

namespace gs::protocol {
namespace {

std::string text(const boost::json::object& object, std::string_view key) {
    const boost::json::value* value = object.if_contains(key);
    if (!value) {
        return {};
    }
    if (value->is_string()) {
        return std::string(value->get_string());
    }
    if (value->is_int64()) {
        return std::to_string(value->get_int64());
    }
    if (value->is_uint64()) {
        return std::to_string(value->get_uint64());
    }
    if (value->is_double()) {
        return js::numberToString(value->get_double());
    }
    return {};
}

std::vector<CodeInfo> codes(const boost::json::object& root, std::string_view key) {
    std::vector<CodeInfo> out;
    if (const boost::json::value* list = root.if_contains(key); list && list->is_array()) {
        for (const boost::json::value& item : list->get_array()) {
            const boost::json::object& o = item.as_object();
            out.push_back({text(o, "code"), text(o, "message"), text(o, "description")});
        }
    }
    return out;
}

}  // namespace

FirmwareTables::FirmwareTables(std::string_view resourceName) {
    const std::optional<std::string_view> json = resources::find(resourceName);
    if (!json) {
        throw std::runtime_error("missing embedded resource " + std::string(resourceName));
    }
    const boost::json::object root = boost::json::parse(*json).as_object();
    errors_ = codes(root, "errors");
    alarms_ = codes(root, "alarms");
    if (const boost::json::value* list = root.if_contains("settings"); list && list->is_array()) {
        for (const boost::json::value& item : list->get_array()) {
            const boost::json::object& o = item.as_object();
            settings_.push_back({text(o, "setting"), text(o, "message"), text(o, "category"), text(o, "units"),
                                 text(o, "description"), text(o, "inputType"), o});
        }
    }
}

const FirmwareTables& FirmwareTables::get(Firmware firmware) {
    static const FirmwareTables grbl("data/grbl.json");
    static const FirmwareTables grblHal("data/grblhal.json");
    return firmware == Firmware::Grbl ? grbl : grblHal;
}

const CodeInfo* FirmwareTables::error(std::string_view code) const {
    for (const CodeInfo& info : errors_) {
        if (info.code == code) {
            return &info;
        }
    }
    return nullptr;
}

const CodeInfo* FirmwareTables::alarm(std::string_view code) const {
    for (const CodeInfo& info : alarms_) {
        if (info.code == code) {
            return &info;
        }
    }
    return nullptr;
}

const SettingInfo* FirmwareTables::setting(std::string_view name) const {
    for (const SettingInfo& info : settings_) {
        if (info.setting == name) {
            return &info;
        }
    }
    return nullptr;
}

}  // namespace gs::protocol
