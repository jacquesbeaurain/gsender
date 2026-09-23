#pragma once

// What gSender's widgets do with the controller - the job control buttons,
// the DRO's zeroing and go-to-zero, the workspace shortcuts - ported from
// the React components so buttons and keyboard shortcuts share one set of
// rules.

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

}  // namespace gs::controller
