#include "app_settings.hpp"

#include "gs/util/jsnumber.hpp"

#include <boost/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>

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

inline void readProp(const json::object& o, std::string_view key, bool& target) {
    target = flag(o, key, target);
}
inline void readProp(const json::object& o, std::string_view key, double& target) {
    target = number(o, key, target);
}
inline void readProp(const json::object& o, std::string_view key, int& target) {
    target = static_cast<int>(number(o, key, static_cast<double>(target)));
}
inline void readProp(const json::object& o, std::string_view key, std::int64_t& target) {
    target = static_cast<std::int64_t>(number(o, key, static_cast<double>(target)));
}
inline void readProp(const json::object& o, std::string_view key, std::string& target) {
    target = text(o, key, target);
}


std::vector<RecentFile> loadRecentFiles(const json::value* value) {
    std::vector<RecentFile> files;
    if (!value || !value->is_array()) {
        return files;
    }
    for (const json::value& entry : value->as_array()) {
        if (!entry.is_object()) {
            continue;
        }
        const json::object& o = entry.as_object();
        RecentFile file{text(o, "fileName"), text(o, "filePath"), static_cast<std::int64_t>(number(o, "fileSize", 0)),
                        static_cast<std::int64_t>(number(o, "timeUploaded", 0))};
        if (!file.filePath.empty()) {
            files.push_back(std::move(file));
        }
    }
    return files;
}

json::array saveRecentFiles(const std::vector<RecentFile>& files) {
    json::array out;
    for (const RecentFile& file : files) {
        out.push_back(json::object{{"fileName", file.fileName},
                                   {"filePath", file.filePath},
                                   {"fileSize", file.fileSize},
                                   {"timeUploaded", file.timeUploaded}});
    }
    return out;
}

rotary::FirmwareValues loadFirmwareValues(const json::value* value, rotary::FirmwareValues values) {
    if (!value || !value->is_object()) {
        return values;
    }
    for (auto& [key, current] : values) {
        if (const json::value* v = value->as_object().if_contains(key)) {
            // Settings are strings; a switch may have stored a boolean.
            current = v->is_string()   ? std::string(v->as_string())
                      : v->is_bool()   ? (v->as_bool() ? "1" : "0")
                      : v->is_number() ? js::numberToString(v->to_number<double>())
                                       : current;
        }
    }
    return values;
}

json::object saveFirmwareValues(const rotary::FirmwareValues& values) {
    json::object out;
    for (const auto& [key, value] : values) {
        out[key] = value;
    }
    return out;
}

// A number that may have been stored as the text of an input (upstream's
// rotary surfacing tool saves its fields as typed).
double numberOrText(const json::object& object, std::string_view key, double fallback) {
    const json::value* value = object.if_contains(key);
    if (value && value->is_string()) {
        const double parsed = js::stringToNumber(value->as_string());
        return std::isfinite(parsed) ? parsed : fallback;
    }
    return number(object, key, fallback);
}

inline void readPropOrText(const json::object& o, std::string_view key, double& target) {
    target = numberOrText(o, key, target);
}
inline void readPropOrText(const json::object& o, std::string_view key, int& target) {
    target = static_cast<int>(numberOrText(o, key, static_cast<double>(target)));
}


rotary::StockTurningOptions loadStockTurning(const json::object& o) {
    rotary::StockTurningOptions t;
    readPropOrText(o, "stockLength", t.stockLength);
    readPropOrText(o, "stepdown", t.stepdown);
    readPropOrText(o, "bitDiameter", t.bitDiameter);
    readPropOrText(o, "spindleRPM", t.spindleRPM);
    readPropOrText(o, "feedrate", t.feedrate);
    readPropOrText(o, "stepover", t.stepover);
    readPropOrText(o, "startHeight", t.startHeight);
    readPropOrText(o, "finalHeight", t.finalHeight);
    readProp(o, "enableRehoming", t.enableRehoming);
    readProp(o, "shouldDwell", t.shouldDwell);
    readPropOrText(o, "toolNumber", t.toolNumber);
    return t;
}

json::object saveStockTurning(const rotary::StockTurningOptions& t) {
    return {{"stockLength", t.stockLength}, {"stepdown", t.stepdown},       {"bitDiameter", t.bitDiameter},
            {"spindleRPM", t.spindleRPM},   {"feedrate", t.feedrate},       {"stepover", t.stepover},
            {"startHeight", t.startHeight}, {"finalHeight", t.finalHeight}, {"enableRehoming", t.enableRehoming},
            {"shouldDwell", t.shouldDwell}, {"toolNumber", t.toolNumber}};
}

RotarySettings loadRotary(const json::object& o) {
    RotarySettings r;
    r.showControls = flag(o, "showControls", false);
    r.rotaryMode = text(o, "mode", "DEFAULT") == "ROTARY";
    r.firmware = loadFirmwareValues(o.if_contains("firmwareSettings"), r.firmware);
    r.defaults = loadFirmwareValues(o.if_contains("defaultFirmwareSettings"), r.defaults);
    if (const json::value* turning = o.if_contains("stockTurning"); turning && turning->is_object()) {
        r.stockTurning = loadStockTurning(turning->as_object());
    }
    r.diameterOffset = flag(o, "diameterOffsetEnabled", false);
    return r;
}

