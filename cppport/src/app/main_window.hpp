#pragma once

#include <QMainWindow>

class QLabel;

namespace gs::app {

class ConsolePanel;
class Machine;

class MainWindow final : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(Machine& machine, QWidget* parent = nullptr);

    // Errors and alarms open dialogs unless suppressed (screenshots, tests).
    void setDialogsEnabled(bool enabled) noexcept { dialogsEnabled_ = enabled; }

private:
    void showError(const QString& title, const QString& detail);

    Machine& machine_;
    ConsolePanel* console_;
    QLabel* visualizer_;
    bool dialogsEnabled_ = true;
};

}  // namespace gs::app
