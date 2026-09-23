#include "calibration_dialogs.hpp"

#include "machine.hpp"

#include "gs/util/jsnumber.hpp"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cctype>
#include <cmath>

namespace gs::app {
namespace {

QString unitName(const Machine& machine) {
    return machine.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

QString number(double value) {
    return QString::fromStdString(js::numberToString(value));
}

// A step row's state: pending, current or done.
enum class RowState { Pending, Current, Done };

void showState(QLabel* label, RowState state) {
    const QChar glyph = state == RowState::Done ? QChar(0x2714) : state == RowState::Current ? QChar(0x25B6)
                                                                                            : QChar(0x2022);
    const char* color = state == RowState::Done ? "#16a34a" : state == RowState::Current ? "#3b82f6" : "#9ca3af";
    label->setText(QString(glyph));
    label->setStyleSheet(QString("QLabel { color: %1; font-size: 16px; }").arg(color));
}

void setUnits(QDoubleSpinBox* box, bool metric) {
    box->setDecimals(metric ? 3 : 4);
    box->setSuffix(metric ? QStringLiteral(" mm") : QStringLiteral(" in"));
}

QDoubleSpinBox* distanceBox() {
    auto* box = new QDoubleSpinBox;
    box->setRange(-10000, 10000);
    box->setAlignment(Qt::AlignRight);
    box->setMinimumWidth(110);
    return box;
}

// Asks before writing steps/mm (upstream's "Update Firmware" dialog).
bool confirmUpdate(QWidget* parent, const QString& text) {
    QMessageBox box(QMessageBox::Question, QObject::tr("Update Firmware"), text, QMessageBox::NoButton, parent);
    QPushButton* update = box.addButton(QObject::tr("Update Firmware"), QMessageBox::AcceptRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    return box.clickedButton() == update;
}

}  // namespace

// ---- Movement Tuning ---------------------------------------------------------------------

MovementTuningDialog::MovementTuningDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("Movement Tuning"));
    resize(620, 420);
    auto* layout = new QVBoxLayout(this);
    pages_ = new QStackedWidget;
    layout->addWidget(pages_, 1);

    // The introduction and the axis.
    auto* intro = new QWidget;
    auto* introLayout = new QVBoxLayout(intro);
    auto* about = new QLabel(
        tr("If you're looking to use your CNC for more accurate work and notice a specific axis is always off by a "
           "small amount - say 102mm instead of 100 - then use this tool.") +
        "\n\n" +
        tr("Since CNC firmware needs to understand its hardware to make exact movements, small manufacturing "
           "variations in the motors, lead screws, pulleys, or incorrect firmware will create inaccuracies over "
           "longer distances.") +
        "\n\n" +
        tr("By testing for this difference using a marker or tape and a measuring tape, this tool will better tune "
           "the firmware to your machine."));
    about->setWordWrap(true);
    introLayout->addWidget(about);
    auto* axisRow = new QHBoxLayout;
    axisRow->addWidget(new QLabel("<b>" + tr("Axis to Tune") + "</b>"));
    axisCombo_ = new QComboBox;
    for (const char axis : {'X', 'Y', 'Z'}) {
        axisCombo_->addItem(tr("%1-Axis").arg(axis), static_cast<int>(axis));
    }
    axisRow->addWidget(axisCombo_, 1);
    introLayout->addLayout(axisRow);
    auto* place = new QLabel("<b>" +
                             tr("Whichever axis you'll be tuning, please place it in an initial location so that "
                                "it'll have space to move to the right (for X), backwards (for Y), and downwards "
                                "(for Z).") +
                             "</b>");
    place->setWordWrap(true);
    introLayout->addWidget(place);
    connectNote_ = new QLabel(tr("Please connect to a device before starting the movement tuning wizard."));
    connectNote_->setStyleSheet("QLabel { color: #854d0e; background: #fef9c3; border: 1px solid #fde68a; "
                                "border-radius: 6px; padding: 8px; }");
    introLayout->addWidget(connectNote_);
    introLayout->addStretch(1);
    start_ = new QPushButton(tr("Start Movement Tuning"));
    introLayout->addWidget(start_, 0, Qt::AlignLeft);
    pages_->addWidget(intro);

