#pragma once

// Preferences (general, tool change) and the firmware's $ settings.

#include <QDialog>

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
    enum class Page { General, ToolChange, Probe, Firmware };
    explicit SettingsDialog(Machine& machine, QWidget* parent = nullptr);
    void showPage(Page page);

private:
    void load();
    void save();

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
    QComboBox* outlineMode_;
    QDoubleSpinBox* outlineSpeed_;
    QComboBox* toolChange_;
    QCheckBox* passthrough_;
    QCheckBox* skipDialog_;
    QPlainTextEdit* preHook_;
    QPlainTextEdit* postHook_;
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
