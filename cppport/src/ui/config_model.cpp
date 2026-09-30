#include "config_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "qt_text.hpp"
#include "visualizer_theme.hpp"

#include "gs/config/machine_profiles.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/controller.hpp"
#include "gs/protocol/firmware_data.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <QDate>
#include <QDateTime>
#include <QFile>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <set>

namespace gs::ui {
namespace {

using Staged = ConfigModel::Staged;
using Pref = ConfigModel::Pref;
using Entry = ConfigModel::Entry;

// "$110" -> 110 (grblHAL keys its own descriptions by number).
std::optional<int> settingNumber(const std::string& name) {
    int number = 0;
    if (name.size() < 2 || name[0] != '$') {
        return std::nullopt;
    }
    const auto [end, ec] = std::from_chars(name.data() + 1, name.data() + name.size(), number);
    return ec == std::errc() && end == name.data() + name.size() ? std::optional<int>(number) : std::nullopt;
}

// Lengths stored in mm (mm/min), shown in the staged units.
double shown(const Staged& st, double mm) {
    return st.s.metric ? mm : units::convertToImperial(mm);
}
double stored(const Staged& st, double value) {
    return st.s.metric ? value : units::convertToMetric(value);
}

std::string rotaryFirmware(const Staged& st, const char* key) {
    for (const auto& [name, value] : st.s.rotary.firmware) {
        if (name == key) {
            return value;
        }
    }
    return {};
}

void setRotaryFirmware(Staged& st, const char* key, std::string value) {
    for (auto& [name, current] : st.s.rotary.firmware) {
        if (name == key) {
            current = std::move(value);
            return;
        }
    }
    st.s.rotary.firmware.emplace_back(key, std::move(value));
}

double number(const std::string& text) {
    const double value = js::stringToNumber(text);
    return std::isfinite(value) ? value : 0.0;
}

const char* const kEvents[][3] = {
    {"gcode:start", QT_TRANSLATE_NOOP("ConfigModel", "File start"),
     QT_TRANSLATE_NOOP("ConfigModel", "Runs when you start a job, before the file itself runs.")},
    {"gcode:pause", QT_TRANSLATE_NOOP("ConfigModel", "File pause"),
     QT_TRANSLATE_NOOP("ConfigModel", "If you'd like to stop accessories or move out of the way when you pause during a job.")},
    {"gcode:resume", QT_TRANSLATE_NOOP("ConfigModel", "File resume"),
     QT_TRANSLATE_NOOP("ConfigModel", "Ensure that anything you set up for File pause is undone when you resume.")},
    {"gcode:stop", QT_TRANSLATE_NOOP("ConfigModel", "File stop/end"),
     QT_TRANSLATE_NOOP("ConfigModel", "A catch-all to ensure that stopped or ended jobs always safely turn everything off.")},
};

}  // namespace

// ---- the rows ------------------------------------------------------------------------------

#define GETTER(expr) [](const ConfigModel&, const Staged& st) { const auto& s = st.s; (void)s; return QVariant(expr); }
#define SETTER(stmt) [](ConfigModel&, Staged& st, const QVariant& v) { auto& s = st.s; (void)s; (void)v; stmt; }

namespace {

Pref boolPref(QString key, QString label, QString description,
              std::function<QVariant(const ConfigModel&, const Staged&)> get,
              std::function<void(ConfigModel&, Staged&, const QVariant&)> set) {
    Pref p;
    p.key = std::move(key);
    p.label = std::move(label);
    p.description = std::move(description);
    p.type = "bool";
    p.get = std::move(get);
    p.set = std::move(set);
    return p;
}

Pref numberPref(QString key, QString label, QString description, double min, double max, int decimals, QString unit,
                std::function<QVariant(const ConfigModel&, const Staged&)> get,
                std::function<void(ConfigModel&, Staged&, const QVariant&)> set) {
    Pref p = boolPref(std::move(key), std::move(label), std::move(description), std::move(get), std::move(set));
    p.type = "number";
    p.min = min;
    p.max = max;
    p.decimals = decimals;
    p.unit = std::move(unit);
    return p;
}

Pref selectPref(QString key, QString label, QString description, QStringList options,
                std::function<QVariant(const ConfigModel&, const Staged&)> get,
                std::function<void(ConfigModel&, Staged&, const QVariant&)> set) {
    Pref p = boolPref(std::move(key), std::move(label), std::move(description), std::move(get), std::move(set));
    p.type = "select";
    p.options = std::move(options);
    return p;
}


// Member-pointer binding helpers for AppSettings properties

inline Pref bindBool(QString key, QString label, QString description, bool app::AppSettings::*field) {
    return boolPref(std::move(key), std::move(label), std::move(description),
                    [field](const ConfigModel&, const Staged& st) { return QVariant(st.s.*field); },
                    [field](ConfigModel&, Staged& st, const QVariant& v) { st.s.*field = v.toBool(); });
}

template <typename Sub>
inline Pref bindBool(QString key, QString label, QString description, Sub app::AppSettings::*sub, bool Sub::*field) {
    return boolPref(std::move(key), std::move(label), std::move(description),
                    [sub, field](const ConfigModel&, const Staged& st) { return QVariant((st.s.*sub).*field); },
                    [sub, field](ConfigModel&, Staged& st, const QVariant& v) { (st.s.*sub).*field = v.toBool(); });
}

template <typename Sub, typename Sub2>
inline Pref bindBool(QString key, QString label, QString description, Sub app::AppSettings::*sub, Sub2 Sub::*sub2, bool Sub2::*field) {
    return boolPref(std::move(key), std::move(label), std::move(description),
                    [sub, sub2, field](const ConfigModel&, const Staged& st) { return QVariant(((st.s.*sub).*sub2).*field); },
                    [sub, sub2, field](ConfigModel&, Staged& st, const QVariant& v) { ((st.s.*sub).*sub2).*field = v.toBool(); });
}


inline Pref bindInt(QString key, QString label, QString description, int min, int max, QString unit, int app::AppSettings::*field) {
    return numberPref(std::move(key), std::move(label), std::move(description), min, max, 0, std::move(unit),
                      [field](const ConfigModel&, const Staged& st) { return QVariant(st.s.*field); },
                      [field](ConfigModel&, Staged& st, const QVariant& v) { st.s.*field = v.toInt(); });
}

template <typename Sub>
inline Pref bindInt(QString key, QString label, QString description, int min, int max, QString unit, Sub app::AppSettings::*sub, int Sub::*field) {
    return numberPref(std::move(key), std::move(label), std::move(description), min, max, 0, std::move(unit),
                      [sub, field](const ConfigModel&, const Staged& st) { return QVariant((st.s.*sub).*field); },
                      [sub, field](ConfigModel&, Staged& st, const QVariant& v) { (st.s.*sub).*field = v.toInt(); });
}

inline Pref bindDouble(QString key, QString label, QString description, double min, double max, int decimals, QString unit, double app::AppSettings::*field) {
    return numberPref(std::move(key), std::move(label), std::move(description), min, max, decimals, std::move(unit),
                      [field](const ConfigModel&, const Staged& st) { return QVariant(st.s.*field); },
                      [field](ConfigModel&, Staged& st, const QVariant& v) { st.s.*field = v.toDouble(); });
}

template <typename Sub>
inline Pref bindDouble(QString key, QString label, QString description, double min, double max, int decimals, QString unit, Sub app::AppSettings::*sub, double Sub::*field) {
    return numberPref(std::move(key), std::move(label), std::move(description), min, max, decimals, std::move(unit),
                      [sub, field](const ConfigModel&, const Staged& st) { return QVariant((st.s.*sub).*field); },
                      [sub, field](ConfigModel&, Staged& st, const QVariant& v) { (st.s.*sub).*field = v.toDouble(); });
}

template <typename Sub, typename Sub2>
inline Pref bindDouble(QString key, QString label, QString description, double min, double max, int decimals, QString unit, Sub app::AppSettings::*sub, Sub2 Sub::*sub2, double Sub2::*field) {
    return numberPref(std::move(key), std::move(label), std::move(description), min, max, decimals, std::move(unit),
                      [sub, sub2, field](const ConfigModel&, const Staged& st) { return QVariant(((st.s.*sub).*sub2).*field); },
                      [sub, sub2, field](ConfigModel&, Staged& st, const QVariant& v) { ((st.s.*sub).*sub2).*field = v.toDouble(); });
}

inline Pref bindString(QString key, QString label, QString description, QStringList options, std::string app::AppSettings::*field) {
    return selectPref(std::move(key), std::move(label), std::move(description), std::move(options),
                      [field](const ConfigModel&, const Staged& st) { return QVariant(QString::fromStdString(st.s.*field)); },
                      [field](ConfigModel&, Staged& st, const QVariant& v) { st.s.*field = v.toString().toStdString(); });
}

template <typename Sub>
inline Pref bindString(QString key, QString label, QString description, QStringList options, Sub app::AppSettings::*sub, std::string Sub::*field) {
    return selectPref(std::move(key), std::move(label), std::move(description), std::move(options),
                      [sub, field](const ConfigModel&, const Staged& st) { return QVariant(QString::fromStdString((st.s.*sub).*field)); },
                      [sub, field](ConfigModel&, Staged& st, const QVariant& v) { (st.s.*sub).*field = v.toString().toStdString(); });
}

Pref withType(Pref p, QString type) {
    p.type = std::move(type);
    return p;
}

Pref hiddenUnless(Pref p, std::function<bool(const Staged&)> hidden) {
    p.hidden = std::move(hidden);
    return p;
}

}  // namespace

ConfigModel::ConfigModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    // First, so the rows are rebuilt before anything reads them again.
    connect(this, &ConfigModel::changed, this, [this] { rowsStale_ = true; });
    buildMenu();
    reloadStaged();
    defaults_.s = app::AppSettings{};
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged, &app::Machine::workflowChanged}) {
        connect(&machine_, signal, this, &ConfigModel::liveChanged);
    }
    connect(&machine_, &app::Machine::connectionChanged, this, &ConfigModel::changed);  // the board's rows
    // The saved settings changed elsewhere (or by Apply): the edits made here
    // stay, the rest follow.
    connect(&machine_, &app::Machine::appSettingsChanged, this, [this] {
        if (!applying_) {
            reloadStaged();
        }
        Q_EMIT changed();
    });
    connect(&machine_, &app::Machine::settingsChanged, this, [this] {
        // The board's settings read again: edits it now has are done.
        for (auto it = eepromEdits_.begin(); it != eepromEdits_.end();) {
            const auto board = boardSetting(it->first);
            it = board && *board == it->second ? eepromEdits_.erase(it) : std::next(it);
        }
        Q_EMIT changed();
    });
}