    // Mark, move, measure.
    auto* steps = new QWidget;
    auto* stepsLayout = new QVBoxLayout(steps);
    stepsLayout->addWidget(new QLabel("<b>" + tr("Instructions") + "</b>"));
    instruction_ = new QLabel;
    instruction_->setWordWrap(true);
    instruction_->setMinimumHeight(64);
    instruction_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    stepsLayout->addWidget(instruction_);
    auto* grid = new QGridLayout;
    for (int i = 0; i < 3; ++i) {
        marks_[i] = new QLabel;
        marks_[i]->setFixedWidth(24);
        grid->addWidget(marks_[i], i, 0);
    }
    mark_ = new QPushButton(tr("Mark First Location"));
    move_ = new QPushButton;
    moveDistance_ = distanceBox();
    travelled_ = new QPushButton(tr("Set Distance Travelled"));
    measured_ = distanceBox();
    grid->addWidget(mark_, 0, 1);
    grid->addWidget(move_, 1, 1);
    grid->addWidget(moveDistance_, 1, 2);
    grid->addWidget(travelled_, 2, 1);
    grid->addWidget(measured_, 2, 2);
    grid->setColumnStretch(3, 1);
    stepsLayout->addLayout(grid);
    stepsLayout->addStretch(1);
    pages_->addWidget(steps);

    // The verdict.
    auto* resultPage = new QWidget;
    auto* resultLayout = new QVBoxLayout(resultPage);
    result_ = new QLabel;
    result_->setWordWrap(true);
    result_->setAlignment(Qt::AlignCenter);
    result_->setMinimumHeight(150);
    update_ = new QPushButton(tr("Update step/mm"));
    resultLayout->addWidget(result_);
    resultLayout->addWidget(update_, 0, Qt::AlignCenter);
    resultLayout->addStretch(1);
    pages_->addWidget(resultPage);

    auto* restartButton = new QPushButton(tr("Restart Wizard"));
    layout->addWidget(restartButton, 0, Qt::AlignLeft);

