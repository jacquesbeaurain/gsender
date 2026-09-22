#include "gs/controller/jog_limits.hpp"

#include "gs/util/jsnumber.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace gs::controller {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();

double settingNumber(const protocol::OrderedMap& settings, std::string_view key) {
    const std::string* value = settings.find(key);
    return value ? js::stringToNumber(*value) : std::numeric_limits<double>::quiet_NaN();
}

double fixed2(double value) {
    return js::stringToNumber(js::toFixed(value, 2));
}

int sign(double v) noexcept {
    return v > 0 ? 1 : (v < 0 ? -1 : 0);
}

}  // namespace

HomingLocation homingLocation(double homingMask) noexcept {
    if (!std::isfinite(homingMask)) {
        return HomingLocation::BackRight;
    }
    switch (static_cast<std::int64_t>(homingMask) & 7) {
        case 0: return HomingLocation::BackRight;
        case 1: return HomingLocation::BackLeft;
        case 2: return HomingLocation::FrontRight;
        case 3: return HomingLocation::FrontLeft;
        default: return HomingLocation::BackRight;
    }
}

double determineMaxMovement(double position, int movementDirection, int limitLocation, double limit) {
    const double offset = 1;
    limit -= offset;
    if (position == 0) {
        if (movementDirection != limitLocation) {
            return 0;
        }
        return fixed2((limit - offset) * movementDirection);
    }
    if (movementDirection == 1) {
        return limitLocation == 1 ? fixed2(limit - position - offset) : fixed2(position - offset);
    }
    if (movementDirection == -1) {
        return limitLocation == 1 ? fixed2(-1 * (position - offset)) : fixed2(-1 * (limit - position - offset));
    }
    return 0;
}

std::pair<int, int> axisMaximumLocation(double homingMask) noexcept {
    switch (homingLocation(homingMask)) {
        case HomingLocation::BackRight: return {-1, -1};
        case HomingLocation::BackLeft: return {1, -1};
        case HomingLocation::FrontRight: return {-1, 1};
        case HomingLocation::FrontLeft: return {1, 1};
    }
    return {1, 1};
}

bool determineMachineZeroFlagSet(const protocol::AxisValues& mpos, const protocol::OrderedMap& settings) {
    const HomingLocation location = homingLocation(settingNumber(settings, "$23"));
    // parseInt truncates, so anything within 1mm of zero counts as zero.
    const auto whole = [&mpos](std::size_t i) { return std::trunc(i < mpos.count ? mpos.values[i] : 0.0); };
    return location != HomingLocation::BackRight && whole(0) == 0 && whole(1) == 0 && whole(2) == 0;
}

bool isBitSetInNumber(double number, int bit) noexcept {
    if (!std::isfinite(number)) {
        return false;
    }
    return (static_cast<std::int64_t>(number) & (std::int64_t{1} << bit)) != 0;
}

bool determineHalMachineZeroFlag(const protocol::OrderedMap& settings) {
    const std::string* mask = settings.find("$22");
    if (!mask) {
        return false;
    }
    return isBitSetInNumber(js::stringToNumber(*mask), 3);
}

double axisTravelLimit(int direction, double position, double maxTravel, double offset) {
    if (position == 0) {
        return fixed2((maxTravel - offset) * direction);
    }
    if (direction == 1) {
        return fixed2(position - offset);
    }
    return fixed2(-1 * (maxTravel - position - offset));
}

Axes4 computeTravelBudget(const Axes4& direction, const protocol::OrderedMap& settings,
                          const protocol::AxisValues& mpos, bool homingFlagSet, bool softLimitsEnabled) {
    Axes4 budget{kInfinity, kInfinity, kInfinity, kInfinity};
    if (!softLimitsEnabled) {
        return budget;
    }

    // Positions in mm, whatever the firmware reports in ($13).
    const bool inches = settings.get("$13") == "1";
    const auto metric = [&](std::size_t i) {
        const double value = i < mpos.count ? mpos.values[i] : 0.0;
        return inches ? fixed2(value * 25.4) : value;
    };
    const std::array<double, 3> maxTravel{settingNumber(settings, "$130"), settingNumber(settings, "$131"),
                                          settingNumber(settings, "$132")};

    if (homingFlagSet) {
        const auto [xMax, yMax] = axisMaximumLocation(settingNumber(settings, "$23"));
        const std::array<int, 2> maxLocation{xMax, yMax};
        for (std::size_t axis = 0; axis < 2; ++axis) {
            if (direction[axis] == 0) {
                continue;
            }
            budget[axis] =
                determineMaxMovement(std::fabs(metric(axis)), sign(direction[axis]), maxLocation[axis], maxTravel[axis]);
        }
    } else {
        for (std::size_t axis = 0; axis < 2; ++axis) {
            if (direction[axis] == 0) {
                continue;
            }
            budget[axis] = axisTravelLimit(sign(direction[axis]), std::fabs(metric(axis)), maxTravel[axis]);
        }
    }

    // Z is bounded regardless of the homing flag: its switch is at the top.
    if (direction.Z != 0) {
        budget.Z = axisTravelLimit(sign(direction.Z), std::fabs(metric(2)), maxTravel[2]);
    }
    return budget;
}

}  // namespace gs::controller