json::object saveRotary(const RotarySettings& r) {
    return {{"showControls", r.showControls},
            {"mode", r.rotaryMode ? "ROTARY" : "DEFAULT"},
            {"firmwareSettings", saveFirmwareValues(r.firmware)},
            {"defaultFirmwareSettings", saveFirmwareValues(r.defaults)},
            {"stockTurning", saveStockTurning(r.stockTurning)},
            {"diameterOffsetEnabled", r.diameterOffset}};
}

SpindleSettings loadSpindle(const json::object& o) {
    SpindleSettings s;
    s.laserMode = text(o, "mode", "spindle") == "laser";
    s.inputType = text(o, "inputType", "Slider") == "Number" ? "Number" : "Slider";
    readProp(o, "speed", s.speed);
    readProp(o, "spindleMax", s.spindleMax);
    readProp(o, "spindleMin", s.spindleMin);
    if (const json::value* laser = o.if_contains("laser"); laser && laser->is_object()) {
        const json::object& l = laser->as_object();
        readProp(l, "laserOnOutline", s.laser.onOutline);
        readProp(l, "power", s.laser.power);
        readProp(l, "duration", s.laser.duration);
        readProp(l, "xOffset", s.laser.xOffset);
        readProp(l, "yOffset", s.laser.yOffset);
        readProp(l, "minPower", s.laser.minPower);
        readProp(l, "maxPower", s.laser.maxPower);
    }
    return s;
}

json::object saveSpindle(const SpindleSettings& s) {
    const LaserSettings& l = s.laser;
    return {{"mode", s.laserMode ? "laser" : "spindle"},
            {"inputType", s.inputType},
            {"speed", s.speed},
            {"spindleMax", s.spindleMax},
            {"spindleMin", s.spindleMin},
            {"laser", json::object{{"laserOnOutline", l.onOutline},
                                   {"power", l.power},
                                   {"duration", l.duration},
                                   {"xOffset", l.xOffset},
                                   {"yOffset", l.yOffset},
                                   {"minPower", l.minPower},
                                   {"maxPower", l.maxPower}}}};
}

// The port keeps gSender's own shape, so both read the same way.
AccessibilitySettings loadAccessibility(const json::object& o) {
    AccessibilitySettings a;
    readProp(o, "statusAnnouncements", a.statusAnnouncements);
    readProp(o, "jobProgressAnnouncements", a.jobProgressAnnouncements);
    a.jobProgressIncrement = std::clamp(static_cast<int>(number(o, "jobProgressIncrement", 10)), 1, 50);
    readProp(o, "focusRings", a.focusRings);
    readProp(o, "focusTrapping", a.focusTrapping);
    readProp(o, "visualizerKeyboardControl", a.visualizerKeyboardControl);
    if (const json::value* cues = o.if_contains("audioCues"); cues && cues->is_object()) {
        const json::object& c = cues->as_object();
        readProp(c, "enabled", a.audioCues);
        readProp(c, "jobComplete", a.cueJobComplete);
        readProp(c, "alarmTriggered", a.cueAlarm);
        readProp(c, "toolChange", a.cueToolChange);
        readProp(c, "probeSuccess", a.cueProbeSuccess);
    }
    readProp(o, "reducedMotion", a.reducedMotion);
    if (const json::value* summary = o.if_contains("gcodeSummary"); summary && summary->is_object()) {
        readProp(summary->as_object(), "enabled", a.gcodeSummary);
        readProp(summary->as_object(), "showVisually", a.gcodeSummaryVisible);
    }
    readProp(o, "showKeyboardMap", a.showKeyboardMap);
    readProp(o, "displayScaleFactor", a.displayScale);
    return a;
}

json::object saveAccessibility(const AccessibilitySettings& a) {
    return {{"statusAnnouncements", a.statusAnnouncements},
            {"jobProgressAnnouncements", a.jobProgressAnnouncements},
            {"jobProgressIncrement", a.jobProgressIncrement},
            {"focusRings", a.focusRings},
            {"focusTrapping", a.focusTrapping},
            {"visualizerKeyboardControl", a.visualizerKeyboardControl},
            {"audioCues", json::object{{"enabled", a.audioCues},
                                       {"jobComplete", a.cueJobComplete},
                                       {"alarmTriggered", a.cueAlarm},
                                       {"toolChange", a.cueToolChange},
                                       {"probeSuccess", a.cueProbeSuccess}}},
            {"reducedMotion", a.reducedMotion},
            {"gcodeSummary", json::object{{"enabled", a.gcodeSummary}, {"showVisually", a.gcodeSummaryVisible}}},
            {"showKeyboardMap", a.showKeyboardMap},
            {"displayScaleFactor", a.displayScale}};
}

// An IPv4 address as upstream keeps it: [192, 168, 5, 1].
std::array<int, 4> loadIp(const json::value* value, std::array<int, 4> fallback) {
    if (!value || !value->is_array() || value->as_array().size() != 4) {
        return fallback;
    }
    std::array<int, 4> ip{};
    for (std::size_t i = 0; i < 4; ++i) {
        const json::value& part = value->as_array()[i];
        if (!part.is_number()) {
            return fallback;
        }
        ip[i] = std::clamp(static_cast<int>(part.to_number<double>()), 0, 255);
    }
    return ip;
}

