#pragma once

// The application's own preferences (gSender kept these in the UI's store:
// workspace.toolChangeOption/Hooks, spindle delay, line warnings, the
// connection defaults). Persisted under "app" in the port's config file.

#include "gs/config/config_store.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/jogging.hpp"
#include "gs/job/outline.hpp"
#include "gs/probe/probing.hpp"
#include "gs/rotary/rotary.hpp"
#include "gs/surfacing/surfacing.hpp"
#include "gs/toolchange/wizards.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string_view>
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
    std::string inputType = "Slider";  // widgets.spindle.inputType: "Slider" or "Number"
    double speed = 1000;      // rpm for M3/M4
    double spindleMax = 30000;  // $30/$31 kept for spindle mode while the laser has them
    double spindleMin = 10000;
    LaserSettings laser;
};

// Rotary: widgets.rotary (the controls, the surfacing tool), workspace.mode
// and workspace.rotaryAxis (the firmware values rotary mode writes on Grbl,
// and the ones it saved to restore).
struct RotarySettings {
    bool showControls = false;  // "Rotary controls": the Rotary tab and A jogging
    bool rotaryMode = false;    // workspace.mode ROTARY
    rotary::FirmwareValues firmware = rotary::rotaryFirmwareSettings();
    rotary::FirmwareValues defaults = rotary::defaultFirmwareSettings();
    rotary::StockTurningOptions stockTurning;  // mm
    // "Visualize non-center zeros" (widgets.visualizer.rotaryDiameterOffsetEnabled).
    bool diameterOffset = false;
};

