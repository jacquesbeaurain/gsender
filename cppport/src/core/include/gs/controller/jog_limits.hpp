#pragma once

// Soft-limit travel budgets for jogging, and the homing-location helpers they
// depend on. Ports of src/server/lib/jog-limits.js and homing.js.

#include "gs/protocol/types.hpp"

#include <array>
#include <utility>

namespace gs::controller {

// Per-axis values in X, Y, Z, A order.
struct Axes4 {
    double X = 0;
    double Y = 0;
    double Z = 0;
    double A = 0;

    double& operator[](std::size_t i) noexcept { return i == 0 ? X : i == 1 ? Y : i == 2 ? Z : A; }
    double operator[](std::size_t i) const noexcept { return i == 0 ? X : i == 1 ? Y : i == 2 ? Z : A; }
    bool operator==(const Axes4&) const = default;
};

inline constexpr std::array<char, 4> kJogAxes{'X', 'Y', 'Z', 'A'};

// ---- homing.js ----

enum class HomingLocation { BackRight, BackLeft, FrontRight, FrontLeft };

// $23 (homing direction invert mask) -> which corner the machine homes to.
HomingLocation homingLocation(double homingMask) noexcept;

// Remaining travel (mm, signed) when machine zero is known. JS returned the
// value via toFixed(2); the result is rounded the same way.
double determineMaxMovement(double position, int movementDirection, int limitLocation, double limit);

// Direction (+1/-1) of the far end of X and Y for a homing corner.
std::pair<int, int> axisMaximumLocation(double homingMask) noexcept;

// Grbl: "machine zero" is inferred after homing when homing to anything but
// back-right leaves MPos at 0,0,0.
bool determineMachineZeroFlagSet(const protocol::AxisValues& mpos, const protocol::OrderedMap& settings);

// grblHAL: $22 bit 3 ("home to machine origin") sets the flag.
bool determineHalMachineZeroFlag(const protocol::OrderedMap& settings);

bool isBitSetInNumber(double number, int bit) noexcept;

// ---- jog-limits.js ----

inline constexpr double kTravelOffset = 1;  // mm safety margin at the limits

double axisTravelLimit(int direction, double position, double maxTravel, double offset = kTravelOffset);

// Signed remaining travel per axis for a jog in `direction`; +/-infinity where
// soft limits do not apply (A is always unbounded).
Axes4 computeTravelBudget(const Axes4& direction, const protocol::OrderedMap& settings,
                          const protocol::AxisValues& mpos, bool homingFlagSet, bool softLimitsEnabled);

}  // namespace gs::controller