probe::ProbeSettings loadProbe(const json::object& o) {
    probe::ProbeSettings p;
    p.plateType = probe::plateTypeFromName(text(o, "touchplateType")).value_or(p.plateType);
    if (const json::value* z = o.if_contains("zThickness"); z && z->is_object()) {
        const json::object& t = z->as_object();
        readProp(t, "standardBlock", p.zThickness.standardBlock);
        readProp(t, "autoZero", p.zThickness.autoZero);
        readProp(t, "zProbe", p.zThickness.zProbe);
        readProp(t, "probe3D", p.zThickness.probe3D);
        readProp(t, "bitZero", p.zThickness.bitZero);
        readProp(t, "bitZeroZOnly", p.zThickness.bitZeroZOnly);
    }
    readProp(o, "xyThickness", p.xyThickness);
    readProp(o, "probeFeedrate", p.probeFeedrate);
    readProp(o, "probeFastFeedrate", p.probeFastFeedrate);
    readProp(o, "retractionDistance", p.retractionDistance);
    readProp(o, "zRetractNormal", p.zRetractNormal);
    readProp(o, "zRetractAuto", p.zRetractAuto);
    readProp(o, "zProbeDistance", p.zProbeDistance);
    readProp(o, "tipDiameter3D", p.tipDiameter3D);
    readProp(o, "xyRetract3D", p.xyRetract3D);
    readProp(o, "probeMovementSpeed", p.probeMovementSpeed);
    readProp(o, "connectivityTest", p.connectivityTest);
    p.direction = static_cast<int>(number(o, "direction", p.direction)) & 3;
    return p;
}

// workspace.tools: [{metricDiameter, imperialDiameter}]; the defaults when
// there is no list.
std::vector<probe::ToolDiameter> loadTools(const json::value* tools) {
    if (!tools || !tools->is_array()) {
        return probe::defaultTools();
    }
    std::vector<probe::ToolDiameter> list;
    for (const json::value& tool : tools->as_array()) {
        if (tool.is_object()) {
            const double metric = number(tool.as_object(), "metricDiameter", 0);
            const double imperial = number(tool.as_object(), "imperialDiameter", 0);
            if (metric > 0 || imperial > 0) {
                list.push_back({metric, imperial});
            }
        }
    }
    return list;
}

json::array saveTools(const std::vector<probe::ToolDiameter>& tools) {
    json::array list;
    for (const probe::ToolDiameter& tool : tools) {
        list.push_back(json::object{{"metricDiameter", tool.metric}, {"imperialDiameter", tool.imperial}});
    }
    return list;
}

json::object saveProbe(const probe::ProbeSettings& p) {
    const probe::PlateThickness& z = p.zThickness;
    return {
        {"touchplateType", probe::plateTypeName(p.plateType)},
        {"zThickness", json::object{{"standardBlock", z.standardBlock},
                                    {"autoZero", z.autoZero},
                                    {"zProbe", z.zProbe},
                                    {"probe3D", z.probe3D},
                                    {"bitZero", z.bitZero},
                                    {"bitZeroZOnly", z.bitZeroZOnly}}},
        {"xyThickness", p.xyThickness},
        {"probeFeedrate", p.probeFeedrate},
        {"probeFastFeedrate", p.probeFastFeedrate},
        {"retractionDistance", p.retractionDistance},
        {"zRetractNormal", p.zRetractNormal},
        {"zRetractAuto", p.zRetractAuto},
        {"zProbeDistance", p.zProbeDistance},
        {"tipDiameter3D", p.tipDiameter3D},
        {"xyRetract3D", p.xyRetract3D},
        {"probeMovementSpeed", p.probeMovementSpeed},
        {"connectivityTest", p.connectivityTest},
        {"direction", p.direction},
    };
}

surfacing::Options loadSurfacing(const json::object& o) {
    surfacing::Options s;
    readProp(o, "bitDiameter", s.bitDiameter);
    readProp(o, "stepover", s.stepover);
    readProp(o, "feedrate", s.feedrate);
    readProp(o, "length", s.length);
    readProp(o, "width", s.width);
    readProp(o, "skimDepth", s.skimDepth);
    readProp(o, "maxDepth", s.maxDepth);
    readProp(o, "spindleRPM", s.spindleRPM);
    s.type = surfacing::patternFromName(text(o, "type")).value_or(s.type);
    s.startPosition = surfacing::startPositionFromName(text(o, "startPosition")).value_or(s.startPosition);
    readProp(o, "spindle", s.spindle);
    readProp(o, "cutDirectionFlipped", s.cutDirectionFlipped);
    readProp(o, "shouldDwell", s.shouldDwell);
    readProp(o, "flood", s.flood);
    readProp(o, "mist", s.mist);
    readProp(o, "toolNumber", s.toolNumber);
    return s;
}

json::object saveSurfacing(const surfacing::Options& s) {
    return {
        {"bitDiameter", s.bitDiameter},
        {"stepover", s.stepover},
        {"feedrate", s.feedrate},
        {"length", s.length},
        {"width", s.width},
        {"skimDepth", s.skimDepth},
        {"maxDepth", s.maxDepth},
        {"spindleRPM", s.spindleRPM},
        {"type", surfacing::patternName(s.type)},
        {"startPosition", surfacing::startPositionName(s.startPosition)},
        {"spindle", s.spindle},
        {"cutDirectionFlipped", s.cutDirectionFlipped},
        {"shouldDwell", s.shouldDwell},
        {"flood", s.flood},
        {"mist", s.mist},
        {"toolNumber", s.toolNumber},
    };
}

toolchange::MachinePosition loadPosition(const json::object& root, std::string_view key) {
    toolchange::MachinePosition p;
    if (const json::value* value = root.if_contains(key); value && value->is_object()) {
        const json::object& o = value->as_object();
        p = {number(o, "x", 0), number(o, "y", 0), number(o, "z", 0)};
    }
    return p;
}

