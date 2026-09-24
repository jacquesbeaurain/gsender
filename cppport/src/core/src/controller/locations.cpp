#include "gs/controller/locations.hpp"

#include "gs/controller/jog_limits.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>

namespace gs::controller {
namespace {

// RapidPosition.ts's OFFSET_DISTANCE: how far below the top of Z the moves
// travel when homing sets the origin.
constexpr double kOffsetDistance = 1;

std::string num(double value) {
    return js::numberToString(value);
}

// Number(setting): NaN when the board did not report it (undefined).
double reported(const std::string& value) {
    return value.empty() ? std::numeric_limits<double>::quiet_NaN() : js::stringToNumber(value);
}

// Number((value).toFixed(3)).
double fixed3(double value) {
    return std::isnan(value) ? value : js::stringToNumber(js::toFixed(value, 3));
}

// JavaScript's `!value` for a number.
bool falsy(double value) {
    return value == 0 || std::isnan(value);
}

// The Z lift before the corner and park moves.
double topOfZ(const LocationSettings& settings, double pullOff) {
    return settings.homingSetsOrigin() ? -kOffsetDistance : -pullOff;
}

char upper(char axis) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
}

}  // namespace

MachineCorner homingCorner(std::string_view homingDirMask) {
    const double mask = js::stringToNumber(homingDirMask);
    // Number(value) & 7: NaN converts to 0.
    const std::int64_t bits = std::isfinite(mask) ? static_cast<std::int64_t>(mask) & 7 : 0;
    switch (bits) {
        case 0: return MachineCorner::BackRight;
        case 1: return MachineCorner::BackLeft;
        case 2: return MachineCorner::FrontRight;
        case 3: return MachineCorner::FrontLeft;
        default: return MachineCorner::Other;
    }
}

std::string homingString(std::string_view homingDirMask) {
    const char* location = "Back Right";
    switch (homingCorner(homingDirMask)) {
        case MachineCorner::BackLeft: location = "Back Left"; break;
        case MachineCorner::FrontRight: location = "Front Right"; break;
        case MachineCorner::FrontLeft: location = "Front Left"; break;
        default: break;
    }
    return std::string(homingDirMask) + " (" + location + ")";
}

WorkRect machineBedWorkRect(std::string_view homingDirMask, double width, double depth, double wcoX, double wcoY) {
    // getAxisMaximumLocation(): from the home corner towards the opposite one.
    double signX = 1;
    double signY = 1;
    switch (homingCorner(homingDirMask)) {
        case MachineCorner::BackRight: signX = -1; signY = -1; break;
        case MachineCorner::BackLeft: signY = -1; break;
        case MachineCorner::FrontRight: signX = -1; break;
        default: break;
    }
    const double cornerX = signX * width;
    const double cornerY = signY * depth;
    return {std::min(0.0, cornerX) - wcoX, std::min(0.0, cornerY) - wcoY, std::max(0.0, cornerX) - wcoX,
            std::max(0.0, cornerY) - wcoY};
}

WorkRect keepoutWorkRect(double xMin, double xMax, double yMin, double yMax, double wcoX, double wcoY) {
    return {xMin - wcoX, yMin - wcoY, xMax - wcoX, yMax - wcoY};
}

double LocationSettings::pullOffDistance() const {
    return pullOff.empty() ? 1 : js::stringToNumber(pullOff);
}

bool LocationSettings::homingSetsOrigin() const {
    return isBitSetInNumber(js::stringToNumber(homing), 3);  // $22 ?? "0"
}

std::vector<std::string> cornerCommands(MachineCorner requested, const LocationSettings& settings, bool homingFlag,
                                        double pullOff, bool grblHal) {
    std::vector<std::string> gcode;
    gcode.push_back("G53 G21 G0 Z" + num(topOfZ(settings, pullOff)));
    MachineCorner homing = homingCorner(settings.homingDirMask);
    if (grblHal) {
        homingFlag = settings.homingSetsOrigin();
    }

    // getPositionMovements()
    const double xLimit = fixed3(reported(settings.xMaxTravel) - pullOff);
    const double yLimit = fixed3(reported(settings.yMaxTravel) - pullOff);
    if (!homingFlag) {
        homing = MachineCorner::BackRight;  // unhomed: everything is negative space
    }
    if (falsy(xLimit) || falsy(yLimit)) {
        return {};  // "Unable to find machine limits"
    }
    std::pair<double, double> move;
    if (requested == MachineCorner::Center) {
        switch (homing) {
            case MachineCorner::FrontRight: move = {(xLimit * -1) / 2, yLimit / 2}; break;
            case MachineCorner::FrontLeft: move = {xLimit / 2, yLimit / 2}; break;
            case MachineCorner::BackLeft: move = {xLimit / 2, (yLimit * -1) / 2}; break;
            default: move = {(xLimit * -1) / 2, (yLimit * -1) / 2}; break;
        }
    } else {
        // The last branch of each homing corner is upstream's "else".
        switch (homing) {
            case MachineCorner::FrontRight:
                move = requested == MachineCorner::FrontRight  ? std::pair{pullOff * -1, pullOff}
                       : requested == MachineCorner::FrontLeft ? std::pair{xLimit * -1, pullOff}
                       : requested == MachineCorner::BackLeft  ? std::pair{xLimit * -1, yLimit}
                                                               : std::pair{pullOff * -1, yLimit};
                break;
            case MachineCorner::FrontLeft:
                move = requested == MachineCorner::FrontRight  ? std::pair{xLimit, pullOff}
                       : requested == MachineCorner::FrontLeft ? std::pair{pullOff, pullOff}
                       : requested == MachineCorner::BackRight ? std::pair{xLimit, yLimit}
                                                               : std::pair{pullOff, yLimit};
                break;
            case MachineCorner::BackLeft:
                move = requested == MachineCorner::FrontRight  ? std::pair{xLimit, yLimit * -1}
                       : requested == MachineCorner::FrontLeft ? std::pair{pullOff, yLimit * -1}
                       : requested == MachineCorner::BackLeft  ? std::pair{pullOff, pullOff * -1}
                                                               : std::pair{xLimit, pullOff * -1};
                break;
            case MachineCorner::BackRight:
                move = requested == MachineCorner::FrontRight  ? std::pair{pullOff * -1, yLimit * -1}
                       : requested == MachineCorner::FrontLeft ? std::pair{xLimit * -1, yLimit * -1}
                       : requested == MachineCorner::BackLeft  ? std::pair{xLimit * -1, pullOff * -1}
                                                               : std::pair{pullOff * -1, pullOff * -1};
                break;
            default:
                return {};  // homing corner Other: "Unable to calculate position movements"
        }
    }
    gcode.push_back("G53 G21 G0 X" + num(move.first) + " Y" + num(move.second));
    return gcode;
}

