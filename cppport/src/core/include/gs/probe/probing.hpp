#pragma once

// Touch plate probing (gSender's src/app/src/lib/Probing.ts and the option
// building in src/app/src/features/Probe/index.tsx): plate and machine
// settings in, G-code lines out. The routines use the controller's
// "%NAME=expression" assignments and "[expression]" words, so they are run
// through Controller::gcodeSafe(), which evaluates them against the live
// machine state. The output matches upstream byte for byte; the golden cases
// in tests/data/probing_golden.json come from running the JavaScript.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::probe {

// The plate's corner on the stock, clockwise from bottom left. Upstream
// passes these as plain numbers (and prints them in BitZero comments).
enum Corner : int { kBottomLeft = 0, kTopLeft = 1, kTopRight = 2, kBottomRight = 3 };

// The corner after `corner`, wrapping from bottom right to bottom left.
int nextCorner(int corner);

enum class PlateType { StandardBlock, AutoZero, ZProbe, Probe3D, BitZero };
enum class ProbeType { Auto, Tip, Diameter };

// Upstream's names ("Standard Block", "AutoZero", ...), as stored in the
// configuration and shown in the UI.
std::string_view plateTypeName(PlateType type);
std::optional<PlateType> plateTypeFromName(std::string_view name);
std::string_view probeTypeName(ProbeType type);
std::optional<ProbeType> probeTypeFromName(std::string_view name);

struct Axes {
    bool x = false;
    bool y = false;
    bool z = false;
};

// Plate thicknesses per plate type (workspace.probeProfile.zThickness).
struct PlateThickness {
    double standardBlock = 15;
    double autoZero = 5;
    double zProbe = 15;
    double probe3D = 0;
    double bitZero = 13;         // 0 (or NaN) falls back to the BitZero V2 inset, 13
    double bitZeroZOnly = 15.5;  // 0 (or NaN) falls back to 15.5
};

// Probing.ts's ProbingOptions as the Probe widget fills it: lengths and feeds
// in the workspace units, except the AutoZero values, which are always mm.
struct ProbingOptions {
    std::string modal = "21";  // units G-code number: "21" mm, "20" inches
    bool metric = true;        // units === 'mm'
    double toolDiameter = 0;
    double tipDiameter3D = 2;
    double zRetractNormal = 2;
    double zRetractAuto = 1;
    double xyRetract3D = 10;
    double retract = 2;
    Axes axes;
    double probeDistanceX = 30;
    double probeDistanceY = 30;
    double probeDistanceZ = 30;
    double probeFast = 150;
    double probeSlow = 75;
    PlateThickness zThickness;
    double xyThickness = 10;
    double probeMovementSpeed = 0;      // workspace units; 0 moves back with G0
    double probeMovementSpeedAuto = 0;  // mm/min for the AutoZero routines
    bool grblHal = false;               // shorter dwell after each touch
    bool reportInches = false;          // $13=1: AutoZero moves by reported positions in G20
    PlateType plateType = PlateType::StandardBlock;
    ProbeType probeType = ProbeType::Diameter;
    bool homingEnabled = false;         // $22 != 0: cap the downward probe by the Z travel left
    // SoftLimits.getZDownTravel(): |$132| - |MPos Z| - 1 caps downward probes.
    double zMaxTravel = 0;  // Number($132)
    double machineZ = 0;    // machine Z position
};

// getProbeCode(): the routine for `options` with the plate on `corner`.
// Empty when no axis is selected.
std::vector<std::string> probeCode(const ProbingOptions& options, int corner);

// The Probe widget's settings (widgets.probe and workspace.probeProfile),
// stored in mm and mm/min whatever the workspace units.
struct ProbeSettings {
    PlateType plateType = PlateType::StandardBlock;
    PlateThickness zThickness;
    double xyThickness = 10;
    double probeFeedrate = 75;        // slow approach
    double probeFastFeedrate = 150;
    double retractionDistance = 2;
    double zRetractNormal = 2;
    double zRetractAuto = 1;
    double zProbeDistance = 30;
    double tipDiameter3D = 2;
    double xyRetract3D = 10;
    double probeMovementSpeed = 0;
    bool connectivityTest = true;     // wait for the probe pin before running
    int direction = kBottomLeft;
};

// What generateProbeCommands() reads from the machine.
struct MachineFacts {
    bool grblHal = false;
    std::string reportInches = "0";   // $13
    std::string homing = "0";         // $22
    std::string zMaxTravel = "0";     // $132
    double machineZ = 0;
};

// generateProbeCommands()'s option building: converts the stored mm values
// for imperial workspaces (except the AutoZero ones) and picks the probe
// distances. `toolDiameter` is in the workspace units (0 for Auto/Tip).
ProbingOptions makeProbingOptions(const ProbeSettings& settings, bool metric, Axes axes, ProbeType probeType,
                                  double toolDiameter, const MachineFacts& machine);

// The probe commands the widget offers for a plate: "Z Touch" only for a
// Z probe, otherwise Z, XYZ, XY, X and Y.
struct ProbeCommand {
    std::string id;
    Axes axes;
    bool needsTool = false;  // X/Y probing compensates for the tool diameter
};
std::vector<ProbeCommand> probeCommands(PlateType plate);

// Common end mill diameters (workspace.tools defaults).
struct ToolDiameter {
    double metric;
    double imperial;
};
const std::vector<ToolDiameter>& defaultTools();

}  // namespace gs::probe