json::object savePosition(const toolchange::MachinePosition& p) {
    return {{"x", p.x}, {"y", p.y}, {"z", p.z}};
}

controller::JogSpeeds loadSpeeds(const json::object& root, std::string_view key, controller::JogSpeeds speeds) {
    const json::value* value = root.if_contains(key);
    if (!value || !value->is_object()) {
        return speeds;
    }
    const json::object& o = value->as_object();
    readProp(o, "xyStep", speeds.xyStep);
    readProp(o, "zStep", speeds.zStep);
    readProp(o, "aStep", speeds.aStep);
    readProp(o, "feedrate", speeds.feedrate);
    return speeds;
}

json::object saveSpeeds(const controller::JogSpeeds& s) {
    return {{"xyStep", s.xyStep}, {"zStep", s.zStep}, {"aStep", s.aStep}, {"feedrate", s.feedrate}};
}

}  // namespace

const controller::JogSpeeds& JogSettings::speeds(controller::JogPreset preset) const {
    switch (preset) {
        case controller::JogPreset::Rapid: return rapid;
        case controller::JogPreset::Precise: return precise;
        case controller::JogPreset::Custom: return custom;
        case controller::JogPreset::Normal: break;
    }
    return normal;
}

void addRecentFile(std::vector<RecentFile>& files, RecentFile file) {
    // Known: its date renews (updateRecentFileDate). Either way it goes in
    // front, so files loaded within the same millisecond keep newest first.
    std::erase_if(files, [&file](const RecentFile& f) { return f.filePath == file.filePath; });
    files.insert(files.begin(), std::move(file));
    std::stable_sort(files.begin(), files.end(),
                     [](const RecentFile& a, const RecentFile& b) { return a.timeUploaded > b.timeUploaded; });
    if (files.size() > kRecentFileLimit) {
        files.resize(kRecentFileLimit);
    }
}

AppSettings loadAppSettings(const config::ConfigStore& store) {
    const json::value app = store.get("app", json::object());
    return app.is_object() ? appSettingsFromJson(app.as_object()) : AppSettings{};
}

AppSettings appSettingsFromJson(const json::object& root) {
    AppSettings settings;
    if (const json::value* tool = root.if_contains("toolChange"); tool && tool->is_object()) {
        const json::object& t = tool->as_object();
        readProp(t, "option", settings.toolChange.option);
        readProp(t, "passthrough", settings.toolChange.passthrough);
        readProp(t, "preHook", settings.toolChange.preHook);
        readProp(t, "postHook", settings.toolChange.postHook);
        readProp(t, "skipDialog", settings.toolChange.skipDialog);
        readProp(t, "firstToolBehaviour", settings.firstToolBehaviour);
        readProp(t, "moveToManualPosition", settings.moveToManualPosition);
        settings.manualPosition = loadPosition(t, "manualPosition");
    }
    settings.toolChangePosition = loadPosition(root, "toolChangePosition");
    readProp(root, "spindleDelay", settings.preferences.spindleDelay);
    readProp(root, "showLineWarnings", settings.preferences.showLineWarnings);
    readProp(root, "useAaxisForGrbl", settings.preferences.useAaxisForGrbl);
    readProp(root, "port", settings.port);
    readProp(root, "baudRate", settings.baudRate);
    readProp(root, "networkPort", settings.networkPort);
    settings.ethernetIp = loadIp(root.if_contains("ethernetIp"), settings.ethernetIp);
    settings.defaultFirmware =
        text(root, "defaultFirmware", "Grbl") == "grblHAL" ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl;
    if (const json::value* probe = root.if_contains("probe"); probe && probe->is_object()) {
        settings.probe = loadProbe(probe->as_object());
        readProp(probe->as_object(), "touchplateTypeSwitcher", settings.touchplateTypeSwitcher);
        settings.probeTools = loadTools(probe->as_object().if_contains("tools"));
    }
    if (const json::value* surfacing = root.if_contains("surfacing"); surfacing && surfacing->is_object()) {
        settings.surfacing = loadSurfacing(surfacing->as_object());
    }
    if (const json::value* spindle = root.if_contains("spindle"); spindle && spindle->is_object()) {
        settings.spindle = loadSpindle(spindle->as_object());
    }
    if (const json::value* rotaryObject = root.if_contains("rotary"); rotaryObject && rotaryObject->is_object()) {
        settings.rotary = loadRotary(rotaryObject->as_object());
    }
    if (const json::value* a11y = root.if_contains("accessibility"); a11y && a11y->is_object()) {
        settings.accessibility = loadAccessibility(a11y->as_object());
    }
    if (const json::value* jog = root.if_contains("jog"); jog && jog->is_object()) {
        const json::object& j = jog->as_object();
        settings.jog.rapid = loadSpeeds(j, "rapid", settings.jog.rapid);
        settings.jog.normal = loadSpeeds(j, "normal", settings.jog.normal);
        settings.jog.precise = loadSpeeds(j, "precise", settings.jog.precise);
        settings.jog.custom = loadSpeeds(j, "custom", settings.jog.custom);
        readProp(j, "threshold", settings.jog.threshold);
        readProp(j, "preventJoggingPastLimits", settings.jog.preventJoggingPastLimits);
    }
    settings.metric = text(root, "units", "mm") != "in";
    readProp(root, "customDecimalPlaces", settings.customDecimalPlaces);
    readProp(root, "safeRetractHeight", settings.safeRetractHeight);
    readProp(root, "warnZero", settings.warnZero);
    settings.park = loadPosition(root, "park");
    readProp(root, "stepperRestoreValue", settings.stepperRestoreValue);
    settings.recentFiles = loadRecentFiles(root.if_contains("recentFiles"));
    settings.outlineMode = job::outlineModeFromName(text(root, "outlineMode")).value_or(settings.outlineMode);
    readProp(root, "outlineSpeed", settings.outlineSpeed);
    readProp(root, "toastDuration", settings.toastDuration);
    readProp(root, "jobEndModal", settings.jobEndModal);
    readProp(root, "maintenanceNotifications", settings.maintenanceNotifications);
    readProp(root, "liteMode", settings.liteMode);
    readProp(root, "autoReconnect", settings.autoReconnect);
    readProp(root, "revertWorkspace", settings.revertWorkspace);
    readProp(root, "powerSaving", settings.powerSaving);
    readProp(root, "promptExit", settings.promptExit);
    readProp(root, "spindleFunctions", settings.spindleFunctions);
    readProp(root, "coolantFunctions", settings.coolantFunctions);
    readProp(root, "hideProcessedLines", settings.hideProcessedLines);
    readProp(root, "warnBadFile", settings.warnBadFile);
    readProp(root, "visualizerTheme", settings.visualizerTheme);
    settings.perspective = text(root, "projection", "Perspective") != "Orthographic";
    readProp(root, "darkMode", settings.darkMode);
    readProp(root, "machineProfileId", settings.machineProfileId);
    readProp(root, "showBoundingBox", settings.showBoundingBox);
    readProp(root, "boundingBoxLabels", settings.boundingBoxLabels);
    readProp(root, "showMachineBed", settings.showMachineBed);
    readProp(root, "trimGridToBed", settings.trimGridToBed);
    readProp(root, "followTool", settings.followTool);
    readProp(root, "backupFrequency", settings.backupFrequency);
    readProp(root, "backupLocation", settings.backupLocation);
    readProp(root, "lastBackupTime", settings.lastBackupTime);
    readProp(root, "lastBackupVersion", settings.lastBackupVersion);
    settings.liteOption = text(root, "liteOption", "Light") == "Everything" ? "Everything" : "Light";
    if (const json::value* shortcuts = root.if_contains("shortcuts"); shortcuts && shortcuts->is_object()) {
        for (const auto& [id, value] : shortcuts->as_object()) {
            if (value.is_object()) {
                settings.shortcuts[std::string(id)] =
                    ShortcutBinding{text(value.as_object(), "keys"), flag(value.as_object(), "isActive", true)};
            }
        }
    }
    readProp(root, "shortcutsEnabled", settings.shortcutsEnabled);
    if (const json::value* pads = root.if_contains("gamepad"); pads && pads->is_object()) {
        if (const json::value* profiles = pads->as_object().if_contains("profiles"); profiles && profiles->is_array()) {
            settings.gamepadProfiles = gamepad::profilesFromJson(*profiles);
        }
    }
    return settings;
}