// workspace.accessibility (Settings > Accessibility).
struct AccessibilitySettings {
    // Screen reader announcements: the machine's status, the job's progress
    // every so many percent.
    bool statusAnnouncements = false;
    bool jobProgressAnnouncements = false;
    int jobProgressIncrement = 10;  // %, 1-50
    // Audio cues (audioCues.*): a job finishing, an alarm, a tool change, a
    // probe succeeding.
    bool audioCues = false;
    bool cueJobComplete = false;
    bool cueAlarm = false;
    bool cueToolChange = false;
    bool cueProbeSuccess = false;
    bool focusRings = false;
    bool focusTrapping = false;
    bool reducedMotion = false;
    std::string displayScale = "100%";  // displayScaleFactor: "50%" ... "200%"
    bool visualizerKeyboardControl = false;
    // gcodeSummary: the loaded file in words, and shown above the visualizer.
    bool gcodeSummary = false;
    bool gcodeSummaryVisible = false;
    bool showKeyboardMap = false;
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
    int networkPort = 23;  // widgets.connection.ethernetPort
    // "Connect to IP" (widgets.connection.ip): the board the connection list
    // offers over Ethernet.
    std::array<int, 4> ethernetIp{192, 168, 5, 1};
    std::string ethernetAddress() const;  // "192.168.5.1"
    protocol::Firmware defaultFirmware = protocol::Firmware::Grbl;
    // Probe widget and touch plate profile (mm)
    probe::ProbeSettings probe;
    // "Show touch plate switcher" (widgets.probe.touchplateTypeSwitcher): the
    // plate type chosen on the Probe tab too, not only in the settings.
    bool touchplateTypeSwitcher = false;
    // Surfacing tool (mm)
    surfacing::Options surfacing;
    // Spindle/Laser tab
    SpindleSettings spindle;
    // "Spindle/laser controls" and "Coolant controls" (workspace.
    // spindleFunctions / coolantFunctions): their tabs on the main window -
    // and the spindle's override - shown.
    bool spindleFunctions = false;
    bool coolantFunctions = true;
    // Rotary tab and mode
    RotarySettings rotary;
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
    // Notifications: how long pop-ups stay (workspace.toastDuration, ms;
    // 0 the default 5 s, -1 until closed, -2 none), and the alerts at a
    // job's end (widgets.visualizer.jobEndModal, maintenanceTaskNotifications).
    int toastDuration = 0;
    bool jobEndModal = true;
    bool maintenanceNotifications = true;
    // Lightweight mode (widgets.visualizer.liteMode / liteOption): for big
    // files, "Light" draws the cuts flat from above, "Everything" nothing.
    bool liteMode = false;
    std::string liteOption = "Light";
    // Basics: reconnect to the last machine on start
    // (widgets.connection.autoReconnect); let M2/M30 leave the job's end in
    // G54 rather than put the job's workspace back (workspace.revertWorkspace);
    // let the display sleep (workspace.powerSaving); ask before exiting
    // (workspace.promptExit).
    bool autoReconnect = false;
    bool revertWorkspace = false;
    bool powerSaving = false;
    bool promptExit = false;
    // Visualizer: done lines hidden rather than greyed
    // (widgets.visualizer.hideProcessedLines); the invalid lines of a
    // loaded file reported (widgets.visualizer.showWarning).
    bool hideProcessedLines = false;
    bool warnBadFile = false;
    // Visualizer options: the colour scheme (widgets.visualizer.theme), the
    // job's bounding box and its labels (objects.limits.visible,
    // boundingBoxLabels), the homed machine's bed and the grid trimmed to
    // it (objects.machineBed.visible / trimGridToBed), and the camera
    // following the tool while a job runs (followToolDuringRuntime).
    std::string visualizerTheme = "Dark";
    bool perspective = true;  // widgets.visualizer.projection: Perspective or Orthographic
    bool darkMode = false;  // workspace.enableDarkMode: the application dark
    // workspace.machineProfile (its id): the machine whose EEPROM defaults
    // the firmware settings compare with and restore; its size is the
    // visualizer's machine bed.
    int machineProfileId = -1;  // -1: the default (LongMill MK2 30x30)
    bool showBoundingBox = true;
    bool boundingBoxLabels = false;
    bool showMachineBed = false;
    bool trimGridToBed = false;
    bool followTool = false;
    // Settings backups (workspace.backupFreq / backupLoc / lastBackupTime):
    // "On Update", "Daily", "Weekly" or "Monthly", into the folder given
    // (the application data folder when empty or missing).
    std::string backupFrequency = "On Update";
    std::string backupLocation;
    std::int64_t lastBackupTime = 0;    // ms since the epoch
    std::string lastBackupVersion;      // the version that last backed up
    // Run outline (workspace.outlineMode / outlineSpeed; 0: rapid moves)
    job::OutlineMode outlineMode = job::OutlineMode::Detailed;
    double outlineSpeed = 0;
    AccessibilitySettings accessibility;
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
// Accessibility's "App display scale" as the settings file has it - read
// before Qt starts, which takes its scale once: 1 when unset or unreadable.
double displayScaleFactor(const std::filesystem::path& configFile);
void saveAppSettings(config::ConfigStore& store, const AppSettings& settings);
// The "app" object as stored in the config file and in exported settings.
AppSettings appSettingsFromJson(const boost::json::object& app);
boost::json::object appSettingsToJson(const AppSettings& settings);

// A key combination as gSender records it (Mousetrap's "ctrl+alt+command+h",
// "shift+pageup", "~") in Qt's portable text ("Ctrl+Alt+Meta+H",
// "Shift+PgUp", "~"); "" stays unbound. Nullopt for what Qt cannot name.
std::optional<std::string> keysFromMousetrap(std::string_view combo);

// A gSender settings file - its Export ({settings, events}) or its store
// file ({version, state}) - read as storeUpdate() does: upstream's keys over
// the defaults, "AutoZero Touchplate" read as AutoZero. Nullopt when the
// file is neither.
struct GSenderSettings {
    AppSettings settings;
    std::optional<boost::json::object> events;  // the event hooks, when present
    std::vector<std::string> unreadableShortcuts;  // commands whose keys Qt cannot name
};
std::optional<GSenderSettings> readGSenderSettings(const boost::json::value& file);

}  // namespace gs::app
