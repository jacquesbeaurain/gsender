#pragma once

// The application's own preferences (gSender kept these in the UI's store:
// workspace.toolChangeOption/Hooks, spindle delay, line warnings, the
// connection defaults). Persisted under "app" in the port's config file.

#include "gs/config/config_store.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/jogging.hpp"
#include "gs/probe/probing.hpp"
#include "gs/surfacing/surfacing.hpp"

#include <map>
#include <string>

namespace gs::app {

// widgets.axes.jog: the presets, the hold time that turns a press into a
// continuous jog, and the limit-switch protection (mm).
struct JogSettings {
    controller::JogSpeeds rapid = controller::defaultJogSpeeds(controller::JogPreset::Rapid);
    controller::JogSpeeds normal = controller::defaultJogSpeeds(controller::JogPreset::Normal);
    controller::JogSpeeds precise = controller::defaultJogSpeeds(controller::JogPreset::Precise);
    int threshold = 250;  // ms
    bool preventJoggingPastLimits = false;
    const controller::JogSpeeds& speeds(controller::JogPreset preset) const;
};

// A keyboard shortcut the user changed (gSender's commandKeys entry).
struct ShortcutBinding {
    std::string keys;  // QKeySequence portable text; empty: none
    bool active = true;
    bool operator==(const ShortcutBinding&) const = default;
};

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
    // Jogging and positioning
    JogSettings jog;
    double safeRetractHeight = 0;  // mm lifted before go-to-zero moves; 0: none
    // Keyboard shortcuts: changes from the defaults, by gSender's command id,
    // and the global switch (preferences.shortcuts.shouldHold, inverted).
    std::map<std::string, ShortcutBinding> shortcuts;
    bool shortcutsEnabled = true;
};

// The tool change strategies the port supports so far (gSender's option
// names): the Re-zero and tool-sensor wizards are not ported yet.
inline constexpr const char* kToolChangeOptions[] = {"Ignore", "Pause", "Code"};

AppSettings loadAppSettings(const config::ConfigStore& store);
void saveAppSettings(config::ConfigStore& store, const AppSettings& settings);

}  // namespace gs::app