namespace {

json::object jogObject(const JogSettings& jog) {
    return {{"rapid", saveSpeeds(jog.rapid)},
            {"normal", saveSpeeds(jog.normal)},
            {"precise", saveSpeeds(jog.precise)},
            {"custom", saveSpeeds(jog.custom)},
            {"threshold", jog.threshold},
            {"preventJoggingPastLimits", jog.preventJoggingPastLimits}};
}

json::object shortcutsObject(const std::map<std::string, ShortcutBinding>& shortcuts) {
    json::object out;
    for (const auto& [id, binding] : shortcuts) {
        out[id] = json::object{{"keys", binding.keys}, {"isActive", binding.active}};
    }
    return out;
}

}  // namespace

double displayScaleFactor(const std::filesystem::path& configFile) {
    std::ifstream in(configFile, std::ios::binary);
    if (!in) {
        return 1.0;
    }
    const std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    boost::system::error_code error;
    const json::value document = json::parse(content, error);
    const auto member = [](const json::value* value, std::string_view key) -> const json::value* {
        const json::value* found = value && value->is_object() ? value->as_object().if_contains(key) : nullptr;
        return found && found->is_object() ? found : nullptr;
    };
    const json::value* accessibility = error ? nullptr : member(member(&document, "app"), "accessibility");
    if (!accessibility) {
        return 1.0;
    }
    // parseFloat("125%") / 100
    const double percent = js::parseFloat(text(accessibility->as_object(), "displayScaleFactor", "100%"));
    return std::isfinite(percent) && percent >= 25 && percent <= 400 ? percent / 100 : 1.0;
}

void saveAppSettings(config::ConfigStore& store, const AppSettings& settings) {
    store.set("app", appSettingsToJson(settings));
}

