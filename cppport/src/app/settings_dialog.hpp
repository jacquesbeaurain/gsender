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
    enum class Page { General, ToolChange, Firmware };
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
    QComboBox* toolChange_;
    QCheckBox* passthrough_;
    QCheckBox* skipDialog_;
    QPlainTextEdit* preHook_;
    QPlainTextEdit* postHook_;
};

}  // namespace gs::app
