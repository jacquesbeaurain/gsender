#pragma once

// The UI side of jogging in gSender (src/app/src/features/Jogging/utils):
// step-jog commands, the limit-switch filter, the speed presets and the
// key/button helper that turns a tap into a step and a hold into a
// continuous jog. Continuous jogs themselves run in the controller
// (jog_streamer).

#include "gs/runtime/event_loop.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::controller {

// Axis letter -> distance (or direction), in the order the axes were added.
using JogAxes = std::vector<std::pair<char, double>>;

// jogAxis(): "$J=G21 G91 X5 Y-5 F3000" (G20 for inch workspaces).
std::string jogCommand(const JogAxes& axes, double feedrate, bool metric);

// filterAxesForLimits(): with "prevent jogging past limits" on, drops moves
// towards a triggered limit switch (X-, Y-, A- and Z+, the usual homing
// ends). `pinState` holds the triggered pin letters (Pn:). Nothing left:
// nullopt, and no jog is sent.
std::optional<JogAxes> filterAxesForLimits(const JogAxes& axes, std::string_view pinState,
                                           bool preventJoggingPastLimits);

// widgets.axes.jog presets.
struct JogSpeeds {
    double xyStep = 5;
    double zStep = 2;
    double aStep = 5;
    double feedrate = 3000;
    bool operator==(const JogSpeeds&) const = default;
};
enum class JogPreset { Rapid, Normal, Precise };
JogSpeeds defaultJogSpeeds(JogPreset preset);
JogPreset nextJogPreset(JogPreset preset);  // Rapid -> Normal -> Precise -> Rapid

// JogInput's - and + buttons: the value one step down or up. The step is a
// unit of the value's leading digit (0.01 for 0.02, 100 for 234), a digit
// finer where stepping down would pass it (110 - 10, 0.01 - 0.001); the
// result is rounded as upstream's formatNewValue: 3 decimals below 1, 2
// below 10, larger values to their second digit (-: up to it, +: down to
// it, ex. 115 -> 120 / 110, 45.1 -> 55 / 35). Never below 0.
double jogInputNudge(double current, bool increment);

// gSender's JogHelper: a key (or button) released within `thresholdMs` jogs
// one step; held longer, it starts a continuous jog that stops on release.
// Repeated key-downs while held are ignored (keyboard auto-repeat). As
// upstream, steps and starts are throttled to one per 150 ms and stops to
// one per threshold - 25 ms.
class JogHelper {
public:
    struct Callbacks {
        std::function<void(const JogAxes& distances, double feedrate)> jog;
        std::function<void(const JogAxes& distances, double feedrate)> startContinuous;
        std::function<void()> stopContinuous;
    };

    JogHelper(runtime::EventLoop& loop, Callbacks callbacks, int thresholdMs = 250);
    ~JogHelper();
    JogHelper(const JogHelper&) = delete;
    JogHelper& operator=(const JogHelper&) = delete;

    void keyDown(const JogAxes& distances, double feedrate);
    void keyUp();
    bool isPressed() const noexcept { return pressed_; }

private:
    // lodash throttle, leading edge only.
    struct Throttle {
        std::int64_t waitMs;
        std::optional<std::int64_t> last;
        bool allow(std::int64_t now);
    };

    runtime::EventLoop& loop_;
    Callbacks callbacks_;
    int thresholdMs_;
    runtime::TimerId timer_ = 0;
    bool pressed_ = false;  // the timeout is pending or fired (upstream's timeoutFunction)
    bool didPress_ = false;
    std::int64_t startTime_ = 0;
    JogAxes distances_;
    double feedrate_ = 3000;
    Throttle jogThrottle_{150, {}};
    Throttle continuousThrottle_{150, {}};
    Throttle stopThrottle_;
};

}  // namespace gs::controller
