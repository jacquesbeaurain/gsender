#pragma once

// The Rotary tab's rules (features/Rotary: Toggle, Actions), shared by both
// UIs: when its buttons work, and what entering rotary mode asks first.

#include <QString>

namespace gs::app {

class Machine;

namespace rotary_actions {

// The board's firmware is Grbl (the default one while disconnected).
bool isGrbl(Machine& machine);
// Upstream's rules: Grbl has the rotary only in rotary mode (on Y); the Y
// alignment and the mounting setup are for the machine outside it; the
// probing and mounting buttons need an idle machine with no job running.
bool surfacingAvailable(Machine& machine);
bool probeZAvailable(Machine& machine);
bool alignYAvailable(Machine& machine);
bool mountingAvailable(Machine& machine);
// Deviation: upstream's switch only needs a connection; here a running job
// keeps its firmware settings.
bool modeSwitchAvailable(Machine& machine);
// updateWorkspaceMode()'s confirmation for entering rotary mode, by
// firmware (rich text).
QString enableConfirmation(bool grblHal);

}  // namespace rotary_actions
}  // namespace gs::app
