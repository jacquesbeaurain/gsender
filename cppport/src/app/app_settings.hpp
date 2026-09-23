#pragma once

// The application's own preferences (gSender kept these in the UI's store:
// workspace.toolChangeOption/Hooks, spindle delay, line warnings, the
// connection defaults). Persisted under "app" in the port's config file.

#include "gs/config/config_store.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/jogging.hpp"
#include "gs/job/outline.hpp"
#include "gs/probe/probing.hpp"
#include "gs/surfacing/surfacing.hpp"
#include "gs/toolchange/wizards.hpp"

#include <cstdint>
#include <map>
#include <string>
#include <vector>

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

// widgets.spindle: the Spindle/Laser tab (upstream's defaults).
struct LaserSettings {
    bool onOutline = false;  // laserOnOutline: outlines traced with the laser lit
    double power = 100;      // % of the maximum, for focusing and the test
    double duration = 1;     // s, the laser test
    double xOffset = 0;      // mm from the spindle (Grbl; grblHAL: $770/$771)
    double yOffset = 0;
    double minPower = 0;     // $31 in laser mode (Grbl; grblHAL: $731)
    double maxPower = 255;   // $30 in laser mode (Grbl; grblHAL: $730)
};

struct SpindleSettings {
    bool laserMode = false;   // the mode last chosen (the board's $32 rules when connected)
    double speed = 1000;      // rpm for M3/M4
    double spindleMax = 30000;  // $30/$31 kept for spindle mode while the laser has them
    double spindleMin = 10000;
    LaserSettings laser;
};

// workspace.recentFiles: a file loaded from disk.
struct RecentFile {
    std::string fileName;
    std::string filePath;
    std::int64_t fileSize = 0;
    std::int64_t timeUploaded = 0;  // ms since the epoch
    bool operator==(const RecentFile&) const = default;
};

inline constexpr std::size_t kRecentFileLimit = 8;  // RECENT_FILE_LIMIT

// addRecentFile(): a file loaded again moves to the top with its new time;
// newest first, the oldest dropped past the limit.
void addRecentFile(std::vector<RecentFile>& files, RecentFile file);

struct AppSettings {
    // Workspace units (workspace.units): positions, jogging and the tools
    // show and take inches when false; storage stays mm.
    bool metric = true;
    int customDecimalPlaces = 0;  // 0: upstream's defaults (2 mm, 3 in)
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
    // Spindle/Laser tab
    SpindleSettings spindle;
    // Jogging and positioning
    JogSettings jog;
    double safeRetractHeight = 0;  // mm lifted before go-to-zero moves; 0: none
    bool warnZero = false;         // workspace.shouldWarnZero: the zero buttons ask first
    // The park position (workspace.park, machine coordinates, mm): the DRO's
    // Park button goes there once the machine has homed.
    toolchange::MachinePosition park;
    // Machine Info's stepper lock: the $1 to restore on unlocking
    // (workspace.diagnostics.stepperMotor.storedValue); empty: none.
    std::string stepperRestoreValue;
    // Files loaded last, newest first.
    std::vector<RecentFile> recentFiles;
    // Run outline (workspace.outlineMode / outlineSpeed; 0: rapid moves)
    job::OutlineMode outlineMode = job::OutlineMode::Detailed;
    double outlineSpeed = 0;
    // Keyboard shortcuts: changes from the defaults, by gSender's command id,
    // and the global switch (preferences.shortcuts.shouldHold, inverted).
    std::map<std::string, ShortcutBinding> shortcuts;
    bool shortcutsEnabled = true;
    // Tool change wizards: the fixed sensor's position (machine
    // coordinates, workspace.toolChangePosition), the first tool with it,
    // and the optional position to change bits at.
    toolchange::MachinePosition toolChangePosition;
    std::string firstToolBehaviour = toolchange::kFirstToolBehaviours[0];
    bool moveToManualPosition = false;
    toolchange::MachinePosition manualPosition;
};

// The tool change strategies (gSender's option names, in its order).
inline constexpr const char* kToolChangeOptions[] = {"Ignore",           "Pause",
                                                     "Standard Re-zero", "Flexible Re-zero",
                                                     "Fixed Tool Sensor", "Code"};

AppSettings loadAppSettings(const config::ConfigStore& store);
void saveAppSettings(config::ConfigStore& store, const AppSettings& settings);

}  // namespace gs::app