    connect(axisCombo_, &QComboBox::activated, this,
            [this](int index) { setAxis(static_cast<char>(axisCombo_->itemData(index).toInt())); });
    connect(start_, &QPushButton::clicked, this, &MovementTuningDialog::start);
    connect(mark_, &QPushButton::clicked, this, &MovementTuningDialog::markLocation);
    connect(move_, &QPushButton::clicked, this, &MovementTuningDialog::moveAxis);
    connect(travelled_, &QPushButton::clicked, this, &MovementTuningDialog::confirmTravelled);
    connect(restartButton, &QPushButton::clicked, this, &MovementTuningDialog::restart);
    connect(update_, &QPushButton::clicked, this, [this] {
        const double current = machine_.settingNumber(calibration::stepsSetting(axis_));
        if (confirmUpdate(this, tr("This will update the %1-axis step/mm value in your CNC firmware (%2).\n\n"
                                   "From: %3\nTo: %4")
                                    .arg(axis_)
                                    .arg(QString::fromStdString(calibration::stepsSetting(axis_)))
                                    .arg(number(current))
                                    .arg(number(recommendedStepsPerMm())))) {
            updateFirmware();
        }
    });
    connect(&machine_, &Machine::stateChanged, this, &MovementTuningDialog::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &MovementTuningDialog::refresh);
    connect(&machine_, &Machine::appSettingsChanged, this, &MovementTuningDialog::refresh);
    setAxis('X');
}

void MovementTuningDialog::setAxis(char axis) {
    axis_ = static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
    axisCombo_->setCurrentIndex(axis_ == 'Z' ? 2 : axis_ == 'Y' ? 1 : 0);
    const bool metric = machine_.settings().metric;
    setUnits(moveDistance_, metric);
    setUnits(measured_, metric);
    const double distance = calibration::defaultTuningDistance(axis_, metric);
    moveDistance_->setValue(distance);
    measured_->setValue(distance);
    refresh();
}

void MovementTuningDialog::setMoveDistance(double distance) {
    moveDistance_->setValue(distance);
}

void MovementTuningDialog::setTravelled(double distance) {
    measured_->setValue(distance);
}

bool MovementTuningDialog::start() {
    if (step_ != Intro || !machine_.canMove()) {
        return false;
    }
    step_ = Mark;
    refresh();
    return true;
}

void MovementTuningDialog::markLocation() {
    if (step_ == Mark) {
        step_ = Move;
        refresh();
    }
}

bool MovementTuningDialog::moveAxis() {
    // Deviation: a move refused (not idle, or towards a triggered limit)
    // does not count as made; upstream went on regardless.
    if (step_ != Move || !machine_.canMove() || !machine_.runTuningMove(axis_, moveDistance_->value())) {
        return false;
    }
    step_ = Measure;
    refresh();
    return true;
}

void MovementTuningDialog::confirmTravelled() {
    if (step_ == Measure) {
        step_ = Result;
        refresh();
    }
}

double MovementTuningDialog::recommendedStepsPerMm() const {
    return calibration::newStepsPerMm(machine_.settingNumber(calibration::stepsSetting(axis_)),
                                      moveDistance_->value(), measured_->value());
}

QString MovementTuningDialog::resultText() const {
    const double moved = moveDistance_->value();
    const double measured = measured_->value();
    if (moved == measured) {
        return tr("Your %1-axis looks accurate, so you should be good to go!").arg(axis_);
    }
    return tr("Your %1-axis movement was off by <b>%2 %3.</b> Consider updating your %1-axis step/mm value in your "
              "CNC firmware.")
        .arg(axis_)
        .arg(number(calibration::tuningError(moved, measured)))
        .arg(unitName(machine_));
}

void MovementTuningDialog::updateFirmware() {
    machine_.writeFirmwareSettings(calibration::tuningUpdateCommands(axis_, recommendedStepsPerMm()));
    Q_EMIT machine_.notice(tr("Updated steps-per-mm value"));
}

void MovementTuningDialog::restart() {
    step_ = Intro;
    setAxis('X');
}

void MovementTuningDialog::refresh() {
    const bool metric = machine_.settings().metric;
    setUnits(moveDistance_, metric);
    setUnits(measured_, metric);
    connectNote_->setVisible(machine_.controller() == nullptr);
    start_->setEnabled(machine_.canMove());
    pages_->setCurrentIndex(step_ == Intro ? 0 : step_ == Result ? 2 : 1);
    static const char* const kInstructions[] = {
        QT_TR_NOOP("First, mark next to the gantry in the location shown with your marker, pencil, or using a strip "
                   "of tape."),
        QT_TR_NOOP("Now move any distance you wish. A larger value will better tune your movement, just make sure "
                   "you don't hit your machine limits. Once you are ready, click the Move Axis button."),
        QT_TR_NOOP("Lastly, measure the distance travelled between the original mark and the current gantry "
                   "location. Take your time when entering this value, a more accurate measurement will give you "
                   "better tuning results."),
    };
    if (step_ >= Mark && step_ <= Measure) {
        instruction_->setText(tr(kInstructions[step_ - Mark]));
    }
    for (int i = 0; i < 3; ++i) {
        const int row = Mark + i;
        showState(marks_[i], step_ > row ? RowState::Done : step_ == row ? RowState::Current : RowState::Pending);
    }
    mark_->setEnabled(step_ == Mark);
    move_->setText(tr("Move %1-axis").arg(axis_));
    move_->setEnabled(step_ == Move && machine_.canMove());
    moveDistance_->setEnabled(step_ == Move);
    travelled_->setEnabled(step_ == Measure);
    measured_->setEnabled(step_ == Measure);
    if (step_ == Result) {
        const bool accurate = moveDistance_->value() == measured_->value();
        result_->setText(resultText());
        result_->setStyleSheet(accurate ? "QLabel { color: #166534; background: #dcfce7; border-radius: 8px; "
                                          "padding: 16px; font-size: 15px; }"
                                        : "QLabel { color: #854d0e; background: #fef9c3; border-radius: 8px; "
                                          "padding: 16px; font-size: 15px; }");
        update_->setVisible(!accurate);
        update_->setEnabled(machine_.controller() != nullptr);
    }
}

// ---- the triangle diagram ------------------------------------------------------------------

TriangleDiagram::TriangleDiagram(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 220);
}

