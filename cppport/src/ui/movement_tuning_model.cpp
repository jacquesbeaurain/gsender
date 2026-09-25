#include "movement_tuning_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/calibration/calibration.hpp"
#include "gs/util/jsnumber.hpp"

#include <cctype>

namespace gs::ui {
namespace {

QString number(double value) {
    return QString::fromStdString(js::numberToString(value));
}

}  // namespace

MovementTuningModel::MovementTuningModel(QObject* parent)
    : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged,
                        &app::Machine::appSettingsChanged, &app::Machine::settingsChanged}) {
        connect(&machine_, signal, this, &MovementTuningModel::changed);
    }
    setAxis(QStringLiteral("X"));
}

QString MovementTuningModel::step() const {
    static const char* const kSteps[] = {"intro", "mark", "move", "measure", "result"};
    return QString::fromLatin1(kSteps[step_]);
}

void MovementTuningModel::setAxis(const QString& axis) {
    const char letter = axis.isEmpty() ? 'X' : static_cast<char>(std::toupper(axis.front().toLatin1()));
    axis_ = letter == 'Y' || letter == 'Z' ? letter : 'X';
    moveDistance_ = travelled_ = calibration::defaultTuningDistance(axis_, machine_.settings().metric);
    Q_EMIT changed();
}

void MovementTuningModel::setMoveDistance(double distance) {
    moveDistance_ = distance;
    Q_EMIT changed();
}

void MovementTuningModel::setTravelled(double distance) {
    travelled_ = distance;
    Q_EMIT changed();
}

QString MovementTuningModel::units() const {
    return machine_.settings().metric ? QStringLiteral("mm") : QStringLiteral("in");
}

bool MovementTuningModel::connected() const {
    return machine_.controller() != nullptr;
}

bool MovementTuningModel::canMove() const {
    return machine_.canMove();
}

QString MovementTuningModel::instruction() const {
    switch (step_) {
        case Mark:
            return tr("First, mark next to the gantry in the location shown with your marker, pencil, or using a "
                      "strip of tape.");
        case Move:
            return tr("Now move any distance you wish. A larger value will better tune your movement, just make sure "
                      "you don't hit your machine limits. Once you are ready, click the Move Axis button.");
        case Measure:
            return tr("Lastly, measure the distance travelled between the original mark and the current gantry "
                      "location. Take your time when entering this value, a more accurate measurement will give you "
                      "better tuning results.");
        default: return {};
    }
}

QString MovementTuningModel::resultText() const {
    if (accurate()) {
        return tr("Your %1-axis looks accurate, so you should be good to go!").arg(axis());
    }
    return tr("Your %1-axis movement was off by <b>%2 %3.</b> Consider updating your %1-axis step/mm value in your "
              "CNC firmware.")
        .arg(axis(), number(calibration::tuningError(moveDistance_, travelled_)), units());
}

QString MovementTuningModel::updateText() const {
    const std::string setting = calibration::stepsSetting(axis_);
    return tr("This will update the %1-axis step/mm value in your CNC firmware (%2).\n\nFrom: %3\nTo: %4")
        .arg(axis(), QString::fromStdString(setting), number(machine_.settingNumber(setting)),
             number(recommendedStepsPerMm()));
}

bool MovementTuningModel::start() {
    if (step_ != Intro || !machine_.canMove()) {
        return false;
    }
    step_ = Mark;
    Q_EMIT changed();
    return true;
}

void MovementTuningModel::markLocation() {
    if (step_ == Mark) {
        step_ = Move;
        Q_EMIT changed();
    }
}

bool MovementTuningModel::moveAxis() {
    // Deviation: a move refused (not idle, or towards a triggered limit)
    // does not count as made; upstream went on regardless.
    if (step_ != Move || !machine_.canMove() || !machine_.runTuningMove(axis_, moveDistance_)) {
        return false;
    }
    step_ = Measure;
    Q_EMIT changed();
    return true;
}

void MovementTuningModel::confirmTravelled() {
    if (step_ == Measure) {
        step_ = Result;
        Q_EMIT changed();
    }
}

double MovementTuningModel::recommendedStepsPerMm() const {
    return calibration::newStepsPerMm(machine_.settingNumber(calibration::stepsSetting(axis_)), moveDistance_,
                                      travelled_);
}

void MovementTuningModel::updateFirmware() {
    machine_.writeFirmwareSettings(calibration::tuningUpdateCommands(axis_, recommendedStepsPerMm()));
    Q_EMIT machine_.successNotice(tr("Updated steps-per-mm value"));
}

void MovementTuningModel::restart() {
    step_ = Intro;
    setAxis(QStringLiteral("X"));
}

}  // namespace gs::ui
