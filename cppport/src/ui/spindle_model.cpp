#include "spindle_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"
#include "gs/controller/spindle.hpp"
#include "gs/util/jsnumber.hpp"

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace gs::ui {
namespace {

// canClick: connected, no job running, idle.
bool idle(app::Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && !c->workflow().isRunning() && c->state().status.activeState == "Idle";
}

}  // namespace

// ---- coolant ------------------------------------------------------------------------

CoolantModel::CoolantModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::workflowChanged, &app::Machine::connectionChanged}) {
        connect(&machine_, signal, this, &CoolantModel::changed);
    }
}

bool CoolantModel::canClick() const {
    return idle(machine_);
}

bool CoolantModel::hasCoolant(const char* code) const {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    const auto& coolant = c->state().parserState.modal.coolant;
    return std::find(coolant.begin(), coolant.end(), code) != coolant.end();
}

bool CoolantModel::mistActive() const {
    return hasCoolant("M7");
}

bool CoolantModel::floodActive() const {
    return hasCoolant("M8");
}

void CoolantModel::command(const char* code) {
    if (controller::Controller* c = machine_.controller(); c && canClick()) {
        c->gcode(code);
    }
}

void CoolantModel::mist() {
    command("M7");
}

void CoolantModel::flood() {
    command("M8");
}

void CoolantModel::off() {
    command("M9");
}

// ---- spindle and laser --------------------------------------------------------------

SpindleModel::SpindleModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    speedTimer_ = new QTimer(this);
    speedTimer_->setSingleShot(true);
    speedTimer_->setInterval(300);
    connect(speedTimer_, &QTimer::timeout, this, &SpindleModel::applySpeed);
    powerTimer_ = new QTimer(this);
    powerTimer_->setSingleShot(true);
    powerTimer_->setInterval(300);
    connect(powerTimer_, &QTimer::timeout, this, &SpindleModel::applyPower);
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged, &app::Machine::workflowChanged,
                        &app::Machine::settingsChanged, &app::Machine::spindlesChanged,
                        &app::Machine::appSettingsChanged}) {
        connect(&machine_, signal, this, [this] {
            sync();
            Q_EMIT changed();
        });
    }
    connect(&machine_, &app::Machine::connectionChanged, this, [this] {
        if (!machine_.isConnected()) {
            spindleOn_ = laserLit_ = false;
        }
    });
    sync();
}

void SpindleModel::sync() {
    const app::AppSettings& settings = machine_.settings();
    if (!speedTimer_->isActive()) {
        // Clamped to the range, as upstream keeps it.
        speed_ = std::clamp(settings.spindle.speed, std::min(speedMin(), speedMax()), speedMax());
    }
    if (!powerTimer_->isActive()) {
        power_ = settings.spindle.laser.power;
    }
}

bool SpindleModel::canClick() const {
    return idle(machine_);
}

bool SpindleModel::connected() const {
    return machine_.isConnected();
}

bool SpindleModel::laserMode() const {
    return machine_.laserMode();
}

bool SpindleModel::forward() const {
    controller::Controller* c = machine_.controller();
    return c && c->state().parserState.modal.spindle == "M3";
}

bool SpindleModel::reverse() const {
    controller::Controller* c = machine_.controller();
    return c && c->state().parserState.modal.spindle == "M4";
}

bool SpindleModel::laserOn() const {
    controller::Controller* c = machine_.controller();
    const std::string spindle = c ? c->state().parserState.modal.spindle : std::string();
    return !spindle.empty() && spindle != "M5";
}

double SpindleModel::speedMin() const {
    controller::Controller* c = machine_.controller();
    const double min = c ? js::stringToNumber(c->runner().setting("$31", "1000")) : machine_.settings().spindle.spindleMin;
    return std::isfinite(min) ? min : 0;
}

double SpindleModel::speedMax() const {
    controller::Controller* c = machine_.controller();
    const double max = c ? js::stringToNumber(c->runner().setting("$30", "30000")) : machine_.settings().spindle.spindleMax;
    return std::isfinite(max) ? max : 30000;
}