std::vector<std::string> parkCommands(const MachineLocation& park, const LocationSettings& settings) {
    return {
        "G53 G21 G0 Z" + num(topOfZ(settings, settings.pullOffDistance())),
        "G53 G21 G0 X" + num(park.x) + " Y" + num(park.y),
        "G53 G21 G0 Z" + num(park.z),
    };
}

std::optional<MachineLocation> defaultToolChangePosition(const LocationSettings& settings, bool homingFlag) {
    if (settings.xMaxTravel.empty() || settings.yMaxTravel.empty()) {
        return std::nullopt;
    }
    const MachineCorner corner = homingFlag ? homingCorner(settings.homingDirMask) : MachineCorner::BackRight;
    const bool right = corner == MachineCorner::FrontRight || corner == MachineCorner::BackRight;
    const bool front = corner == MachineCorner::FrontRight || corner == MachineCorner::FrontLeft;
    const double xLimit = js::stringToNumber(settings.xMaxTravel);
    const double yLimit = js::stringToNumber(settings.yMaxTravel);
    constexpr double kFromRight = 1.0 / 3;  // X_FRAC_FROM_RIGHT
    constexpr double kFromBack = 2.0 / 3;   // Y_FRAC_FROM_BACK
    MachineLocation at;
    at.x = right ? -(xLimit * kFromRight) : xLimit * (1 - kFromRight);
    at.y = front ? yLimit * (1 - kFromBack) : -(yLimit * kFromBack);
    return at;
}

std::vector<std::string> locationCommands(const MachineLocation& location, const LocationSettings& settings) {
    return {
        "G53 G0 Z" + num(topOfZ(settings, settings.pullOffDistance())),
        "G53 G0 X" + num(location.x) + " Y" + num(location.y),
        "G53 G0 Z" + num(location.z),
    };
}

std::vector<std::string> goToLocationCommands(const GoToLocation& location) {
    std::string axisValues = "X" + num(location.x);
    if (location.yAvailable) {
        axisValues += " Y" + num(location.y);
    }
    if (location.aAvailable) {
        axisValues += " A" + num(location.a);
    }
    std::vector<std::string> code;
    if (location.mode == GoToMode::Machine) {
        code.push_back("G53 G0 " + axisValues);
        return code;
    }
    const bool incremental = location.mode == GoToMode::Incremental;
    const std::string modal = incremental ? "G91" : "G90";
    const double retractHeight = location.safeRetractHeight;
    // The retract in the workspace units (see the header's deviation note).
    const auto inUnits = [&location](double mm) { return location.metric ? mm : units::convertToImperial(mm); };
    if (retractHeight != 0) {
        if (location.homingEnabled) {
            const double retract = std::fabs(retractHeight) * -1;
            if (location.machineZ < retract) {
                code.push_back("G53 G0 Z" + num(inUnits(retract)));
            }
        } else {
            code.emplace_back("G91");
            code.push_back("G0Z" + num(inUnits(retractHeight)));
        }
    }
    code.push_back(modal);
    code.push_back("G0 " + axisValues);
    if (retractHeight != 0 && !location.homingEnabled && incremental) {
        // Back down to where Z was, plus the move.
        code.push_back("G90 G0 Z" + num(location.z + location.workZ));
    } else {
        code.push_back(modal);
        code.push_back("G0 Z" + num(location.z));
    }
    return code;
}

std::string manualOffsetCommand(char axis, double value) {
    return std::string("G10 P0 L20 ") + upper(axis) + num(value);
}

std::string homeAxisCommand(char axis) {
    return std::string("$H") + upper(axis);
}

bool singleAxisHomingEnabled(std::string_view homing) {
    return isBitSetInNumber(js::stringToNumber(homing), 1);
}

}  // namespace gs::controller
