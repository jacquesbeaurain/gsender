#include "gs/gamepad/input.hpp"

#include "gs/util/jsnumber.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gs::gamepad {

double roundAxis(double value) {
    // GamepadHandler.resolveAxisValue: Math.round(value * 100) / 100.
    return js::mathRound(value * 100) / 100;
}

int stickDegrees(double x, double y) {
    const double radians = std::atan2(y, x);
    const int degrees = -static_cast<int>(js::mathRound(radians * (180 / std::numbers::pi)));
    return (degrees + 360) % 360;
}

double stickDistance(double x, double y) {
    return js::stringToNumber(js::toFixed(std::sqrt(x * x + y * y), 2));
}

bool sticksIdle(const std::vector<double>& axes, double zeroThreshold) {
    const double deadZone = zeroThreshold / 100;
    if (deadZone == 0) {
        return std::all_of(axes.begin(), axes.end(), [](double a) { return a == 0; });
    }
    // lodash inRange(axis, -deadZone, deadZone): start inclusive, end exclusive.
    return std::all_of(axes.begin(), axes.end(), [deadZone](double a) { return a >= -deadZone && a < deadZone; });
}

bool Listener::anyConnected() const {
    return std::any_of(pads_.begin(), pads_.end(), [](const std::optional<PadState>& p) { return p.has_value(); });
}

std::vector<Event> Listener::update(const PadSlots& pads) {
    std::vector<Event> events;
    for (int index = 0; index < kSlots; ++index) {
        std::optional<PadState>& last = pads_[static_cast<std::size_t>(index)];
        const std::optional<PadState>& now = pads[static_cast<std::size_t>(index)];
        if (!now) {
            if (last) {
                events.emplace_back(std::in_place_type<Disconnected>, index, last->id);
                last.reset();
            }
            continue;
        }
        PadState state = *now;
        for (double& axis : state.axes) {
            axis = roundAxis(axis);
        }
        // A different pad in the same slot: the old one went.
        if (last && last->id != state.id) {
            events.emplace_back(std::in_place_type<Disconnected>, index, last->id);
            last.reset();
        }
        if (!last) {
            events.emplace_back(std::in_place_type<Connected>, index, state.id);
            last = std::move(state);
            continue;
        }
        // Axes before buttons, as GamepadHandler.update().
        for (std::size_t a = 0; a < state.axes.size(); ++a) {
            if (a >= last->axes.size() || last->axes[a] != state.axes[a]) {
                events.emplace_back(std::in_place_type<AxisChanged>, index, static_cast<int>(a), state.axes[a]);
            }
        }
        for (std::size_t b = 0; b < state.buttons.size(); ++b) {
            const bool was = b < last->buttons.size() && last->buttons[b];
            if (was != state.buttons[b]) {
                events.emplace_back(std::in_place_type<ButtonChanged>, index, static_cast<int>(b),
                                    static_cast<bool>(state.buttons[b]));
            }
        }
        last = std::move(state);
    }
    return events;
}

}  // namespace gs::gamepad
