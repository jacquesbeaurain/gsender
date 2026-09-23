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

SpindleSettings loadSpindle(const json::object& o) {
    SpindleSettings s;
    s.laserMode = text(o, "mode", "spindle") == "laser";
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
    settings.outlineMode = job::outlineModeFromName(text(root, "outlineMode")).value_or(settings.outlineMode);
    settings.outlineSpeed = number(root, "outlineSpeed", 0);
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

void saveAppSettings(config::ConfigStore& store, const AppSettings& settings) {
    const controller::ToolChangeContext& t = settings.toolChange;
    store.set("app", json::object{
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
                         {"jog", jogObject(settings.jog)},
                         {"units", settings.metric ? "mm" : "in"},
                         {"customDecimalPlaces", settings.customDecimalPlaces},
                         {"safeRetractHeight", settings.safeRetractHeight},
                         {"warnZero", settings.warnZero},
                         {"park", savePosition(settings.park)},
                         {"stepperRestoreValue", settings.stepperRestoreValue},
                         {"outlineMode", job::outlineModeName(settings.outlineMode)},
                         {"outlineSpeed", settings.outlineSpeed},
                         {"shortcuts", shortcutsObject(settings.shortcuts)},
                         {"shortcutsEnabled", settings.shortcutsEnabled},
                     });
}

}  // namespace gs::app