void TriangleDiagram::setState(int mainStep, int markedPoints, int activePoint, int activeSide, char moving,
                               const calibration::Triangle& triangle, const QString& units) {
    mainStep_ = mainStep;
    marked_ = markedPoints;
    activePoint_ = activePoint;
    activeSide_ = activeSide;
    moving_ = moving;
    triangle_ = triangle;
    units_ = units;
    update();
}

void TriangleDiagram::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double size = std::max(40.0, std::min(width(), height()) - 60.0);
    const QPointF origin((width() - size) / 2, (height() - size) / 2);
    // 1 bottom left, 2 bottom right (the square corner), 3 top right.
    const QPointF points[3] = {origin + QPointF(0, size), origin + QPointF(size, size), origin + QPointF(size, 0)};
    const int sides[3][2] = {{0, 1}, {1, 2}, {0, 2}};
    const double lengths[3] = {triangle_.a, triangle_.b, triangle_.c};
    const QColor active("#3b82f6");
    const QColor idle("#9ca3af");

    for (int i = 0; i < 3; ++i) {
        const auto [from, to] = std::pair{sides[i][0], sides[i][1]};
        if (to >= marked_) {
            continue;  // both ends must be marked
        }
        QPen pen(i == activeSide_ ? active : idle, i == activeSide_ ? 3 : 2);
        if (i == 2 && mainStep_ < 2) {
            pen.setStyle(Qt::DashLine);  // the diagonal is only measured
        }
        p.setPen(pen);
        p.drawLine(points[from], points[to]);
        if (mainStep_ >= 2 && lengths[i] > 0) {
            const QPointF middle = (points[from] + points[to]) / 2;
            // Below the bottom edge, inside the right edge, above the diagonal.
            const QPointF offset = i == 0 ? QPointF(0, 18) : i == 1 ? QPointF(-34, 0) : QPointF(-34, -8);
            p.setPen(palette().color(QPalette::WindowText));
            p.drawText(QRectF(middle + offset - QPointF(40, 10), QSizeF(80, 20)), Qt::AlignCenter,
                       QString("%1 %2").arg(number(lengths[i]), units_));
        }
    }
    if (marked_ >= 3) {  // the square corner
        p.setPen(QPen(idle, 1));
        p.drawPolyline(QPolygonF{points[1] + QPointF(0, -16), points[1] + QPointF(-16, -16),
                                 points[1] + QPointF(-16, 0)});
    }
    // The move being made: X from 1 towards 2, Y from 2 towards 3.
    if (moving_ == 'X' || moving_ == 'Y') {
        const QPointF from = moving_ == 'X' ? points[0] : points[1];
        const QPointF to = moving_ == 'X' ? points[1] : points[2];
        p.setPen(QPen(active, 3, Qt::DashLine));
        p.drawLine(from, to);
        const QPointF dir = (to - from) / std::hypot(to.x() - from.x(), to.y() - from.y());
        const QPointF normal(-dir.y(), dir.x());
        QPainterPath head;
        head.moveTo(to);
        head.lineTo(to - dir * 14 + normal * 7);
        head.lineTo(to - dir * 14 - normal * 7);
        head.closeSubpath();
        p.fillPath(head, active);
    }
    for (int i = 0; i < 3; ++i) {
        if (i >= marked_) {
            continue;
        }
        p.setPen(Qt::NoPen);
        p.setBrush(i == activePoint_ ? active : QColor("#16a34a"));
        p.drawEllipse(points[i], 13, 13);
        p.setPen(Qt::white);
        QFont bold = font();
        bold.setBold(true);
        p.setFont(bold);
        p.drawText(QRectF(points[i] - QPointF(13, 13), QSizeF(26, 26)), Qt::AlignCenter, QString::number(i + 1));
        p.setFont(font());
    }
}