ConfigModel::~ConfigModel() = default;

bool ConfigModel::differs(const Pref& p, const Staged& saved) const {
    // Compared in the staged units (saved: the saved state in them), but
    // the units themselves as they are.
    return p.get(*this, staged_) != p.get(*this, p.key == "units" ? saved_ : saved);
}

ConfigModel::Staged ConfigModel::savedInStagedUnits() const {
    Staged saved = saved_;
    saved.s.metric = staged_.s.metric;
    return saved;
}

void ConfigModel::reloadStaged() {
    // Keep the pending edits: re-apply them onto the new saved state.
    std::vector<std::pair<const Pref*, QVariant>> pending;
    const Staged saved = savedInStagedUnits();
    for (const Pref& p : prefs_) {
        if (differs(p, saved)) {
            pending.emplace_back(&p, p.get(*this, staged_));
        }
    }
    saved_.s = machine_.settings();
    saved_.hooks.clear();
    const config::EventStore hooks(machine_.config());
    for (const auto& event : kEvents) {
        const auto record = hooks.find(event[0]);
        saved_.hooks[event[0]] = {record && record->enabled, record ? record->commands : std::string()};
    }
    staged_ = saved_;
    for (const auto& [p, value] : pending) {
        p->set(*this, staged_, value);
    }
}

