#pragma once

// Start From Line (gSender's StartFromLine.tsx): resume a job after a stop,
// power loss or lost connection from a chosen line, rising to a safe height
// above the file's highest Z first.

#include <QDialog>

class QDoubleSpinBox;
class QSpinBox;

namespace gs::app {

class Machine;

class StartFromLineDialog final : public QDialog {
    Q_OBJECT
public:
    explicit StartFromLineDialog(Machine& machine, QWidget* parent = nullptr);

    int line() const;
    void setLine(int line);
    double safeHeight() const;  // mm
    // Starts the job; false (and stays open) when the machine cannot.
    bool start();

private:
    Machine& machine_;
    QSpinBox* line_;
    QDoubleSpinBox* safeHeight_;
};

}  // namespace gs::app
