#include "gs/probe/probing.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <array>
#include <cmath>
#include <limits>

namespace gs::probe {
namespace {

// Template literals print numbers with Number.prototype.toString().
std::string num(double value) {
    return js::numberToString(value);
}

// Number(x.toFixed(digits))
double fixed(double value, int digits) {
    return js::stringToNumber(js::toFixed(value, digits));
}

using units::convertToImperial;
using units::convertToMetric;
using units::mm2in;

// JS truthiness of a number.
bool truthy(double value) {
    return value != 0 && !std::isnan(value);
}

// Math.min: NaN wins.
double jsMin(double a, double b) {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return b < a ? b : a;
}

// SoftLimits.getZDownTravel(): what is left of the Z travel below the
// machine position, less a 1 mm margin.
double zDownTravel(const ProbingOptions& o, double requested) {
    const double zMaxTravel = std::fabs(o.zMaxTravel);
    const double zMpos = std::fabs(o.machineZ);
    return jsMin(zMaxTravel - zMpos - 1, requested);
}

double probeDelay(const ProbingOptions& o) {
    return o.grblHal ? 0.05 : 0.15;
}

// Probing directions for X and Y: +1 or -1 towards the plate.
std::array<double, 2> probeDirections(int corner) {
    switch (corner) {
        case kBottomLeft: return {1, 1};
        case kTopLeft: return {1, -1};
        case kTopRight: return {-1, -1};
        case kBottomRight: return {-1, 1};
        default: return {0, 0};
    }
}

// The values updateOptionsForDirection() adds for a standard plate.
struct Directed {
    double xRetractModifier = 0;
    double yRetractModifier = 0;
    double xProbeDistance = 0;
    double yProbeDistance = 0;
    double zProbeDistance = 0;
    double xRetract = 0;
    double yRetract = 0;
    double zRetract = 0;
    double xThickness = 0;
    double yThickness = 0;
    double xyPositionAdjust = 0;
    double zPositionAdjust = 0;
};

Directed forDirection(const ProbingOptions& o, int corner) {
    const bool is3D = o.plateType == PlateType::Probe3D;
    const double diameter = is3D ? o.tipDiameter3D : o.toolDiameter;
    const double xyThickness = is3D ? 0 : o.xyThickness;
    const double zThickness = is3D ? o.zThickness.probe3D : o.zThickness.standardBlock;

    Directed d;
    const auto [xProbeDir, yProbeDir] = probeDirections(corner);
    d.xRetractModifier = xProbeDir * -1;
    d.yRetractModifier = yProbeDir * -1;

    d.xProbeDistance = o.probeDistanceX * xProbeDir;
    d.zProbeDistance = o.probeDistanceZ * -1;
    d.yProbeDistance = o.probeDistanceY * yProbeDir;

    d.xRetract = o.retract * d.xRetractModifier;
    d.yRetract = o.retract * d.yRetractModifier;
    d.zRetract = o.retract;

    // X and Y zero sits a tool radius plus the plate wall beyond the touch.
    const double toolRadius = diameter / 2;
    const double toolCompensatedXY = fixed(-1 * toolRadius - xyThickness, 3);
    d.yThickness = toolCompensatedXY * yProbeDir;
    d.xThickness = toolCompensatedXY * xProbeDir;

    d.xyPositionAdjust = (is3D ? o.xyRetract3D : xyThickness) + o.retract + toolRadius;

    double probe3dOffset = is3D ? 5 : 0;
    probe3dOffset = o.metric ? probe3dOffset : fixed(mm2in(probe3dOffset), 3);
    d.zPositionAdjust = o.retract + zThickness + probe3dOffset;
    return d;
}

std::vector<std::string> preamble(const ProbingOptions& o, const Directed& d) {
    double zThickness = o.zThickness.standardBlock;
    if (o.plateType == PlateType::ZProbe) {
        zThickness = o.zThickness.zProbe;
    } else if (o.plateType == PlateType::Probe3D) {
        zThickness = o.zThickness.probe3D;
    }

    std::string initialOffsets = "G10 L20 P0 ";
    if (o.axes.x) {
        initialOffsets += "X0";
    }
    if (o.axes.y) {
        initialOffsets += "Y0";
    }
    if (o.axes.z) {
        initialOffsets += "Z0";
    }

    double zProbeDistance = d.zProbeDistance;
    if (o.homingEnabled) {
        zProbeDistance = zDownTravel(o, std::fabs(zProbeDistance)) * -1;
    }

    return {
        "; Initial Probe setup",
        "%UNITS=modal.units",
        "%Z_ADJUST=" + num(d.zPositionAdjust),
        "%X_ADJUST=" + num(d.xyPositionAdjust),
        "%Y_ADJUST=" + num(d.xyPositionAdjust),
        "%X_PROBE_DISTANCE=" + num(d.xProbeDistance),
        "%Y_PROBE_DISTANCE=" + num(d.yProbeDistance),
        "%Z_PROBE_DISTANCE=" + num(zProbeDistance),
        "%PROBE_FAST_FEED=" + num(o.probeFast),
        "%PROBE_SLOW_FEED=" + num(o.probeSlow),
        "%X_RETRACT_DISTANCE=" + num(d.xRetract),
        "%Y_RETRACT_DISTANCE=" + num(d.yRetract),
        "%Z_RETRACT_DISTANCE=" + num(d.zRetract),
        "%Z_RETRACT_FINAL=" + num(o.zRetractNormal),
        "%Z_THICKNESS=" + num(zThickness),
        "%X_THICKNESS=" + num(d.xThickness),
        "%Y_THICKNESS=" + num(d.yThickness),
        "%PROBE_DELAY=" + num(probeDelay(o)),
        "%Y_RETRACT_DIRECTION=" + num(d.yRetractModifier),
        "%X_RETRACT_DIRECTION=" + num(d.xRetractModifier),
        "%PROBE_MOVE_FEED=" + num(o.probeMovementSpeed),
        initialOffsets,
        "G91 G" + o.modal,
    };
}

void append(std::vector<std::string>& code, std::vector<std::string> more) {
    code.insert(code.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
}

std::vector<std::string> singleAxisStandardRoutine(char axis, bool useFinalZ = false) {
    const std::string a(1, axis);
    const std::string axisRetract = a + "_RETRACT_DISTANCE";
    const std::string finalRetract = useFinalZ ? "Z_RETRACT_FINAL" : axisRetract;
    return {
        "; " + a + "-probe",
        "G38.2 " + a + "[" + a + "_PROBE_DISTANCE] F[PROBE_FAST_FEED]",
        "G91 G0 " + a + "[" + axisRetract + "]",
        "%retractSign=Math.sign(" + axisRetract + ")",
        "G38.2 " + a + "[(Math.abs(" + axisRetract + ") + 1) * (retractSign * -1)] F[PROBE_SLOW_FEED]",
        "G4 P[PROBE_DELAY]",
        "G10 L20 P0 " + a + "[" + a + "_THICKNESS]",
        "G91 G0 " + a + "[" + finalRetract + "]",
    };
}

std::vector<std::string> standardRoutine(const ProbingOptions& o, const Directed& d) {
    std::vector<std::string> code = preamble(o, d);

    // Extra room for the bit's placement, from the starting circle's size.
    const std::string initialPositionAdjustment = o.metric ? num(6) : js::toFixed(mm2in(6), 3);
    const auto finalZeroMove = [&](const std::string& coords) {
        return o.probeMovementSpeed > 0 ? "G90 G1 " + coords + " F[PROBE_MOVE_FEED]" : "G90 G0 " + coords;
    };

    if (o.axes.z) {
        append(code, singleAxisStandardRoutine('Z'));
        // Z also positions the bit for X.
        code.push_back("G91 G0 X[(X_ADJUST + " + initialPositionAdjustment + ") * X_RETRACT_DIRECTION]");
        code.emplace_back("G91 G0 Z-[Z_ADJUST]");
    }
    if (o.axes.x) {
        if (!o.axes.z) {
            // XY probing starts from a different spot.
            code.emplace_back("G91 G0 X[X_RETRACT_DISTANCE]");
            code.emplace_back("G91 G0 Y[Y_ADJUST * -1 * Y_RETRACT_DIRECTION]");
        }
        append(code, singleAxisStandardRoutine('X'));
    }
    if (o.axes.y) {
        code.push_back("G91 G0 Y[(Y_ADJUST + " + initialPositionAdjustment + ") * Y_RETRACT_DIRECTION]");
        code.emplace_back("G91 G0 X[X_ADJUST * -1 * X_RETRACT_DIRECTION]");
        append(code, singleAxisStandardRoutine('Y'));
    }
    if (o.axes.z) {
        code.emplace_back("G91 G0 Z[Z_ADJUST + Z_RETRACT_FINAL]");
        code.push_back(finalZeroMove("X0Y0"));
    }
    return code;
}

// determineAutoPlateOffsetValues(): the AutoZero pocket centre relative to
// the stock corner.
std::array<double, 2> autoPlateOffsets(int corner) {
    const double xOff = 22.5;
    const double yOff = 22.5;
    if (corner == kBottomRight) {
        return {xOff * -1, yOff};
    }
    if (corner == kTopRight) {
        return {xOff * -1, yOff * -1};
    }
    if (corner == kTopLeft) {
        return {xOff, yOff * -1};
    }
    return {xOff, yOff};
}

struct AutoCommon {
    std::string xOff;
    std::string yOff;
    std::string delay;
    std::string moveFeed;
    std::string zDistance;
    std::string zThickness;
    std::string zRetract;
    std::string units;  // "G20" when positions are reported in inches
};

AutoCommon autoCommon(const ProbingOptions& o, int corner) {
    const auto [xOff, yOff] = autoPlateOffsets(corner);
    double zDistance = 25;
    if (o.homingEnabled) {
        zDistance = zDownTravel(o, zDistance);
    }
    return {num(xOff),
            num(yOff),
            num(probeDelay(o)),
            num(o.probeMovementSpeedAuto),
            num(zDistance),
            num(o.zThickness.autoZero),
            num(o.zRetractAuto),
            o.reportInches ? "G20" : ""};
}

std::string autoFinalZeroMove(const ProbingOptions& o, const std::string& coords) {
    return o.probeMovementSpeedAuto > 0 ? "G21 G90 G1 " + coords + " F[PROBE_MOVE_FEED]" : "G21 G90 G0 " + coords;
}

// get3AxisAutoRoutine(): the bit finds the pocket's walls itself.
std::vector<std::string> autoRoutine(const ProbingOptions& o, int corner) {
    const AutoCommon c = autoCommon(o, corner);
    const Axes& axes = o.axes;
    if (axes.x && axes.y && axes.z) {
        return {
            "; AZ Probe XYZ Auto - direction: " + num(corner),
            "%X_OFF = " + c.xOff,
            "%Y_OFF = " + c.yOff,
            "%Z_THICKNESS = " + c.zThickness,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z2",
            "G38.2 Z-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Z[Z_THICKNESS]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z3",
            "G21 G91 G0 X-13",
            "G38.2 X-30 F150",
            "G21 G91 G0 X2",
            "G38.2 X-5 F75",
            "G4 P[PROBE_DELAY]",
            "%X_LEFT=posx",
            "G21 G91 G0 X26",
            "G38.2 X30 F150",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            "%X_RIGHT=posx",
            "%X_CENTER=((X_RIGHT - X_LEFT)/2)*-1",
            c.units + " G91 G0 X[X_CENTER]",
            "G21 G91 G0 Y-13",
            "G38.2 Y-30 F250",
            "G21 G91 G0 Y2",
            "G38.2 Y-5 F75",
            "G4 P[PROBE_DELAY]",
            "%Y_BOTTOM = posy",
            "G21 G91 G0 Y26",
            "G38.2 Y30 F250",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            "%Y_TOP = posy",
            "%Y_CENTER = ((Y_TOP - Y_BOTTOM)/2) * -1",
            c.units + " G0 Y[Y_CENTER]",
            "G21 G10 L20 P0 X[X_OFF] Y[Y_OFF]",
            autoFinalZeroMove(o, "X0 Y0"),
            "G21 G0 G90 Z" + c.zRetract,
        };
    }
    if (axes.x && axes.y) {
        return {
            "; AZ Probe XY Auto",
            "%X_OFF = " + c.xOff,
            "%Y_OFF = " + c.yOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z3",
            "G21 G91 G0 X-13",
            "G38.2 X-30 F150",
            "G21 G91 G0 X2",
            "G38.2 X-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X0",
            "G21 G91 G0 X26",
            "G38.2 X30 F150",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 X[posx/2]",
            c.units + " G90 G0 X0",
            "G21 G91 G0 Y-13",
            "G38.2 Y-30 F150",
            "G21 G91 G0 Y2",
            "G38.2 Y-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y0",
            "G21 G91 G0 Y26",
            "G38.2 Y30 F150",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 Y[posy/2]",
            "G21 G90 G0 X0 Y0",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X[X_OFF] Y[Y_OFF]",
            autoFinalZeroMove(o, "X0 Y0"),
        };
    }
    if (axes.z) {
        return {
            "; AZ Probe Z Auto",
            "%Z_THICKNESS = " + c.zThickness,
            "%PROBE_DELAY=" + c.delay,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z2",
            "G38.2 Z-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Z[Z_THICKNESS]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    if (axes.x) {
        return {
            "; AZ Probe X Auto",
            "%X_OFF = " + c.xOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z3",
            "G21 G91 G0 X-13",
            "G38.2 X-30 F150",
            "G21 G91 G0 X2",
            "G38.2 X-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X0",
            "G21 G91 G0 X26",
            "G38.2 X30 F150",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 X[posx/2]",
            autoFinalZeroMove(o, "X0"),
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X[X_OFF]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    if (axes.y) {
        return {
            "; AZ Probe Y Auto",
            "%Y_OFF = " + c.yOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z3",
            "G21 G91 G0 Y-13",
            "G38.2 Y-30 F150",
            "G21 G91 G0 Y2",
            "G38.2 Y-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y0",
            "G21 G91 G0 Y26",
            "G38.2 Y30 F150",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 Y[posy/2]",
            autoFinalZeroMove(o, "Y0"),
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y[Y_OFF]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    return {};
}

// get3AxisAutoTipRoutine(): as Auto, with short moves for a V-bit tip.
std::vector<std::string> autoTipRoutine(const ProbingOptions& o, int corner) {
    const AutoCommon c = autoCommon(o, corner);
    const Axes& axes = o.axes;
    if (axes.x && axes.y && axes.z) {
        return {
            "; AZ Probe XYZ Tip",
            "%X_OFF = " + c.xOff,
            "%Y_OFF = " + c.yOff,
            "%Z_THICKNESS = " + c.zThickness,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z2",
            "G38.2 Z-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Z[Z_THICKNESS]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z0.5",
            "G21 G91 G0 X-3",
            "G38.2 X-30 F150",
            "G21 G91 G0 X2",
            "G38.2 X-5 F75",
            "G4 P[PROBE_DELAY]",
            "%X_LEFT=posx",
            "G21 G91 G0 X14",
            "G38.2 X15 F150",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            "%X_RIGHT=posx",
            "%X_CENTER=((X_RIGHT - X_LEFT)/2)*-1",
            c.units + " G91 G0 X[X_CENTER]",
            "G21 G91 G0 Y-3",
            "G38.2 Y-15 F150",
            "G21 G91 G0 Y2",
            "G38.2 Y-5 F75",
            "G4 P[PROBE_DELAY]",
            "%Y_BOTTOM = posy",
            "G21 G91 G0 Y14",
            "G38.2 Y15 F150",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            "%Y_TOP = posy",
            "%Y_CENTER = ((Y_TOP - Y_BOTTOM)/2) * -1",
            c.units + " G0 Y[Y_CENTER]",
            "G21 G10 L20 P0 X[X_OFF] Y[Y_OFF]",
            autoFinalZeroMove(o, "X0 Y0"),
            "G21 G0 G90 Z" + c.zRetract,
        };
    }
    if (axes.x && axes.y) {
        return {
            "; AZ Probe XY Tip",
            "%X_OFF = " + c.xOff,
            "%Y_OFF = " + c.yOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z0.5",
            "G21 G91 G0 X-3",
            "G38.2 X-30 F150",
            "G21 G91 G0 X2",
            "G38.2 X-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X0",
            "G21 G91 G0 X14",
            "G38.2 X15 F150",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 X[posx/2]",
            c.units + " G90 G0 X0",
            "G21 G91 G0 Y-3",
            "G38.2 Y-15 F150",
            "G21 G91 G0 Y2",
            "G38.2 Y-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y0",
            "G21 G91 G0 Y14",
            "G38.2 Y15 F150",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 Y[posy/2]",
            c.units + " G90 G0 X0 Y0",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X[X_OFF] Y[Y_OFF]",
            autoFinalZeroMove(o, "X0 Y0"),
        };
    }
    if (axes.z) {
        return {
            "; AZ Probe Z Tip",
            "%Z_THICKNESS = " + c.zThickness,
            "%PROBE_DELAY=" + c.delay,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z2",
            "G38.2 Z-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Z[Z_THICKNESS]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    if (axes.x) {
        return {
            "; AZ Probe X Tip",
            "%X_OFF = " + c.xOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z0.5",
            "G21 G91 G0 X-3",
            "G38.2 X-30 F150",
            "G21 G91 G0 X2",
            "G38.2 X-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X0",
            "G21 G91 G0 X14",
            "G38.2 X15 F150",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 X[posx/2]",
            autoFinalZeroMove(o, "X0"),
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X[X_OFF]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    if (axes.y) {
        return {
            "; AZ Probe Y Tip",
            "%Y_OFF = " + c.yOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z0.5",
            "G21 G91 G0 Y-3",
            "G38.2 Y-15 F150",
            "G21 G91 G0 Y2",
            "G38.2 Y-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y0",
            "G21 G91 G0 Y14",
            "G38.2 Y15 F150",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            c.units + " G10 L20 P0 Y[posy/2]",
            autoFinalZeroMove(o, "Y0"),
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y[Y_OFF]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    return {};
}

// get3AxisAutoDiameterRoutine(): a known tool diameter, one wall per axis.
std::vector<std::string> autoDiameterRoutine(const ProbingOptions& o, int corner) {
    const AutoCommon c = autoCommon(o, corner);
    const Axes& axes = o.axes;
    const double toolRadius = (o.metric ? o.toolDiameter : convertToMetric(o.toolDiameter)) / 2;
    const double toolCompensatedThickness = -1 * toolRadius;
    const std::string compensated = num(fixed(22.5 + toolCompensatedThickness, 3));

    // Upstream tests `axes.z && axes.y && axes.z`, so Y+Z without X takes
    // the XYZ routine; kept for byte-identical output.
    if (axes.z && axes.y) {
        return {
            "; AZ Probe XYZ specific dia",
            "%X_OFF = " + c.xOff,
            "%Y_OFF = " + c.yOff,
            "%Z_THICKNESS = " + c.zThickness,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z2",
            "G38.2 Z-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Z[Z_THICKNESS]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z3",
            "G21 G91 G0 X13",
            "G38.2 X20 F250",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X" + compensated,
            "G21 G90 G0 X0",
            "G21 G91 G0 Y13",
            "G38.2 Y20 F250",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 Y" + compensated,
            "G21 G90 G0 X0 Y0",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X[X_OFF] Y[Y_OFF]",
            autoFinalZeroMove(o, "X0 Y0"),
            "G21 G90 G0 Z" + c.zRetract,
        };
    }
    if (axes.x && axes.y) {
        return {
            "; AZ Probe XY specific dia",
            "%X_OFF = " + c.xOff,
            "%Y_OFF = " + c.yOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z3",
            "G21 G91 G0 X13",
            "G38.2 X20 F250",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X" + compensated,
            "G21 G90 G0 X0",
            "G21 G91 G0 Y13",
            "G38.2 Y20 F250",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 Y" + compensated,
            "G21 G90 G0 X0 Y0",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X[X_OFF] Y[Y_OFF]",
            "G4 P[PROBE_DELAY]",
            autoFinalZeroMove(o, "X0 Y0"),
        };
    }
    if (axes.z) {
        return {
            "; AZ Probe Z specific dia",
            "%Z_THICKNESS = " + c.zThickness,
            "%PROBE_DELAY=" + c.delay,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z2",
            "G38.2 Z-5 F75",
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Z[Z_THICKNESS]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    if (axes.y) {
        return {
            "; AZ Probe Y specific dia",
            "%Y_OFF = " + c.yOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z3",
            "G21 G91 G0 Y13",
            "G38.2 Y20 F250",
            "G21 G91 G0 Y-2",
            "G38.2 Y5 F75",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 Y" + compensated,
            autoFinalZeroMove(o, "Y0"),
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 Y[Y_OFF]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    if (axes.x) {
        return {
            "; AZ Probe X specific dia",
            "%X_OFF = " + c.xOff,
            "%PROBE_DELAY=" + c.delay,
            "%PROBE_MOVE_FEED=" + c.moveFeed,
            "G21 G91",
            "G38.2 Z-" + c.zDistance + " F200",
            "G21 G91 G0 Z3",
            "G21 G91 G0 X13",
            "G38.2 X20 F250",
            "G21 G91 G0 X-2",
            "G38.2 X5 F75",
            "G4 P[PROBE_DELAY]",
            "G21 G10 L20 P0 X" + compensated,
            autoFinalZeroMove(o, "X0"),
            "G4 P[PROBE_DELAY]",
            "G10 L20 P0 X[X_OFF]",
            "G4 P[PROBE_DELAY]",
            "G21 G91 G0 Z" + c.zRetract,
        };
    }
    return {};
}

// BitZero V2 (from https://github.com/vsergeev/nomad3-cncjs-macros): the bit
// starts inside the bore, whose centre becomes X0 Y0.
constexpr double kBoreDiameter = 15;
constexpr double kInsetThickness = 13;   // for XYZ
constexpr double kProbeThickness = 15.5; // for Z only
constexpr double kProbeFeed = 100;
constexpr double kProbeSlowFeed = 50;
constexpr double kTravelFeed = 300;
constexpr double kSafeHeight = 15;

std::vector<std::string> bitZeroRoutine(const ProbingOptions& o, int corner) {
    const std::string delay = num(probeDelay(o));
    const std::string probeFeed = num(kProbeFeed);
    const std::string slowFeed = num(kProbeSlowFeed);
    const std::string travel = num(kTravelFeed);
    const std::string xOff = num(0);
    const std::string yOff = num(0);

    // The diagonal move out of the bore goes away from the stock.
    double xMod = 1;
    double yMod = 1;
    if (corner == kTopLeft) {
        yMod = -1;
    } else if (corner == kTopRight) {
        xMod = -1;
        yMod = -1;
    } else if (corner == kBottomRight) {
        xMod = -1;
    }
    const std::string units = o.reportInches ? "G20" : "";
    const double insetThickness = truthy(o.zThickness.bitZero) ? o.zThickness.bitZero : kInsetThickness;
    const double probeThickness = truthy(o.zThickness.bitZeroZOnly) ? o.zThickness.bitZeroZOnly : kProbeThickness;

    const std::vector<std::string> probeX{
        "G91",
        "G38.2 X-[BORE_DIAMETER] F" + probeFeed,
        "G4 P0.1",
        "%X1 = Number(posx)",
        "G1 X1 F" + travel,
        "G38.2 X[BORE_DIAMETER] F" + probeFeed,
        "G4 P0.1",
        "%X2 = Number(posx)",
        "G90",
        "G1 X[(X1 + X2) / 2] F" + travel,
        "G4 P0.1",
        "G10 L20 P0 X" + xOff,
        "G4 P0.1",
    };
    const std::vector<std::string> probeY{
        "G91",
        "G38.2 Y[BORE_DIAMETER] F" + probeFeed,
        "G4 P0.1",
        "%Y1 = Number(posy)",
        "G1 Y-1 F" + travel,
        "G38.2 Y-[BORE_DIAMETER] F" + probeFeed,
        "G4 P0.1",
        "%Y2 = Number(posy)",
        "G90",
        "G1 Y[(Y1 + Y2) / 2] F" + travel,
        "G4 P0.1",
        "G10 L20 P0 Y" + yOff,
        "G4 P0.1",
    };
    const std::vector<std::string> retract{"G91", "G1 Z[SAFE_HEIGHT] F" + travel};

    std::vector<std::string> code;
    const Axes& axes = o.axes;
    if (axes.x && axes.y && axes.z) {
        code = {
            "; BitZero V2 Probe XYZ",
            "; Direction: " + num(corner) + " (0=BL, 1=TL, 2=TR, 3=BR)",
            "; Instructions: Run with tool located inside probing bore.",
            "%PROBE_DELAY=" + delay,
            "%BORE_DIAMETER=" + num(kBoreDiameter),
            "%INSET_THICKNESS=" + num(insetThickness),
            "%SAFE_HEIGHT=" + num(kSafeHeight),
            "%X_MOD=" + num(xMod),
            "%Y_MOD=" + num(yMod),
            "M5",
            "G21",
        };
        append(code, probeX);
        append(code, probeY);
        append(code, {
                         "G91",
                         "G1 Z[INSET_THICKNESS] F" + travel,
                         "G1 X[BORE_DIAMETER * X_MOD] Y[BORE_DIAMETER * Y_MOD] F" + travel,
                         "G38.2 Z-[INSET_THICKNESS] F" + probeFeed,
                         "G1 Z2 F" + travel,
                         "G38.2 Z-5 F" + slowFeed,
                         "G4 P0.1",
                         "G10 L20 P0 Z[INSET_THICKNESS]",
                         "G4 P0.1",
                     });
        append(code, retract);
        append(code, {"G90", units + " G0 X0 Y0"});
    } else if (axes.x && axes.y) {
        code = {
            "; BitZero V2 Probe XY",
            "; Direction: " + num(corner) + " (0=BL, 1=TL, 2=TR, 3=BR)",
            "; Instructions: Run with tool located inside probing bore.",
            "%PROBE_DELAY=" + delay,
            "%BORE_DIAMETER=" + num(kBoreDiameter),
            "%SAFE_HEIGHT=" + num(kSafeHeight),
            "M5",
            "G21",
        };
        append(code, probeX);
        append(code, probeY);
        append(code, retract);
        append(code, {"G90", units + " G0 X0 Y0"});
    } else if (axes.z) {
        code = {
            "; BitZero V2 Probe Z",
            "; Instructions: Run with tool within 20mm of top of probe surface.",
            "%PROBE_DELAY=" + delay,
            "%PROBE_THICKNESS=" + num(probeThickness),
            "%PROBE_DISTANCE=20",
            "%SAFE_HEIGHT=" + num(kSafeHeight),
            "M5",
            "G21",
            "G91",
            "G38.2 Z-[PROBE_DISTANCE] F" + probeFeed,
            "G1 Z2 F" + travel,
            "G38.2 Z-5 F" + slowFeed,
            "G4 P0.1",
            "G10 L20 P0 Z[PROBE_THICKNESS]",
            "G4 P0.1",
        };
        append(code, retract);
    } else if (axes.x) {
        code = {
            "; BitZero V2 Probe X",
            "; Instructions: Run with tool located inside probing bore.",
            "%PROBE_DELAY=" + delay,
            "%BORE_DIAMETER=" + num(kBoreDiameter),
            "%SAFE_HEIGHT=" + num(kSafeHeight),
            "M5",
            "G21",
        };
        append(code, probeX);
        append(code, retract);
        append(code, {"G90", units + " G0 X0"});
    } else if (axes.y) {
        code = {
            "; BitZero V2 Probe Y",
            "; Instructions: Run with tool located inside probing bore.",
            "%PROBE_DELAY=" + delay,
            "%BORE_DIAMETER=" + num(kBoreDiameter),
            "%SAFE_HEIGHT=" + num(kSafeHeight),
            "M5",
            "G21",
        };
        append(code, probeY);
        append(code, retract);
        append(code, {"G90", units + " G0 Y0"});
    }
    return code;
}

}  // namespace

int nextCorner(int corner) {
    return corner == kBottomRight ? kBottomLeft : corner + 1;
}

std::string_view plateTypeName(PlateType type) {
    switch (type) {
        case PlateType::StandardBlock: return "Standard Block";
        case PlateType::AutoZero: return "AutoZero";
        case PlateType::ZProbe: return "Z Probe";
        case PlateType::Probe3D: return "3D Probe";
        case PlateType::BitZero: return "BitZero";
    }
    return "Standard Block";
}

std::optional<PlateType> plateTypeFromName(std::string_view name) {
    for (const PlateType type : {PlateType::StandardBlock, PlateType::AutoZero, PlateType::ZProbe, PlateType::Probe3D,
                                 PlateType::BitZero}) {
        if (plateTypeName(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

std::string_view probeTypeName(ProbeType type) {
    switch (type) {
        case ProbeType::Auto: return "Auto";
        case ProbeType::Tip: return "Tip";
        case ProbeType::Diameter: return "Diameter";
    }
    return "Diameter";
}

std::optional<ProbeType> probeTypeFromName(std::string_view name) {
    for (const ProbeType type : {ProbeType::Auto, ProbeType::Tip, ProbeType::Diameter}) {
        if (probeTypeName(type) == name) {
            return type;
        }
    }
    return std::nullopt;
}

std::vector<std::string> probeCode(const ProbingOptions& options, int corner) {
    if (options.plateType == PlateType::AutoZero) {
        if (options.probeType == ProbeType::Auto) {
            return autoRoutine(options, corner);
        }
        if (options.probeType == ProbeType::Tip) {
            return autoTipRoutine(options, corner);
        }
        return autoDiameterRoutine(options, corner);
    }
    if (options.plateType == PlateType::BitZero) {
        return bitZeroRoutine(options, corner);
    }

    const Directed directed = forDirection(options, corner);
    const Axes& axes = options.axes;
    if (axes.x && axes.y) {
        return standardRoutine(options, directed);
    }
    std::vector<std::string> code;
    if (axes.z) {
        code = preamble(options, directed);
        append(code, singleAxisStandardRoutine('Z', true));
    } else if (axes.y) {
        code = preamble(options, directed);
        append(code, singleAxisStandardRoutine('Y'));
    } else if (axes.x) {
        code = preamble(options, directed);
        append(code, singleAxisStandardRoutine('X'));
    }
    return code;
}

ProbingOptions makeProbingOptions(const ProbeSettings& s, bool metric, Axes axes, ProbeType probeType,
                                  double toolDiameter, const MachineFacts& machine) {
    ProbingOptions o;
    o.axes = axes;
    o.metric = metric;
    o.modal = metric ? "21" : "20";
    o.plateType = s.plateType;
    o.probeType = probeType;
    o.toolDiameter = toolDiameter;
    o.zRetractAuto = s.zRetractAuto;
    o.probeMovementSpeedAuto = s.probeMovementSpeed;  // AutoZero runs in mm whatever the units
    o.grblHal = machine.grblHal;
    o.reportInches = machine.reportInches == "1";
    o.homingEnabled = machine.homing != "0";
    o.zMaxTravel = js::stringToNumber(machine.zMaxTravel);
    o.machineZ = machine.machineZ;
    if (metric) {
        o.probeDistanceX = 30;
        o.probeDistanceY = 30;
        o.probeDistanceZ = truthy(s.zProbeDistance) ? s.zProbeDistance : 30;
        o.zThickness = s.zThickness;
        o.xyThickness = s.xyThickness;
        o.probeSlow = s.probeFeedrate;
        o.probeFast = s.probeFastFeedrate;
        o.retract = s.retractionDistance;
        o.zRetractNormal = s.zRetractNormal;
        o.tipDiameter3D = s.tipDiameter3D;
        o.xyRetract3D = s.xyRetract3D;
        o.probeMovementSpeed = s.probeMovementSpeed;
    } else {
        o.probeDistanceX = 1.2;
        o.probeDistanceY = 1.2;
        o.probeDistanceZ = truthy(s.zProbeDistance) ? convertToImperial(s.zProbeDistance) : 1.2;
        // Only these four are converted; the AutoZero thickness stays mm and
        // the BitZero ones are left out (so their routine uses its defaults).
        o.zThickness = PlateThickness{
            convertToImperial(s.zThickness.standardBlock),
            s.zThickness.autoZero,
            convertToImperial(s.zThickness.zProbe),
            convertToImperial(s.zThickness.probe3D),
            0,
            0,
        };
        o.xyThickness = convertToImperial(s.xyThickness);
        o.probeSlow = convertToImperial(s.probeFeedrate);
        o.probeFast = convertToImperial(s.probeFastFeedrate);
        o.retract = convertToImperial(s.retractionDistance);
        o.zRetractNormal = convertToImperial(s.zRetractNormal);
        o.tipDiameter3D = convertToImperial(s.tipDiameter3D);
        o.xyRetract3D = convertToImperial(s.xyRetract3D);
        o.probeMovementSpeed = truthy(s.probeMovementSpeed) ? convertToImperial(s.probeMovementSpeed) : 0;
    }
    return o;
}

std::vector<ProbeCommand> probeCommands(PlateType plate) {
    std::vector<ProbeCommand> commands{{"Z Touch", {false, false, true}, false}};
    if (plate == PlateType::ZProbe) {
        return commands;
    }
    const bool tool = plate != PlateType::Probe3D;
    commands.push_back({"XYZ Touch", {true, true, true}, tool});
    commands.push_back({"XY Touch", {true, true, false}, tool});
    commands.push_back({"X Touch", {true, false, false}, tool});
    commands.push_back({"Y Touch", {false, true, false}, tool});
    return commands;
}

const std::vector<ToolDiameter>& defaultTools() {
    static const std::vector<ToolDiameter> tools{
        {6.35, 0.25}, {3.175, 0.125}, {9.525, 0.375}, {12.7, 0.5}, {15.875, 0.625},
    };
    return tools;
}

}  // namespace gs::probe
