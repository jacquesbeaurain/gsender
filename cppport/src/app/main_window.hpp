#pragma once

#include "settings_dialog.hpp"

#include <QMainWindow>

namespace gs::app {

class ConsolePanel;
class Machine;
class ToolpathView;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Machine& machine, QWidget* parent = nullptr);

    // Errors and alarms open dialogs unless suppressed (screenshots, tests).
    void setDialogsEnabled(bool enabled) noexcept { dialogsEnabled_ = enabled; }
    ToolpathView& toolpathView() noexcept { return *visualizer_; }

private:
    void showError(const QString& title, const QString& detail);
    void createMenus();
    void openFile();
    void openSettings(SettingsDialog::Page page);

    Machine& machine_;
    ConsolePanel* console_;
    ToolpathView* visualizer_;
    bool dialogsEnabled_ = true;
};

}  // namespace gs::app
