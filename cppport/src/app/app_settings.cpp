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

rotary::StockTurningOptions loadStockTurning(const json::object& o) {
    rotary::StockTurningOptions t;
    t.stockLength = numberOrText(o, "stockLength", t.stockLength);
    t.stepdown = numberOrText(o, "stepdown", t.stepdown);
    t.bitDiameter = numberOrText(o, "bitDiameter", t.bitDiameter);
    t.spindleRPM = numberOrText(o, "spindleRPM", t.spindleRPM);
    t.feedrate = numberOrText(o, "feedrate", t.feedrate);
    t.stepover = numberOrText(o, "stepover", t.stepover);
    t.startHeight = numberOrText(o, "startHeight", t.startHeight);
    t.finalHeight = numberOrText(o, "finalHeight", t.finalHeight);
    t.enableRehoming = flag(o, "enableRehoming", t.enableRehoming);
    t.shouldDwell = flag(o, "shouldDwell", t.shouldDwell);
    t.toolNumber = static_cast<int>(numberOrText(o, "toolNumber", t.toolNumber));
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
    return r;
}

json::object saveRotary(const RotarySettings& r) {
    return {{"showControls", r.showControls},
            {"mode", r.rotaryMode ? "ROTARY" : "DEFAULT"},
            {"firmwareSettings", saveFirmwareValues(r.firmware)},
            {"defaultFirmwareSettings", saveFirmwareValues(r.defaults)},
            {"stockTurning", saveStockTurning(r.stockTurning)}};
}

