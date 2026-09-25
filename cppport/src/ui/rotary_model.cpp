#include "rotary_model.hpp"

#include "backend.hpp"
#include "machine.hpp"
#include "rotary_actions.hpp"

#include "gs/controller/controller.hpp"
#include "gs/rotary/rotary.hpp"

namespace gs::ui {

using namespace app::rotary_actions;

RotaryModel::RotaryModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged, &app::Machine::workflowChanged,
                        &app::Machine::appSettingsChanged}) {
        connect(&machine_, signal, this, &RotaryModel::changed);
    }
}

bool RotaryModel::rotaryMode() const {
    return machine_.rotaryMode();
}

bool RotaryModel::grblHal() const {
    return !isGrbl(machine_);
}

bool RotaryModel::canSwitch() const {
    return modeSwitchAvailable(machine_);
}

bool RotaryModel::surfacingAvailable() const {
    return app::rotary_actions::surfacingAvailable(machine_);
}

bool RotaryModel::probeZAvailable() const {
    return app::rotary_actions::probeZAvailable(machine_);
}

bool RotaryModel::alignYAvailable() const {
    return app::rotary_actions::alignYAvailable(machine_);
}

bool RotaryModel::mountingAvailable() const {
    return app::rotary_actions::mountingAvailable(machine_);
}

QString RotaryModel::enableConfirmation() const {
    return app::rotary_actions::enableConfirmation(grblHal());
}

bool RotaryModel::setRotaryMode(bool rotary) {
    if (!canSwitch() || rotary == machine_.rotaryMode() || !machine_.setRotaryMode(rotary)) {
        return false;
    }
    Q_EMIT machine_.notice(rotary ? tr("Rotary Mode Enabled") : tr("Rotary Mode Disabled"));
    return true;
}

bool RotaryModel::runProbe(bool yAlignment) {
    const bool available = yAlignment ? alignYAvailable() : probeZAvailable();
    if (!available || !machine_.runRotaryProbe(yAlignment)) {
        return false;
    }
    Q_EMIT machine_.notice(tr("Running %1 probing commands").arg(yAlignment ? tr("Y-Axis Alignment") : tr("Rotary Z-Axis")));
    return true;
}

QString RotaryModel::mountingImage(bool linesUp, int holes) const {
    // getIllustrationImage(): the custom boring layout unless the track
    // lines up; then the standard track or the one with the extension.
    const char* name = !linesUp      ? "custom-boring-track-top-view.png"
                       : holes == 10 ? "extension-track-top-view.png"
                                     : "standard-track-top-view.png";
    return QString("qrc:/images/rotary/") + name;
}

bool RotaryModel::loadMounting(bool linesUp, bool quarterInchBit, int holes, bool longExtension) {
    controller::Controller* c = machine_.controller();
    const std::optional<std::string> program = rotary::mountingProgram(
        {.linesUp = linesUp, .quarterInchBit = quarterInchBit, .holes = holes, .longExtension = longExtension});
    if (!program || (c && c->workflow().isRunning())) {
        return false;
    }
    machine_.loadProgram("gSender_Rotary_Mounting_Setup", *program);
    Q_EMIT machine_.notice(tr("Loaded rotary mounting setup macro"));
    return true;
}

}  // namespace gs::ui
