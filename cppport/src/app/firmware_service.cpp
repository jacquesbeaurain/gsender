#include "firmware_service.hpp"
#include "machine.hpp"
#include "rotary_actions.hpp"
#include "gs/calibration/calibration.hpp"
#include "gs/controller/controller.hpp"
#include "gs/controller/spindle.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <cmath>

namespace gs::app {

FirmwareService::FirmwareService(Machine& machine, QObject* parent)
    : QObject(parent), machine_(machine) {}

double FirmwareService::settingNumber(const std::string& key) const {
    const controller::Controller* c = machine_.controller();
    const std::string value = c ? c->runner().setting(key) : std::string();
    return value.empty() ? std::nan("") : js::stringToNumber(value);
}

void FirmwareService::writeFirmwareSettings(const std::vector<std::string>& lines) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(lines);
    }
}

bool FirmwareService::stepperLocked() const {
    const controller::Controller* c = machine_.controller();
    return c && c->runner().setting("$1") == "255";
}

void FirmwareService::setStepperLock(bool lock) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    AppSettings settings = machine_.settings();
    if (lock) {
        settings.stepperRestoreValue = c->runner().setting("$1");
        c->gcode(std::vector<std::string>{"$1=255", "$$"});
    } else {
        const std::string value = settings.stepperRestoreValue.empty() ? "50" : settings.stepperRestoreValue;
        c->gcode(std::vector<std::string>{"$1=" + value, "$$"});
        settings.stepperRestoreValue.clear();
    }
    machine_.setSettings(settings);
}

bool FirmwareService::laserMode() const {
    const controller::Controller* c = machine_.controller();
    const std::string mode = c ? c->runner().setting("$32") : std::string();
    return mode.empty() ? machine_.settings().spindle.laserMode : js::stringToNumber(mode) != 0;
}

double FirmwareService::laserMaxPower() const {
    const controller::Controller* c = machine_.controller();
    if (c && c->isGrblHal()) {
        const std::string max = c->runner().setting("$730", "255");
        return js::stringToNumber(max);
    }
    return machine_.settings().spindle.laser.maxPower;
}

void FirmwareService::setLaserMode(bool laser) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    protocol::Runner& runner = c->runner();
    const bool hal = c->isGrblHal();
    AppSettings s = machine_.settings();
    controller::ModeSwitch change;
    change.toLaser = laser;
    change.metric = s.metric;
    change.deviceUnits = runner.modal().units;
    change.spindleOn = runner.modal().spindle != "M5";
    change.wcs = runner.modal().wcs;
    const std::array<double, 4> work = machine_.workPositionMm();
    change.workX = work[0];
    change.workY = work[1];
    if (hal) {
        const auto offset = [&runner](const char* key, const char* older) {
            std::string value = runner.setting(key);
            if (value.empty()) {
                value = runner.setting(older);
            }
            return value.empty() ? 0.0 : js::stringToNumber(value);
        };
        change.offset = {offset("$770", "$741"), offset("$771", "$742")};
    } else {
        change.offset = {s.spindle.laser.xOffset, s.spindle.laser.yOffset};
    }
    const double currentMax = js::stringToNumber(runner.setting("$30", "30000"));
    const double currentMin = js::stringToNumber(runner.setting("$31", "1000"));
    if (laser) {
        if (!hal) {
            s.spindle.spindleMax = currentMax;
            s.spindle.spindleMin = currentMin;
            change.range = std::pair{s.spindle.laser.maxPower, s.spindle.laser.minPower};
        }
    } else {
        const bool laserSpindle = hal && std::any_of(machine_.spindles().begin(), machine_.spindles().end(), [](const auto& spindle) {
                                      return spindle.label == "SLB_LASER" || spindle.label == "PWM2";
                                  });
        if (!laserSpindle) {
            s.spindle.laser.maxPower = currentMax;
            s.spindle.laser.minPower = currentMin;
            change.range = std::pair{s.spindle.spindleMax, s.spindle.spindleMin};
        }
    }
    s.spindle.laserMode = laser;
    machine_.setSettings(s);
    c->gcode(controller::modeSwitchCommands(change));
    if (change.range) {
        runner.setSetting("$30", js::numberToString(change.range->first));
        runner.setSetting("$31", js::numberToString(change.range->second));
    }
    runner.setSetting("$32", laser ? "1" : "0");
    Q_EMIT machine_.settingsChanged();
}

void FirmwareService::selectSpindle(int id) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    machine_.clearSpindles();
    Q_EMIT machine_.spindlesChanged();
    c->gcode(std::vector<std::string>{"M104 Q" + std::to_string(id), c->spindleListCommand()});
}

bool FirmwareService::setRotaryMode(bool rotaryMode) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    AppSettings settings = machine_.settings();
    rotary::ModeSwitch change;
    change.enable = rotaryMode;
    change.grblHal = c->isGrblHal();
    if (!change.grblHal) {
        if (rotaryMode) {
            settings.rotary.defaults = rotary::currentFirmwareValues(c->settings().settings);
            change.grblSettings = settings.rotary.firmware;
        } else {
            change.grblSettings = settings.rotary.defaults;
        }
    }
    c->gcode(rotary::modeSwitchCommands(change, c->settings().settings));
    if (change.grblHal) {
        c->setRotaryMode(rotaryMode);
    }
    settings.rotary.rotaryMode = rotaryMode;
    machine_.setSettings(settings);
    return true;
}

bool FirmwareService::runTuningMove(char axis, double distance) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    const auto axes = controller::filterAxesForLimits({{axis, distance}}, c->state().status.pinState,
                                                      machine_.settings().jog.preventJoggingPastLimits);
    if (!axes) {
        return false;
    }
    c->gcode(calibration::tuningMove(axis, distance, machine_.settings().metric));
    return true;
}

void FirmwareService::runSquaringMove(char axis, double distance) {
    if (controller::Controller* c = machine_.controller()) {
        c->gcode(calibration::squaringMove(axis, distance, machine_.settings().metric));
    }
}

}  // namespace gs::app
