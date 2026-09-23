#include "gs/controller/actions.hpp"

#include "gs/controller/controller.hpp"
#include "gs/util/jsnumber.hpp"

#include <cctype>
#include <cmath>

namespace gs::controller {

bool canRun(std::string_view activeState, WorkflowState workflow) {
    return (activeState == "Idle" || activeState == "Hold" || activeState == "Check") &&
           workflow != WorkflowState::Running;
}

bool canPause(std::string_view activeState, WorkflowState workflow) {
    return workflow == WorkflowState::Running && activeState == "Run";
}

bool canStop(WorkflowState workflow) {
    return workflow == WorkflowState::Running || workflow == WorkflowState::Paused;
}

void runJob(Controller& c) {
    const std::string& activeState = c.state().status.activeState;
    const WorkflowState workflow = c.workflow().state();
    if (workflow == WorkflowState::Paused || activeState == "Hold") {
        c.resume();
        return;
    }
    if (workflow == WorkflowState::Idle) {
        if (activeState == "Check") {
            c.gcode("%global.state.testWCS=modal.wcs");  // test in the current WCS
        }
        c.start();
    }
}

void pauseJob(Controller& c) {
    c.pause();
}

void stopJob(Controller& c) {
    const bool checking = c.state().status.activeState == "Check";
    c.stop(/*force=*/true);
    if (checking) {
        c.gcode("$C");
    }
}

void stopWithoutJob(Controller& c) {
    const std::string& activeState = c.state().status.activeState;
    if (activeState == "Jog") {
        c.jogCancel();
    } else if (activeState == "Idle") {
        return;
    } else if (c.isGrblHal()) {
        c.resetSoft();
    } else {
        c.reset();
    }
}

std::string zeroAxisCommand(char axis, double value) {
    return std::string("G10 L20 P0 ") + static_cast<char>(std::toupper(static_cast<unsigned char>(axis))) +
           js::numberToString(value);
}

std::vector<std::string> zeroAllCommands(bool grblHal, bool hasA) {
    std::vector<std::string> commands{"G10 L20 P0 X0 Y0 Z0"};
    if (grblHal && hasA) {
        commands.emplace_back("G10 L20 P0 A0");
    }
    return commands;
}

std::vector<std::string> goToZeroCommands(std::string_view axes, bool homingEnabled, double safeRetractHeight,
                                          double machineZ) {
    const bool xy = axes == "XY";
    const bool lift = safeRetractHeight != 0 && (xy || axes != "Z");
    std::vector<std::string> commands;
    if (lift) {
        if (homingEnabled) {
            // Only lift when below the safe height (machine Z counts down).
            const double retract = std::fabs(safeRetractHeight) * -1;
            if (machineZ < retract) {
                commands.push_back("G53 G0 Z" + js::numberToString(retract));
            }
        } else {
            commands.emplace_back("G91");
            commands.push_back("G0Z" + js::numberToString(safeRetractHeight));
        }
    }
    commands.push_back(xy ? std::string("G90 G0 X0 Y0") : "G90 G0 " + std::string(axes) + "0");
    if (lift && !homingEnabled) {
        commands.push_back("G91 G0 Z" + js::numberToString(safeRetractHeight * -1));
        commands.emplace_back("G90");
    }
    return commands;
}

namespace {

// The alarm code as upstream's number; -1 for "Homing" or none.
int alarmNumber(std::string_view code) {
    const double number = js::stringToNumber(code);
    return !code.empty() && std::isfinite(number) ? static_cast<int>(number) : -1;
}

bool isHomingLock(std::string_view code) {
    return code == "Homing" || alarmNumber(code) == 11;
}

}  // namespace

std::string statusLabel(std::string_view activeState) {
    if (activeState.empty()) {
        return "Disconnected";
    }
    if (activeState == "Run") {
        return "Running";
    }
    if (activeState == "Jog") {
        return "Jogging";
    }
    if (activeState == "Home") {
        return "Homing";
    }
    if (activeState == "Tool") {
        return "Tool Change";
    }
    return std::string(activeState);  // Idle, Hold, Check, Sleep, Alarm, Door
}

bool isHomingFailureAlarm(std::string_view alarmCode) {
    const int code = alarmNumber(alarmCode);
    return code >= 6 && code <= 9;
}

bool isLimitSwitchFaultAlarm(std::string_view alarmCode) {
    const int code = alarmNumber(alarmCode);
    return code == 8 || code == 9;
}

UnlockAction alarmButtonAction(std::string_view activeState, std::string_view alarmCode) {
    if (activeState == "Alarm") {
        const int code = alarmNumber(alarmCode);
        if (code == 1 || code == 2 || code == 10 || code == 14 || code == 17) {
            return UnlockAction::ResetLimit;
        }
        if (isHomingLock(alarmCode)) {
            return UnlockAction::Home;
        }
        if (isHomingFailureAlarm(alarmCode)) {
            return UnlockAction::ConfirmHomingFailure;
        }
    } else if (activeState == "Hold") {
        return UnlockAction::CycleStart;
    }
    return UnlockAction::Unlock;
}

bool alarmButtonHomes(std::string_view activeState, std::string_view alarmCode) {
    return activeState == "Alarm" && isHomingLock(alarmCode);
}

UnlockAction lockIconAction(std::string_view activeState, std::string_view alarmCode) {
    if (activeState != "Alarm") {
        return UnlockAction::CycleStart;
    }
    const int code = alarmNumber(alarmCode);
    if (code == 17 || code == 10) {
        return UnlockAction::ResetLimit;
    }
    if (isHomingFailureAlarm(alarmCode)) {
        return UnlockAction::ConfirmHomingFailure;
    }
    return UnlockAction::Unlock;
}

bool lockIconRepopulates(std::string_view activeState, std::string_view alarmCode) {
    return activeState == "Alarm" && isHomingLock(alarmCode);
}

void runUnlockAction(Controller& c, UnlockAction action) {
    switch (action) {
        case UnlockAction::ResetLimit: c.resetLimit(); break;
        case UnlockAction::Home: c.home(); break;
        case UnlockAction::Unlock: c.unlock(); break;
        case UnlockAction::CycleStart: c.cycleStart(); break;
        case UnlockAction::ConfirmHomingFailure: break;
    }
}

bool runControllerCommand(Controller& c, ControllerCommand command) {
    const std::string& activeState = c.state().status.activeState;
    const std::string& alarmCode = c.state().status.alarmCode;
    const bool alarmCommand = command == ControllerCommand::Reset || command == ControllerCommand::ResetLimit ||
                              command == ControllerCommand::Homing;
    const bool allowed = (alarmCommand && activeState == "Alarm") ||
                         (command == ControllerCommand::ToolChangeAcknowledge && activeState == "Tool") ||
                         (command != ControllerCommand::ResetLimit && activeState == "Idle");
    if (!allowed) {
        return false;
    }
    // Hard and soft limit alarms (1, 2) get the command; others unlock.
    if (activeState == "Alarm" && alarmCode != "1" && alarmCode != "2") {
        c.unlock();
        return true;
    }
    switch (command) {
        case ControllerCommand::ResetLimit: c.resetLimit(); break;
        case ControllerCommand::Reset: c.reset(); break;
        case ControllerCommand::Homing: c.home(); break;
        case ControllerCommand::RealtimeReport: c.realtimeReport(); break;
        case ControllerCommand::ErrorClear: c.errorClear(); break;
        case ControllerCommand::ToolChangeAcknowledge: c.toolChangeAcknowledge(); break;
        case ControllerCommand::VirtualStopToggle: c.virtualStopToggle(); break;
    }
    return true;
}

}  // namespace gs::controller
