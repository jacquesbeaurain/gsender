#pragma once

// gSender's Probe widget: choose the routine (Z, XYZ, XY, X, Y), the tool
// and the plate's corner, then run it after the probe circuit check.

#include "gs/probe/probing.hpp"

#include <QDialog>
#include <QWidget>

#include <vector>

class QButtonGroup;
class QComboBox;
class QHBoxLayout;
class QLabel;
class QPushButton;

namespace gs::app {

class Machine;

// A plan view of the stock corner with the plate on it; a click moves the
// plate to the next corner, clockwise.
class CornerView final : public QWidget {
    Q_OBJECT
public:
    explicit CornerView(QWidget* parent = nullptr);
    int corner() const noexcept { return corner_; }
    void setCorner(int corner);
    void setPlateType(probe::PlateType type);
    QSize sizeHint() const override { return {120, 120}; }

Q_SIGNALS:
    void cornerChanged(int corner);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;

private:
    int corner_ = probe::kBottomLeft;
    probe::PlateType plate_ = probe::PlateType::StandardBlock;
};

class ProbePanel final : public QWidget {
    Q_OBJECT
public:
    explicit ProbePanel(Machine& machine, QWidget* parent = nullptr);

    // The current choice.
    const probe::ProbeCommand& command() const { return available_.at(selected_); }
    probe::ProbeType probeType() const;
    double toolDiameter() const;  // mm; 0 for Auto and Tip
    int corner() const;
    void selectCommand(int index);

    // "Probe": the run dialog (non-modal, deleted on close).
    class RunProbeDialog* openRunDialog();

private:
    void settingsChanged();
    void rebuildCommands();
    void rebuildTools();
    void refresh();
    void storePlateAndCorner();

    Machine& machine_;
    QComboBox* plate_;
    QHBoxLayout* commandRow_;
    QButtonGroup* commandButtons_;
    QLabel* toolLabel_;
    QComboBox* tool_;
    CornerView* cornerView_;
    QLabel* cornerLabel_;
    QPushButton* probe_;
    std::vector<probe::ProbeCommand> available_;
    int selected_ = 0;
};

// The run step: check the circuit - the probe pin must trigger once while
// the dialog is open, unless the check is off or confirmed by hand - then
// start the routine.
class RunProbeDialog final : public QDialog {
    Q_OBJECT
public:
    RunProbeDialog(Machine& machine, probe::ProbeCommand command, probe::ProbeType type, double toolDiameter,
                   int corner, QWidget* parent = nullptr);

    bool circuitConfirmed() const noexcept { return confirmed_; }
    void confirmCircuit();
    // Runs the routine and closes; false (and stays open) if the machine
    // cannot take it.
    bool start();

private:
    void refresh();

    Machine& machine_;
    probe::ProbeCommand command_;
    probe::ProbeType type_;
    double toolDiameter_;
    int corner_;
    bool confirmed_ = false;
    QLabel* light_;
    QLabel* circuit_;
    QPushButton* confirm_;
    QPushButton* start_;
};

}  // namespace gs::app