bool SpindleModel::speedSlider() const {
    return machine_.settings().spindle.inputType != "Number";
}

double SpindleModel::duration() const {
    return machine_.settings().spindle.laser.duration;
}

bool SpindleModel::grblHal() const {
    controller::Controller* c = machine_.controller();
    return c && c->isGrblHal();
}

QVariantList SpindleModel::spindles() const {
    QVariantList list;
    for (const auto& spindle : machine_.spindles()) {
        list.append(QVariantMap{{"id", spindle.id.value_or(0)}, {"label", QString::fromStdString(spindle.label)}});
    }
    return list;
}

int SpindleModel::spindleId() const {
    for (const auto& spindle : machine_.spindles()) {
        if (spindle.enabled) {
            return spindle.id.value_or(0);
        }
    }
    return 0;
}

void SpindleModel::command(const std::string& gcode) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(gcode);
    }
}

void SpindleModel::startClockwise() {
    if (!canClick()) {
        return;
    }
    if (laserMode()) {
        // sendLaserM3(): focus at the set power.
        laserLit_ = true;
        command(controller::laserOnCommand(power_, machine_.laserMaxPower()));
    } else {
        spindleOn_ = true;
        command("M3 S" + js::numberToString(speed_));
    }
}

void SpindleModel::startCounterClockwise() {
    if (!canClick()) {
        return;
    }
    if (laserMode()) {
        // runLaserTest(): the laser fires for the duration, then goes off.
        const double seconds = duration();
        if (controller::Controller* c = machine_.controller()) {
            c->laserTestOn(power_, seconds);
        }
        QTimer::singleShot(static_cast<int>(seconds * 1000), this, [this] {
            laserLit_ = false;
            command("M5 S0");
        });
    } else {
        spindleOn_ = true;
        command("M4 S" + js::numberToString(speed_));
    }
}

void SpindleModel::stop() {
    if (!canClick()) {
        return;
    }
    spindleOn_ = laserLit_ = false;
    command("M5 S0");
}

void SpindleModel::toggleMode() {
    if (canClick()) {
        spindleOn_ = laserLit_ = false;
        machine_.setLaserMode(!machine_.laserMode());
    }
}

void SpindleModel::setSpeed(double rpm) {
    if (!std::isfinite(rpm)) {
        return;
    }
    speed_ = std::clamp(rpm, std::min(speedMin(), speedMax()), speedMax());
    speedTimer_->start();
    Q_EMIT changed();
}

void SpindleModel::setPower(double percent) {
    if (!std::isfinite(percent)) {
        return;
    }
    power_ = std::clamp(percent, 0.0, 100.0);
    powerTimer_->start();
    Q_EMIT changed();
}

void SpindleModel::setDuration(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0) {
        return;
    }
    app::AppSettings settings = machine_.settings();
    settings.spindle.laser.duration = seconds;
    machine_.setSettings(settings);
}

void SpindleModel::selectSpindle(int id) {
    if (canClick()) {
        machine_.selectSpindle(id);
    }
}

void SpindleModel::applyNow() {
    if (speedTimer_->isActive()) {
        speedTimer_->stop();
        applySpeed();
    }
    if (powerTimer_->isActive()) {
        powerTimer_->stop();
        applyPower();
    }
}

void SpindleModel::applySpeed() {
    app::AppSettings settings = machine_.settings();
    settings.spindle.speed = speed_;
    machine_.setSettings(settings);
    controller::Controller* c = machine_.controller();
    if (c && spindleOn_) {
        c->spindleSpeedChange(speed_);
    }
}

void SpindleModel::applyPower() {
    app::AppSettings settings = machine_.settings();
    settings.spindle.laser.power = power_;
    machine_.setSettings(settings);
    controller::Controller* c = machine_.controller();
    if (c && laserLit_) {
        c->laserPowerChange(power_, machine_.laserMaxPower());
    }
}

}  // namespace gs::ui
