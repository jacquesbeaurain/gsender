#pragma once

// Jogging with a gamepad's sticks, from gSender's Jogging widget
// (features/Jogging/index.tsx's gamepad:axis handler, JoystickLoop.js,
// MPGJogManager.ts):
//
// - a stick pushed and let go within 600 ms jogs one step (the jog presets'
//   step) in the direction it pointed;
// - held longer, it streams a continuous jog, retargeted every 50 ms to the
//   stick's direction and - unless "fixed speed mode" - its deflection;
// - a stick whose action is an MPG axis is a handwheel: each quarter turn
//   jogs a step on that axis.
//
// The jogs go out through callbacks (the application sends them the way its
// jog buttons do); time comes from the core event loop.

#include "gs/controller/jogging.hpp"
#include "gs/gamepad/input.hpp"
#include "gs/gamepad/profile.hpp"
#include "gs/runtime/event_loop.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace gs::gamepad {

// What the Jogging widget knew when an axis moved.
struct StickContext {
    bool connected = false;         // a machine is connected
    bool canJog = false;            // canClickShortcut(): no job, idle or jogging
    controller::JogSpeeds speeds;   // the selected preset, in the workspace units
    bool rotaryMode = false;        // workspace.mode ROTARY
    bool grbl = true;               // Grbl rather than grblHAL
};

// Axis letters are upper case ('X', 'Y', 'Z', 'A').
struct StickCallbacks {
    // jogAxis(): one step (distances), after a short push.
    std::function<void(const controller::JogAxes& distances, double feedrate)> stepJog;
    // continuousJogAxis() / updateContinuousJog(): directions (+1/-1).
    std::function<void(const controller::JogAxes& directions, double feedrate)> startContinuous;
    std::function<void(const controller::JogAxes& directions, double feedrate)> updateContinuous;
    // feedJog(): distance added to a handwheel jog.
    std::function<void(const controller::JogAxes& distances, double feedrate)> feed;
    // stopContinuousJog().
    std::function<void()> stopContinuous;
};

// MPGJogManager: stick rotation to handwheel pulses (four per turn,
// clockwise positive), per stick and action.
class MpgJogManager {
public:
    struct Command {
        char axis;        // upper case
        double distance;  // signed
        double feedrate;
    };
    struct Params {
        const JoystickOptions* joystickOptions = nullptr;
        int stick = 0;          // 0: stick1, 1: stick2
        bool secondary = false; // the 2nd action button held
        const PadState* pad = nullptr;
        int degrees = 0;        // the stick's angle
        bool grbl = true;
        bool rotaryMode = false;
        bool canJog = false;
        std::optional<int> lockout;
        controller::JogSpeeds speeds;
    };
    std::optional<Command> buildCommand(const Params& params);
    void reset() { rotation_.clear(); }

private:
    struct Rotation {
        std::optional<int> lastDegrees;
        double accumulated = 0;
    };
    std::map<std::pair<int, bool>, Rotation> rotation_;
};

// The stick jogging of one gamepad (the Jogging widget's joystickLoop and
// mpgJogManager). Axis changes arrive through onAxis; the pad's live state
// through `pad`.
class StickJogger {
public:
    static constexpr std::int64_t kThrottleMs = 50;       // the gamepad:axis handler's throttle
    static constexpr std::int64_t kHoldMs = 600;          // JoystickLoop.timeoutAmount
    static constexpr std::int64_t kUpdateMs = 50;         // JOYSTICK_UPDATE_MS
    static constexpr std::int64_t kMpgIdleMs = 400;       // MPG_IDLE_MS

    StickJogger(runtime::EventLoop& loop, StickCallbacks callbacks, std::function<StickContext()> context,
                std::function<std::optional<PadState>(int index)> pad);
    ~StickJogger();
    StickJogger(const StickJogger&) = delete;
    StickJogger& operator=(const StickJogger&) = delete;

    // An axis of pad `index` changed; `profile` is the pad's profile.
    void onAxis(int index, int axis, const Profile& profile);
    // Stops everything at once - a continuous jog, a handwheel jog, a
    // pending step - without the step a short push would make: the pad went,
    // the window lost focus, the machine disconnected.
    void cancel();
    bool isRunning() const noexcept { return running_; }
    bool isStreaming() const noexcept { return streamActive_; }

private:
    void handleAxis();  // the throttled handler
    // JoystickLoop
    void start();
    void stop();
    void runJog();
    std::vector<std::optional<std::pair<char, int>>> axesAndDirection(int degrees) const;
    double computeFeedrate(double stickValue) const;
    void resetSmoothing();
    void mpgJog(const MpgJogManager::Command& command);

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    StickCallbacks callbacks_;
    std::function<StickContext()> context_;
    std::function<std::optional<PadState>(int)> pad_;

    // The throttle's latest call.
    runtime::TimerId throttle_ = 0;
    int pendingIndex_ = 0;
    int pendingAxis_ = 0;
    Profile pendingProfile_;

    MpgJogManager mpg_;
    runtime::TimerId mpgIdle_ = 0;

    // JoystickLoop's state.
    bool running_ = false;
    std::int64_t startTime_ = 0;
    runtime::TimerId holdTimer_ = 0;
    runtime::TimerId runTimer_ = 0;
    Profile profile_;
    int padIndex_ = 0;
    double feedrate_ = 0;
    std::vector<std::optional<std::pair<char, int>>> tapAxes_;  // what a short push steps
    std::array<double, 2> multiplier_{1, 1};                   // stick deflection (left, right)
    int degrees_ = 0;
    int activeAxis_ = -1;
    std::array<std::vector<double>, 4> axisHistory_;
    std::optional<std::string> currentDirection_;
    std::optional<std::string> pendingDirection_;
    int pendingDirectionCount_ = 0;
    bool streamActive_ = false;
    std::array<double, 4> variableSmoothed_{};
    char horizontalDominant_ = 0;  // 'x', 'y' or none
};

// computeAxesAndDirection(): the widget's 8 sectors of 30/60 degrees - what a
// short push steps: per stick direction (horizontal first), the axis and
// +1/-1, or none.
std::vector<std::optional<std::pair<char, int>>> tapAxesAndDirection(const StickOptions& stick, bool secondary,
                                                                      int degrees);

}  // namespace gs::gamepad
