#pragma once

// The application's own preferences (gSender kept these in the UI's store:
// workspace.toolChangeOption/Hooks, spindle delay, line warnings, the
// connection defaults). Persisted under "app" in the port's config file.

#include "gs/config/config_store.hpp"
#include "gs/controller/controller.hpp"
#include "gs/probe/probing.hpp"
#include "gs/surfacing/surfacing.hpp"

#include <string>

namespace gs::app {

struct AppSettings {
    // Controller behaviour
    controller::ToolChangeContext toolChange{"Ignore"};
    controller::Preferences preferences;
    // Connection
    std::string port;
    int baudRate = 115200;
    int networkPort = 23;
    protocol::Firmware defaultFirmware = protocol::Firmware::Grbl;
    // Probe widget and touch plate profile (mm)
    probe::ProbeSettings probe;
    // Surfacing tool (mm)
    surfacing::Options surfacing;
};

// The tool change strategies the port supports so far (gSender's option
// names): the Re-zero and tool-sensor wizards are not ported yet.
inline constexpr const char* kToolChangeOptions[] = {"Ignore", "Pause", "Code"};

AppSettings loadAppSettings(const config::ConfigStore& store);
void saveAppSettings(config::ConfigStore& store, const AppSettings& settings);

}  // namespace gs::app
