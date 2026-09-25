#pragma once

// Config (features/Config) for QML: upstream's sections - Basics, Motors,
// Probe, Homing/Limits, Spindle/Laser, Accessory Outputs, Rotary,
// Automations, Tool Changing, Ethernet, the board's other settings and
// Accessibility - each mixing gSender's preferences with the board's own
// ($) settings, as upstream's SettingsMenu does. Edits are staged, marked
// as changed and written by Apply Settings (the preferences saved, the
// board's settings sent as $n=value); settings away from their default can
// be reset. The machine profile, the board's defaults, EEPROM files and the
// application settings' files are here too. The board's settings no
// section places are listed at the end, so none is out of reach.

#include "app_settings.hpp"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariant>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gs::app {
class Machine;
}

namespace gs::ui {

class ConfigModel : public QObject {
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QStringList sections READ sections CONSTANT)
    // The rows shown (search and filter applied), each {kind: section,
    // subsection, setting, eeprom or action; key; section; label;
    // description; type; options; unit; value; changed; modified;
    // defaultText; ...}. Setting types: bool, number, length, speed, select,
    // text, path, textarea, location, ip, jog, event. EEPROM rows add
    // editor (switch, bits, exclusiveBits, axes, select, text) and bits.
    Q_PROPERTY(QVariantList rows READ rows NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(bool onlyModified READ onlyModified WRITE setOnlyModified NOTIFY changed)
    Q_PROPERTY(int pendingChanges READ pendingChanges NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY liveChanged)
    Q_PROPERTY(bool idle READ idle NOTIFY liveChanged)
    Q_PROPERTY(QString pinState READ pinState NOTIFY liveChanged)  // the status report's Pn
    Q_PROPERTY(QString units READ units NOTIFY changed)       // the staged units
    // The machine profiles ({id, name}) and the one chosen.
    Q_PROPERTY(QVariantList profiles READ profiles CONSTANT)
    Q_PROPERTY(int profileId READ profileId WRITE setProfileId NOTIFY changed)
    Q_PROPERTY(bool canRestoreFirmwareDefaults READ canRestoreFirmwareDefaults NOTIFY liveChanged)
    Q_PROPERTY(QString profileName READ profileName NOTIFY changed)

public:
    explicit ConfigModel(QObject* parent = nullptr);
    ~ConfigModel() override;

    QStringList sections() const;
    QVariantList rows() const;  // cached until the next change
    QString search() const { return search_; }
    void setSearch(const QString& search);
    bool onlyModified() const { return onlyModified_; }
    void setOnlyModified(bool only);
    int pendingChanges() const;
    bool connected() const;
    bool idle() const;
    QString pinState() const;
    QString units() const;
    QVariantList profiles() const;
    int profileId() const;
    void setProfileId(int id);
    bool canRestoreFirmwareDefaults() const;
    QString profileName() const;

    // A row's section (where the menu scrolls to): the index of its header
    // in `rows`, or -1 when filtered out.
    Q_INVOKABLE int sectionRow(const QString& section) const;

    // ---- edits (staged) ----
    Q_INVOKABLE void setValue(const QString& key, const QVariant& value);
    Q_INVOKABLE void resetValue(const QString& key);   // to its default
    Q_INVOKABLE void setEeprom(const QString& setting, const QString& value);
    Q_INVOKABLE void resetEeprom(const QString& setting);  // to the profile's default
    Q_INVOKABLE void apply();   // Apply Settings
    Q_INVOKABLE void revert();  // the staged edits dropped

    // ---- actions ----
    Q_INVOKABLE void useCurrentPosition(const QString& key);  // a location row: the machine's position
    Q_INVOKABLE void goToLocation(const QString& key);       // Park: move there now
    // The sections' test buttons (the known commands only).
    Q_INVOKABLE void sendTest(const QString& command);
    Q_INVOKABLE void jogAxis(const QString& axis, double distance);  // JogWizard / AJogWizard: 1000 mm/min
    Q_INVOKABLE QString exportSettings(const QUrl& file);   // "" or the error
    Q_INVOKABLE QString importSettings(const QUrl& file);   // the report; `ok` says how it went
    Q_INVOKABLE bool lastImportOk() const { return importOk_; }
    Q_INVOKABLE void restoreDefaultSettings();
    Q_INVOKABLE QString settingsFileName() const;           // gSender-cpp-settings-<date>.json
    Q_INVOKABLE bool restoreFirmwareDefaults();
    Q_INVOKABLE QString importEeprom(const QUrl& file);     // "" or the error
    Q_INVOKABLE QString exportEeprom(const QUrl& file);     // "" or the error
    Q_INVOKABLE QString eepromFileName() const;
    Q_INVOKABLE void reloadFirmware();                      // $$

Q_SIGNALS:
    void changed();
    void liveChanged();  // the machine's state: connected, idle, the pins

public:
    // The staged state: the settings, the event hooks, the rotary's board
    // values (grblHAL's $103/$113 are EEPROM rows instead).
    struct Staged {
        app::AppSettings s;
        struct Hook {
            bool enabled = false;
            std::string commands;
            bool operator==(const Hook&) const = default;
        };
        std::map<std::string, Hook> hooks;
    };
    struct Pref {
        QString key;
        QString label;
        QString description;
        QString type;
        QStringList options;
        QString unit;
        double min = 0;
        double max = 0;
        int decimals = 3;
        std::function<QVariant(const ConfigModel&, const Staged&)> get;
        std::function<void(ConfigModel&, Staged&, const QVariant&)> set;
        std::function<bool(const Staged&)> hidden;
        bool noDefault = false;  // no reset (a path, the hooks)
    };
    struct Entry {
        enum Kind { Section, Subsection, Setting, Eeprom, Action } kind = Setting;
        QString text;          // a header's label, an action's key
        std::string eeprom;    // an EEPROM row's setting
        std::string remap;     // its number on boards that moved it
        int pref = -1;         // a setting row's Pref
        QString label;         // an EEPROM row's own label (the rotary's hybrid rows)
    };

private:
    void buildMenu();
    void reloadStaged();
    const Pref* pref(const QString& key) const;
    QVariantMap eepromRow(const std::string& name, const QString& section, const QString& label) const;
    std::string eepromValue(const std::string& name) const;  // staged, else the board's
    std::optional<std::string> boardSetting(const std::string& name) const;
    bool matches(const QVariantMap& row) const;
    QVariantList buildRows() const;
    bool differs(const Pref& p, const Staged& savedInStagedUnits) const;
    Staged savedInStagedUnits() const;

    app::Machine& machine_;
    std::vector<Pref> prefs_;
    std::vector<std::pair<QString, std::vector<Entry>>> menu_;
    Staged saved_;
    Staged staged_;
    Staged defaults_;
    std::map<std::string, std::string> eepromEdits_;
    QString search_;
    bool onlyModified_ = false;
    bool importOk_ = false;
    bool applying_ = false;
    mutable QVariantList rows_;
    mutable bool rowsStale_ = true;
};

}  // namespace gs::ui
