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
