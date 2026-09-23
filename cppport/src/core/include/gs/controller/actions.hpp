#pragma once

// What gSender's widgets do with the controller - the job control buttons,
// the DRO's zeroing and go-to-zero, the workspace shortcuts, the status
// area's unlock buttons - ported from the React components so buttons and
// keyboard shortcuts share one set of rules.

#include "gs/controller/streaming.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace gs::controller {

class Controller;

// ---- job control (JobControl/ControlButton.tsx) ----

// Start/resume: the machine idle, held or in check mode, the job not running.
bool canRun(std::string_view activeState, WorkflowState workflow);
// Pause: the job running and the machine moving.
bool canPause(std::string_view activeState, WorkflowState workflow);
// Stop: a job running or paused.
bool canStop(WorkflowState workflow);

// handleRun(): resume a paused or held job, else start it (in check mode,
// remembering the work coordinate system first).
void runJob(Controller& c);
void pauseJob(Controller& c);
// handleStop(): a forced stop, then out of check mode ($C) if in it.
void stopJob(Controller& c);
// The Stop shortcut with no job to stop: cancels a jog, else resets
// (grblHAL: soft reset); nothing when idle.
void stopWithoutJob(Controller& c);

// ---- positions (DRO/utils/DRO.ts) ----

// zeroWCS(): "G10 L20 P0 X0".
std::string zeroAxisCommand(char axis, double value = 0);
// zeroAllAxes(): X, Y and Z, then A on a grblHAL board that has one (two
// separate commands, as upstream sends them).
std::vector<std::string> zeroAllCommands(bool grblHal, bool hasA);
// gotoZero(axis) / goXYAxes() for `axes` "X", "Y", "Z", "A" or "XY": with a
// safe retract height, Z first lifts (to machine Z -|height| with homing,
// else by the height, lowering again afterwards). Run with gcode:safe in mm.
std::vector<std::string> goToZeroCommands(std::string_view axes, bool homingEnabled, double safeRetractHeight,
                                          double machineZ);

// ---- workspace shortcuts (workspace/index.tsx) ----

enum class ControllerCommand {
    ResetLimit,              // "Unlock"
    Reset,                   // "Soft reset"
    Homing,                  // "Home machine"
    RealtimeReport,          // grblHAL
    ErrorClear,              // grblHAL
    ToolChangeAcknowledge,   // grblHAL
    VirtualStopToggle,       // grblHAL "Feed hold"
};

// CONTROLLER_COMMAND: runs `command` only where upstream allows it - reset,
// unlock and homing in an alarm, the tool change acknowledgement in the Tool
// state, anything but unlock when idle. In an alarm other than 1 or 2 (hard
// and soft limits) every allowed command just unlocks, as upstream does.
// False when nothing was sent.
bool runControllerCommand(Controller& c, ControllerCommand command);

// ---- the status area (MachineStatus, UnlockButton) ----

// The state as the status area names it: "Run" is "Running", "Jog"
// "Jogging", "Home" "Homing", "Tool" "Tool Change"; "Disconnected" when
// there is none.
std::string statusLabel(std::string_view activeState);

// ALARM 6-9: the homing cycle failed, so the position cannot be trusted.
bool isHomingFailureAlarm(std::string_view alarmCode);
// ALARM 8 and 9: a limit switch was not found or would not release.
bool isLimitSwitchFaultAlarm(std::string_view alarmCode);

enum class UnlockAction {
    ResetLimit,            // reset:limit - soft reset, then $X
    Home,                  // the homing cycle
    ConfirmHomingFailure,  // ask first: re-home, or unlock anyway
    Unlock,                // $X
    CycleStart,            // ~
};

// The button under the status in an alarm (MachineStatus's unlock()):
// limit-type alarms (1, 2, 10, 14, 17) reset, the homing lock ("Homing",
// 11) homes, a failed homing cycle (6-9) asks, others unlock; in a hold it
// resumes.
UnlockAction alarmButtonAction(std::string_view activeState, std::string_view alarmCode);
// Its label says "Run Homing" rather than "Unlock Machine".
bool alarmButtonHomes(std::string_view activeState, std::string_view alarmCode);
// The lock icon beside the status (UnlockButton's unlockFirmware()): in an
// alarm 10 and 17 reset, 6-9 ask, anything else unlocks - the homing lock
// too, re-reading the configuration afterwards (lockIconRepopulates); in
// any other state it sends a cycle start.
UnlockAction lockIconAction(std::string_view activeState, std::string_view alarmCode);
bool lockIconRepopulates(std::string_view activeState, std::string_view alarmCode);
// Sends `action`; ConfirmHomingFailure is the caller's to resolve (into
// Home or Unlock) and sends nothing.
void runUnlockAction(Controller& c, UnlockAction action);

}  // namespace gs::controller
