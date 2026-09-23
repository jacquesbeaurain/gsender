#include "gs/controller/jogging.hpp"

#include "gs/util/jsnumber.hpp"

#include <cctype>

namespace gs::controller {

std::string jogCommand(const JogAxes& axes, double feedrate, bool metric) {
    std::string words;
    for (const auto& [letter, value] : axes) {
        if (!words.empty()) {
            words += ' ';
        }
        words += static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
        words += js::numberToString(value);
    }
    return std::string("$J=") + (metric ? "G21" : "G20") + " G91 " + words + " F" + js::numberToString(feedrate);
}

std::optional<JogAxes> filterAxesForLimits(const JogAxes& axes, std::string_view pinState,
                                           bool preventJoggingPastLimits) {
    if (!preventJoggingPastLimits) {
        return axes;
    }
    JogAxes kept;
    for (const auto& [letter, value] : axes) {
        const char axis = static_cast<char>(std::toupper(static_cast<unsigned char>(letter)));
        const bool triggered = pinState.find(axis) != std::string_view::npos;
        const bool towardsNegativeEnd = axis == 'X' || axis == 'Y' || axis == 'A';
        const bool blocked = triggered && (towardsNegativeEnd ? value < 0 : value > 0);
        if (!blocked) {
            kept.emplace_back(letter, value);
        }
    }
    if (kept.empty()) {
        return std::nullopt;
    }
    return kept;
}

JogSpeeds defaultJogSpeeds(JogPreset preset) {
    switch (preset) {
        case JogPreset::Rapid: return {20, 10, 20, 5000};
        case JogPreset::Precise: return {0.5, 0.1, 0.5, 1000};
        case JogPreset::Normal: break;
    }
    return {5, 2, 5, 3000};
}

JogPreset nextJogPreset(JogPreset preset) {
    switch (preset) {
        case JogPreset::Rapid: return JogPreset::Normal;
        case JogPreset::Normal: return JogPreset::Precise;
        case JogPreset::Precise: break;
    }
    return JogPreset::Rapid;
}

bool JogHelper::Throttle::allow(std::int64_t now) {
    if (last && now - *last < waitMs) {
        return false;
    }
    last = now;
    return true;
}

JogHelper::JogHelper(runtime::EventLoop& loop, Callbacks callbacks, int thresholdMs)
    : loop_(loop),
      callbacks_(std::move(callbacks)),
      thresholdMs_(thresholdMs),
      stopThrottle_{thresholdMs - 25, {}} {}

JogHelper::~JogHelper() {
    loop_.clear(timer_);
}

void JogHelper::keyDown(const JogAxes& distances, double feedrate) {
    if (pressed_) {
        return;  // held: auto-repeat
    }
    startTime_ = loop_.nowMs();
    distances_ = distances;
    didPress_ = true;
    feedrate_ = feedrate;
    pressed_ = true;
    timer_ = loop_.setTimeout(thresholdMs_, [this] {
        timer_ = 0;
        if (continuousThrottle_.allow(loop_.nowMs()) && callbacks_.startContinuous) {
            callbacks_.startContinuous(distances_, feedrate_);
        }
    });
}

void JogHelper::keyUp() {
    if (!pressed_) {
        return;
    }
    const std::int64_t held = loop_.nowMs() - startTime_;
    loop_.clear(timer_);
    timer_ = 0;
    pressed_ = false;
    if (held < thresholdMs_ && didPress_) {
        if (jogThrottle_.allow(loop_.nowMs()) && callbacks_.jog) {
            callbacks_.jog(distances_, feedrate_);
        }
    } else if (stopThrottle_.allow(loop_.nowMs()) && callbacks_.stopContinuous) {
        callbacks_.stopContinuous();
    }
    startTime_ = loop_.nowMs();
    didPress_ = false;
    distances_.clear();
}

}  // namespace gs::controller
