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
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;

namespace gs::app {

class Machine;

// The firmware's settings ($$) with their descriptions; edited values are
// written back as $n=value.
class FirmwareSettingsTable final : public QWidget {
    Q_OBJECT
public:
    explicit FirmwareSettingsTable(Machine& machine, QWidget* parent = nullptr);

private:
    void reload();
    void apply();
    void refreshButtons();

    Machine& machine_;
    QTableWidget* table_;
    QLabel* status_;
    QPushButton* reload_;
    QPushButton* apply_;
    bool loading_ = false;
};

class SettingsDialog final : public QDialog {
    Q_OBJECT
public:
    enum class Page { General, ToolChange, SpindleLaser, Probe, Rotary, Automations, Firmware };  // tab order
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
    QCheckBox* showBoundingBox_;
    QCheckBox* boundingBoxLabels_;
    QCheckBox* showMachineBed_;
    QCheckBox* trimGridToBed_;
    QCheckBox* followTool_;
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
    // On grblHAL the resolution and speed are the board's A axis ($103,
    // $113), as loaded; on Grbl the values rotary mode writes to Y.
    bool rotaryFromBoard_ = false;
    double boardResolution_ = 0;
    double boardMaxSpeed_ = 0;
};

}  // namespace gs::app
