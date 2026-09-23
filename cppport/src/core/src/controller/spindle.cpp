#include "gs/controller/spindle.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

namespace gs::controller {
namespace {

std::string num(double value) {
    return js::numberToString(value);
}

// rounding.ts: roundMetric (2 decimals) / roundImperial (3).
double roundTo(double value, bool metric) {
    return js::stringToNumber(js::toFixed(value, metric ? 2 : 3));
}

}  // namespace

int wcsNumber(std::string_view wcs) {
    static constexpr std::string_view kSystems[] = {"G54", "G55", "G56", "G57", "G58", "G59"};
    for (int i = 0; i < 6; ++i) {
        if (wcs == kSystems[i]) {
            return i + 1;
        }
    }
    return 0;
}

std::string toolOffsetCommand(const ToolOffset& offset, bool toLaser, bool metric, double workX, double workY,
                              std::string_view wcs) {
    // Back to the spindle, the offsets are negated first.
    double x = toLaser ? offset.x : offset.x * -1;
    double y = toLaser ? offset.y : offset.y * -1;
    x = metric ? roundTo(x, true) : units::convertToImperial(x);
    y = metric ? roundTo(y, true) : units::convertToImperial(y);
    // calculateAdjustedOffsets(): the position, in the preferred units, plus the offset.
    const double px = metric ? workX : workX / 25.4;
    const double py = metric ? workY : workY / 25.4;
    const std::string adjustedX = num(roundTo(px + x, metric));
    const std::string adjustedY = num(roundTo(py + y, metric));
    const std::string p = "G10 L20 P" + std::to_string(wcsNumber(wcs));
    if (x == 0 && y != 0) {
        return p + " Y" + adjustedY;
    }
    if (x != 0 && y == 0) {
        return p + " X" + adjustedX;
    }
    if (x != 0 && y != 0) {
        return p + " X" + adjustedX + " Y" + adjustedY;
    }
    return {};
}

std::vector<std::string> modeSwitchCommands(const ModeSwitch& change) {
    std::vector<std::string> commands;
    if (change.spindleOn) {
        commands.emplace_back("M5");
    }
    commands.emplace_back(change.metric ? "G21" : "G20");
    if (std::string shift =
            toolOffsetCommand(change.offset, change.toLaser, change.metric, change.workX, change.workY, change.wcs);
        !shift.empty()) {
        commands.push_back(std::move(shift));
    }
    if (change.range) {
        commands.push_back("$30=" + num(change.range->first));
        commands.push_back("$31=" + num(change.range->second));
    }
    commands.emplace_back(change.toLaser ? "$32=1" : "$32=0");
    commands.push_back(change.deviceUnits);
    return commands;
}

std::string laserOnCommand(double powerPercent, double maxPower) {
    return "G1F1 M3 S" + num(maxPower * (powerPercent / 100));
}

}  // namespace gs::controller
