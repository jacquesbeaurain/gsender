#include "jogger.hpp"

#include "machine.hpp"

#include "gs/util/units.hpp"

#include <cctype>

namespace gs::app {

Jogger::Jogger(Machine& machine, QObject* parent) : QObject(parent), machine_(machine) {
    metric_ = machine_.settings().metric;
    presets_ = {machine_.settings().jog.rapid, machine_.settings().jog.normal, machine_.settings().jog.precise};
    speeds_ = presetSpeeds(preset_);
    rebuildHelper();
    connect(&machine_, &Machine::appSettingsChanged, this, [this] {
        if (machine_.settings().jog.threshold != threshold_) {
            rebuildHelper();
        }
        const JogSettings& jog = machine_.settings().jog;
        const std::array<controller::JogSpeeds, 3> presets{jog.rapid, jog.normal, jog.precise};
        if (machine_.settings().metric != metric_ || presets != presets_) {
            metric_ = machine_.settings().metric;
            presets_ = presets;
            selectPreset(preset_);  // the preset again, as now stored and in the units now shown
        }
    });
    // Never leave a continuous jog running when the connection goes.
    connect(&machine_, &Machine::connectionChanged, this, [this] {
        if (!machine_.controller()) {
            rebuildHelper();
        }
    });
}

Jogger::~Jogger() = default;

void Jogger::rebuildHelper() {
    if (helper_ && helper_->isPressed()) {
        helper_->keyUp();  // stops a continuous jog in progress
    }
    threshold_ = machine_.settings().jog.threshold;
    helper_ = std::make_unique<controller::JogHelper>(
        machine_.eventLoop(),
        controller::JogHelper::Callbacks{
            [this](const controller::JogAxes& distances, double feedrate) { stepJog(distances, feedrate); },
            [this](const controller::JogAxes& distances, double feedrate) { startContinuous(distances, feedrate); },
            [this] { stopContinuous(); },
        },
        threshold_);
}

controller::JogSpeeds Jogger::presetSpeeds(controller::JogPreset preset) const {
    controller::JogSpeeds speeds = machine_.settings().jog.speeds(preset);
    if (!metric_) {
        speeds.xyStep = units::convertValue(speeds.xyStep, true, false);
        speeds.zStep = units::convertValue(speeds.zStep, true, false);
        speeds.feedrate = units::convertValue(speeds.feedrate, true, false);
    }
    return speeds;
}

void Jogger::selectPreset(controller::JogPreset preset) {
    preset_ = preset;
    speeds_ = presetSpeeds(preset);
    Q_EMIT changed();
}

void Jogger::cyclePreset() {
    selectPreset(controller::nextJogPreset(preset_));
}

void Jogger::setSpeeds(const controller::JogSpeeds& speeds) {
    speeds_ = speeds;
    Q_EMIT changed();
}

void Jogger::press(const controller::JogAxes& directions) {
    if (!canJog()) {
        return;
    }
    rotaryJog_ = false;
    controller::JogAxes distances;
    for (const auto& [axis, direction] : directions) {
        const char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
        const double step = upper == 'Z' ? speeds_.zStep : upper == 'A' ? speeds_.aStep : speeds_.xyStep;
        distances.emplace_back(upper, step * direction);
    }
    helper_->keyDown(distances, speeds_.feedrate);
}

void Jogger::pressRotary(int direction) {
    if (!canJog()) {
        return;
    }
    rotaryJog_ = true;
    const char axis = machine_.rotaryMode() ? 'Y' : 'A';
    helper_->keyDown({{axis, speeds_.aStep * direction}}, speeds_.feedrate);
}

void Jogger::release() {
    helper_->keyUp();
}

bool Jogger::isPressed() const {
    return helper_->isPressed();
}

bool Jogger::canJog() const {
    controller::Controller* c = machine_.controller();
    if (!c || c->workflow().isRunning()) {
        return false;
    }
    const std::string& state = c->state().status.activeState;
    return state == "Idle" || state == "Jog";
}

void Jogger::stepJog(const controller::JogAxes& distances, double feedrate) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const auto allowed = controller::filterAxesForLimits(distances, c->state().status.pinState,
                                                         machine_.settings().jog.preventJoggingPastLimits);
    if (allowed) {
        const bool metric = metric_ || rotaryJog_;
        c->gcode(controller::jogCommand(*allowed, metric_ ? feedrate : (rotaryJog_ ? units::in2mm(feedrate) : feedrate),
                                        metric));
    }
}

void Jogger::startContinuous(const controller::JogAxes& distances, double feedrate) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    // Continuous jogs only take directions.
    controller::JogAxes directions;
    for (const auto& [axis, distance] : distances) {
        directions.emplace_back(axis, distance > 0 ? 1 : -1);
    }
    const auto allowed = controller::filterAxesForLimits(directions, c->state().status.pinState,
                                                         machine_.settings().jog.preventJoggingPastLimits);
    if (!allowed) {
        return;
    }
    controller::Axes4 direction;
    for (const auto& [axis, value] : *allowed) {
        for (std::size_t i = 0; i < controller::kJogAxes.size(); ++i) {
            if (controller::kJogAxes[i] == axis) {
                direction[i] = value;
            }
        }
    }
    if (rotaryJog_ && !metric_) {
        c->jogStart(direction, units::in2mm(feedrate), controller::JogUnits::Millimetres);
        return;
    }
    c->jogStart(direction, feedrate, metric_ ? controller::JogUnits::Millimetres : controller::JogUnits::Inches);
}

void Jogger::stopContinuous() {
    if (controller::Controller* c = machine_.controller()) {
        c->jogStop();
    }
}

}  // namespace gs::app