// ---- XY Squaring --------------------------------------------------------------------------

SquaringDialog::SquaringDialog(Machine& machine, QWidget* parent) : QDialog(parent), machine_(machine) {
    setWindowTitle(tr("XY Squaring"));
    resize(780, 480);
    auto* layout = new QVBoxLayout(this);
    auto* columns = new QHBoxLayout;
    auto* left = new QVBoxLayout;
    title_ = new QLabel;
    QFont big = title_->font();
    big.setPointSizeF(big.pointSizeF() * 1.4);
    big.setBold(true);
    title_->setFont(big);
    description_ = new QLabel;
    description_->setWordWrap(true);
    instruction_ = new QLabel;
    instruction_->setWordWrap(true);
    instruction_->setStyleSheet("QLabel { background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px; "
                                "padding: 8px; }");
    rowsBox_ = new QWidget;
    rowsLayout_ = new QVBoxLayout(rowsBox_);
    rowsLayout_->setContentsMargins(0, 0, 0, 0);
    result_ = new QLabel;
    result_->setWordWrap(true);
    result_->setTextFormat(Qt::RichText);
    result_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    update_ = new QPushButton(tr("Update step/mm"));
    left->addWidget(title_);
    left->addWidget(description_);
    left->addWidget(instruction_);
    left->addWidget(rowsBox_);
    left->addWidget(result_);
    left->addWidget(update_, 0, Qt::AlignLeft);
    left->addStretch(1);
    diagram_ = new TriangleDiagram;
    columns->addLayout(left, 3);
    columns->addWidget(diagram_, 2);
    layout->addLayout(columns, 1);
    auto* buttons = new QHBoxLayout;
    auto* restartButton = new QPushButton(tr("Restart"));
    back_ = new QPushButton(tr("Back"));
    next_ = new QPushButton(tr("Next"));
    buttons->addWidget(restartButton);
    buttons->addStretch(1);
    buttons->addWidget(back_);
    buttons->addWidget(next_);
    layout->addLayout(buttons);

    connect(restartButton, &QPushButton::clicked, this, &SquaringDialog::restart);
    connect(back_, &QPushButton::clicked, this, &SquaringDialog::back);
    connect(next_, &QPushButton::clicked, this, &SquaringDialog::next);
    connect(update_, &QPushButton::clicked, this, [this] {
        const calibration::StepsAdjustment a = adjustment();
        if (confirmUpdate(this, tr("This will update the X-axis ($100) and Y-axis ($101) step/mm values in your "
                                   "CNC firmware to the new ones below:\n\nX-axis: %1\nY-axis: %2")
                                    .arg(QString::fromStdString(js::toFixed(a.x.stepsPerMm, 3)))
                                    .arg(QString::fromStdString(js::toFixed(a.y.stepsPerMm, 3))))) {
            updateFirmware();
        }
    });
    connect(&machine_, &Machine::stateChanged, this, &SquaringDialog::refresh);
    connect(&machine_, &Machine::connectionChanged, this, &SquaringDialog::refresh);
    connect(&machine_, &Machine::settingsChanged, this, &SquaringDialog::refresh);
    buildSteps();
    rebuildRows();
    refresh();
}

void SquaringDialog::buildSteps() {
    const double distance = calibration::defaultSquaringDistance(machine_.settings().metric);
    rows_ = {
        {},
        {
            {tr("Mark Point 1"),
             tr("First, we'll mark three points on your machine in a triangle. Stick the first tape to the wasteboard "
                "at the CNC's current position. The pointed tip should almost be touching the center of the X.")},
            {tr("Move X-axis"),
             tr("Input the farthest your CNC can move in the X-axis to create a horizontal line."), true, distance},
            {tr("Mark Point 2"), tr("Now mark the second location with the second piece of tape.")},
            {tr("Move Y-axis"), tr("Input the farthest your CNC can move in the Y-axis to create a vertical line."),
             true, distance},
            {tr("Mark Point 3"), tr("Place the last piece of tape with an X mark at the current position.")},
        },
        {
            {tr("Measure Distance 1-2"),
             tr("Now, measure the distances between the points you've marked and enter them below. Measure the "
                "distance between points 1 and 2."),
             true, 0},
            {tr("Measure Distance 2-3"), tr("Measure the distance between points 2 and 3."), true, 0},
            {tr("Measure Distance 1-3"), tr("Measure the distance between points 1 and 3."), true, 0},
        },
        {},
    };
}