json::object appSettingsToJson(const AppSettings& settings) {
    const controller::ToolChangeContext& t = settings.toolChange;
    return json::object{
                         {"toolChange", json::object{{"option", t.option},
                                                     {"passthrough", t.passthrough},
                                                     {"preHook", t.preHook},
                                                     {"postHook", t.postHook},
                                                     {"skipDialog", t.skipDialog},
                                                     {"firstToolBehaviour", settings.firstToolBehaviour},
                                                     {"moveToManualPosition", settings.moveToManualPosition},
                                                     {"manualPosition", savePosition(settings.manualPosition)}}},
                         {"toolChangePosition", savePosition(settings.toolChangePosition)},
                         {"spindleDelay", settings.preferences.spindleDelay},
                         {"showLineWarnings", settings.preferences.showLineWarnings},
                         {"useAaxisForGrbl", settings.preferences.useAaxisForGrbl},
                         {"port", settings.port},
                         {"baudRate", settings.baudRate},
                         {"networkPort", settings.networkPort},
                         {"ethernetIp", json::array(settings.ethernetIp.begin(), settings.ethernetIp.end())},
                         {"defaultFirmware",
                          settings.defaultFirmware == protocol::Firmware::GrblHal ? "grblHAL" : "Grbl"},
                         {"probe", [&settings] {
                              json::object probe = saveProbe(settings.probe);
                              probe["touchplateTypeSwitcher"] = settings.touchplateTypeSwitcher;
                              probe["tools"] = saveTools(settings.probeTools);
                              return probe;
                          }()},
                         {"surfacing", saveSurfacing(settings.surfacing)},
                         {"spindle", saveSpindle(settings.spindle)},
                         {"rotary", saveRotary(settings.rotary)},
                         {"spindleFunctions", settings.spindleFunctions},
                         {"coolantFunctions", settings.coolantFunctions},
                         {"accessibility", saveAccessibility(settings.accessibility)},
                         {"jog", jogObject(settings.jog)},
                         {"units", settings.metric ? "mm" : "in"},
                         {"customDecimalPlaces", settings.customDecimalPlaces},
                         {"safeRetractHeight", settings.safeRetractHeight},
                         {"warnZero", settings.warnZero},
                         {"park", savePosition(settings.park)},
                         {"stepperRestoreValue", settings.stepperRestoreValue},
                         {"recentFiles", saveRecentFiles(settings.recentFiles)},
                         {"outlineMode", job::outlineModeName(settings.outlineMode)},
                         {"outlineSpeed", settings.outlineSpeed},
                         {"toastDuration", settings.toastDuration},
                         {"jobEndModal", settings.jobEndModal},
                         {"maintenanceNotifications", settings.maintenanceNotifications},
                         {"liteMode", settings.liteMode},
                         {"autoReconnect", settings.autoReconnect},
                         {"revertWorkspace", settings.revertWorkspace},
                         {"powerSaving", settings.powerSaving},
                         {"promptExit", settings.promptExit},
                         {"hideProcessedLines", settings.hideProcessedLines},
                         {"warnBadFile", settings.warnBadFile},
                         {"visualizerTheme", settings.visualizerTheme},
                         {"projection", settings.perspective ? "Perspective" : "Orthographic"},
                         {"darkMode", settings.darkMode},
                         {"machineProfileId", settings.machineProfileId},
                         {"showBoundingBox", settings.showBoundingBox},
                         {"boundingBoxLabels", settings.boundingBoxLabels},
                         {"showMachineBed", settings.showMachineBed},
                         {"trimGridToBed", settings.trimGridToBed},
                         {"followTool", settings.followTool},
                         {"backupFrequency", settings.backupFrequency},
                         {"backupLocation", settings.backupLocation},
                         {"lastBackupTime", settings.lastBackupTime},
                         {"lastBackupVersion", settings.lastBackupVersion},
                         {"liteOption", settings.liteOption},
                         {"shortcuts", shortcutsObject(settings.shortcuts)},
                         {"shortcutsEnabled", settings.shortcutsEnabled},
                         {"gamepad", json::object{{"profiles", gamepad::profilesToJson(settings.gamepadProfiles)}}},
                     };
}

// ---- gSender's settings ----------------------------------------------------------------

std::optional<std::string> keysFromMousetrap(std::string_view combo) {
    if (combo.empty()) {
        return std::string();
    }
    // "+" separates, but the key itself may be "+" ("shift++").
    std::vector<std::string> parts;
    std::string current;
    for (const char c : combo) {
        if (c == '+' && !current.empty()) {
            parts.push_back(std::move(current));
            current.clear();
        } else {
            current.push_back(c);
        }
    }
    if (!current.empty()) {
        parts.push_back(std::move(current));
    }
    const auto lower = [](std::string text) {
        std::transform(text.begin(), text.end(), text.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return text;
    };
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    bool meta = false;
    for (std::size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string name = lower(parts[i]);
        if (name == "ctrl" || name == "control") {
            ctrl = true;
        } else if (name == "alt" || name == "option") {
            alt = true;
        } else if (name == "shift") {
            shift = true;
        } else if (name == "command" || name == "meta" || name == "cmd" || name == "mod") {
            meta = true;
        } else {
            return std::nullopt;
        }
    }
    static const std::pair<std::string_view, std::string_view> kNamed[] = {
        {"backspace", "Backspace"}, {"tab", "Tab"},     {"enter", "Return"},     {"return", "Return"},
        {"capslock", "CapsLock"},   {"escape", "Esc"},  {"esc", "Esc"},          {"space", "Space"},
        {"pageup", "PgUp"},         {"pagedown", "PgDown"}, {"left", "Left"},    {"right", "Right"},
        {"up", "Up"},               {"down", "Down"},   {"del", "Del"},          {"delete", "Del"},
        {"ins", "Ins"},             {"insert", "Ins"},  {"end", "End"},          {"home", "Home"},
        {"plus", "+"},
    };
    const std::string key = lower(parts.back());
    std::string name;
    for (const auto& [from, to] : kNamed) {
        if (key == from) {
            name = to;
        }
    }
    if (name.empty() && key.size() >= 2 && key.size() <= 3 && key[0] == 'f' &&
        std::all_of(key.begin() + 1, key.end(), [](char c) { return c >= '0' && c <= '9'; })) {
        name = "F" + key.substr(1);  // f1 ... f35
    }
    if (name.empty() && key.size() == 1 && key[0] > ' ' && key[0] < 0x7f) {
        name = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(key[0]))));
    }
    if (name.empty()) {
        return std::nullopt;
    }
    std::string out;
    if (ctrl) {
        out += "Ctrl+";
    }
    if (alt) {
        out += "Alt+";
    }
    if (shift) {
        out += "Shift+";
    }
    if (meta) {
        out += "Meta+";
    }
    return out + name;
}

