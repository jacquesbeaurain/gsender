#include "app_settings.hpp"

#include <boost/json.hpp>

namespace gs::app {
namespace {

namespace json = boost::json;

std::string text(const json::object& object, std::string_view key, std::string fallback = {}) {
    const json::value* value = object.if_contains(key);
    return value && value->is_string() ? std::string(value->as_string()) : std::move(fallback);
}

double number(const json::object& object, std::string_view key, double fallback) {
    const json::value* value = object.if_contains(key);
    return value && value->is_number() ? value->to_number<double>() : fallback;
}

bool flag(const json::object& object, std::string_view key, bool fallback) {
    const json::value* value = object.if_contains(key);
    return value && value->is_bool() ? value->as_bool() : fallback;
}

}  // namespace

AppSettings loadAppSettings(const config::ConfigStore& store) {
    AppSettings settings;
    const json::value app = store.get("app", json::object());
    if (!app.is_object()) {
        return settings;
    }
    const json::object& root = app.as_object();
    if (const json::value* tool = root.if_contains("toolChange"); tool && tool->is_object()) {
        const json::object& t = tool->as_object();
        settings.toolChange.option = text(t, "option", "Ignore");
        settings.toolChange.passthrough = flag(t, "passthrough", false);
        settings.toolChange.preHook = text(t, "preHook");
        settings.toolChange.postHook = text(t, "postHook");
        settings.toolChange.skipDialog = flag(t, "skipDialog", false);
    }
    settings.preferences.spindleDelay = number(root, "spindleDelay", 0);
    settings.preferences.showLineWarnings = flag(root, "showLineWarnings", false);
    settings.preferences.useAaxisForGrbl = flag(root, "useAaxisForGrbl", false);
    settings.port = text(root, "port");
    settings.baudRate = static_cast<int>(number(root, "baudRate", 115200));
    settings.networkPort = static_cast<int>(number(root, "networkPort", 23));
    settings.defaultFirmware =
        text(root, "defaultFirmware", "Grbl") == "grblHAL" ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl;
    return settings;
}

void saveAppSettings(config::ConfigStore& store, const AppSettings& settings) {
    const controller::ToolChangeContext& t = settings.toolChange;
    store.set("app", json::object{
                         {"toolChange", json::object{{"option", t.option},
                                                     {"passthrough", t.passthrough},
                                                     {"preHook", t.preHook},
                                                     {"postHook", t.postHook},
                                                     {"skipDialog", t.skipDialog}}},
                         {"spindleDelay", settings.preferences.spindleDelay},
                         {"showLineWarnings", settings.preferences.showLineWarnings},
                         {"useAaxisForGrbl", settings.preferences.useAaxisForGrbl},
                         {"port", settings.port},
                         {"baudRate", settings.baudRate},
                         {"networkPort", settings.networkPort},
                         {"defaultFirmware",
                          settings.defaultFirmware == protocol::Firmware::GrblHal ? "grblHAL" : "Grbl"},
                     });
}

}  // namespace gs::app