void SquaringDialog::rebuildRows() {
    // Called from the dialog's own buttons, never a row's: direct deletion is safe.
    for (QWidget* child : rowsBox_->findChildren<QWidget*>(Qt::FindDirectChildrenOnly)) {
        delete child;
    }
    while (QLayoutItem* item = rowsLayout_->takeAt(0)) {
        delete item;
    }
    rowMarks_.clear();
    rowButtons_.clear();
    rowValues_.clear();
    const std::vector<Row>& rows = rows_[static_cast<std::size_t>(mainStep_)];
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        auto* line = new QWidget(rowsBox_);
        auto* lineLayout = new QHBoxLayout(line);
        lineLayout->setContentsMargins(0, 0, 0, 0);
        auto* mark = new QLabel;
        mark->setFixedWidth(24);
        auto* button = new QPushButton(row.button);
        button->setMinimumWidth(170);
        QDoubleSpinBox* value = nullptr;
        lineLayout->addWidget(mark);
        lineLayout->addWidget(button);
        if (row.hasValue) {
            value = distanceBox();
            setUnits(value, machine_.settings().metric);
            if (mainStep_ == 2) {
                value->setMinimum(0);
            }
            value->setValue(row.value);
            lineLayout->addWidget(value);
            const int index = static_cast<int>(i);
            connect(value, &QDoubleSpinBox::valueChanged, this, [this, index](double v) {
                rows_[static_cast<std::size_t>(mainStep_)][static_cast<std::size_t>(index)].value = v;
                refresh();
            });
        }
        lineLayout->addStretch(1);
        const int index = static_cast<int>(i);
        connect(button, &QPushButton::clicked, this, [this, index] { completeRow(index); });
        rowsLayout_->addWidget(line);
        rowMarks_.push_back(mark);
        rowButtons_.push_back(button);
        rowValues_.push_back(value);
    }
}

bool SquaringDialog::canGoNext() const {
    if (mainStep_ >= 3) {
        return false;
    }
    const std::vector<Row>& rows = rows_[static_cast<std::size_t>(mainStep_)];
    return std::all_of(rows.begin(), rows.end(), [](const Row& row) { return row.completed; });
}

void SquaringDialog::next() {
    if (!canGoNext()) {
        return;
    }
    ++mainStep_;
    subStep_ = 0;
    rebuildRows();
    refresh();
}

void SquaringDialog::back() {
    if (mainStep_ == 0) {
        return;
    }
    // Upstream resets the current and the previous step.
    for (const int step : {mainStep_, mainStep_ - 1}) {
        for (Row& row : rows_[static_cast<std::size_t>(step)]) {
            row.completed = false;
        }
    }
    --mainStep_;
    subStep_ = 0;
    rebuildRows();
    refresh();
}

void SquaringDialog::restart() {
    mainStep_ = 0;
    subStep_ = 0;
    triangle_ = {};
    moves_ = {};
    buildSteps();
    rebuildRows();
    refresh();
}

