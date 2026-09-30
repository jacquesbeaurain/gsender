#include "config_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "qt_text.hpp"

#include "gs/config/machine_profiles.hpp"
#include "gs/config/records.hpp"
#include "gs/controller/controller.hpp"
#include "gs/protocol/firmware_data.hpp"
#include "gs/util/jsnumber.hpp"

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
    for (const char* event : hookEvents()) {
        const auto record = hooks.find(event);
        saved_.hooks[event] = {record && record->enabled, record ? record->commands : std::string()};
    }
    staged_ = saved_;
    for (const auto& [p, value] : pending) {
        p->set(*this, staged_, value);
    }
}

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
