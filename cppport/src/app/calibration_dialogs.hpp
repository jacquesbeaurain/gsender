#pragma once

// The calibration tools (gSender's Tools > Movement Tuning and XY
// Squaring): wizards that move the machine, take the operator's
// measurements and offer the corrected steps/mm. The arithmetic and G-code
// are in gs/calibration.

#include "gs/calibration/calibration.hpp"

#include <QDialog>
#include <QWidget>

#include <vector>

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace gs::app {

class Machine;

// Movement Tuning: mark where the axis is, move it a set distance, measure
// how far it went, and rescale its steps/mm ($100-$102).
class MovementTuningDialog final : public QDialog {
    Q_OBJECT
public:
    explicit MovementTuningDialog(Machine& machine, QWidget* parent = nullptr);

    enum Step { Intro, Mark, Move, Measure, Result };
    Step step() const noexcept { return step_; }
    // The wizard's controls, for tests and shortcuts.
    void setAxis(char axis);  // resets the distances to the axis' defaults
    char axis() const noexcept { return axis_; }
    void setMoveDistance(double distance);
    void setTravelled(double distance);
    bool start();         // "Start Movement Tuning" (connected, idle or jogging)
    void markLocation();  // "Mark First Location"
    bool moveAxis();      // "Move X-axis"
    void confirmTravelled();  // "Set Distance Travelled"
    double recommendedStepsPerMm() const;
    QString resultText() const;
    void updateFirmware();  // writes the recommendation, without asking
    void restart();

private:
    void refresh();

    Machine& machine_;
    Step step_ = Intro;
    char axis_ = 'X';
    QStackedWidget* pages_;
    QComboBox* axisCombo_;
    QLabel* connectNote_;
    QPushButton* start_;
    QLabel* instruction_;
    QLabel* marks_[3];
    QPushButton* mark_;
    QPushButton* move_;
    QDoubleSpinBox* moveDistance_;
    QPushButton* travelled_;
    QDoubleSpinBox* measured_;
    QLabel* result_;
    QPushButton* update_;
};

// XY Squaring's diagram: points 1 (bottom left), 2 (bottom right) and 3
// (top right), the moves and the measured sides.
class TriangleDiagram final : public QWidget {
    Q_OBJECT
public:
    explicit TriangleDiagram(QWidget* parent = nullptr);
    // What to show: the main step (1 marking, 2 measuring, 3 results), the
    // points marked so far, the highlighted point or side (-1: none), the
    // moving axis ('X', 'Y' or 0) and the measured sides.
    void setState(int mainStep, int markedPoints, int activePoint, int activeSide, char moving,
                  const calibration::Triangle& triangle, const QString& units);
    QSize sizeHint() const override { return {240, 240}; }

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    int mainStep_ = 0;
    int marked_ = 0;
    int activePoint_ = -1;
    int activeSide_ = -1;
    char moving_ = 0;
    calibration::Triangle triangle_;
    QString units_;
};

// XY Squaring: mark a right triangle with the machine (X then Y moves),
// measure its sides, and see how far X and Y are from square - with the
// steps/mm the measured sides suggest.
class SquaringDialog final : public QDialog {
    Q_OBJECT
public:
    explicit SquaringDialog(Machine& machine, QWidget* parent = nullptr);

    int mainStep() const noexcept { return mainStep_; }  // 0 setup, 1 mark, 2 measure, 3 results
    int subStep() const noexcept { return subStep_; }
    bool canGoNext() const;
    void next();
    void back();
    void restart();
    // The current main step's row `index`, as its button: marks, moves (the
    // X and Y rows) and measurements (their values must be positive).
    bool completeRow(int index);
    void setRowValue(int index, double value);  // a move's distance or a measurement
    const calibration::Triangle& triangle() const noexcept { return triangle_; }
    calibration::SquaringResult result() const;
    calibration::StepsAdjustment adjustment() const;
    QString resultText() const;
    void updateFirmware();  // writes the recommended steps/mm, without asking

private:
    struct Row {
        QString button;
        QString description;
        bool hasValue = false;
        double value = 0;
        bool completed = false;
    };
    void buildSteps();
    void rebuildRows();
    void refresh();

    Machine& machine_;
    int mainStep_ = 0;
    int subStep_ = 0;
    std::vector<std::vector<Row>> rows_;  // per main step
    calibration::Triangle triangle_;
    calibration::SquaringMoves moves_;
    QLabel* title_;
    QLabel* description_;
    QLabel* instruction_;
    QWidget* rowsBox_;
    QVBoxLayout* rowsLayout_;
    std::vector<QLabel*> rowMarks_;
    std::vector<QPushButton*> rowButtons_;
    std::vector<QDoubleSpinBox*> rowValues_;  // null for rows without a value
    QLabel* result_;
    QPushButton* update_;
    TriangleDiagram* diagram_;
    QPushButton* back_;
    QPushButton* next_;
};

}  // namespace gs::app