bool SquaringDialog::completeRow(int index) {
    std::vector<Row>& rows = rows_[static_cast<std::size_t>(mainStep_)];
    if (index < 0 || index >= static_cast<int>(rows.size()) || index != subStep_) {
        return false;
    }
    Row& row = rows[static_cast<std::size_t>(index)];
    if (row.completed) {
        return false;
    }
    if (mainStep_ == 1 && row.hasValue) {
        // jogMachine(): the move, remembered for the steps/mm check.
        if (!machine_.canMove()) {
            return false;
        }
        const char axis = row.button.contains('X') ? 'X' : 'Y';
        machine_.runSquaringMove(axis, row.value);
        (axis == 'X' ? moves_.x : moves_.y) = row.value;
    } else if (mainStep_ == 2) {
        if (!(row.value > 0)) {
            return false;
        }
        (index == 0 ? triangle_.a : index == 1 ? triangle_.b : triangle_.c) = row.value;
    }
    row.completed = true;
    const auto open = std::find_if(rows.begin(), rows.end(), [](const Row& r) { return !r.completed; });
    if (open != rows.end()) {
        subStep_ = static_cast<int>(open - rows.begin());
    }
    refresh();
    return true;
}

void SquaringDialog::setRowValue(int index, double value) {
    if (index >= 0 && index < static_cast<int>(rowValues_.size()) && rowValues_[static_cast<std::size_t>(index)]) {
        rowValues_[static_cast<std::size_t>(index)]->setValue(value);  // updates the row
    }
}

calibration::SquaringResult SquaringDialog::result() const {
    return calibration::squaringResult(triangle_, machine_.settings().metric);
}

calibration::StepsAdjustment SquaringDialog::adjustment() const {
    return calibration::stepAdjustment(triangle_, moves_, machine_.settingNumber("$100"),
                                       machine_.settingNumber("$101"));
}

QString SquaringDialog::resultText() const {
    const calibration::SquaringResult r = result();
    const QString units = unitName(machine_);
    const QString error = QString::fromStdString(r.diagonalError) + units;
    QString text;
    switch (r.verdict) {
        case calibration::Squareness::Square:
            text = "<p><b>" + tr("Your machine is properly squared!") + "</b></p>";
            break;
        case calibration::Squareness::SlightlyOut:
            text = "<p><b>" + tr("Your machine is slightly out of square") + "</b></p><p>" +
                   tr("The deviation is minor (%1) but you may want to adjust the right Y-axis rail to achieve "
                      "better squareness.")
                       .arg(error) +
                   "</p><p>" +
                   tr("You can move either the right Y-axis rail forward by %1 or the left Y-axis rail backward by "
                      "%1.")
                       .arg(error) +
                   "</p>";
            break;
        case calibration::Squareness::NeedsAdjustment:
            text = "<p><b>" + tr("Your machine needs adjustment") + "</b></p><p>" +
                   tr("The machine is off by %1&deg; or <b>%2</b> on the diagonal.")
                       .arg(QString::fromStdString(js::toFixed(r.angle, 2)), error) +
                   "</p><p>" +
                   tr("You can move either the right Y-axis rail forward by <b>%1</b> or the left Y-axis rail "
                      "backward by <b>%1</b>.")
                       .arg(error) +
                   "</p>";
            break;
    }
    text += "<p>" + tr("Bottom edge (1-2): %1%4<br>Right edge (2-3): %2%4<br>Diagonal (1-3): %3%4<br>Angle "
                       "deviation: %5&deg;")
                        .arg(number(triangle_.a), number(triangle_.b), number(triangle_.c), units,
                             QString::fromStdString(js::toFixed(r.angle, 2))) +
            "</p>";
    const calibration::StepsAdjustment a = adjustment();
    if (a.x.needed || a.y.needed) {
        text += "<p><b>" + tr("Other Recommendations") + "</b><br>" +
                tr("We also noticed from the results that your motor movement settings could be updated to improve "
                   "your machine's accuracy.") +
                "<br>" +
                tr("X-axis step/mm - current: %1, recommended: %2<br>Y-axis step/mm - current: %3, recommended: %4")
                    .arg(number(machine_.settingNumber("$100")),
                         QString::fromStdString(js::toFixed(a.x.stepsPerMm, 3)),
                         number(machine_.settingNumber("$101")),
                         QString::fromStdString(js::toFixed(a.y.stepsPerMm, 3))) +
                "</p>";
    }
    return text;
}

