#pragma once

// The DRO (gSender's features/DRO and WorkspaceSelector): work and machine
// positions, zeroing or single-axis homing per axis, typed work positions,
// go to zero, the work coordinate system, Go To Location, the corner and
// park moves, homing, unlock and reset.

#include "gs/controller/locations.hpp"

#include <QDialog>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QToolButton;

namespace gs::app {

class Machine;

// Go To Location (DRO/component/GoTo.tsx): absolute or incremental work
// coordinates, or machine coordinates once homed, in the workspace units.
class GoToDialog final : public QDialog {
    Q_OBJECT
public:
    explicit GoToDialog(Machine& machine, QWidget* parent = nullptr);

    controller::GoToMode mode() const noexcept { return mode_; }
    // Switches the mode and fills the fields for it, as upstream: the work
    // position (ABS), zeros (INC), the machine position (MCS).
    void setMode(controller::GoToMode mode);
    void setTarget(double x, double y, double z, double a);
    double value(int axis) const;  // X Y Z A
    void go();                     // "Go!"

private:
    void fill();
    void updateEnabled();

    Machine& machine_;
    controller::GoToMode mode_ = controller::GoToMode::Absolute;
    QPushButton* modes_[3];
    QDoubleSpinBox* values_[4];
    QPushButton* go_;
};

class PositionPanel final : public QWidget {
    Q_OBJECT
public:
    explicit PositionPanel(Machine& machine, QWidget* parent = nullptr);

    // With single-axis homing the axis buttons home ("HX") instead of zeroing.
    void setHomingMode(bool on);
    bool homingMode() const noexcept { return homingMode_; }
    // A typed work position, as Enter in the axis' field does.
    void enterWorkPosition(int axis, const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void refresh();
    void axisClicked(int axis);
    void zeroAll();
    bool confirmZero(const QString& title, const QString& question);
    void openGoTo();

    Machine& machine_;
    bool homingMode_ = false;
    QPushButton* units_;
    QComboBox* workspace_;
    QPushButton* goTo_;
    QToolButton* corners_[4];
    QPushButton* park_;
    QPushButton* axisButton_[4];
    QLineEdit* work_[4];
    QLabel* machinePos_[4];
    QPushButton* goZero_[4];
    QWidget* rowA_[4];
    QPushButton* zeroAll_;
    QPushButton* home_;
    QCheckBox* singleAxis_;
    QPushButton* goXY_;
    QPushButton* unlock_;
    QPushButton* reset_;
    GoToDialog* goToDialog_ = nullptr;
};

}  // namespace gs::app