SpindleSettings loadSpindle(const json::object& o) {
    SpindleSettings s;
    s.laserMode = text(o, "mode", "spindle") == "laser";
    s.inputType = text(o, "inputType", "Slider") == "Number" ? "Number" : "Slider";
    s.speed = number(o, "speed", s.speed);
    s.spindleMax = number(o, "spindleMax", s.spindleMax);
    s.spindleMin = number(o, "spindleMin", s.spindleMin);
    if (const json::value* laser = o.if_contains("laser"); laser && laser->is_object()) {
        const json::object& l = laser->as_object();
        s.laser.onOutline = flag(l, "laserOnOutline", s.laser.onOutline);
        s.laser.power = number(l, "power", s.laser.power);
        s.laser.duration = number(l, "duration", s.laser.duration);
        s.laser.xOffset = number(l, "xOffset", s.laser.xOffset);
        s.laser.yOffset = number(l, "yOffset", s.laser.yOffset);
        s.laser.minPower = number(l, "minPower", s.laser.minPower);
        s.laser.maxPower = number(l, "maxPower", s.laser.maxPower);
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
    a.statusAnnouncements = flag(o, "statusAnnouncements", false);
    a.jobProgressAnnouncements = flag(o, "jobProgressAnnouncements", false);
    a.jobProgressIncrement = std::clamp(static_cast<int>(number(o, "jobProgressIncrement", 10)), 1, 50);
    a.focusRings = flag(o, "focusRings", false);
    a.focusTrapping = flag(o, "focusTrapping", false);
    a.visualizerKeyboardControl = flag(o, "visualizerKeyboardControl", false);
    if (const json::value* cues = o.if_contains("audioCues"); cues && cues->is_object()) {
        const json::object& c = cues->as_object();
        a.audioCues = flag(c, "enabled", false);
        a.cueJobComplete = flag(c, "jobComplete", false);
        a.cueAlarm = flag(c, "alarmTriggered", false);
        a.cueToolChange = flag(c, "toolChange", false);
        a.cueProbeSuccess = flag(c, "probeSuccess", false);
    }
    a.reducedMotion = flag(o, "reducedMotion", false);
    if (const json::value* summary = o.if_contains("gcodeSummary"); summary && summary->is_object()) {
        a.gcodeSummary = flag(summary->as_object(), "enabled", false);
        a.gcodeSummaryVisible = flag(summary->as_object(), "showVisually", false);
    }
    a.showKeyboardMap = flag(o, "showKeyboardMap", false);
    a.displayScale = text(o, "displayScaleFactor", "100%");
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

probe::ProbeSettings loadProbe(const json::object& o) {
    probe::ProbeSettings p;
    p.plateType = probe::plateTypeFromName(text(o, "touchplateType")).value_or(p.plateType);
    if (const json::value* z = o.if_contains("zThickness"); z && z->is_object()) {
        const json::object& t = z->as_object();
        p.zThickness.standardBlock = number(t, "standardBlock", p.zThickness.standardBlock);
        p.zThickness.autoZero = number(t, "autoZero", p.zThickness.autoZero);
        p.zThickness.zProbe = number(t, "zProbe", p.zThickness.zProbe);
        p.zThickness.probe3D = number(t, "probe3D", p.zThickness.probe3D);
        p.zThickness.bitZero = number(t, "bitZero", p.zThickness.bitZero);
        p.zThickness.bitZeroZOnly = number(t, "bitZeroZOnly", p.zThickness.bitZeroZOnly);
    }
    p.xyThickness = number(o, "xyThickness", p.xyThickness);
    p.probeFeedrate = number(o, "probeFeedrate", p.probeFeedrate);
    p.probeFastFeedrate = number(o, "probeFastFeedrate", p.probeFastFeedrate);
    p.retractionDistance = number(o, "retractionDistance", p.retractionDistance);
    p.zRetractNormal = number(o, "zRetractNormal", p.zRetractNormal);
    p.zRetractAuto = number(o, "zRetractAuto", p.zRetractAuto);
    p.zProbeDistance = number(o, "zProbeDistance", p.zProbeDistance);
    p.tipDiameter3D = number(o, "tipDiameter3D", p.tipDiameter3D);
    p.xyRetract3D = number(o, "xyRetract3D", p.xyRetract3D);
    p.probeMovementSpeed = number(o, "probeMovementSpeed", p.probeMovementSpeed);
    p.connectivityTest = flag(o, "connectivityTest", p.connectivityTest);
    p.direction = static_cast<int>(number(o, "direction", p.direction)) & 3;
    return p;
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
    s.bitDiameter = number(o, "bitDiameter", s.bitDiameter);
    s.stepover = number(o, "stepover", s.stepover);
    s.feedrate = number(o, "feedrate", s.feedrate);
    s.length = number(o, "length", s.length);
    s.width = number(o, "width", s.width);
    s.skimDepth = number(o, "skimDepth", s.skimDepth);
    s.maxDepth = number(o, "maxDepth", s.maxDepth);
    s.spindleRPM = number(o, "spindleRPM", s.spindleRPM);
    s.type = surfacing::patternFromName(text(o, "type")).value_or(s.type);
    s.startPosition = surfacing::startPositionFromName(text(o, "startPosition")).value_or(s.startPosition);
    s.spindle = text(o, "spindle", s.spindle);
    s.cutDirectionFlipped = flag(o, "cutDirectionFlipped", s.cutDirectionFlipped);
    s.shouldDwell = flag(o, "shouldDwell", s.shouldDwell);
    s.flood = flag(o, "flood", s.flood);
    s.mist = flag(o, "mist", s.mist);
    s.toolNumber = static_cast<int>(number(o, "toolNumber", s.toolNumber));
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
    speeds.xyStep = number(o, "xyStep", speeds.xyStep);
    speeds.zStep = number(o, "zStep", speeds.zStep);
    speeds.aStep = number(o, "aStep", speeds.aStep);
    speeds.feedrate = number(o, "feedrate", speeds.feedrate);
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
        settings.toolChange.option = text(t, "option", "Ignore");
        settings.toolChange.passthrough = flag(t, "passthrough", false);
        settings.toolChange.preHook = text(t, "preHook");
        settings.toolChange.postHook = text(t, "postHook");
        settings.toolChange.skipDialog = flag(t, "skipDialog", false);
        settings.firstToolBehaviour = text(t, "firstToolBehaviour", settings.firstToolBehaviour);
        settings.moveToManualPosition = flag(t, "moveToManualPosition", false);
        settings.manualPosition = loadPosition(t, "manualPosition");
    }
    settings.toolChangePosition = loadPosition(root, "toolChangePosition");
    settings.preferences.spindleDelay = number(root, "spindleDelay", 0);
    settings.preferences.showLineWarnings = flag(root, "showLineWarnings", false);
    settings.preferences.useAaxisForGrbl = flag(root, "useAaxisForGrbl", false);
    settings.port = text(root, "port");
    settings.baudRate = static_cast<int>(number(root, "baudRate", 115200));
    settings.networkPort = static_cast<int>(number(root, "networkPort", 23));
    settings.defaultFirmware =
        text(root, "defaultFirmware", "Grbl") == "grblHAL" ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl;
    if (const json::value* probe = root.if_contains("probe"); probe && probe->is_object()) {
        settings.probe = loadProbe(probe->as_object());
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
        settings.jog.threshold = static_cast<int>(number(j, "threshold", settings.jog.threshold));
        settings.jog.preventJoggingPastLimits = flag(j, "preventJoggingPastLimits", false);
    }
    settings.metric = text(root, "units", "mm") != "in";
    settings.customDecimalPlaces = static_cast<int>(number(root, "customDecimalPlaces", 0));
    settings.safeRetractHeight = number(root, "safeRetractHeight", 0);
    settings.warnZero = flag(root, "warnZero", false);
    settings.park = loadPosition(root, "park");
    settings.stepperRestoreValue = text(root, "stepperRestoreValue");
    settings.recentFiles = loadRecentFiles(root.if_contains("recentFiles"));
    settings.outlineMode = job::outlineModeFromName(text(root, "outlineMode")).value_or(settings.outlineMode);
    settings.outlineSpeed = number(root, "outlineSpeed", 0);
    settings.toastDuration = static_cast<int>(number(root, "toastDuration", 0));
    settings.jobEndModal = flag(root, "jobEndModal", true);
    settings.maintenanceNotifications = flag(root, "maintenanceNotifications", true);
    settings.liteMode = flag(root, "liteMode", false);
    settings.autoReconnect = flag(root, "autoReconnect", false);
    settings.revertWorkspace = flag(root, "revertWorkspace", false);
    settings.powerSaving = flag(root, "powerSaving", false);
    settings.promptExit = flag(root, "promptExit", false);
    settings.hideProcessedLines = flag(root, "hideProcessedLines", false);
    settings.warnBadFile = flag(root, "warnBadFile", false);
    settings.visualizerTheme = text(root, "visualizerTheme", "Dark");
    settings.perspective = text(root, "projection", "Perspective") != "Orthographic";
    settings.darkMode = flag(root, "darkMode", false);
    settings.machineProfileId = static_cast<int>(number(root, "machineProfileId", -1));
    settings.showBoundingBox = flag(root, "showBoundingBox", true);
    settings.boundingBoxLabels = flag(root, "boundingBoxLabels", false);
    settings.showMachineBed = flag(root, "showMachineBed", false);
    settings.trimGridToBed = flag(root, "trimGridToBed", false);
    settings.followTool = flag(root, "followTool", false);
    settings.backupFrequency = text(root, "backupFrequency", "On Update");
    settings.backupLocation = text(root, "backupLocation");
    settings.lastBackupTime = static_cast<std::int64_t>(number(root, "lastBackupTime", 0));
    settings.lastBackupVersion = text(root, "lastBackupVersion");
    settings.liteOption = text(root, "liteOption", "Light") == "Everything" ? "Everything" : "Light";
    if (const json::value* shortcuts = root.if_contains("shortcuts"); shortcuts && shortcuts->is_object()) {
        for (const auto& [id, value] : shortcuts->as_object()) {
            if (value.is_object()) {
                settings.shortcuts[std::string(id)] =
                    ShortcutBinding{text(value.as_object(), "keys"), flag(value.as_object(), "isActive", true)};
            }
        }
    }
    settings.shortcutsEnabled = flag(root, "shortcutsEnabled", true);
    return settings;
}

namespace {

json::object jogObject(const JogSettings& jog) {
    return {{"rapid", saveSpeeds(jog.rapid)},
            {"normal", saveSpeeds(jog.normal)},
            {"precise", saveSpeeds(jog.precise)},
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
                         {"defaultFirmware",
                          settings.defaultFirmware == protocol::Firmware::GrblHal ? "grblHAL" : "Grbl"},
                         {"probe", saveProbe(settings.probe)},
                         {"surfacing", saveSurfacing(settings.surfacing)},
                         {"spindle", saveSpindle(settings.spindle)},
                         {"rotary", saveRotary(settings.rotary)},
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

    if (const json::object* axes = child(g, "axes")) {
        if (const json::object* jog = child(*axes, "jog")) {
            s.jog.rapid = loadSpeeds(*jog, "rapid", s.jog.rapid);
            s.jog.normal = loadSpeeds(*jog, "normal", s.jog.normal);
            s.jog.precise = loadSpeeds(*jog, "precise", s.jog.precise);
            s.jog.threshold = static_cast<int>(number(*jog, "threshold", s.jog.threshold));
        }
    }
    if (const json::object* connection = child(g, "connection")) {
        s.port = text(*connection, "port");
        s.baudRate = static_cast<int>(number(*connection, "baudrate", s.baudRate));
        s.autoReconnect = flag(*connection, "autoReconnect", false);
        s.networkPort = static_cast<int>(number(*connection, "ethernetPort", s.networkPort));
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
    if (const json::object* events = child(root, "events")) {
        out.events = *events;
    }
    return out;
}

}  // namespace gs::app