void SquaringDialog::updateFirmware() {
    machine_.writeFirmwareSettings(calibration::squaringUpdateCommands(adjustment()));
    Q_EMIT machine_.notice(tr("Updated EEPROM values"));
}

void SquaringDialog::refresh() {
    static const char* const kTitles[] = {QT_TR_NOOP("Initial Setup"), QT_TR_NOOP("Mark Reference Points"),
                                          QT_TR_NOOP("Take Measurements"), QT_TR_NOOP("Results")};
    title_->setText(tr(kTitles[mainStep_]));
    switch (mainStep_) {
        case 0:
            description_->setText(tr("If your CNC is making skewed cuts, it's because the X and Y axes aren't "
                                     "squared to each other. This can be fixed (5 - 10 minutes)."));
            instruction_->setText(tr("To know how much adjustment is needed, follow the steps below. Prepare:") +
                                  "\n  - " + tr("3 squares of tape marked with an 'X'") + "\n  - " +
                                  tr("A long ruler or measuring tape") + "\n  - " +
                                  tr("Put something pointed in the spindle like an old v-bit, tapered bit, pencil, "
                                     "or a pointed dowel") +
                                  "\n\n" +
                                  tr("Use the jog controls to position your CNC near its front, left corner with the "
                                     "pointed tip almost touching the wasteboard, then continue."));
            break;
        case 1: description_->setText(tr("First, we'll mark three points on your machine in a triangle.")); break;
        case 2:
            description_->setText(
                tr("Now, measure the distances between the points you've marked and enter them below."));
            break;
        default: description_->clear(); break;
    }
    const std::vector<Row>& rows = rows_[static_cast<std::size_t>(mainStep_)];
    if (!rows.empty()) {
        instruction_->setText(rows[static_cast<std::size_t>(subStep_)].description);
    }
    instruction_->setVisible(mainStep_ < 3);
    description_->setVisible(!description_->text().isEmpty());
    rowsBox_->setVisible(!rows.empty());
    const bool canMove = machine_.canMove();
    for (std::size_t i = 0; i < rows.size() && i < rowButtons_.size(); ++i) {
        const Row& row = rows[i];
        const bool current = static_cast<int>(i) == subStep_ && !row.completed;
        showState(rowMarks_[i], row.completed ? RowState::Done : current ? RowState::Current : RowState::Pending);
        const bool isMove = mainStep_ == 1 && row.hasValue;
        const bool isMeasure = mainStep_ == 2;
        rowButtons_[i]->setEnabled(current && (!isMove || canMove) && (!isMeasure || row.value > 0));
        if (rowValues_[i]) {
            rowValues_[i]->setEnabled(current);
        }
    }
    const bool results = mainStep_ == 3;
    result_->setVisible(results);
    if (results) {
        result_->setText(resultText());
    }
    const calibration::StepsAdjustment a = adjustment();
    update_->setVisible(results && (a.x.needed || a.y.needed));
    back_->setEnabled(mainStep_ > 0);
    next_->setVisible(mainStep_ < 3);
    next_->setEnabled(canGoNext());
    next_->setText(mainStep_ == 2 ? tr("See Results") : tr("Next"));

    // The diagram.
    int marked = 3;
    int activePoint = -1;
    int activeSide = -1;
    char moving = 0;
    if (mainStep_ == 0) {
        marked = 0;
    } else if (mainStep_ == 1) {
        marked = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (static_cast<int>(i) <= subStep_ && rows[i].button.startsWith(tr("Mark Point"))) {
                ++marked;
            }
        }
        const Row& row = rows[static_cast<std::size_t>(subStep_)];
        if (!row.completed) {
            if (row.hasValue) {
                moving = row.button.contains('X') ? 'X' : 'Y';
            } else {
                activePoint = row.button.back().digitValue() - 1;
            }
        }
    } else if (mainStep_ == 2 && !rows[static_cast<std::size_t>(subStep_)].completed) {
        activeSide = subStep_;
    }
    diagram_->setState(mainStep_, marked, activePoint, activeSide, moving, triangle_, unitName(machine_));
}

}  // namespace gs::app
