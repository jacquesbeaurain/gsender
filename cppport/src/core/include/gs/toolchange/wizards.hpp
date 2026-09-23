#pragma once

// gSender's tool change wizards (src/app/src/wizards): the step-by-step
// instructions the application shows when a job reaches M6 with the
// "Standard Re-zero", "Flexible Re-zero" or "Fixed Tool Sensor" strategy.
// Each wizard has start-up G-code (stores the position and modals in
// global.toolchange.*), steps of instructions, and actions whose G-code the
// operator runs; the last one ends with %toolchange_complete, which resumes
// the job. The content matches upstream (tests/data/toolchange_golden.json).

#include "gs/probe/probing.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gs::toolchange {

struct WizardAction {
    std::string label;
    std::vector<std::string> gcode;
};

struct WizardSubstep {
    std::string title;
    std::string description;
    bool toolBanner = false;  // shows the tool to load
    std::vector<WizardAction> actions;
};

struct WizardStep {
    std::string title;
    std::vector<WizardSubstep> substeps;
};

struct Wizard {
    std::string title;
    std::string intro;
    std::vector<std::string> start;
    // The Fixed Tool Sensor wizards send their start with the gcode command
    // right away; the others through wizard:start (after the tool change
    // pause settles).
    bool startDirect = false;
    std::vector<WizardStep> steps;
};

// getProbeSettings(): the touch plate's thickness for a Z probe (BitZero
// flat on the surface) and the probe feeds, retract and distance (mm).
struct ProbeSettings {
    double zProbeThickness = 15;
    double zProbeDistance = 30;
    double fastSpeed = 150;
    double slowSpeed = 75;
    double retract = 2;
};
ProbeSettings toolChangeProbeSettings(const probe::ProbeSettings& settings);

// What the wizards read from the machine.
struct MachineFacts {
    std::string reportInches = "0";  // $13
    std::string softLimits = "0";    // $20
    std::string zMaxTravel;          // $132
    double machineZ = 0;             // MPos Z
    std::string tool = "-";          // the parser state's tool number
};

struct MachinePosition {
    double x = 0;
    double y = 0;
    double z = 0;
};

// Standard Re-zero: change the bit, re-zero Z with the touch plate (or the
// paper method), resume.
Wizard standardRezero(const ProbeSettings& probe, const MachineFacts& machine);
// Flexible Re-zero: the first change measures the current tool's offset on
// the plate, every change probes the new tool there and applies it.
Wizard flexibleRezero(int count, const ProbeSettings& probe, const MachineFacts& machine);
// Fixed Tool Sensor: measure at the sensor (machine position), changing
// the bit at the manual tool change position first when `moveToManual`
// (the position is stored in the start-up G-code either way).
Wizard fixedToolSensor(int count, const ProbeSettings& probe, const MachineFacts& machine,
                       const MachinePosition& sensor, const MachinePosition& manualPosition, bool moveToManual);
// The first-tool variant that only measures the current tool.
Wizard probeToolLength(const ProbeSettings& probe, const MachineFacts& machine, const MachinePosition& sensor);

// The first tool with a fixed sensor (workspace.toolChange.firstToolBehaviour).
inline constexpr const char* kFirstToolBehaviours[] = {"Always run full wizard", "Prompt for first tool",
                                                       "Always probe length only"};

}  // namespace gs::toolchange
