#pragma once

// Preferences (general, tool change, probe, automations) and the firmware's $
// settings.

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
    enum class Page { General, ToolChange, Probe, Automations, Firmware };  // tab order
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
};

}  // namespace gs::app
