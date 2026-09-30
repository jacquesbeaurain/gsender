#include "movement_tuning_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "qt_text.hpp"

#include "gs/calibration/calibration.hpp"
#include "gs/util/jsnumber.hpp"

#include <cctype>

namespace gs::ui {
MovementTuningModel::MovementTuningModel(QObject* parent)
    : WizardModelBase(parent) {
    connectMachineSignals(true, false);
    setTotalSteps(5);
    setAxis(QStringLiteral("X"));
}

QString MovementTuningModel::step() const {
    static const char* const kSteps[] = {"intro", "mark", "move", "measure", "result"};
    return QString::fromLatin1(kSteps[stepIndex_]);
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

bool MovementTuningModel::canMove() const {
    return machine_.canMove();
}

QString MovementTuningModel::instruction() const {
    switch (stepIndex_) {
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
        .arg(axis(), jsNumber(calibration::tuningError(moveDistance_, travelled_)), units());
}

QString MovementTuningModel::updateText() const {
    const std::string setting = calibration::stepsSetting(axis_);
    return tr("This will update the %1-axis step/mm value in your CNC firmware (%2).\n\nFrom: %3\nTo: %4")
        .arg(axis(), QString::fromStdString(setting), jsNumber(machine_.settingNumber(setting)),
             jsNumber(recommendedStepsPerMm()));
}

bool MovementTuningModel::start() {
    if (stepIndex_ != Intro || !machine_.canMove()) {
        return false;
    }
    setStepIndex(Mark);
    return true;
}

void MovementTuningModel::markLocation() {
    if (stepIndex_ == Mark) {
        setStepIndex(Move);
    }
}

bool MovementTuningModel::moveAxis() {
    // Deviation: a move refused (not idle, or towards a triggered limit)
    // does not count as made; upstream went on regardless.
    if (stepIndex_ != Move || !machine_.canMove() || !machine_.runTuningMove(axis_, moveDistance_)) {
        return false;
    }
    setStepIndex(Measure);
    return true;
}

void MovementTuningModel::confirmTravelled() {
    if (stepIndex_ == Measure) {
        setStepIndex(Result);
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
    WizardModelBase::restart();
    setAxis(QStringLiteral("X"));
}

}  // namespace gs::ui
