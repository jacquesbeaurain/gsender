#include "jog_model.hpp"

#include "backend.hpp"
#include "jogger.hpp"
#include "machine.hpp"

#include "gs/controller/controller.hpp"

namespace gs::ui {

JogModel::JogModel(QObject* parent)
    : QObject(parent), machine_(UiBackend::instance()->machine()), jogger_(UiBackend::instance()->jogger()) {
    connect(&jogger_, &app::Jogger::changed, this, &JogModel::changed);
    for (auto signal : {&app::Machine::appSettingsChanged, &app::Machine::stateChanged,
                        &app::Machine::connectionChanged, &app::Machine::workflowChanged}) {
        connect(&machine_, signal, this, &JogModel::changed);
    }
}

QString JogModel::preset() const {
    switch (jogger_.preset()) {
        case controller::JogPreset::Rapid: return QStringLiteral("Rapid");
        case controller::JogPreset::Normal: return QStringLiteral("Normal");
        case controller::JogPreset::Precise: return QStringLiteral("Precise");
    }
    return {};
}

double JogModel::xyStep() const {
    return jogger_.speeds().xyStep;
}

double JogModel::zStep() const {
    return jogger_.speeds().zStep;
}

double JogModel::aStep() const {
    return jogger_.speeds().aStep;
}

double JogModel::feedrate() const {
    return jogger_.speeds().feedrate;
}

QString JogModel::units() const {
    return jogger_.metric() ? QStringLiteral("mm") : QStringLiteral("in");
}

bool JogModel::canJog() const {
    controller::Controller* c = machine_.controller();
    return c && machine_.isConnected() && !c->workflow().isRunning() && c->state().status.activeState != "Alarm";
}

bool JogModel::connected() const {
    return machine_.isConnected();
}

bool JogModel::rotaryMode() const {
    return machine_.rotaryMode();
}

bool JogModel::showA() const {
    controller::Controller* c = machine_.controller();
    const app::AppSettings& settings = machine_.settings();
    return (((c && c->isGrblHal()) || settings.rotary.rotaryMode) && settings.rotary.showControls) ||
           settings.preferences.useAaxisForGrbl;
}

void JogModel::selectPreset(const QString& preset) {
    jogger_.selectPreset(preset == "Rapid"     ? controller::JogPreset::Rapid
                         : preset == "Precise" ? controller::JogPreset::Precise
                                               : controller::JogPreset::Normal);
}

void JogModel::setField(const QString& field, double value) {
    if (!(value > 0)) {
        return;
    }
    controller::JogSpeeds speeds = jogger_.speeds();
    if (field == "xy") {
        speeds.xyStep = value;
    } else if (field == "z") {
        speeds.zStep = value;
    } else if (field == "a") {
        speeds.aStep = value;
    } else if (field == "feedrate") {
        speeds.feedrate = value;
    } else {
        return;
    }
    jogger_.setSpeeds(speeds);
}

void JogModel::nudge(const QString& field, bool increment) {
    const controller::JogSpeeds& speeds = jogger_.speeds();
    const double current = field == "xy"  ? speeds.xyStep
                           : field == "z" ? speeds.zStep
                           : field == "a" ? speeds.aStep
                                          : speeds.feedrate;
    setField(field, controller::jogInputNudge(current, increment));
}

void JogModel::press(int x, int y, int z) {
    if (!canJog()) {
        return;
    }
    controller::JogAxes directions;
    if (x != 0) {
        directions.emplace_back('X', x);
    }
    if (y != 0 && !rotaryMode()) {
        directions.emplace_back('Y', y);
    }
    if (z != 0) {
        directions.emplace_back('Z', z);
    }
    if (!directions.empty()) {
        jogger_.press(directions);
    }
}

void JogModel::pressA(int direction) {
    if (canJog() && direction != 0) {
        jogger_.pressRotary(direction);
    }
}

void JogModel::release() {
    jogger_.release();
}

void JogModel::stop() {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    jogger_.release();
    const std::string& state = c->state().status.activeState;
    if (state == "Jog") {
        c->jogCancel();
    } else if (state != "Idle" && !state.empty()) {
        c->isGrblHal() ? c->resetSoft() : c->reset();
    }
}

}  // namespace gs::ui
