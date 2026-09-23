#pragma once

#include "settings_dialog.hpp"

#include <QMainWindow>

namespace gs::app {

class ConsolePanel;
class Jogger;
class Machine;
class ProbePanel;
class RotaryPanel;
class ShortcutManager;
class SpindlePanel;
class StatusArea;
class ToolpathView;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Machine& machine, QWidget* parent = nullptr);

    // Errors and alarms open dialogs unless suppressed (screenshots, tests).
    void setDialogsEnabled(bool enabled) noexcept { dialogsEnabled_ = enabled; }
    ToolpathView& toolpathView() noexcept { return *visualizer_; }
    ShortcutManager& shortcuts() noexcept { return *shortcuts_; }
    Jogger& jogger() noexcept { return *jogger_; }
    RotaryPanel& rotaryPanel() noexcept { return *rotary_; }
    // The Rotary tab shows while the Rotary controls are on.
    bool rotaryTabVisible() const;

private:
    void showError(const QString& title, const QString& detail);
    void createMenus();
    void openFile();
    // A recent file: loaded again, or forgotten when it has gone.
    void openRecent(const QString& path);
    void openSettings(SettingsDialog::Page page);
    void installShortcuts();

    Machine& machine_;
    ConsolePanel* console_;
    ToolpathView* visualizer_;
    StatusArea* statusArea_;
    Jogger* jogger_;
    ShortcutManager* shortcuts_;
    SpindlePanel* spindle_;
    ProbePanel* probe_;
    RotaryPanel* rotary_;
    class QTabWidget* tabs_;
    bool dialogsEnabled_ = true;
};

}  // namespace gs::app