void ConfigModel::buildMenu() {
    const auto add = [this](Pref p) {
        prefs_.push_back(std::move(p));
        Entry e;
        e.kind = Entry::Setting;
        e.pref = static_cast<int>(prefs_.size()) - 1;
        return e;
    };
    const auto sub = [](const QString& label) {
        Entry e;
        e.kind = Entry::Subsection;
        e.text = label;
        return e;
    };
    const auto eeprom = [](const char* name, const char* remap = nullptr, const QString& label = {}) {
        Entry e;
        e.kind = Entry::Eeprom;
        e.eeprom = name;
        e.remap = remap ? remap : "";
        e.label = label;
        return e;
    };
    const auto action = [](const char* key) {
        Entry e;
        e.kind = Entry::Action;
        e.text = QString::fromLatin1(key);
        return e;
    };
    const auto eeproms = [&eeprom](std::vector<Entry>& into, std::initializer_list<const char*> names) {
        for (const char* name : names) {
            into.push_back(eeprom(name));
        }
    };
    const auto location = [](QString key, QString label, QString description,
                             toolchange::MachinePosition app::AppSettings::*field) {
        Pref p;
        p.key = std::move(key);
        p.label = std::move(label);
        p.description = std::move(description);
        p.type = "location";
        p.unit = "mm";
        p.get = [field](const ConfigModel&, const Staged& st) {
            const toolchange::MachinePosition& at = st.s.*field;
            return QVariant(QVariantList{at.x, at.y, at.z});
        };
        p.set = [field](ConfigModel&, Staged& st, const QVariant& v) {
            const QVariantList list = v.toList();
            if (list.size() == 3) {
                st.s.*field = {list[0].toDouble(), list[1].toDouble(), list[2].toDouble()};
            }
        };
        return p;
    };
    const auto length = [](QString key, QString label, QString description, bool speed,
                           double app::AppSettings::*field) {
        Pref p;
        p.key = std::move(key);
        p.label = std::move(label);
        p.description = std::move(description);
        p.type = speed ? "speed" : "length";
        p.min = 0;
        p.max = speed ? 100000 : 200;
        p.get = [field](const ConfigModel&, const Staged& st) { return QVariant(shown(st, st.s.*field)); };
        p.set = [field](ConfigModel&, Staged& st, const QVariant& v) { st.s.*field = stored(st, v.toDouble()); };
        return p;
    };

    // ---- Basics ----
    std::vector<Entry> basics;
    basics.push_back(add(selectPref("units", tr("Carve screen units"),
                                    tr("What units would you like gSender to show you?"), {"mm", "in"},
                                    GETTER(QString(s.metric ? "mm" : "in")), SETTER(s.metric = v.toString() == "mm"))));
    basics.push_back(add(bindBool("autoReconnect", tr("Reconnect automatically"),
                                  tr("Automatically reconnect to the last machine you used when you open gSender."),
                                  &app::AppSettings::autoReconnect)));
    basics.push_back(add(selectPref(
        "defaultFirmware", tr("Firmware fallback"),
        tr("Assumed when a connected board does not identify itself"), {"Grbl", "grblHAL"},
        GETTER(QString(s.defaultFirmware == protocol::Firmware::GrblHal ? "grblHAL" : "Grbl")),
        SETTER(s.defaultFirmware = v.toString() == "grblHAL" ? protocol::Firmware::GrblHal : protocol::Firmware::Grbl))));
    {
        Pref p = length("safeRetractHeight", tr("Safe height"),
                        tr("Z lifts this far before go-to-zero moves (machine Z with homing). 0 means none."), false,
                        &app::AppSettings::safeRetractHeight);
        basics.push_back(add(p));
    }
    QStringList outlineModes;
    for (const job::OutlineMode mode :
         {job::OutlineMode::Detailed, job::OutlineMode::Square, job::OutlineMode::RapidlessSquare}) {
        outlineModes << QString::fromUtf8(job::outlineModeName(mode).data());
    }
    basics.push_back(add(selectPref(
        "outlineMode", tr("Outline style"),
        tr("Detailed follows the toolpath's hull; Square its box; Rapidless Square the box of its cutting moves"),
        outlineModes, GETTER(QString::fromUtf8(job::outlineModeName(s.outlineMode).data())),
        SETTER(s.outlineMode = job::outlineModeFromName(v.toString().toStdString()).value_or(s.outlineMode)))));
    basics.push_back(add(bindDouble("outlineSpeed", tr("Outline speed"),
                                    tr("The outline's feed rate; 0 moves at rapid (G0)."), 0, 20000, 0, "mm/min",
                                    &app::AppSettings::outlineSpeed)));
    basics.push_back(add(bindBool("revertWorkspace", tr("Revert workspace"),
                                  tr("Allow g-code 'job finishing' commands like M2 and M30 to reset your CNCs "
                                     "workspace back to G54 at the end of each job."),
                                  &app::AppSettings::revertWorkspace)));
    basics.push_back(add(bindBool("powerSaving", tr("Power Saving"), tr("Allow screen to blank/sleep."),
                                  &app::AppSettings::powerSaving)));
    basics.push_back(add(bindBool("promptExit", tr("Prompt on exit"),
                                  tr("Pop up a confirmation window when exiting the program."),
                                  &app::AppSettings::promptExit)));
    basics.push_back(add(bindString("backupFrequency", tr("Run settings backup"),
                                    tr("Choose how often gSender will backup your settings. Useful in case you need "
                                       "to revert them in the future."),
                                    {"On Update", "Daily", "Weekly", "Monthly"},
                                    &app::AppSettings::backupFrequency)));
    basics.push_back(add(withType(bindString("backupLocation", tr("Settings backup location"),
                                                           tr("Choose the location to backup your settings to. Default: your OS's appData "
                                                              "location."),
                                                           {}, &app::AppSettings::backupLocation),
                                                "path")));
    basics.push_back(sub(tr("UI Options")));
    basics.push_back(add(bindBool("darkMode", tr("Dark mode"), tr("The application in dark colours."),
                                  &app::AppSettings::darkMode)));
    basics.push_back(add(bindInt("customDecimalPlaces", tr("DRO zeros"),
                                    tr("Decimal places of the position display. 0 keeps the defaults (2 in mm, 3 in "
                                       "inches)."),
                                    0, 5, QString(), &app::AppSettings::customDecimalPlaces)));
    basics.push_back(sub(tr("Visualizer options")));
    basics.push_back(add(bindString("visualizerTheme", tr("Visualizer theme"),
                                    tr("Independent colour control for the visualizer."), app::visualizerThemeNames(),
                                    &app::AppSettings::visualizerTheme)));
    basics.push_back(add(selectPref("projection", tr("Camera projection"),
                                    tr("Perspective (default) shows depth like a normal camera view. Orthographic "
                                       "removes that depth distortion, keeping parallel lines parallel - useful for "
                                       "lining up toolpaths precisely."),
                                    {"Perspective", "Orthographic"},
                                    GETTER(QString(s.perspective ? "Perspective" : "Orthographic")),
                                    SETTER(s.perspective = v.toString() == "Perspective"))));
    basics.push_back(add(bindBool("showBoundingBox", tr("Show bounding box"),
                                  tr("Draw a wireframe around the extents of the loaded G-code file."),
                                  &app::AppSettings::showBoundingBox)));
    basics.push_back(add(bindBool("boundingBoxLabels", tr("Show bounding box labels"),
                                  tr("Show X/Y/Z dimension labels on the bounding box."),
                                  &app::AppSettings::boundingBoxLabels)));
    basics.push_back(add(bindBool("showMachineBed", tr("Show machine bed indicator"),
                                  tr("Draw an outline of the machine's homed work area once homing is complete."),
                                  &app::AppSettings::showMachineBed)));
    basics.push_back(add(bindBool("trimGridToBed", tr("Trim grid to machine bed"),
                                  tr("When the machine bed indicator is shown, clip the background grid to just past "
                                     "the bed's edges instead of a fixed square."),
                                  &app::AppSettings::trimGridToBed)));
    basics.push_back(add(bindBool("hideProcessedLines", tr("Hide processed lines"),
                                  tr("Hide processed g-code lines in the visualizer."),
                                  &app::AppSettings::hideProcessedLines)));
    basics.push_back(add(bindString("liteOption", tr("Lightweight options"),
                                    tr("Enable with the feather when big files are slowing down your computer. "
                                       "(Light turns off 3D file view, Everything disables the visualizer)"),
                                    {"Light", "Everything"},
                                    &app::AppSettings::liteOption)));
    basics.push_back(add(bindBool("followTool", tr("Follow tool during runtime"),
                                  tr("While a job is running, pan the camera to track the tool in X/Y, keeping the "
                                     "same viewing angle and height."),
                                  &app::AppSettings::followTool)));
    basics.push_back(sub(tr("Jogging Presets")));
    {
        const QString names[] = {tr("Rapid"), tr("Normal"), tr("Precise")};
        for (int i = 0; i < 3; ++i) {
            Pref p;
            p.key = QStringLiteral("jog%1").arg(i);
            p.label = names[i];
            p.description = i == 1 ? tr("Set the XY and Z steps, the A step (degrees) and the speed of each preset.")
                                   : QString();
            p.type = "jog";
            p.get = [i](const ConfigModel&, const Staged& st) {
                const controller::JogSpeeds& j = i == 0 ? st.s.jog.rapid : i == 1 ? st.s.jog.normal : st.s.jog.precise;
                return QVariant(QVariantMap{{"xyStep", shown(st, j.xyStep)},
                                            {"zStep", shown(st, j.zStep)},
                                            {"aStep", j.aStep},
                                            {"feedrate", shown(st, j.feedrate)}});
            };
            p.set = [i](ConfigModel&, Staged& st, const QVariant& v) {
                controller::JogSpeeds& j = i == 0 ? st.s.jog.rapid : i == 1 ? st.s.jog.normal : st.s.jog.precise;
                const QVariantMap m = v.toMap();
                j.xyStep = stored(st, m.value("xyStep").toDouble());
                j.zStep = stored(st, m.value("zStep").toDouble());
                j.aStep = m.value("aStep").toDouble();
                j.feedrate = stored(st, m.value("feedrate").toDouble());
            };
            basics.push_back(add(p));
        }
    }
    basics.push_back(add(bindInt("jogThreshold", tr("Continuous jog delay"),
                                    tr("Where regular presses or clicks make single movements, hold for this long to "
                                       "begin jogging continuously. Some might prefer a longer delay like 700. "
                                       "(Default 250)"),
                                    50, 10000, "ms", &app::AppSettings::jog, &app::JogSettings::threshold)));
    basics.push_back(sub(tr("Notifications")));
    basics.push_back(add(bindBool("warnBadFile", tr("Warn if bad file"),
                                  tr("Report the invalid lines of a file when it loads."),
                                  &app::AppSettings::warnBadFile)));
    basics.push_back(add(bindBool("showLineWarnings", tr("Warn on bad line"),
                                  tr("Report the offending line when a job line errors."),
                                  &app::AppSettings::preferences, &controller::Preferences::showLineWarnings)));
    basics.push_back(add(bindBool("warnZero", tr("Warn when setting zero"),
                                  tr("The zero buttons ask first - useful if you tend to set zero accidentally"),
                                  &app::AppSettings::warnZero)));
    basics.push_back(add(bindBool("jobEndModal", tr("Job end notifications"),
                                  tr("Show a carving summary at the end of each job."),
                                  &app::AppSettings::jobEndModal)));
    basics.push_back(add(bindBool("maintenanceNotifications", tr("Maintenance notifications"),
                                  tr("Show upcoming maintenance tasks at the end of each job."),
                                  &app::AppSettings::maintenanceNotifications)));
    basics.push_back(add(bindInt("toastDuration", tr("Pop-up notification duration"),
                                 tr("How long notifications stay visible, in milliseconds, before auto-dismissing. "
                                    "(-1 keeps them up until manually dismissed, -2 disables them, Default 0 keeps "
                                    "default duration)"),
                                 -2, 10000, "ms", &app::AppSettings::toastDuration)));
    basics.push_back(sub(tr("Shortcuts")));
    basics.push_back(action("keyboardShortcuts"));
    menu_.emplace_back(tr("Basics"), std::move(basics));

    // ---- Motors ----
    std::vector<Entry> motors;
    motors.push_back(action("squareXY"));
    eeproms(motors, {"$3", "$37", "$680"});
    motors.push_back(sub(tr("X-axis")));
    eeproms(motors, {"$100", "$150", "$110", "$120"});
    motors.push_back(sub(tr("Y-axis")));
    eeproms(motors, {"$8", "$101", "$151", "$111", "$121"});
    motors.push_back(sub(tr("Z-axis")));
    eeproms(motors, {"$102", "$152", "$112", "$122"});
    motors.push_back(action("jogWizard"));
    menu_.emplace_back(tr("Motors"), std::move(motors));

    // ---- Probe ----
    std::vector<Entry> probeRows;
    eeproms(probeRows, {"$6", "$668"});
    QStringList plateTypes;
    for (const probe::PlateType type : {probe::PlateType::StandardBlock, probe::PlateType::AutoZero,
                                        probe::PlateType::ZProbe, probe::PlateType::Probe3D,
                                        probe::PlateType::BitZero}) {
        plateTypes << QString::fromUtf8(probe::plateTypeName(type).data());
    }
    probeRows.push_back(add(selectPref(
        "plateType", tr("Touch plate type"), tr("Select the touch plate you're using with your machine."),
        plateTypes, GETTER(QString::fromUtf8(probe::plateTypeName(s.probe.plateType).data())),
        SETTER(s.probe.plateType = probe::plateTypeFromName(v.toString().toStdString()).value_or(s.probe.plateType)))));
    probeRows.push_back(add(bindBool("touchplateTypeSwitcher", tr("Show touch plate switcher"),
                                     tr("Show a button on Probe tab to allow switching between touch plate types."),
                                     &app::AppSettings::touchplateTypeSwitcher)));
    probeRows.push_back(add(bindDouble("tipDiameter3D", tr("Tip diameter"), tr("The 3D probe's tip diameter."), 0, 20, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::tipDiameter3D)));
    probeRows.push_back(add(bindDouble("standardBlock", tr("Block thickness"), tr("The standard block's Z thickness."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zThickness, &probe::PlateThickness::standardBlock)));
    probeRows.push_back(add(bindDouble("autoZero", tr("AutoZero thickness"), tr("The AutoZero plate's Z thickness."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zThickness, &probe::PlateThickness::autoZero)));
    probeRows.push_back(add(bindDouble("zProbe", tr("Puck thickness"), tr("The Z probe puck's thickness."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zThickness, &probe::PlateThickness::zProbe)));
    probeRows.push_back(add(bindDouble("probe3D", tr("Z offset"), tr("The 3D probe's Z offset."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zThickness, &probe::PlateThickness::probe3D)));
    probeRows.push_back(add(bindDouble("bitZero", tr("BitZero thickness (XYZ)"), tr("The BitZero's inset thickness."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zThickness, &probe::PlateThickness::bitZero)));
    probeRows.push_back(add(bindDouble("bitZeroZOnly", tr("BitZero thickness (Z-only)"), tr("The BitZero's Z-only thickness."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zThickness, &probe::PlateThickness::bitZeroZOnly)));
    probeRows.push_back(add(bindDouble("xyThickness", tr("XY thickness"), tr("The standard block's XY thickness."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::xyThickness)));
    probeRows.push_back(add(bindDouble("xyRetract3D", tr("XY retract"), tr("How far the 3D probe backs off in X and Y."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::xyRetract3D)));
    probeRows.push_back(add(bindDouble("zProbeDistance", tr("Z probe distance"), tr("How far Z travels looking for the plate."), 0, 500, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zProbeDistance)));
    probeRows.push_back(add(bindDouble("probeFastFeedrate", tr("Fast find"), tr("The first, faster probe's speed."), 0, 10000, 3, "mm/min",
                                       &app::AppSettings::probe, &probe::ProbeSettings::probeFastFeedrate)));
    probeRows.push_back(add(bindDouble("probeFeedrate", tr("Slow find"), tr("The second, slower probe's speed."), 0, 10000, 3, "mm/min",
                                       &app::AppSettings::probe, &probe::ProbeSettings::probeFeedrate)));
    probeRows.push_back(add(bindDouble("retractionDistance", tr("Retraction"), tr("How far to back off between probes."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::retractionDistance)));
    probeRows.push_back(add(bindDouble("probeMovementSpeed", tr("Probe Movement Speed"),
                                       tr("The speed of the moves between probes; 0 moves at rapid (G0)."), 0, 20000, 3, "mm/min",
                                       &app::AppSettings::probe, &probe::ProbeSettings::probeMovementSpeed)));
    probeRows.push_back(add(bindDouble("zRetractNormal", tr("Final Z retract"), tr("How high Z ends after probing."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zRetractNormal)));
    probeRows.push_back(add(bindDouble("zRetractAuto", tr("Final Z retract (AutoZero)"),
                                       tr("How high Z ends after probing with the AutoZero."), 0, 100, 3, "mm",
                                       &app::AppSettings::probe, &probe::ProbeSettings::zRetractAuto)));
    probeRows.push_back(add(bindBool("connectivityTest", tr("Connection test"),
                                     tr("Check the probe circuit before probing."),
                                     &app::AppSettings::probe, &probe::ProbeSettings::connectivityTest)));
    probeRows.push_back(action("probePin"));
    menu_.emplace_back(tr("Probe"), std::move(probeRows));

    // ---- Action Buttons ----
    std::vector<Entry> buttons;
    buttons.push_back(eeprom("$450", "$590"));
    buttons.push_back(eeprom("$451", "$591"));
    buttons.push_back(eeprom("$452", "$592"));
    buttons.push_back(eeprom("$453", "$490"));
    buttons.push_back(eeprom("$454", "$491"));
    buttons.push_back(eeprom("$455", "$492"));
    menu_.emplace_back(tr("Action Buttons"), std::move(buttons));

    // ---- Homing/Limits ----
    std::vector<Entry> homing;
    eeproms(homing, {"$5", "$22", "$130", "$131", "$132", "$133", "$20", "$40"});
    homing.push_back(add(bindBool("preventJoggingPastLimits", tr("Stop jogging past limits"),
                                  tr("Prevent jogging in a direction where a limit switch has already been triggered."),
                                  &app::AppSettings::jog, &app::JogSettings::preventJoggingPastLimits)));
    homing.push_back(eeprom("$21"));
    homing.push_back(action("limitPins"));
    homing.push_back(sub(tr("Homing Behaviour")));
    eeproms(homing, {"$23", "$43", "$44", "$45", "$46", "$47", "$25", "$190", "$191", "$192", "$193", "$24", "$180",
                     "$181", "$182", "$183", "$26", "$27", "$290", "$291", "$292", "$293", "$170", "$171", "$172",
                     "$173", "$347", "$348", "$349"});
    homing.push_back(sub(tr("Parking")));
    homing.push_back(add(location("park", tr("Park location"),
                                  tr("Where the DRO's Park button goes once the machine has homed (machine "
                                     "coordinates)."),
                                  &app::AppSettings::park)));
    menu_.emplace_back(tr("Homing/Limits"), std::move(homing));

    // ---- Spindle/Laser ----
    std::vector<Entry> spindle;
    spindle.push_back(add(bindBool("spindleFunctions", tr("Spindle/laser controls"),
                                   tr("Show the Spindle/Laser tab and related functions on the main Carve page."),
                                   &app::AppSettings::spindleFunctions)));
    eeproms(spindle, {"$32", "$394", "$392"});
    spindle.push_back(add(bindDouble("spindleDelay", tr("Insert dwell for spindle commands"),
                                     tr("Dwell after each spindle start (M3/M4) in loaded jobs"), 0, 60, 1, "s",
                                     &app::AppSettings::preferences, &controller::Preferences::spindleDelay)));
    spindle.push_back(eeprom("$539"));
    spindle.push_back(add(bindDouble("spindleMin", tr("Minimum spindle speed"),
                                     tr("Written back as $31 when switching from laser to spindle mode"), 0, 100000, 0,
                                     "rpm", &app::AppSettings::spindle, &app::SpindleSettings::spindleMin)));
    spindle.push_back(eeprom("$31"));
    spindle.push_back(add(bindDouble("spindleMax", tr("Maximum spindle speed"),
                                     tr("Written back as $30 when switching from laser to spindle mode"), 0, 100000, 0,
                                     "rpm", &app::AppSettings::spindle, &app::SpindleSettings::spindleMax)));
    spindle.push_back(eeprom("$30"));
    eeproms(spindle, {"$395", "$511", "$512", "$513", "$520", "$521", "$522", "$523"});
    spindle.push_back(action("spindleTest"));
    spindle.push_back(sub(tr("Spindle PWM")));
    eeproms(spindle, {"$9", "$16", "$33", "$34", "$35", "$36", "$709"});
    spindle.push_back(sub(tr("Spindle Modbus")));
    eeproms(spindle, {"$340", "$374", "$375", "$462", "$463", "$464", "$465", "$466", "$467", "$468", "$469", "$470",
                      "$471", "$476", "$477", "$478", "$479", "$681"});
    spindle.push_back(sub(tr("Laser")));
    spindle.push_back(eeprom("$743"));
    spindle.push_back(add(bindDouble("laserMin", tr("Minimum laser power"),
                                     tr("Match this to the minimum S word setting in your laser CAM software. ($31 in "
                                        "laser mode; grblHAL $731, Default 0)"),
                                     0, 100000, 3, QString(),
                                     &app::AppSettings::spindle, &app::SpindleSettings::laser, &app::LaserSettings::minPower)));
    spindle.push_back(eeprom("$731"));
    spindle.push_back(add(bindDouble("laserMax", tr("Maximum laser power"),
                                     tr("Match this to the maximum S word setting in your laser CAM software. ($30 in "
                                        "laser mode; grblHAL $730, Default 255)"),
                                     0, 100000, 3, QString(),
                                     &app::AppSettings::spindle, &app::SpindleSettings::laser, &app::LaserSettings::maxPower)));
    spindle.push_back(eeprom("$730"));
    spindle.push_back(add(bindBool("laserOnOutline", tr("Laser on during outline"),
                                   tr("Turn on the laser at its lowest power to see the job position better"),
                                   &app::AppSettings::spindle, &app::SpindleSettings::laser, &app::LaserSettings::onOutline)));
    spindle.push_back(add(bindDouble("laserX", tr("Laser X offset"),
                                     tr("X-axis offset from the spindle (mark with a v-bit, then track the laser to "
                                        "that mark; grblHAL $770)"),
                                     -1000, 1000, 3, "mm",
                                     &app::AppSettings::spindle, &app::SpindleSettings::laser, &app::LaserSettings::xOffset)));
    spindle.push_back(eeprom("$741"));
    spindle.push_back(add(bindDouble("laserY", tr("Laser Y offset"), tr("Y-axis offset from the spindle (grblHAL $771)"),
                                     -1000, 1000, 3, "mm",
                                     &app::AppSettings::spindle, &app::SpindleSettings::laser, &app::LaserSettings::yOffset)));
    eeproms(spindle, {"$742", "$733", "$734", "$735", "$736"});
    spindle.push_back(action("laserTest"));
    menu_.emplace_back(tr("Spindle/Laser"), std::move(spindle));

    // ---- Accessory Outputs ----
    std::vector<Entry> outputs;
    outputs.push_back(add(bindBool("coolantFunctions", tr("Coolant controls"),
                                   tr("Show the coolant tab and related functions on the main Carve page."),
                                   &app::AppSettings::coolantFunctions)));
    outputs.push_back(eeprom("$673"));
    outputs.push_back(eeprom("$456", "$750"));
    outputs.push_back(eeprom("$457", "$751"));
    outputs.push_back(eeprom("$458", "$752"));
    outputs.push_back(eeprom("$459", "$753"));
    eeproms(outputs, {"$754", "$755", "$756", "$757", "$758", "$759", "$760", "$761", "$762", "$763", "$764", "$765",
                      "$766", "$767", "$768", "$769"});
    outputs.push_back(action("outputsTest"));
    menu_.emplace_back(tr("Accessory Outputs"), std::move(outputs));

    // ---- Rotary ----
    std::vector<Entry> rotaryRows;
    rotaryRows.push_back(add(bindBool("rotaryControls", tr("Rotary controls"),
                                      tr("Show the Rotary tab and related functions on the main Carve page. Turning "
                                         "it off leaves rotary mode."),
                                      &app::AppSettings::rotary, &app::RotarySettings::showControls)));
    rotaryRows.push_back(eeprom("$376"));
    // Upstream's hybrid settings: grblHAL's own A axis ($103, $113), else what
    // rotary mode writes to Grbl's Y ($101, $111).
    {
        Pref p = numberPref("rotaryResolution", tr("Resolution"),
                            tr("Travel resolution in steps per degree. ($103, Default 19.75308642)"), 0.000001, 100000,
                            8, tr("step/deg"), GETTER(number(rotaryFirmware(st, "$101"))),
                            SETTER(setRotaryFirmware(st, "$101", js::numberToString(v.toDouble()))));
        p.hidden = [this](const Staged&) { return boardSetting("$103").has_value(); };
        rotaryRows.push_back(add(p));
        rotaryRows.push_back(eeprom("$103", nullptr, tr("Resolution")));
        Pref speed = numberPref("rotaryMaxSpeed", tr("Max speed"),
                                tr("Max axis speed, also used for G0 rapids. ($113, Default 8000)"), 1, 1000000, 3,
                                tr("deg/min"), GETTER(number(rotaryFirmware(st, "$111"))),
                                SETTER(setRotaryFirmware(st, "$111", js::numberToString(v.toDouble()))));
        speed.hidden = [this](const Staged&) { return boardSetting("$113").has_value(); };
        rotaryRows.push_back(add(speed));
        rotaryRows.push_back(eeprom("$113", nullptr, tr("Max speed")));
    }
    rotaryRows.push_back(eeprom("$123"));
    rotaryRows.push_back(add(boolPref("forceSoftLimits", tr("Force soft limits"),
                                      tr("Enable soft limits when toggling into rotary mode. (grbl only)"),
                                      GETTER(rotaryFirmware(st, "$20") == "1"),
                                      SETTER(setRotaryFirmware(st, "$20", v.toBool() ? "1" : "0")))));
    rotaryRows.push_back(add(boolPref("forceHardLimits", tr("Force hard limits"),
                                      tr("Enable hard limits when toggling into rotary mode. (grbl only)"),
                                      GETTER(rotaryFirmware(st, "$21") == "1"),
                                      SETTER(setRotaryFirmware(st, "$21", v.toBool() ? "1" : "0")))));
    rotaryRows.push_back(eeprom("$538"));
    rotaryRows.push_back(add(bindBool("diameterOffset", tr("Visualize non-center zeros"),
                                      tr("For any rotary files that aren't zeroed to the centerpoint, apply an offset "
                                         "when a cylinder diameter is found in the file."),
                                      &app::AppSettings::rotary, &app::RotarySettings::diameterOffset)));
    rotaryRows.push_back(add(bindBool("useAaxisForGrbl", tr("Use A-axis for grbl"),
                                      tr("Enables A-axis controls and commands to be sent for devices running "
                                         "modified 4-axis grbl, rather than translating A into Y. (grbl only)"),
                                      &app::AppSettings::preferences, &controller::Preferences::useAaxisForGrbl)));
    rotaryRows.push_back(action("aJog"));
    menu_.emplace_back(tr("Rotary"), std::move(rotaryRows));

    // ---- Automations ----
    std::vector<Entry> automations;
    for (const auto& event : kEvents) {
        Pref p;
        p.key = QString::fromLatin1(event[0]);
        p.label = tr(event[1]);
        p.description = tr(event[2]);
        p.type = "event";
        p.noDefault = true;
        const std::string key = event[0];
        p.get = [key](const ConfigModel&, const Staged& st) {
            const auto it = st.hooks.find(key);
            const Staged::Hook hook = it == st.hooks.end() ? Staged::Hook{} : it->second;
            return QVariant(QVariantMap{{"enabled", hook.enabled}, {"commands", qstr(hook.commands)}});
        };
        p.set = [key](ConfigModel&, Staged& st, const QVariant& v) {
            const QVariantMap m = v.toMap();
            st.hooks[key] = {m.value("enabled").toBool(), m.value("commands").toString().toStdString()};
        };
        automations.push_back(add(p));
    }
    menu_.emplace_back(tr("Automations"), std::move(automations));

    // ---- Tool Changing ----
    std::vector<Entry> tools;
    QStringList strategies;
    for (const char* option : app::kToolChangeOptions) {
        strategies << QString::fromLatin1(option);
    }
    tools.push_back(add(bindBool("passthrough", tr("Passthrough"),
                                 tr("Send M6 to the firmware (it handles tool changes)"),
                                 &app::AppSettings::toolChange, &controller::ToolChangeContext::passthrough)));
    tools.push_back(add(hiddenUnless(
        selectPref("toolChangeOption", tr("gSender strategy"),
                   tr("Ignore: comment M6 out. Pause: pause the job at M6. Standard Re-zero: a wizard to change the "
                      "bit and re-zero Z. Flexible Re-zero: a wizard measuring tools on the touch plate. Fixed Tool "
                      "Sensor: a wizard measuring tools on a sensor (needs homing). Code: run the hooks below around "
                      "the tool change."),
                   strategies, GETTER(qstr(s.toolChange.option)),
                   SETTER(s.toolChange.option = v.toString().toStdString())),
        [](const Staged& st) { return st.s.toolChange.passthrough; })));
    const auto isOption = [](const char* option) {
        return [option](const Staged& st) { return st.s.toolChange.passthrough || st.s.toolChange.option != option; };
    };
    tools.push_back(add(hiddenUnless(bindBool("skipDialog", tr("Skip dialog"),
                                              tr("Code: run both hooks without asking in between"),
                                              &app::AppSettings::toolChange, &controller::ToolChangeContext::skipDialog),
                                     isOption("Code"))));
    tools.push_back(add(hiddenUnless(location("toolChangePosition", tr("Fixed sensor location"),
                                              tr("Where the fixed tool sensor is (machine coordinates)."),
                                              &app::AppSettings::toolChangePosition),
                                     isOption("Fixed Tool Sensor"))));
    QStringList firstTools;
    for (const char* behaviour : toolchange::kFirstToolBehaviours) {
        firstTools << QString::fromLatin1(behaviour);
    }
    tools.push_back(add(hiddenUnless(bindString("firstToolBehaviour", tr("First tool behaviour"),
                                                tr("What the first tool change of a job does."), firstTools,
                                                &app::AppSettings::firstToolBehaviour),
                                     isOption("Fixed Tool Sensor"))));
    tools.push_back(add(hiddenUnless(bindBool("moveToManualPosition", tr("Set tool change location"),
                                              tr("Move to a tool change location to change bits"),
                                              &app::AppSettings::moveToManualPosition),
                                     [](const Staged& st) { return st.s.toolChange.passthrough; })));
    tools.push_back(add(hiddenUnless(location("manualPosition", tr("Manual tool change location"),
                                              tr("Where the machine goes to change bits (machine coordinates)."),
                                              &app::AppSettings::manualPosition),
                                     [](const Staged& st) {
                                         return st.s.toolChange.passthrough || !st.s.moveToManualPosition;
                                     })));
    {
        Pref pre = withType(boolPref("preHook", tr("Before tool change"), tr("G-code run before the tool change."),
                                     GETTER(qstr(s.toolChange.preHook)),
                                     SETTER(s.toolChange.preHook = v.toString().toStdString())),
                            "textarea");
        Pref post = withType(boolPref("postHook", tr("After tool change"), tr("G-code run after the tool change."),
                                      GETTER(qstr(s.toolChange.postHook)),
                                      SETTER(s.toolChange.postHook = v.toString().toStdString())),
                             "textarea");
        tools.push_back(add(hiddenUnless(pre, isOption("Code"))));
        tools.push_back(add(hiddenUnless(post, isOption("Code"))));
    }
    tools.push_back(eeprom("$675"));
    menu_.emplace_back(tr("Tool Changing"), std::move(tools));

    // ---- Ethernet ----
    std::vector<Entry> ethernet;
    {
        Pref ip;
        ip.key = "ethernetIp";
        ip.label = tr("Connect to IP");
        ip.description =
            tr("IP address used to connect to CNCs over Ethernet and other network scanning. (Default 192.168.5.1)");
        ip.type = "ip";
        ip.get = GETTER(QVariantList({s.ethernetIp[0], s.ethernetIp[1], s.ethernetIp[2], s.ethernetIp[3]}));
        ip.set = [](ConfigModel&, Staged& st, const QVariant& v) {
            const QVariantList list = v.toList();
            for (int i = 0; i < 4 && i < list.size(); ++i) {
                st.s.ethernetIp[static_cast<std::size_t>(i)] = std::clamp(list[i].toInt(), 0, 255);
            }
        };
        ethernet.push_back(add(ip));
    }
    ethernet.push_back(add(bindInt("networkPort", tr("Ethernet port"),
                                      tr("The port exposed by the controller for Ethernet connectivity. (Used when "
                                         "attempting to connect over Ethernet, Default 23)"),
                                      1, 65535, QString(), &app::AppSettings::networkPort)));
    eeproms(ethernet, {"$301", "$302", "$303", "$304", "$70", "$300", "$305", "$307", "$308", "$535"});
    menu_.emplace_back(tr("Ethernet"), std::move(ethernet));

    std::vector<Entry> lights;
    eeproms(lights, {"$664", "$665"});
    menu_.emplace_back(tr("Status Lights"), std::move(lights));

    std::vector<Entry> advanced;
    eeproms(advanced, {"$0",   "$1",   "$2",   "$4",   "$29",  "$140", "$141", "$142", "$143", "$160", "$161",
                       "$162", "$163", "$200", "$201", "$202", "$203", "$210", "$211", "$212", "$213", "$220",
                       "$221", "$222", "$223", "$338", "$339", "$651", "$652", "$653", "$654", "$655", "$656",
                       "$657", "$658", "$659", "$660", "$661", "$662", "$663", "$744", "$745"});
    menu_.emplace_back(tr("Advanced Motors"), std::move(advanced));

    std::vector<Entry> more;
    eeproms(more, {"$10",  "$11",  "$12",  "$13",  "$14",  "$15",  "$17",  "$18",  "$19",  "$28",  "$39",
                   "$41",  "$42",  "$56",  "$57",  "$58",  "$60",  "$61",  "$63",  "$64",  "$65",  "$341",
                   "$342", "$343", "$344", "$345", "$346", "$370", "$372", "$384", "$393", "$398", "$481",
                   "$484", "$486", "$534", "$650", "$666", "$676"});
    menu_.emplace_back(tr("More Settings"), std::move(more));

    // ---- Accessibility ----
    std::vector<Entry> a11y;
    a11y.push_back(sub(tr("Announcements")));
    a11y.push_back(add(bindBool("statusAnnouncements", tr("Machine status"),
                                tr("Automatically announce machine status changes using screen readers. (Idle, "
                                   "Running, Alarm, etc.)"),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::statusAnnouncements)));
    a11y.push_back(add(bindBool("jobProgressAnnouncements", tr("Job progress"),
                                tr("Periodically announce job completion percentage."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::jobProgressAnnouncements)));
    a11y.push_back(add(hiddenUnless(bindInt("jobProgressIncrement", tr("Progress increment"),
                                            tr("The percentage increment at which to announce job progress. "
                                               "(Default 10%)"),
                                            1, 50, "%",
                                            &app::AppSettings::accessibility, &app::AccessibilitySettings::jobProgressIncrement),
                                    [](const Staged& st) { return !st.s.accessibility.jobProgressAnnouncements; })));
    a11y.push_back(sub(tr("Audio Cues")));
    a11y.push_back(add(bindBool("audioCues", tr("Enable audio cues"), tr("Play short sounds for specific machine events."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::audioCues)));
    const auto cueHidden = [](const Staged& st) { return !st.s.accessibility.audioCues; };
    a11y.push_back(add(hiddenUnless(bindBool("cueJobComplete", tr("Job complete sound"),
                                             tr("Play sound when a job finishes."),
                                             &app::AppSettings::accessibility, &app::AccessibilitySettings::cueJobComplete),
                                    cueHidden)));
    a11y.push_back(add(hiddenUnless(bindBool("cueAlarm", tr("Alarm sound"),
                                             tr("Play sound when the machine enters an alarm state."),
                                             &app::AppSettings::accessibility, &app::AccessibilitySettings::cueAlarm),
                                    cueHidden)));
    a11y.push_back(add(hiddenUnless(bindBool("cueToolChange", tr("Tool change sound"),
                                             tr("Play sound when a tool change is required."),
                                             &app::AppSettings::accessibility, &app::AccessibilitySettings::cueToolChange),
                                    cueHidden)));
    a11y.push_back(add(hiddenUnless(bindBool("cueProbeSuccess", tr("Probe success sound"),
                                             tr("Play sound after a successful probe."),
                                             &app::AppSettings::accessibility, &app::AccessibilitySettings::cueProbeSuccess),
                                    cueHidden)));
    a11y.push_back(sub(tr("Navigation & Visuals")));
    a11y.push_back(add(bindBool("focusRings", tr("Focus rings"),
                                tr("Show a high-contrast ring around the currently focused element for better "
                                   "keyboard navigation visibility."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::focusRings)));
    a11y.push_back(add(bindBool("focusTrapping", tr("Focus trapping"),
                                tr("Keep keyboard focus within modals and dialogs when they are open."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::focusTrapping)));
    a11y.push_back(add(bindBool("reducedMotion", tr("Reduced motion"),
                                tr("Minimize animations and UI transitions for improved visibility and accessibility."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::reducedMotion)));
    a11y.push_back(add(bindString("spindleInput", tr("Spindle speed input type"),
                                  tr("Choose between a slider or a number input for adjusting spindle speed."),
                                  {"Slider", "Number"},
                                  &app::AppSettings::spindle, &app::SpindleSettings::inputType)));
    a11y.push_back(add(bindString("displayScale", tr("App display scale"),
                                  tr("Override the app's display scale independently of your OS' DPI settings. Takes "
                                     "effect the next time gSender starts."),
                                  {"50%", "67%", "75%", "100%", "125%", "150%", "175%", "200%"},
                                  &app::AppSettings::accessibility, &app::AccessibilitySettings::displayScale)));
    a11y.push_back(sub(tr("Visualizer")));
    a11y.push_back(add(bindBool("visualizerKeyboardControl", tr("Keyboard control"),
                                tr("Allow orbiting, panning, and zooming of the 3D visualizer using arrow keys and "
                                   "hotkeys."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::visualizerKeyboardControl)));
    a11y.push_back(add(bindBool("gcodeSummary", tr("Job summary"),
                                tr("Provide a text summary of the loaded g-code file for screen readers."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::gcodeSummary)));
    a11y.push_back(add(hiddenUnless(bindBool("gcodeSummaryVisible", tr("Show summary visually"),
                                             tr("Display the g-code summary text visually above the visualizer."),
                                             &app::AppSettings::accessibility, &app::AccessibilitySettings::gcodeSummaryVisible),
                                    [](const Staged& st) { return !st.s.accessibility.gcodeSummary; })));
    a11y.push_back(sub(tr("Keyboard Map")));
    a11y.push_back(add(bindBool("showKeyboardMap", tr("Show keyboard shortcut map"),
                                tr("Show an overlay with active keyboard shortcuts."),
                                &app::AppSettings::accessibility, &app::AccessibilitySettings::showKeyboardMap)));
    menu_.emplace_back(tr("Accessibility"), std::move(a11y));

    // The board's settings no section places (filled per board in rows()).
    menu_.emplace_back(tr("Other Firmware Settings"), std::vector<Entry>{});
}

#undef GETTER
#undef SETTER

// ---- reading -----------------------------------------------------------------------------------

QStringList ConfigModel::sections() const {
    QStringList list;
    for (const auto& [name, entries] : menu_) {
        list << name;
    }
    return list;
}

const Pref* ConfigModel::pref(const QString& key) const {
    for (const Pref& p : prefs_) {
        if (p.key == key) {
            return &p;
        }
    }
    return nullptr;
}

std::optional<std::string> ConfigModel::boardSetting(const std::string& name) const {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return std::nullopt;
    }
    const std::string value = c->runner().setting(name);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

std::string ConfigModel::eepromValue(const std::string& name) const {
    if (const auto it = eepromEdits_.find(name); it != eepromEdits_.end()) {
        return it->second;
    }
    return boardSetting(name).value_or(std::string());
}

QVariantMap ConfigModel::eepromRow(const std::string& name, const QString& section, const QString& label) const {
    controller::Controller* c = machine_.controller();
    const protocol::FirmwareSettings& settings = c->settings();
    const protocol::FirmwareTables& tables = protocol::FirmwareTables::get(c->firmware());
    QString unit;
    QString description;
    QString details;
    int dataType = -1;
    int kind = -1;
    QStringList labels;
    const auto number = settingNumber(name);
    const auto own = number ? settings.descriptions.find(*number) : settings.descriptions.end();
    // grblHAL describes its own settings ($ES/$ESH); the static tables fill the gaps.
    if (own != settings.descriptions.end()) {
        unit = qstr(own->second.unit);
        description = qstr(own->second.description);
        dataType = own->second.dataType;
        kind = dataType;
        for (const std::string& entry : own->second.format) {
            labels << qstr(entry);
        }
    } else if (const protocol::SettingInfo* info = tables.setting(name)) {
        unit = qstr(info->units);
        description = qstr(info->message);
        if (!info->description.empty() && info->description != info->message) {
            details = qstr(info->description);
        }
        const std::string& type = info->inputType;
        kind = type == "switch" || type == "mask-status-report" ? 0
               : type == "axis-mask"                             ? 4
               : type == "select"                                ? 3
               : type == "mask"                                  ? 1
                                                                  : -1;
        if (const boost::json::value* values = info->raw.if_contains("values"); values && values->is_object()) {
            for (const auto& [key, text] : values->as_object()) {
                labels << qstr(text.is_string() ? std::string(text.as_string()) : std::string(key));
            }
        }
    }
    if (kind == 4) {
        const std::string letters = c->state().axes.letters.empty() ? "XYZ" : c->state().axes.letters;
        labels.clear();
        for (const char letter : letters) {
            labels << QString(QChar(letter));
        }
    }
    const QString editor = kind == 0                                     ? "switch"
                           : kind == 1 && !labels.isEmpty()              ? "bits"
                           : kind == 2 && !labels.isEmpty()              ? "exclusiveBits"
                           : kind == 4 && !labels.isEmpty()              ? "bits"
                           : kind == 3 && !labels.isEmpty()              ? "select"
                                                                         : "text";
    const std::string board = boardSetting(name).value_or(std::string());
    const std::string value = eepromValue(name);
    const std::optional<std::string> fallback =
        config::defaultValue(machine_.machineProfile(), machine_.boardContext(), name);
    const bool isDefault = config::isDefaultValue(value, fallback, dataType);
    return {
        {"kind", "eeprom"},
        {"key", qstr(name)},
        {"section", section},
        {"label", label.isEmpty() ? description : label},
        {"description", label.isEmpty() ? details : description},
        {"type", "eeprom"},
        {"editor", editor},
        {"bits", labels},
        {"unit", unit},
        {"value", qstr(value)},
        {"changed", value != board},
        {"modified", fallback.has_value() && !isDefault},
        {"defaultText", fallback ? qstr(*fallback) : QString()},
    };
}

bool ConfigModel::matches(const QVariantMap& row) const {
    if (onlyModified_ && !row.value("modified").toBool() && !row.value("changed").toBool()) {
        return false;
    }
    if (search_.isEmpty()) {
        return true;
    }
    for (const char* field : {"label", "description", "key"}) {
        if (row.value(field).toString().contains(search_, Qt::CaseInsensitive)) {
            return true;
        }
    }
    return false;
}

QVariantList ConfigModel::rows() const {
    if (rowsStale_) {
        rows_ = buildRows();
        rowsStale_ = false;
    }
    return rows_;
}

QVariantList ConfigModel::buildRows() const {
    controller::Controller* c = machine_.controller();
    const Staged saved = savedInStagedUnits();
    std::set<std::string> placed;
    QVariantList list;
    for (const auto& [section, entries] : menu_) {
        QVariantList body;
        QVariantMap subsection;
        const auto push = [&](QVariantMap row) {
            if (!matches(row)) {
                return;
            }
            if (!subsection.isEmpty()) {
                body.append(subsection);
                subsection.clear();
            }
            body.append(row);
        };
        std::vector<Entry> extra;
        const std::vector<Entry>* rows = &entries;
        if (entries.empty() && c) {
            // The board's settings no section placed.
            for (const auto& [name, value] : c->settings().settings.items()) {
                (void)value;
                if (!placed.contains(name)) {
                    Entry e;
                    e.kind = Entry::Eeprom;
                    e.eeprom = name;
                    extra.push_back(e);
                }
            }
            rows = &extra;
        }
        for (const Entry& e : *rows) {
            switch (e.kind) {
                case Entry::Section: break;
                case Entry::Subsection:
                    subsection = {{"kind", "subsection"}, {"label", e.text}, {"section", section}};
                    break;
                case Entry::Setting: {
                    const Pref& p = prefs_[static_cast<std::size_t>(e.pref)];
                    if (p.hidden && p.hidden(staged_)) {
                        break;
                    }
                    const QVariant value = p.get(*this, staged_);
                    const bool metricUnits = staged_.s.metric;
                    QString unit = p.unit;
                    if (p.type == "length") {
                        unit = metricUnits ? "mm" : "in";
                    } else if (p.type == "speed") {
                        unit = metricUnits ? "mm/min" : "in/min";
                    } else if (p.type == "jog") {
                        unit = metricUnits ? "mm" : "in";
                    }
                    // A setting's default compares in the staged units.
                    Staged defaults = defaults_;
                    defaults.s.metric = staged_.s.metric;
                    const QVariant fallback = p.get(*this, defaults);
                    push({
                        {"kind", "setting"},
                        {"key", p.key},
                        {"section", section},
                        {"label", p.label},
                        {"description", p.description},
                        {"type", p.type},
                        {"options", p.options},
                        {"unit", unit},
                        {"min", p.min},
                        {"max", p.max},
                        {"decimals", p.type == "length" || p.type == "speed" ? (metricUnits ? 2 : 3) : p.decimals},
                        {"value", value},
                        {"changed", differs(p, saved)},
                        {"modified", !p.noDefault && p.type != "path" && value != fallback},
                        {"defaultText", p.noDefault || p.type == "location" || p.type == "jog" || p.type == "ip"
                                            ? QString()
                                            : fallback.toString()},
                    });
                    break;
                }
                case Entry::Eeprom: {
                    if (!c) {
                        break;
                    }
                    std::string name = e.eeprom;
                    if (!boardSetting(name) && !e.remap.empty() && boardSetting(e.remap)) {
                        name = e.remap;
                    }
                    if (!boardSetting(name) || placed.contains(name)) {
                        break;
                    }
                    placed.insert(name);
                    push(eepromRow(name, section, e.label));
                    break;
                }
                case Entry::Action: {
                    const bool needsBoard = e.text != "keyboardShortcuts" && e.text != "squareXY";
                    if (needsBoard && !c) {
                        break;
                    }
                    if (!search_.isEmpty() || onlyModified_) {
                        break;  // the wizards show with their whole section
                    }
                    body.append(QVariantMap{{"kind", "action"}, {"key", e.text}, {"section", section}});
                    break;
                }
            }
        }
        if (!body.isEmpty()) {
            list.append(QVariantMap{{"kind", "section"}, {"label", section}, {"section", section}});
            list.append(body);
        }
    }
    return list;
}

int ConfigModel::sectionRow(const QString& section) const {
    const QVariantList all = rows();
    for (int i = 0; i < all.size(); ++i) {
        const QVariantMap row = all[i].toMap();
        if (row.value("kind") == "section" && row.value("label") == section) {
            return i;
        }
    }
    return -1;
}

void ConfigModel::setSearch(const QString& search) {
    search_ = search.trimmed();
    Q_EMIT changed();
}

void ConfigModel::setOnlyModified(bool only) {
    onlyModified_ = only;
    Q_EMIT changed();
}

int ConfigModel::pendingChanges() const {
    int count = 0;
    const Staged saved = savedInStagedUnits();
    for (const Pref& p : prefs_) {
        count += differs(p, saved) ? 1 : 0;
    }
    for (const auto& [name, value] : eepromEdits_) {
        count += boardSetting(name).value_or(std::string()) != value ? 1 : 0;
    }
    return count;
}

bool ConfigModel::connected() const {
    return machine_.controller() != nullptr;
}

bool ConfigModel::idle() const {
    controller::Controller* c = machine_.controller();
    return c && c->workflow().isIdle();
}

QString ConfigModel::pinState() const {
    controller::Controller* c = machine_.controller();
    return c ? qstr(c->state().status.pinState) : QString();
}

QString ConfigModel::units() const {
    return staged_.s.metric ? QStringLiteral("mm") : QStringLiteral("in");
}

QVariantList ConfigModel::profiles() const {
    QVariantList list;
    for (const config::MachineProfile& profile : config::machineProfiles()) {
        list.append(QVariantMap{{"id", profile.id}, {"name", qstr(config::machineProfileName(profile))}});
    }
    return list;
}

int ConfigModel::profileId() const {
    return machine_.machineProfile().id;
}

void ConfigModel::setProfileId(int id) {
    app::AppSettings settings = machine_.settings();
    if (settings.machineProfileId == id) {
        return;
    }
    settings.machineProfileId = id;
    machine_.setSettings(settings);
    Q_EMIT changed();
}

bool ConfigModel::canRestoreFirmwareDefaults() const {
    return idle() && config::canRestoreDefaults(machine_.machineProfile());
}

QString ConfigModel::profileName() const {
    const config::MachineProfile& profile = machine_.machineProfile();
    return qstr(profile.name + " " + profile.type).trimmed();
}

// ---- edits -------------------------------------------------------------------------------------

QVariant ConfigModel::valueOf(const QString& key) const {
    const Pref* p = pref(key);
    return p ? p->get(*this, staged_) : QVariant();
}

void ConfigModel::setValue(const QString& key, const QVariant& value) {
    if (const Pref* p = pref(key)) {
        p->set(*this, staged_, value);
        Q_EMIT changed();
    }
}

namespace {

// A field's text as a number (JavaScript's Number()), none when blank or not one.
std::optional<double> fieldNumber(const QString& text) {
    const std::string trimmed = text.trimmed().toStdString();
    const double value = js::stringToNumber(trimmed);
    return trimmed.empty() || std::isnan(value) ? std::nullopt : std::optional<double>(value);
}

}  // namespace

void ConfigModel::setNumber(const QString& key, const QString& text) {
    const Pref* p = pref(key);
    const std::optional<double> n = fieldNumber(text);
    if (p && n) {
        setValue(key, std::min(p->max, std::max(p->min, *n)));
    }
}

void ConfigModel::setPart(const QString& key, const QVariant& part, const QString& text) {
    const Pref* p = pref(key);
    const std::optional<double> n = fieldNumber(text);
    if (!p || !n) {
        return;
    }
    QVariant value = p->get(*this, staged_);
    if (p->type == "location" || p->type == "ip") {
        QVariantList list = value.toList();
        const int index = part.toInt();
        if (index < 0 || index >= list.size()) {
            return;
        }
        list[index] = p->type == "ip" ? std::clamp(js::mathRound(*n), 0.0, 255.0) : *n;
        value = list;
    } else if (p->type == "jog" && *n >= 0) {
        QVariantMap speeds = value.toMap();
        speeds[part.toString()] = *n;
        value = speeds;
    } else {
        return;
    }
    setValue(key, value);
}

void ConfigModel::setFolder(const QString& key, const QUrl& folder) {
    setValue(key, localPath(folder.toString()));
}

void ConfigModel::resetValue(const QString& key) {
    if (const Pref* p = pref(key); p && !p->noDefault) {
        Staged defaults = defaults_;
        defaults.s.metric = staged_.s.metric;
        p->set(*this, staged_, p->get(*this, defaults));
        Q_EMIT changed();
    }
}

void ConfigModel::setEeprom(const QString& setting, const QString& value) {
    const std::string name = setting.toStdString();
    const std::string text = value.trimmed().toStdString();
    if (boardSetting(name).value_or(std::string()) == text) {
        eepromEdits_.erase(name);
    } else {
        eepromEdits_[name] = text;
    }
    Q_EMIT changed();
}

void ConfigModel::toggleEepromBit(const QString& setting, int bit) {
    if (bit < 0 || bit > 30) {
        return;
    }
    const double current = js::stringToNumber(eepromValue(setting.toStdString()));
    const int value = std::isfinite(current) ? static_cast<int>(current) : 0;
    setEeprom(setting, QString::number(value ^ (1 << bit)));
}

void ConfigModel::resetEeprom(const QString& setting) {
    const std::optional<std::string> value =
        config::defaultValue(machine_.machineProfile(), machine_.boardContext(), setting.toStdString());
    if (value) {
        setEeprom(setting, qstr(*value));
    }
}

void ConfigModel::revert() {
    staged_ = saved_;
    eepromEdits_.clear();
    Q_EMIT changed();
}

void ConfigModel::apply() {
    // Event hooks: upstream's EventInput creates a hook with its first
    // commands (trigger "gcode"); a hook left without commands is disabled.
    config::EventStore hooks(machine_.config());
    for (const auto& [key, hook] : staged_.hooks) {
        if (const auto record = hooks.find(key)) {
            if (record->commands != hook.commands || record->enabled != hook.enabled) {
                config::EventChanges changes;
                changes.commands = hook.commands;
                changes.enabled = hook.enabled;
                hooks.update(key, changes);
            }
        } else if (!hook.commands.empty()) {
            hooks.create(key, "gcode", hook.commands, hook.enabled);
        }
    }
    controller::Controller* c = machine_.controller();
    // Turning the Rotary controls off leaves rotary mode (without a board to
    // tell, only the mode changes - as upstream).
    app::AppSettings s = staged_.s;
    const bool leaveRotary = saved_.s.rotary.showControls && !s.rotary.showControls && s.rotary.rotaryMode;
    if (leaveRotary && !c) {
        s.rotary.rotaryMode = false;
    }
    applying_ = true;
    machine_.setSettings(s);
    applying_ = false;
    reloadStaged();
    if (leaveRotary && c) {
        machine_.setRotaryMode(false);
    }
    std::vector<std::string> commands;
    for (const auto& [name, value] : eepromEdits_) {
        if (boardSetting(name).value_or(std::string()) != value) {
            commands.push_back(name + "=" + value);
        }
    }
    if (c && !commands.empty()) {
        commands.emplace_back("$$");  // read everything back
        c->gcode(commands);
    }
    Q_EMIT machine_.successNotice(tr("Settings Saved"));
    Q_EMIT changed();
}

// ---- actions -----------------------------------------------------------------------------------

void ConfigModel::useCurrentPosition(const QString& key) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const auto& mpos = c->runner().machinePosition();
    setValue(key, QVariantList{mpos[0], mpos[1], mpos[2]});
}

void ConfigModel::goToLocation(const QString& key) {
    const Pref* p = pref(key);
    if (!p || p->type != "location" || !machine_.canMove()) {
        return;
    }
    const QVariantList at = p->get(*this, staged_).toList();
    machine_.goToMachinePosition({at[0].toDouble(), at[1].toDouble(), at[2].toDouble()});
}

void ConfigModel::sendTest(const QString& command) {
    // The sections' wizards (SpindleWizard, LaserWizard, AccessoryOutputWizard).
    static const QStringList kAllowed{"M3 S1000", "M4 S1000", "M5 S0", "G1F1 M3 S1", "M3", "M4", "M5", "M7", "M8", "M9"};
    controller::Controller* c = machine_.controller();
    if (c && kAllowed.contains(command)) {
        c->gcode(command.toStdString());
    }
}

void ConfigModel::jogAxis(const QString& axis, double distance) {
    controller::Controller* c = machine_.controller();
    if (!c || !idle() || axis.size() != 1 || !QStringLiteral("XYZA").contains(axis)) {
        return;
    }
    c->gcode("$J=G21G91" + axis.toStdString() + js::numberToString(distance) + "F1000");
}

QString ConfigModel::settingsFileName() const {
    return QString("gSender-cpp-settings-%1.json").arg(QDate::currentDate().toString(Qt::ISODate));
}

QString ConfigModel::exportSettings(const QUrl& file) {
    QString error;
    if (!machine_.exportSettings(file.isLocalFile() ? file.toLocalFile() : file.toString(), &error)) {
        return error.isEmpty() ? tr("Could not write the file") : error;
    }
    return {};
}

QString ConfigModel::importSettings(const QUrl& file) {
    QString report;
    importOk_ = machine_.importSettings(file.isLocalFile() ? file.toLocalFile() : file.toString(), &report);
    revert();
    reloadStaged();
    Q_EMIT changed();
    return report;
}

void ConfigModel::restoreDefaultSettings() {
    machine_.restoreDefaultSettings();
    revert();
    reloadStaged();
    Q_EMIT changed();
}

bool ConfigModel::restoreFirmwareDefaults() {
    controller::Controller* c = machine_.controller();
    if (!c || !canRestoreFirmwareDefaults()) {
        return false;
    }
    c->gcode(config::restoreDefaultsCommands(machine_.machineProfile(), machine_.boardContext()));
    eepromEdits_.clear();
    Q_EMIT machine_.successNotice(tr("Restored default settings for your machine."));
    Q_EMIT changed();
    return true;
}

QString ConfigModel::eepromFileName() const {
    return QString("gSender-firmware-settings-%1.json")
        .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-HH-mm-ss"));
}

QString ConfigModel::importEeprom(const QUrl& url) {
    controller::Controller* c = machine_.controller();
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!c) {
        return tr("Not connected");
    }
    if (!file.open(QIODevice::ReadOnly)) {
        return file.errorString();
    }
    const QByteArray text = file.readAll();
    const std::optional<std::vector<std::string>> commands = config::importEepromCommands(
        std::string_view(text.constData(), static_cast<std::size_t>(text.size())), &machine_.machineProfile());
    if (!commands) {
        return tr("Failed to import settings. Please check the file format.");
    }
    c->gcode(*commands);
    Q_EMIT machine_.successNotice(tr("EEPROM Settings imported"));
    return {};
}

QString ConfigModel::exportEeprom(const QUrl& url) {
    controller::Controller* c = machine_.controller();
    QFile file(url.isLocalFile() ? url.toLocalFile() : url.toString());
    if (!c) {
        return tr("Not connected");
    }
    if (!file.open(QIODevice::WriteOnly)) {
        return file.errorString();
    }
    const std::string json = config::exportEeprom(c->settings().settings);
    file.write(json.data(), static_cast<qint64>(json.size()));
    return {};
}

void ConfigModel::reloadFirmware() {
    if (controller::Controller* c = machine_.controller(); c && idle()) {
        c->gcode("$$");
    }
}

}  // namespace gs::ui