namespace {

const json::object* child(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    return v && v->is_object() ? &v->as_object() : nullptr;
}

}  // namespace

std::optional<GSenderSettings> readGSenderSettings(const json::value& file) {
    if (!file.is_object()) {
        return std::nullopt;
    }
    const json::object& root = file.as_object();
    // The Export's "settings", or the store file's "state".
    const json::object* state = child(root, "settings");
    if (!state) {
        state = child(root, "state");
    }
    const json::object* workspace = state ? child(*state, "workspace") : nullptr;
    const json::object* widgets = state ? child(*state, "widgets") : nullptr;
    if (!workspace && !widgets) {
        return std::nullopt;
    }
    const json::object none;
    const json::object& w = workspace ? *workspace : none;
    const json::object& g = widgets ? *widgets : none;
    GSenderSettings out;
    AppSettings& s = out.settings;  // over the defaults, as upstream merges

    s.metric = text(w, "units", "mm") != "in";
    s.customDecimalPlaces = static_cast<int>(number(w, "customDecimalPlaces", 0));
    s.safeRetractHeight = number(w, "safeRetractHeight", 0);
    s.warnZero = flag(w, "shouldWarnZero", false);
    s.park = loadPosition(w, "park");
    s.outlineMode = job::outlineModeFromName(text(w, "outlineMode")).value_or(s.outlineMode);
    s.outlineSpeed = number(w, "outlineSpeed", 0);
    s.toastDuration = static_cast<int>(numberOrText(w, "toastDuration", 0));
    s.revertWorkspace = flag(w, "revertWorkspace", false);
    s.powerSaving = flag(w, "powerSaving", false);
    s.promptExit = flag(w, "promptExit", false);
    s.spindleFunctions = flag(w, "spindleFunctions", false);
    s.coolantFunctions = flag(w, "coolantFunctions", true);
    s.darkMode = flag(w, "enableDarkMode", false);
    if (const json::object* a11y = child(w, "accessibility")) {
        s.accessibility = loadAccessibility(*a11y);
    }
    if (const json::object* profile = child(w, "machineProfile")) {
        s.machineProfileId = static_cast<int>(number(*profile, "id", -1));
    }
    if (const std::string frequency = text(w, "backupFreq"); !frequency.empty()) {
        s.backupFrequency = frequency;
    }
    s.backupLocation = text(w, "backupLoc");
    if (const std::string firmware = text(w, "defaultFirmware"); !firmware.empty()) {
        s.defaultFirmware = firmware == "grblHAL" ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl;
    }
    s.recentFiles = loadRecentFiles(w.if_contains("recentFiles"));
    s.jog.preventJoggingPastLimits = flag(w, "preventJoggingPastLimits", false);
    s.rotary.rotaryMode = text(w, "mode", "DEFAULT") == "ROTARY";
    if (const json::object* rotaryAxis = child(w, "rotaryAxis")) {
        s.preferences.useAaxisForGrbl = flag(*rotaryAxis, "useAaxisForGrbl", false);
        s.rotary.firmware = loadFirmwareValues(rotaryAxis->if_contains("firmwareSettings"), s.rotary.firmware);
        s.rotary.defaults = loadFirmwareValues(rotaryAxis->if_contains("defaultFirmwareSettings"), s.rotary.defaults);
    }
    // The surfacing options: the tool reads and saves rotary.stockTurning
    // (beside the widgets, not the widgets.rotary defaults); the defaults
    // when it never saved.
    const auto stockTurning = [](const json::object* rotaryObject) -> const json::object* {
        const json::object* turning = rotaryObject ? child(*rotaryObject, "stockTurning") : nullptr;
        return turning ? child(*turning, "options") : nullptr;
    };
    const json::object* rotaryWidget = child(g, "rotary");
    if (const json::object* tab = rotaryWidget ? child(*rotaryWidget, "tab") : nullptr) {
        s.rotary.showControls = flag(*tab, "show", false);
    }
    const json::object* options = stockTurning(child(*state, "rotary"));
    if (!options) {
        options = stockTurning(rotaryWidget);
    }
    if (options) {
        s.rotary.stockTurning = loadStockTurning(*options);
    }
    if (const json::object* diagnostics = child(w, "diagnostics")) {
        if (const json::object* stepper = child(*diagnostics, "stepperMotor")) {
            if (const json::value* stored = stepper->if_contains("storedValue"); stored && !stored->is_null()) {
                s.stepperRestoreValue =
                    stored->is_string() ? std::string(stored->as_string())
                                        : (stored->is_number() ? js::numberToString(stored->to_number<double>()) : "");
            }
        }
    }

    // Tool changes: the strategy, its hooks and the positions.
    s.toolChange.option = text(w, "toolChangeOption", "Ignore");
    if (const json::object* hooks = child(w, "toolChangeHooks")) {
        s.toolChange.preHook = text(*hooks, "preHook");
        s.toolChange.postHook = text(*hooks, "postHook");
    }
    if (const json::object* tool = child(w, "toolChange")) {
        s.toolChange.passthrough = flag(*tool, "passthrough", false);
        s.toolChange.skipDialog = flag(*tool, "skipDialog", false);
        s.moveToManualPosition = flag(*tool, "moveToManualPosition", false);
        s.firstToolBehaviour = text(*tool, "firstToolBehaviour", s.firstToolBehaviour);
        s.manualPosition = loadPosition(*tool, "manualPosition");
    }
    s.toolChangePosition = loadPosition(w, "toolChangePosition");

    // Probing: the plate profile (workspace) and the widget's feeds and
    // distances - the port keeps them in one object with the same names.
    json::object probe;
    if (const json::object* widget = child(g, "probe")) {
        probe = *widget;
    }
    if (const json::object* profile = child(w, "probeProfile")) {
        for (const auto& member : *profile) {
            probe[member.key()] = member.value();
        }
    }
    if (text(probe, "touchplateType") == "AutoZero Touchplate") {
        probe["touchplateType"] = "AutoZero";  // older files
    }
    s.probe = loadProbe(probe);
    s.touchplateTypeSwitcher = flag(probe, "touchplateTypeSwitcher", false);
    s.probeTools = loadTools(w.if_contains("tools"));

    if (const json::object* axes = child(g, "axes")) {
        if (const json::object* jog = child(*axes, "jog")) {
            s.jog.rapid = loadSpeeds(*jog, "rapid", s.jog.rapid);
            s.jog.normal = loadSpeeds(*jog, "normal", s.jog.normal);
            s.jog.precise = loadSpeeds(*jog, "precise", s.jog.precise);
            s.jog.custom = loadSpeeds(*jog, "custom", s.jog.custom);
            s.jog.threshold = static_cast<int>(number(*jog, "threshold", s.jog.threshold));
        }
    }
    if (const json::object* connection = child(g, "connection")) {
        s.port = text(*connection, "port");
        s.baudRate = static_cast<int>(number(*connection, "baudrate", s.baudRate));
        s.autoReconnect = flag(*connection, "autoReconnect", false);
        s.networkPort = static_cast<int>(number(*connection, "ethernetPort", s.networkPort));
        s.ethernetIp = loadIp(connection->if_contains("ip"), s.ethernetIp);
    }
    if (const json::object* spindle = child(g, "spindle")) {
        s.spindle = loadSpindle(*spindle);
        s.preferences.spindleDelay = number(*spindle, "delay", 0);
    }
    if (const json::object* surfacing = child(g, "surfacing")) {
        s.surfacing = loadSurfacing(*surfacing);
    }
    if (const json::object* visualizer = child(g, "visualizer")) {
        s.preferences.showLineWarnings = flag(*visualizer, "showLineWarnings", false);
        s.jobEndModal = flag(*visualizer, "jobEndModal", true);
        s.maintenanceNotifications = flag(*visualizer, "maintenanceTaskNotifications", true);
        s.liteMode = flag(*visualizer, "liteMode", false);
        s.hideProcessedLines = flag(*visualizer, "hideProcessedLines", false);
        s.warnBadFile = flag(*visualizer, "showWarning", false);
        s.visualizerTheme = text(*visualizer, "theme", "Dark");
        s.perspective = text(*visualizer, "projection", "Perspective") != "Orthographic";
        s.boundingBoxLabels = flag(*visualizer, "boundingBoxLabels", false);
        s.rotary.diameterOffset = flag(*visualizer, "rotaryDiameterOffsetEnabled", false);
        s.followTool = flag(*visualizer, "followToolDuringRuntime", false);
        if (const json::object* objects = child(*visualizer, "objects")) {
            if (const json::object* limits = child(*objects, "limits")) {
                s.showBoundingBox = flag(*limits, "visible", true);
            }
            if (const json::object* bed = child(*objects, "machineBed")) {
                s.showMachineBed = flag(*bed, "visible", false);
                s.trimGridToBed = flag(*bed, "trimGridToBed", false);
            }
        }
        s.liteOption = text(*visualizer, "liteOption", "Light") == "Everything" ? "Everything" : "Light";
    }

    // Keyboard shortcuts: every binding gSender stored, converted; the caller
    // keeps the ones that differ from the port's defaults.
    if (const json::object* commandKeys = child(*state, "commandKeys")) {
        for (const auto& [command, value] : *commandKeys) {
            if (!value.is_object()) {
                continue;
            }
            const std::optional<std::string> keys = keysFromMousetrap(text(value.as_object(), "keys"));
            if (!keys) {
                out.unreadableShortcuts.emplace_back(command);
                continue;
            }
            s.shortcuts[std::string(command)] = ShortcutBinding{*keys, flag(value.as_object(), "isActive", true)};
        }
    }
    if (const json::object* pads = child(w, "gamepad")) {
        if (const json::value* profiles = pads->if_contains("profiles"); profiles && profiles->is_array()) {
            s.gamepadProfiles = gamepad::profilesFromJson(*profiles);
        }
    }
    if (const json::object* events = child(root, "events")) {
        out.events = *events;
    }
    return out;
}

std::string AppSettings::ethernetAddress() const {
    return std::to_string(ethernetIp[0]) + '.' + std::to_string(ethernetIp[1]) + '.' + std::to_string(ethernetIp[2]) +
           '.' + std::to_string(ethernetIp[3]);
}

}  // namespace gs::app
