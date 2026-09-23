#pragma once

#include "settings_dialog.hpp"

#include <QMainWindow>

namespace gs::app {

class ConsolePanel;
class Jogger;
class Machine;
class ProbePanel;
class ShortcutManager;
class SpindlePanel;
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

private:
    void showError(const QString& title, const QString& detail);
    void createMenus();
    void openFile();
    void openSettings(SettingsDialog::Page page);
    void installShortcuts();

    Machine& machine_;
    ConsolePanel* console_;
    ToolpathView* visualizer_;
    Jogger* jogger_;
    ShortcutManager* shortcuts_;
    SpindlePanel* spindle_;
    ProbePanel* probe_;
    class QTabWidget* tabs_;
    bool dialogsEnabled_ = true;
};

}  // namespace gs::app
