#include "status_model.hpp"

#include "backend.hpp"
#include "machine.hpp"

#include "gs/controller/actions.hpp"
#include "gs/controller/controller.hpp"
#include "gs/util/strings.hpp"

#include <QVariantMap>

namespace gs::ui {
namespace {

// MachineInfoDisplay's pins: label and the status report's letter.
struct Pin {
    const char* label;
    char letter;
};
constexpr Pin kPins[] = {
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "X limit"), 'X'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Y limit"), 'Y'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Z limit"), 'Z'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "A limit"), 'A'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Probe/TLS"), 'P'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Door"), 'D'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Cycle start"), 'S'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Hold"), 'H'},
    {QT_TRANSLATE_NOOP("gs::ui::StatusModel", "Soft reset"), 'R'},
};

}  // namespace

StatusModel::StatusModel(QObject* parent) : QObject(parent), machine_(UiBackend::instance()->machine()) {
    for (auto signal : {&app::Machine::stateChanged, &app::Machine::connectionChanged,
                        &app::Machine::settingsChanged}) {
        connect(&machine_, signal, this, &StatusModel::changed);
    }
}

bool StatusModel::connected() const {
    return machine_.isConnected();
}

bool StatusModel::alarm() const {
    controller::Controller* c = machine_.controller();
    return c && c->state().status.activeState == "Alarm";
}

bool StatusModel::alarmButtonHomes() const {
    controller::Controller* c = machine_.controller();
    return c && controller::alarmButtonHomes(c->state().status.activeState, c->state().status.alarmCode);
}

bool StatusModel::lockActive() const {
    controller::Controller* c = machine_.controller();
    const std::string state = c ? c->state().status.activeState : std::string();
    return state == "Hold" || state == "Alarm";
}

QString StatusModel::firmwareVersion() const {
    controller::Controller* c = machine_.controller();
    const std::string version = c ? std::string(str::trim(c->settings().version)) : std::string();
    return version.empty() ? tr("disconnected") : QString::fromStdString(version);
}

QVariantList StatusModel::modals() const {
    controller::Controller* c = machine_.controller();
    const protocol::ModalState* modal = c ? &c->runner().modal() : nullptr;
    std::string coolant;
    if (modal) {
        for (const std::string& code : modal->coolant) {
            coolant += (coolant.empty() ? "" : " ") + code;
        }
    }
    // The port's probe routines always use G38.2 (upstream's probeCommand).
    const std::pair<QString, std::string> rows[] = {
        {tr("Probe style"), "G38.2"},
        {tr("Coordinate system"), modal ? modal->wcs : ""},
        {tr("Plane selection"), modal ? modal->plane : ""},
        {tr("Units"), modal ? modal->units : ""},
        {tr("Distance mode"), modal ? modal->distance : ""},
        {tr("Feed"), modal ? modal->feedrate : ""},
        {tr("Spindle"), modal ? modal->spindle : ""},
        {tr("Coolant"), coolant},
    };
    QVariantList list;
    for (const auto& [label, value] : rows) {
        list.append(QVariantMap{{"label", label}, {"value", c ? QString::fromStdString(value) : QStringLiteral("-")}});
    }
    return list;
}

QVariantList StatusModel::pins() const {
    controller::Controller* c = machine_.controller();
    const std::string pins = c ? c->state().status.pinState : std::string();
    QVariantList list;
    for (const Pin& pin : kPins) {
        list.append(QVariantMap{{"label", tr(pin.label)}, {"on", pins.find(pin.letter) != std::string::npos}});
    }
    return list;
}

int StatusModel::tool() const {
    controller::Controller* c = machine_.controller();
    return c ? c->state().status.currentTool : -1;
}

bool StatusModel::stepperLocked() const {
    return machine_.stepperLocked();
}

bool StatusModel::act(int actionValue, bool repopulate) {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    const auto action = static_cast<controller::UnlockAction>(actionValue);
    if (action == controller::UnlockAction::ConfirmHomingFailure) {
        pendingRepopulate_ = repopulate;
        return true;
    }
    controller::runUnlockAction(*c, action);
    if (repopulate) {
        c->populateConfig();
    }
    return false;
}

bool StatusModel::clickAlarmButton() {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    const auto& status = c->state().status;
    return act(static_cast<int>(controller::alarmButtonAction(status.activeState, status.alarmCode)), false);
}

bool StatusModel::clickLock() {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return false;
    }
    const auto& status = c->state().status;
    return act(static_cast<int>(controller::lockIconAction(status.activeState, status.alarmCode)),
               controller::lockIconRepopulates(status.activeState, status.alarmCode));
}

void StatusModel::resolveHomingFailure(const QString& choice) {
    controller::Controller* c = machine_.controller();
    // Deviation: cancelling does nothing; upstream's dialog unlocked on any close.
    if (!c || choice == "cancel") {
        return;
    }
    controller::runUnlockAction(*c, choice == "rehome" ? controller::UnlockAction::Home
                                                       : controller::UnlockAction::Unlock);
    if (pendingRepopulate_) {
        c->populateConfig();
    }
}

QString StatusModel::homingFailureText() const {
    controller::Controller* c = machine_.controller();
    QString text = tr("The last homing cycle failed, so the machine position is unknown. Re-home the machine before "
                      "continuing. Unlocking without re-homing may let jogging or a job run past the limit switches.");
    if (c && controller::isLimitSwitchFaultAlarm(c->state().status.alarmCode)) {
        text += "\n\n" + tr("ALARM:8 and ALARM:9 mean a limit switch was not found or would not release, so "
                            "re-homing will keep failing until the switch or its wiring is fixed. To use the machine "
                            "without homing until then: choose Unlock Anyway, turn off \"Homing cycle enable\" ($22) "
                            "in Config and apply.");
    }
    return text;
}

void StatusModel::showAlarmHelp() {
    controller::Controller* c = machine_.controller();
    if (!c) {
        return;
    }
    const std::string& code = c->state().status.alarmCode;
    UiBackend::instance()->showHelper(tr("Alarm Code %1").arg(QString::fromStdString(code)),
                                      machine_.alarmDescription(code).toHtmlEscaped(),
                                      "https://resources.sienci.com/view/gs-gsender-grbl-alarm-error-codes/#alarms");
}

void StatusModel::setStepperLock(bool lock) {
    if (machine_.controller()) {
        machine_.setStepperLock(lock);
        Q_EMIT changed();
    }
}

}  // namespace gs::ui
