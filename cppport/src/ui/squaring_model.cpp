#include "squaring_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/util/jsnumber.hpp"

#include <algorithm>

namespace gs::ui {
namespace {

QString number(double value) {
    return QString::fromStdString(js::numberToString(value));
}

}  // namespace

SquaringModel::SquaringModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged,
                        &app::Machine::appSettingsChanged, &app::Machine::settingsChanged}) {
        connect(&machine_, signal, this, &SquaringModel::changed);
    }
    buildSteps();
}

void SquaringModel::buildSteps() {
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

QString SquaringModel::title() const {
    static const char* const kTitles[] = {QT_TR_NOOP("Initial Setup"), QT_TR_NOOP("Mark Reference Points"),
                                          QT_TR_NOOP("Take Measurements"), QT_TR_NOOP("Results")};
    return tr(kTitles[mainStep_]);
}

QString SquaringModel::description() const {
    switch (mainStep_) {
        case 0:
            return tr("If your CNC is making skewed cuts, it's because the X and Y axes aren't squared to each other. "
                      "This can be fixed (5 - 10 minutes).");
        case 1: return tr("First, we'll mark three points on your machine in a triangle.");
        case 2: return tr("Now, measure the distances between the points you've marked and enter them below.");
        default: return {};
    }
}

QString SquaringModel::instruction() const {
    if (mainStep_ == 0) {
        return tr("To know how much adjustment is needed, follow the steps below. Prepare:") + "\n  - " +
               tr("3 squares of tape marked with an 'X'") + "\n  - " + tr("A long ruler or measuring tape") +
               "\n  - " +
               tr("Put something pointed in the spindle like an old v-bit, tapered bit, pencil, or a pointed dowel") +
               "\n\n" +
               tr("Use the jog controls to position your CNC near its front, left corner with the pointed tip almost "
                  "touching the wasteboard, then continue.");
    }
    const std::vector<Row>& rows = currentRows();
    return rows.empty() ? QString() : rows[static_cast<std::size_t>(subStep_)].description;
}

QVariantList SquaringModel::rows() const {
    QVariantList list;
    const std::vector<Row>& rows = currentRows();
    const bool canMove = machine_.canMove();
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const Row& row = rows[i];
        const bool current = static_cast<int>(i) == subStep_ && !row.completed;
        const bool isMove = mainStep_ == 1 && row.hasValue;
        const bool isMeasure = mainStep_ == 2;
        list.append(QVariantMap{
            {"button", row.button},
            {"hasValue", row.hasValue},
            {"value", row.value},
            {"completed", row.completed},
            {"current", current},
            {"enabled", current && (!isMove || canMove) && (!isMeasure || row.value > 0)},
        });
    }
    return list;
}

bool SquaringModel::canGoNext() const {
    if (mainStep_ >= 3) {
        return false;
    }
    const std::vector<Row>& rows = currentRows();
    return std::all_of(rows.begin(), rows.end(), [](const Row& row) { return row.completed; });
}

QString SquaringModel::units() const {
    return machine_.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

void SquaringModel::next() {
    if (!canGoNext()) {
        return;
    }
    ++mainStep_;
    subStep_ = 0;
    Q_EMIT changed();
}

void SquaringModel::back() {
    if (mainStep_ == 0) {
        return;
    }
    for (const int step : {mainStep_, mainStep_ - 1}) {
        for (Row& row : rows_[static_cast<std::size_t>(step)]) {
            row.completed = false;
        }
    }
    --mainStep_;
    subStep_ = 0;
    Q_EMIT changed();
}

void SquaringModel::restart() {
    mainStep_ = 0;
    subStep_ = 0;
    triangle_ = {};
    moves_ = {};
    buildSteps();
    Q_EMIT changed();
}

bool SquaringModel::completeRow(int index) {
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
    Q_EMIT changed();
    return true;
}

void SquaringModel::setRowValue(int index, double value) {
    std::vector<Row>& rows = rows_[static_cast<std::size_t>(mainStep_)];
    if (index < 0 || index >= static_cast<int>(rows.size()) || !rows[static_cast<std::size_t>(index)].hasValue) {
        return;
    }
    rows[static_cast<std::size_t>(index)].value = mainStep_ == 2 ? std::max(0.0, value) : value;
    Q_EMIT changed();
}

calibration::StepsAdjustment SquaringModel::adjustment() const {
    return calibration::stepAdjustment(triangle_, moves_, machine_.settingNumber("$100"),
                                       machine_.settingNumber("$101"));
}

bool SquaringModel::updateNeeded() const {
    const calibration::StepsAdjustment a = adjustment();
    return mainStep_ == 3 && (a.x.needed || a.y.needed);
}

QString SquaringModel::updateText() const {
    const calibration::StepsAdjustment a = adjustment();
    return tr("This will update the X-axis ($100) and Y-axis ($101) step/mm values in your CNC firmware to the new "
              "ones below:\n\nX-axis: %1\nY-axis: %2")
        .arg(QString::fromStdString(js::toFixed(a.x.stepsPerMm, 3)),
             QString::fromStdString(js::toFixed(a.y.stepsPerMm, 3)));
}

QString SquaringModel::resultText() const {
    if (mainStep_ != 3) {
        return {};
    }
    const calibration::SquaringResult r = calibration::squaringResult(triangle_, machine_.settings().metric);
    const QString unit = units();
    const QString error = QString::fromStdString(r.diagonalError) + unit;
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
                        .arg(number(triangle_.a), number(triangle_.b), number(triangle_.c), unit,
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

void SquaringModel::updateFirmware() {
    machine_.writeFirmwareSettings(calibration::squaringUpdateCommands(adjustment()));
    Q_EMIT machine_.successNotice(tr("Updated EEPROM values"));
}

int SquaringModel::markedPoints() const {
    if (mainStep_ == 0) {
        return 0;
    }
    if (mainStep_ > 1) {
        return 3;
    }
    const std::vector<Row>& rows = currentRows();
    int marked = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (static_cast<int>(i) <= subStep_ && !rows[i].hasValue) {
            ++marked;
        }
    }
    return marked;
}

int SquaringModel::activePoint() const {
    if (mainStep_ != 1) {
        return -1;
    }
    const Row& row = currentRows()[static_cast<std::size_t>(subStep_)];
    return row.completed || row.hasValue ? -1 : row.button.back().digitValue() - 1;
}

int SquaringModel::activeSide() const {
    return mainStep_ == 2 && !currentRows()[static_cast<std::size_t>(subStep_)].completed ? subStep_ : -1;
}

QString SquaringModel::moving() const {
    if (mainStep_ != 1) {
        return {};
    }
    const Row& row = currentRows()[static_cast<std::size_t>(subStep_)];
    if (row.completed || !row.hasValue) {
        return {};
    }
    return row.button.contains('X') ? QStringLiteral("X") : QStringLiteral("Y");
}

QVariantList SquaringModel::sides() const {
    return {triangle_.a, triangle_.b, triangle_.c};
}

}  // namespace gs::ui
