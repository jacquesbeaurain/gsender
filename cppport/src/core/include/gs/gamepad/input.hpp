#pragma once

// Gamepad input as gSender reads it (src/app/src/lib/gamepad/gamepad.js and
// the Gamepad class in lib/gamepad/index.ts): up to four pads, polled; a
// change of a button or of an axis (rounded to two decimals) is an event.
// Where the states come from (SDL, a test's fake) is the application's
// business.

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gs::gamepad {

inline constexpr int kSlots = 4;  // navigator.getGamepads()'s four

// One pad now, in the standard layout: buttons 0-16, axes left X, left Y,
// right X, right Y (-1..1, Y down positive).
struct PadState {
    std::string id;         // matched against the profiles' ids
    bool standard = true;   // mapping === "standard"
    std::vector<bool> buttons;
    std::vector<double> axes;
    bool operator==(const PadState&) const = default;
};

using PadSlots = std::array<std::optional<PadState>, kSlots>;

struct Connected {
    int index;
    std::string id;
};
struct Disconnected {
    int index;
    std::string id;
};
struct ButtonChanged {
    int index;
    int button;
    bool pressed;
};
struct AxisChanged {
    int index;
    int axis;
    double value;
};
using Event = std::variant<Connected, Disconnected, ButtonChanged, AxisChanged>;

// GamepadListener + GamepadHandler: diffs each poll against the last. A pad
// that appears reports Connected (no events for its initial values); one
// that goes, Disconnected.
class Listener {
public:
    std::vector<Event> update(const PadSlots& pads);
    // The last polled state of a slot (axes as reported: rounded).
    const std::optional<PadState>& pad(int index) const { return pads_.at(static_cast<std::size_t>(index)); }
    bool anyConnected() const;

private:
    PadSlots pads_;
};

// The axis value as the handler reports it: two decimals (precision 2).
double roundAxis(double value);

// cartesian2Polar(): the stick's angle in whole degrees, counter-clockwise
// from right (0-359; stick up, a negative Y, is 90).
int stickDegrees(double x, double y);
// cartesian2PolarDistance(): how far the stick is pushed, two decimals.
double stickDistance(double x, double y);

// checkThumbsticskAreIdle(): every axis inside the zero threshold (%); with
// none, exactly 0.
bool sticksIdle(const std::vector<double>& axes, double zeroThreshold);

}  // namespace gs::gamepad
