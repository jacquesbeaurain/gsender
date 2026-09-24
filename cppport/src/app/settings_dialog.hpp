#pragma once

// Preferences (general, tool change, spindle/laser, probe, rotary,
// automations) and the firmware's $ settings.

#include <QDialog>
#include <QString>

#include <vector>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace gs::app {

class Machine;

// The firmware's settings ($$) with their descriptions (the Config page's
// EEPROM side): each against the selected machine profile's default -
// changed ones highlighted, each restorable - with the machine's Defaults,
// EEPROM files imported and exported, a search and a changed-only filter.
// Edited values are written back as $n=value.
class FirmwareSettingsTable final : public QWidget {
    Q_OBJECT
public:
    explicit FirmwareSettingsTable(Machine& machine, QWidget* parent = nullptr);

    // What the controls do (the buttons ask first where upstream does).
    void setMachineProfile(int id);
    bool restoreDefaults();                        // the machine's defaults, all of them
    bool restoreSetting(const QString& setting);  // one setting back to its default
    bool importFile(const QString& path, QString* error = nullptr);
    bool exportFile(const QString& path, QString* error = nullptr) const;
    void setFilter(const QString& text);
    void setOnlyModified(bool only);
    int visibleRows() const;
    int modifiedCount() const;  // settings away from their default
    QTableWidget& table() noexcept { return *table_; }

private:
    void reload();
    void apply();
    void refreshButtons();
    void applyFilter();
    // The value's editor by upstream's data types (grblHAL's own, or the
    // static tables' input types): 0 switch, 1 bit field, 2 exclusive bit
    // field, 3 choice, 4 axis mask; others are typed into the cell.
    void addValueEditor(int row, int kind, const QStringList& labels);

    Machine& machine_;
    QComboBox* profile_;
    QPushButton* defaults_;
    QPushButton* import_;
    QPushButton* export_;
    QLineEdit* search_;
    QCheckBox* onlyModified_;
    QTableWidget* table_;
    QLabel* status_;
    QPushButton* reload_;
    QPushButton* apply_;
    bool loading_ = false;
};

// The Config page's live pin indicators (LimitSwitchIndicators,
// ProbePinStatus): each pin's letter lit while the status report lists it.
class PinIndicators final : public QWidget {
    Q_OBJECT
public:
    // `pins`: the letters shown, e.g. "XYZA" or "P".
    PinIndicators(Machine& machine, const QString& pins, QWidget* parent = nullptr);
    bool lit(QChar pin) const;

private:
    void refresh();

    Machine& machine_;
    QString pins_;
    std::vector<QLabel*> lights_;
};

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Page { General, ToolChange, SpindleLaser, Probe, Rotary, Automations, Accessibility, Firmware };  // tab order
    explicit SettingsDialog(Machine& machine, QWidget* parent = nullptr);
    void showPage(Page page);
    // The Automations page's hook for "gcode:start", "gcode:pause",
    // "gcode:resume" or "gcode:stop" (as typed into it).
    void setEventHook(const QString& event, const QString& commands, bool enabled);
    void save();  // OK / Apply

private:
    void load();

    Machine& machine_;
    QTabWidget* tabs_;
    QDoubleSpinBox* spindleDelay_;
    QCheckBox* lineWarnings_;
    QCheckBox* aAxis_;
    QComboBox* firmware_;
    QSpinBox* networkPort_;
    QComboBox* units_;
    QSpinBox* decimals_;
    QDoubleSpinBox* safeRetract_;
    QCheckBox* warnZero_;
    QSpinBox* toastDuration_;
    QCheckBox* jobEndModal_;
    QCheckBox* maintenanceNotifications_;
    QComboBox* liteOption_;
    QCheckBox* autoReconnect_;
    QCheckBox* revertWorkspace_;
    QCheckBox* powerSaving_;
    QCheckBox* promptExit_;
    QCheckBox* hideProcessedLines_;
    QCheckBox* warnBadFile_;
    QComboBox* visualizerTheme_;
    QCheckBox* darkMode_;
    QComboBox* projection_;
    QCheckBox* showBoundingBox_;
    QCheckBox* boundingBoxLabels_;
    QCheckBox* showMachineBed_;
    QCheckBox* trimGridToBed_;
    QCheckBox* followTool_;
    QComboBox* backupFrequency_;
    QLineEdit* backupLocation_;
    QDoubleSpinBox* park_[3];
    QComboBox* outlineMode_;
    QDoubleSpinBox* outlineSpeed_;
    QComboBox* toolChange_;
    QCheckBox* passthrough_;
    QCheckBox* skipDialog_;
    QPlainTextEdit* preHook_;
    QPlainTextEdit* postHook_;
    struct EventEditor {
        QString event;
        QCheckBox* enabled;
        QPlainTextEdit* commands;
    };
    std::vector<EventEditor> events_;
    QDoubleSpinBox* sensor_[3];
    QComboBox* firstTool_;
    QCheckBox* moveToManual_;
    QDoubleSpinBox* manual_[3];
    // Spindle/Laser
    QCheckBox* spindleFunctions_;
    QCheckBox* coolantFunctions_;
    QDoubleSpinBox* spindleMin_;
    QDoubleSpinBox* spindleMax_;
    QDoubleSpinBox* laserMin_;
    QDoubleSpinBox* laserMax_;
    QDoubleSpinBox* laserX_;
    QDoubleSpinBox* laserY_;
    QCheckBox* laserOutline_;
    // Probe
    QComboBox* plateType_;
    QDoubleSpinBox* standardBlock_;
    QDoubleSpinBox* xyThickness_;
    QDoubleSpinBox* autoZero_;
    QDoubleSpinBox* zProbe_;
    QDoubleSpinBox* probe3D_;
    QDoubleSpinBox* tipDiameter3D_;
    QDoubleSpinBox* xyRetract3D_;
    QDoubleSpinBox* bitZero_;
    QDoubleSpinBox* bitZeroZOnly_;
    QDoubleSpinBox* fastFeed_;
    QDoubleSpinBox* slowFeed_;
    QDoubleSpinBox* retraction_;
    QDoubleSpinBox* zRetractNormal_;
    QDoubleSpinBox* zRetractAuto_;
    QDoubleSpinBox* zProbeDistance_;
    QDoubleSpinBox* moveSpeed_;
    QCheckBox* connectivityTest_;
    // Rotary
    QCheckBox* rotaryControls_;
    QDoubleSpinBox* rotaryResolution_;
    QDoubleSpinBox* rotaryMaxSpeed_;
    QCheckBox* forceSoftLimits_;
    QCheckBox* forceHardLimits_;
    // Accessibility
    QCheckBox* statusAnnouncements_;
    QCheckBox* progressAnnouncements_;
    QSpinBox* progressIncrement_;
    QCheckBox* audioCues_;
    QCheckBox* cueJobComplete_;
    QCheckBox* cueAlarm_;
    QCheckBox* cueToolChange_;
    QCheckBox* cueProbeSuccess_;
    QCheckBox* focusRings_;
    QCheckBox* focusTrapping_;
    QCheckBox* reducedMotion_;
    QComboBox* spindleInput_;
    QComboBox* displayScale_;
    QCheckBox* visualizerKeys_;
    QCheckBox* jobSummary_;
    QCheckBox* jobSummaryVisible_;
    QCheckBox* keyboardMap_;
    // On grblHAL the resolution and speed are the board's A axis ($103,
    // $113), as loaded; on Grbl the values rotary mode writes to Y.
    bool rotaryFromBoard_ = false;
    double boardResolution_ = 0;
    double boardMaxSpeed_ = 0;
};

}  // namespace gs::app
