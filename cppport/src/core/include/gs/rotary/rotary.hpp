#pragma once

// Rotary (4th axis) support from gSender's Rotary widget: the rotary
// surfacing generator (features/Rotary/utils/Generator.ts,
// StockTurningGenerator), the rotary probing routines (utils/
// probeCommands.ts), switching the workspace in and out of rotary mode
// (lib/rotary.tsx, updateWorkspaceMode) and the mounting setup programs
// that bore the rotary track's holes (MountingSetup.tsx; the programs are
// extracted into resources/data/rotary_mounting.json).

#include "gs/protocol/types.hpp"

#include <optional>
#include <string>
#include <vector>

namespace gs::rotary {

// ---- rotary surfacing ----

// widgets.rotary.stockTurning.options (the dialog's values, in the
// workspace units): the stock's length along X and its diameters.
struct StockTurningOptions {
    double stockLength = 100;
    double stepdown = 20;
    double bitDiameter = 6.35;
    double spindleRPM = 17000;
    double feedrate = 3000;
    double stepover = 15;      // % of the bit
    double startHeight = 50;   // the stock's diameter now
    double finalHeight = 40;   // the diameter to turn it down to
    bool enableRehoming = false;
    bool shouldDwell = false;
    int toolNumber = 0;
};

// StockTurningGenerator.generate(): the program as one text (its lines
// joined with "\n", blank spacer lines included). The heights are halved
// into radii; one pass (without rehoming) runs as two half spirals, more as
// full spirals alternating direction. `metric`: the workspace units;
// `rotaryMode`: the workspace is in rotary mode (A values in inches then
// divide by 25.4 in an inch workspace, as upstream's processValue()).
// A stepdown that is not positive cuts one layer (upstream never ends).
std::string stockTurningProgram(const StockTurningOptions& options, bool metric, bool rotaryMode);

// ---- rotary probing ----

// getZAxisProbing() / getYAxisAlignmentProbing(): the routines the Rotary
// widget runs with gcode:safe in `$13`'s units (upstream's getUnitModal),
// their distances divided into inches (3 decimals) then. Lines trimmed.
std::vector<std::string> zAxisProbing(bool reportInches);
std::vector<std::string> yAxisAlignmentProbing(bool reportInches);

// ---- rotary mode ----

// ROTARY_MODE_FIRMWARE_SETTINGS / DEFAULT_FIRMWARE_SETTINGS: what Grbl's Y
// (driving the rotary) gets in rotary mode, and what it had before
// (restored on leaving; saved on entering).
using FirmwareValues = std::vector<std::pair<std::string, std::string>>;
const FirmwareValues& rotaryFirmwareSettings();
const FirmwareValues& defaultFirmwareSettings();

struct ModeSwitch {
    bool enable = true;
    bool grblHal = false;
    // Grbl: the rotary values (entering) or the saved ones (leaving).
    FirmwareValues grblSettings;
};

// updateWorkspaceMode(): Grbl - Y zeroed (entering), the settings written,
// $$, then the toggle macro (a dwell and G0 G90 Y[posy]); grblHAL - Y zeroed
// and the A and Y axis settings ($101/$103, $111/$113, $121/$123,
// $131/$133) swapped, $$, the macro (the controller's rotary mode switches
// with it). `settings` are the board's for the swap. Deviation: upstream
// writes "undefined" for a setting the board does not have; such a pair is
// left out.
std::vector<std::string> modeSwitchCommands(const ModeSwitch& change, const protocol::OrderedMap& settings);
// The values to save on entering rotary mode on Grbl ($101, $111, $20, $21).
FirmwareValues currentFirmwareValues(const protocol::OrderedMap& settings);

// ---- mounting setup ----

struct MountingSetup {
    bool linesUp = false;       // the mounting track lines up without interference
    bool quarterInchBit = true; // else 1/8"
    int holes = 6;              // 6 (standard track) or 10 (with the extension)
    bool longExtension = false; // the extension is 460 mm (else 400 mm)
};

// The program's HOLE_TYPES name, as handleSubmit() chooses it.
std::string mountingProgramName(const MountingSetup& setup);
// The program itself (from the embedded data); nullopt if it is missing.
std::optional<std::string> mountingProgram(const MountingSetup& setup);

}  // namespace gs::rotary
