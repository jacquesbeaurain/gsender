#include "gs/gamepad/stick_jog.hpp"

#include "gs/util/jsnumber.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <utility>

namespace gs::gamepad {

namespace {

// lodash inRange: start inclusive, end exclusive.
bool inRange(double value, double start, double end) {
    return value >= start && value < end;
}

char upper(char c) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

char axisLetter(const std::string& action) {
    return action.empty() ? 0 : static_cast<char>(std::tolower(static_cast<unsigned char>(action.front())));
}

const StickOptions& stickOf(const JoystickOptions& options, int axis) {
    return axis < 2 ? options.stick1 : options.stick2;
}

double axisValue(const PadState& pad, std::size_t index) {
    return index < pad.axes.size() ? pad.axes[index] : 0.0;
}

using Entry = std::optional<std::pair<char, int>>;

// The two sector tables share this shape: [X] / [X, Y] / [null, Y].
struct Directions {
    Entry xPositive, xNegative, yPositive, yNegative;
};

Directions directionsOf(const StickOptions& stick, bool secondary) {
    const auto direction = [](bool reversed) { return reversed ? -1 : 1; };
    const char x = axisLetter(secondary ? stick.horizontal.secondaryAction : stick.horizontal.primaryAction);
    const char y = axisLetter(secondary ? stick.vertical.secondaryAction : stick.vertical.primaryAction);
    Directions d;
    if (x) {
        d.xPositive = std::pair{x, direction(stick.horizontal.isReversed)};
        d.xNegative = std::pair{x, direction(!stick.horizontal.isReversed)};
    }
    if (y) {
        d.yPositive = std::pair{y, direction(stick.vertical.isReversed)};
        d.yNegative = std::pair{y, direction(!stick.vertical.isReversed)};
    }
    return d;
}

}  // namespace

std::vector<Entry> tapAxesAndDirection(const StickOptions& stick, bool secondary, int degrees) {
    const Directions d = directionsOf(stick, secondary);
    // Upstream's sectors leave whole degrees out (30, 59, 120, ...): nothing
    // is stepped there. Kept.
    if (inRange(degrees, 0, 30) || inRange(degrees, 330, 360)) {
        return {d.xPositive};
    }
    if (inRange(degrees, 31, 59)) {
        return {d.xPositive, d.yPositive};
    }
    if (inRange(degrees, 60, 120)) {
        return {std::nullopt, d.yPositive};
    }
    if (inRange(degrees, 121, 149)) {
        return {d.xNegative, d.yPositive};
    }
    if (inRange(degrees, 150, 210)) {
        return {d.xNegative};
    }
    if (inRange(degrees, 211, 239)) {
        return {d.xNegative, d.yNegative};
    }
    if (inRange(degrees, 240, 300)) {
        return {std::nullopt, d.yNegative};
    }
    if (inRange(degrees, 301, 329)) {
        return {d.xPositive, d.yNegative};
    }
    return {};
}

// ---- MPGJogManager ---------------------------------------------------------------------

std::optional<MpgJogManager::Command> MpgJogManager::buildCommand(const Params& p) {
    const StickOptions& stick = p.stick == 0 ? p.joystickOptions->stick1 : p.joystickOptions->stick2;
    const std::string& mpgAxis = p.secondary ? stick.mpgMode.secondaryAction : stick.mpgMode.primaryAction;
    if (mpgAxis.empty() || !p.canJog || !p.pad) {
        return std::nullopt;
    }
    const std::size_t xIndex = p.stick == 0 ? 0 : 2;
    const double x = axisValue(*p.pad, xIndex);
    const double y = axisValue(*p.pad, xIndex + 1);
    const double magnitude = std::sqrt(x * x + y * y);
    const double zeroThreshold = p.joystickOptions->zeroThreshold / 100;
    const bool holdingLockout = !p.lockout || isHeld(p.lockout, p.pad->buttons);
    const std::pair key{p.stick, p.secondary};
    if (magnitude < zeroThreshold || !holdingLockout) {
        rotation_[key] = Rotation{};
        return std::nullopt;
    }

    const char normalized = axisLetter(mpgAxis);
    const char resolved = normalized == 'a' && p.rotaryMode ? 'y' : normalized;
    if (p.grbl && normalized == 'a' && !p.rotaryMode) {
        return std::nullopt;
    }
    const double baseDistance = normalized == 'x' || normalized == 'y' ? p.speeds.xyStep
                                : normalized == 'z'                    ? p.speeds.zStep
                                : normalized == 'a'                    ? p.speeds.aStep
                                                                       : 0;
    if (baseDistance == 0) {
        return std::nullopt;
    }

    Rotation& state = rotation_[key];
    if (!state.lastDegrees) {
        state.lastDegrees = p.degrees;
        return std::nullopt;
    }
    double delta = p.degrees - *state.lastDegrees;
    if (delta > 180) {
        delta -= 360;
    } else if (delta < -180) {
        delta += 360;
    }
    state.lastDegrees = p.degrees;
    state.accumulated += delta;

    constexpr double kDegreesPerPulse = 90;  // four pulses per turn
    if (std::abs(state.accumulated) < kDegreesPerPulse) {
        return std::nullopt;
    }
    // Positive deltas are counter-clockwise: clockwise jogs positive.
    const int rotationSign = state.accumulated > 0 ? -1 : 1;
    const int sign = stick.mpgMode.isReversed ? -rotationSign : rotationSign;
    const double pulses = std::floor(std::abs(state.accumulated) / kDegreesPerPulse);
    state.accumulated = std::fmod(state.accumulated, kDegreesPerPulse);  // the remainder keeps turning
    const double feedrate = std::max(1.0, js::mathRound(p.speeds.feedrate * std::max(magnitude, 0.25)));
    return Command{upper(resolved), baseDistance * pulses * sign, feedrate};
}

// ---- StickJogger ------------------------------------------------------------------------

StickJogger::StickJogger(runtime::EventLoop& loop, StickCallbacks callbacks, std::function<StickContext()> context,
                         std::function<std::optional<PadState>(int)> pad)
    : loop_(loop), timers_(loop), callbacks_(std::move(callbacks)), context_(std::move(context)), pad_(std::move(pad)) {}

StickJogger::~StickJogger() = default;

void StickJogger::onAxis(int index, int axis, const Profile& profile) {
    if (axis < 0 || axis > 3) {
        return;  // the standard layout's four stick axes only
    }
    // The Gamepad class drops axis events while a set lockout button is up.
    if (const std::optional<PadState> pad = pad_(index);
        pad && profile.lockout && *profile.lockout < static_cast<int>(pad->buttons.size()) &&
        !isHeld(profile.lockout, pad->buttons)) {
        return;
    }
    // lodash throttle(50, {leading: false, trailing: true}): the last call in
    // each 50 ms runs at its end.
    pendingIndex_ = index;
    pendingAxis_ = axis;
    pendingProfile_ = profile;
    if (throttle_ == 0) {
        throttle_ = timers_.timeout(kThrottleMs, [this] {
            throttle_ = 0;
            handleAxis();
        });
    }
}

void StickJogger::handleAxis() {
    const StickContext context = context_();
    if (!context.connected) {
        return;
    }
    const std::optional<PadState> pad = pad_(pendingIndex_);
    if (!pad) {
        return;
    }
    const Profile& profile = pendingProfile_;
    const int axis = pendingAxis_;
    const JoystickOptions& options = profile.joystickOptions;
    const int leftDegrees = stickDegrees(axisValue(*pad, 0), axisValue(*pad, 1));
    const int rightDegrees = stickDegrees(axisValue(*pad, 2), axisValue(*pad, 3));
    const int degrees = axis < 2 ? leftDegrees : rightDegrees;
    const int stickIndex = axis < 2 ? 0 : 1;
    const StickOptions& stick = stickOf(options, axis);
    const bool secondary = isHeld(profile.modifier, pad->buttons);

    // A stick with an MPG axis for this action is a handwheel.
    if (!(secondary ? stick.mpgMode.secondaryAction : stick.mpgMode.primaryAction).empty()) {
        MpgJogManager::Params params;
        params.joystickOptions = &options;
        params.stick = stickIndex;
        params.secondary = secondary;
        params.pad = &*pad;
        params.degrees = degrees;
        params.grbl = context.grbl;
        params.rotaryMode = context.rotaryMode;
        params.canJog = context.canJog;
        params.lockout = profile.lockout;
        params.speeds = context.speeds;
        if (const auto command = mpg_.buildCommand(params)) {
            mpgJog(*command);
        }
        return;
    }

    std::vector<Entry> tap = tapAxesAndDirection(stick, secondary, degrees);
    if (sticksIdle(pad->axes, options.zeroThreshold)) {
        stop();
        return;
    }
    const bool sameStick = activeAxis_ < 0 || (activeAxis_ < 2) == (axis < 2);
    if (!sameStick && running_) {
        return;  // the other stick is jogging
    }
    profile_ = profile;
    padIndex_ = pendingIndex_;
    feedrate_ = context.speeds.feedrate;
    activeAxis_ = axis;
    tapAxes_ = std::move(tap);
    multiplier_ = {stickDistance(axisValue(*pad, 0), axisValue(*pad, 1)),
                   stickDistance(axisValue(*pad, 2), axisValue(*pad, 3))};
    degrees_ = degrees;
    start();
}

void StickJogger::mpgJog(const MpgJogManager::Command& command) {
    if (callbacks_.feed) {
        callbacks_.feed({{command.axis, command.distance}}, command.feedrate);
    }
    // A handwheel has no release: an idle gap ends the jog.
    timers_.clear(mpgIdle_);
    mpgIdle_ = timers_.timeout(kMpgIdleMs, [this] {
        mpgIdle_ = 0;
        if (callbacks_.stopContinuous) {
            callbacks_.stopContinuous();
        }
    });
}

void StickJogger::start() {
    if (running_) {
        return;
    }
    running_ = true;
    startTime_ = loop_.nowMs();
    holdTimer_ = timers_.timeout(kHoldMs, [this] {
        holdTimer_ = 0;
        runJog();
        if (running_) {
            runTimer_ = timers_.interval(kUpdateMs, [this] { runJog(); });
        }
    });
}

void StickJogger::resetSmoothing() {
    axisHistory_ = {};
    currentDirection_.reset();
    pendingDirection_.reset();
    pendingDirectionCount_ = 0;
    variableSmoothed_ = {};
    horizontalDominant_ = 0;
}

void StickJogger::stop() {
    if (!running_) {
        return;
    }
    timers_.clear(runTimer_);
    timers_.clear(holdTimer_);
    resetSmoothing();
    const bool wasStreaming = streamActive_;
    streamActive_ = false;
    running_ = false;

    if (loop_.nowMs() - startTime_ < kHoldMs) {
        // Let go before the hold time: one step.
        if (wasStreaming && callbacks_.stopContinuous) {
            callbacks_.stopContinuous();
        }
        // _axesArrayToObject: a later entry for the same axis wins.
        std::map<char, int> directions;
        for (const Entry& entry : tapAxes_) {
            if (entry) {
                directions[entry->first] = entry->second;
            }
        }
        if (directions.empty()) {
            return;
        }
        const StickContext context = context_();
        const controller::JogSpeeds& s = context.speeds;
        // handleJoystickJog({doRegularJog}): x, y, z, a in that order; A is
        // the rotary's Y in rotary mode.
        std::map<char, double> distances;
        for (const auto& [axis, direction] : directions) {
            if (axis == 'x') {
                distances['X'] = s.xyStep * direction;
            } else if (axis == 'y') {
                distances['Y'] = s.xyStep * direction;
            } else if (axis == 'z') {
                distances['Z'] = s.zStep * direction;
            }
        }
        if (const auto a = directions.find('a'); a != directions.end()) {
            distances[context.rotaryMode ? 'Y' : 'A'] = s.aStep * a->second;
        }
        controller::JogAxes out;
        for (const char axis : {'X', 'Y', 'Z', 'A'}) {
            if (const auto it = distances.find(axis); it != distances.end()) {
                out.emplace_back(axis, it->second);
            }
        }
        if (callbacks_.stepJog) {
            callbacks_.stepJog(out, s.feedrate);
        }
        return;
    }
    if (wasStreaming && callbacks_.stopContinuous) {
        callbacks_.stopContinuous();
    }
}

void StickJogger::cancel() {
    timers_.clear(throttle_);
    const bool mpgPending = mpgIdle_ != 0;
    timers_.clear(mpgIdle_);
    mpg_.reset();
    const bool wasStreaming = streamActive_;
    timers_.clear(runTimer_);
    timers_.clear(holdTimer_);
    resetSmoothing();
    streamActive_ = false;
    running_ = false;
    activeAxis_ = -1;
    if ((wasStreaming || mpgPending) && callbacks_.stopContinuous) {
        callbacks_.stopContinuous();
    }
}

std::vector<Entry> StickJogger::axesAndDirection(int degrees) const {
    // JoystickLoop._getAxesAndDirection: finer diagonals than the tap's, with
    // the 2nd actions read live.
    const std::optional<PadState> pad = pad_(padIndex_);
    const bool secondary = pad && isHeld(profile_.modifier, pad->buttons);
    const Directions d = directionsOf(stickOf(profile_.joystickOptions, activeAxis_), secondary);
    if (inRange(degrees, 0, 15) || inRange(degrees, 345, 360)) {
        return {d.xPositive};
    }
    if (inRange(degrees, 16, 74)) {
        return {d.xPositive, d.yPositive};
    }
    if (inRange(degrees, 75, 105)) {
        return {std::nullopt, d.yPositive};
    }
    if (inRange(degrees, 106, 164)) {
        return {d.xNegative, d.yPositive};
    }
    if (inRange(degrees, 165, 195)) {
        return {d.xNegative};
    }
    if (inRange(degrees, 196, 254)) {
        return {d.xNegative, d.yNegative};
    }
    if (inRange(degrees, 255, 285)) {
        return {std::nullopt, d.yNegative};
    }
    if (inRange(degrees, 286, 344)) {
        return {d.xPositive, d.yNegative};
    }
    return {};
}

double StickJogger::computeFeedrate(double stickValue) const {
    // The movement override scales the speed (upstream's jog stream has no
    // per-command distance left to scale).
    const double feedrate = feedrate_ * (profile_.joystickOptions.movementDistanceOverride / 100);
    if (profile_.joystickOptions.fixedSpeedMode) {
        return js::mathRound(feedrate);
    }
    if (!std::isfinite(stickValue)) {
        return 0;
    }
    return js::mathRound(std::abs(feedrate * stickValue));
}

void StickJogger::runJog() {
    const std::optional<PadState> pad = pad_(padIndex_);
    if (!pad) {
        return;
    }
    const bool fixedSpeed = profile_.joystickOptions.fixedSpeedMode;
    std::array<double, 4> values{};
    for (std::size_t i = 0; i < 4; ++i) {
        values[i] = axisValue(*pad, i);
        if (fixedSpeed) {
            // A rolling average of three polls.
            std::vector<double>& history = axisHistory_[i];
            history.push_back(values[i]);
            if (history.size() > 3) {
                history.erase(history.begin());
            }
            double sum = 0;
            for (const double v : history) {
                sum += v;
            }
            values[i] = sum / static_cast<double>(history.size());
        }
    }

    // Fixed speed mode locks onto one of 8 directions.
    int degreesForAxes = degrees_;
    if (fixedSpeed) {
        std::optional<std::string> key;
        const double deg = degrees_;
        if (inRange(deg, 0, 22.5) || inRange(deg, 337.5, 360)) {
            key = "X+";
        } else if (inRange(deg, 22.5, 67.5)) {
            key = "X+Y+";
        } else if (inRange(deg, 67.5, 112.5)) {
            key = "Y+";
        } else if (inRange(deg, 112.5, 157.5)) {
            key = "X-Y+";
        } else if (inRange(deg, 157.5, 202.5)) {
            key = "X-";
        } else if (inRange(deg, 202.5, 247.5)) {
            key = "X-Y-";
        } else if (inRange(deg, 247.5, 292.5)) {
            key = "Y-";
        } else if (inRange(deg, 292.5, 337.5)) {
            key = "X+Y-";
        }
        if (key != pendingDirection_) {
            pendingDirection_ = key;
            pendingDirectionCount_ = 1;
        } else {
            ++pendingDirectionCount_;
        }
        constexpr int kStabilityFrames = 1;
        if (pendingDirectionCount_ >= kStabilityFrames && currentDirection_ != pendingDirection_) {
            currentDirection_ = pendingDirection_;
        }
        if (currentDirection_) {
            static const std::map<std::string, int> centers{{"X+", 0},    {"X+Y+", 45},  {"Y+", 90},  {"X-Y+", 135},
                                                            {"X-", 180},  {"X-Y-", 225}, {"Y-", 270}, {"X+Y-", 315}};
            degreesForAxes = centers.at(*currentDirection_);
        }
    }

    const std::vector<Entry> axes = axesAndDirection(degreesForAxes);
    const int numberOfAxes =
        static_cast<int>(std::count_if(axes.begin(), axes.end(), [](const Entry& e) { return e.has_value(); }));
    const std::size_t base = activeAxis_ < 2 ? 0 : 2;
    std::array<double, 2> filtered{values[base], values[base + 1]};

    const std::vector<double> all(values.begin(), values.end());
    const bool lockoutReleased = profile_.lockout && !isHeld(profile_.lockout, pad->buttons);
    if (sticksIdle(all, profile_.joystickOptions.zeroThreshold) || lockoutReleased) {
        stop();
        return;
    }

    if (!fixedSpeed && activeAxis_ < 2) {
        // Variable speed on the XY stick: smoothed, tiny movements idle, and
        // one axis dominant (with hysteresis) unless the stick is clearly
        // diagonal.
        constexpr double kIdleThreshold = 0.14;
        constexpr double kEnterRatio = 1.8;
        constexpr double kExitRatio = 1.35;
        constexpr double kAlpha = 0.35;
        double& sx = variableSmoothed_[base];
        double& sy = variableSmoothed_[base + 1];
        sx += kAlpha * (filtered[0] - sx);
        sy += kAlpha * (filtered[1] - sy);
        const double x = sx;
        const double y = sy;
        const double absX = std::abs(x);
        const double absY = std::abs(y);
        if (std::max(absX, absY) < kIdleThreshold) {
            horizontalDominant_ = 0;
            return;
        }
        if (horizontalDominant_ == 'x') {
            if (absY > absX * kEnterRatio) {
                horizontalDominant_ = 'y';
            } else if (absX < absY * kExitRatio) {
                horizontalDominant_ = 0;
            }
        } else if (horizontalDominant_ == 'y') {
            if (absX > absY * kEnterRatio) {
                horizontalDominant_ = 'x';
            } else if (absY < absX * kExitRatio) {
                horizontalDominant_ = 0;
            }
        } else if (absX > absY * kEnterRatio) {
            horizontalDominant_ = 'x';
        } else if (absY > absX * kEnterRatio) {
            horizontalDominant_ = 'y';
        }
        filtered = horizontalDominant_ == 'x'   ? std::array<double, 2>{x, 0}
                   : horizontalDominant_ == 'y' ? std::array<double, 2>{0, y}
                                                : std::array<double, 2>{x, y};
    }

    const double multiplier = activeAxis_ < 2 ? multiplier_[0] : multiplier_[1];
    const double feedrate =
        computeFeedrate(numberOfAxes == 1 ? std::max(std::abs(filtered[0]), std::abs(filtered[1])) : multiplier);
    if (!std::isfinite(feedrate) || feedrate <= 0) {
        return;
    }
    if (fixedSpeed && !currentDirection_) {
        return;
    }

    controller::JogAxes direction;
    for (std::size_t i = 0; i < 2 && i < axes.size(); ++i) {
        if (!axes[i] || filtered[i] == 0) {
            continue;
        }
        const char axis = upper(axes[i]->first);
        const double sign = axes[i]->second > 0 ? 1 : -1;
        const auto existing = std::find_if(direction.begin(), direction.end(),
                                           [axis](const std::pair<char, double>& d) { return d.first == axis; });
        if (existing != direction.end()) {
            existing->second = sign;
        } else {
            direction.emplace_back(axis, sign);
        }
    }
    if (direction.empty()) {
        return;
    }
    if (streamActive_) {
        if (callbacks_.updateContinuous) {
            callbacks_.updateContinuous(direction, feedrate);
        }
    } else {
        if (callbacks_.startContinuous) {
            callbacks_.startContinuous(direction, feedrate);
        }
        streamActive_ = true;
    }
}

}  // namespace gs::gamepad
