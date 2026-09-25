#include "rotary_actions.hpp"

#include "machine.hpp"

#include "gs/controller/controller.hpp"

#include <QCoreApplication>

namespace gs::app::rotary_actions {
namespace {

// Connected, idle, no job running.
bool machineIdle(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && !c->workflow().isRunning() && c->state().status.activeState == "Idle";
}

QString tr(const char* text) {
    return QCoreApplication::translate("RotaryPanel", text);
}

}  // namespace

bool isGrbl(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c ? c->isGrbl() : machine.settings().defaultFirmware == protocol::Firmware::Grbl;
}

bool surfacingAvailable(Machine& machine) {
    return !isGrbl(machine) || machine.rotaryMode();
}

bool probeZAvailable(Machine& machine) {
    return machineIdle(machine) && surfacingAvailable(machine);
}

bool alignYAvailable(Machine& machine) {
    return machineIdle(machine) && !machine.rotaryMode();
}

bool mountingAvailable(Machine& machine) {
    return machineIdle(machine) && !machine.rotaryMode();
}

bool modeSwitchAvailable(Machine& machine) {
    controller::Controller* c = machine.controller();
    return c && !c->workflow().isRunning();
}

QString enableConfirmation(bool grblHal) {
    const QString actions =
        grblHal ? tr("<li>Zero the Y-Axis in its current position</li>"
                     "<li>Switch these A and Y axis settings:<ul>"
                     "<li>$101 and $103 (travel resolution)</li><li>$111 and $113 (maximum rate)</li>"
                     "<li>$121 and $123 (acceleration)</li><li>$131 and $133 (travel amount)</li></ul></li>")
                : tr("<li>Zero the Y-Axis in its current position</li>"
                     "<li>Turn soft and hard limits off, if they are on</li>"
                     "<li>Update the following firmware values:<ul>"
                     "<li>$101 (Y-Axis travel resolution)</li><li>$111 (Y-Axis maximum rate)</li></ul></li>");
    const QString wiring =
        grblHal ? QString() : tr("<p>Please make sure you have switched over your wiring for the new setup.</p>");
    return tr("<p>Enabling rotary mode will perform the following actions:</p><ol>%1</ol>%2").arg(actions, wiring);
}

}  // namespace gs::app::rotary_actions
