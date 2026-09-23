#pragma once

// The DRO's moves to places: the machine's corners and park position, Go To
// Location, typed work positions and single-axis homing. Ported from
// src/app/src/features/DRO (utils/RapidPosition.ts, utils/DRO.ts,
// component/Parking.tsx, component/GoTo.tsx) and the settings' location
// input (Config/components/SettingInputs/LocationInput.tsx).

#include <string>
#include <string_view>
#include <vector>

namespace gs::controller {

// ---- corners (RapidPosition.ts) ----

enum class MachineCorner { BackRight, BackLeft, FrontRight, FrontLeft, Center, Other };

// getHomingLocation(): the corner the machine homes to from $23's XYZ bits
// - 0 back right, 1 back left, 2 front right, 3 front left, else Other (Z
// homing upwards is inverted: nothing is computed for it). Unlike the
// server's homing.js, which reads 4-7 as back right.
MachineCorner homingCorner(std::string_view homingDirMask);

// The firmware settings the corner and park moves read; empty means not
// reported (upstream reads `undefined`).
struct LocationSettings {
    std::string homing;         // $22
    std::string homingDirMask;  // $23
    std::string pullOff;        // $27
    std::string xMaxTravel;     // $130
    std::string yMaxTravel;     // $131

    // Number($27), 1 when not reported.
    double pullOffDistance() const;
    // $22 bit 3: homing sets the machine origin to 0 (grblHAL).
    bool homingSetsOrigin() const;
};

// getMovementGCode(): up to 1 mm below the top of Z (to the pull-off when
// homing does not set the origin), then, in machine coordinates, to
// `corner`: the pull-off in from the switches at the homing corner, max
// travel less the pull-off at the far sides, Center halfway. Without the
// homing flag every corner is computed as if the machine homed back right;
// grblHAL boards use $22 bit 3 as the flag. `pullOff` is the DRO's
// Number($27 ?? 1). Empty when the limits ($130/$131 less the pull-off) are
// missing or zero, or the homing corner is Other (upstream shows an error).
std::vector<std::string> cornerCommands(MachineCorner corner, const LocationSettings& settings, bool homingFlag,
                                        double pullOff, bool grblHal);

// ---- park and stored locations ----

struct MachineLocation {  // machine coordinates, mm
    double x = 0;
    double y = 0;
    double z = 0;
};

// goToParkLocation() (Parking.tsx): up as for the corners, to the park X/Y,
// then down to its Z - "G53 G21 G0 ..." lines, sent with `gcode`.
std::vector<std::string> parkCommands(const MachineLocation& park, const LocationSettings& settings);
// LocationInput's "Go To": the same moves without the G21 words.
std::vector<std::string> locationCommands(const MachineLocation& location, const LocationSettings& settings);

// ---- Go To Location (GoTo.tsx) ----

enum class GoToMode {
    Absolute,     // "ABS": work coordinates
    Incremental,  // "INC": relative to here
    Machine,      // "MCS": machine coordinates (homing enabled and homed)
};

struct GoToLocation {
    GoToMode mode = GoToMode::Absolute;
    // The target (or the distances), in the workspace units; A in degrees.
    double x = 0;
    double y = 0;
    double z = 0;
    double a = 0;
    bool yAvailable = true;   // false in rotary mode
    bool aAvailable = false;  // a grblHAL board reporting an A axis (or rotary mode)
    bool metric = true;       // the workspace units: the lines run in G21 or G20
    bool homingEnabled = false;
    double safeRetractHeight = 0;  // mm (the settings store it in mm)
    double machineZ = 0;           // mm
    double workZ = 0;              // the work Z before the move, in the workspace units
};

// goToLocation(): lines for gcode:safe in the workspace units (G21/G20).
// ABS/INC: the safe retract (with homing, up to machine Z -|height| when
// below it; else a relative lift), the XY(A) move in G90/G91, then Z - back
// to the starting height plus the Z distance after a relative lift in INC,
// else to (or by) Z. MCS: one "G53 G0" move of X, Y and A; Z is not moved
// and nothing retracts (as upstream). An INC move leaves G91 modal (as
// upstream: gcode:safe restores only the units).
//
// Deviation: upstream writes the retract height, a mm figure, into G20
// lines in an inch workspace (a 10 mm lift became 10 in) and adds the mm
// work Z to an inch target; here the retract is converted to inches and
// workZ is taken in the workspace units.
std::vector<std::string> goToLocationCommands(const GoToLocation& location);

// ---- typed work positions and homing (DRO.ts) ----

// handleManualOffset(): make the current position read `value` on `axis` -
// "G10 P0 L20 X12.5", run with gcode:safe in the workspace units.
std::string manualOffsetCommand(char axis, double value);
// homeAxis(): "$HX" (single-axis homing: grblHAL, $22 bit 1).
std::string homeAxisCommand(char axis);
// The DRO shows the single-axis homing switch for $22 bit 1.
bool singleAxisHomingEnabled(std::string_view homing);

// ---- the visualizer's machine bed (RapidPosition.ts) ----

struct WorkRect {  // work coordinates, mm
    double minX = 0;
    double minY = 0;
    double maxX = 0;
    double maxY = 0;
};

// computeMachineBedWorkRect(): the travel from the homing corner ($23) away
// from it (getAxisMaximumLocation), `width` by `depth`, in the work
// coordinates of a workspace offset `wco` (machine minus work position).
WorkRect machineBedWorkRect(std::string_view homingDirMask, double width, double depth, double wcoX, double wcoY);
// computeKeepoutWorkRect(): grblHAL's ATC keepout ($684-$687 are machine
// coordinates already).
WorkRect keepoutWorkRect(double xMin, double xMax, double yMin, double yMax, double wcoX, double wcoY);

}  // namespace gs::controller
