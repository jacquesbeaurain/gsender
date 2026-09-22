#include "gs/controller/helpers.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <boost/regex.hpp>

#include <cmath>

namespace gs::controller {

std::vector<std::uint8_t> overrideBytes(int difference, OverrideKind kind) {
    std::vector<std::uint8_t> bytes;
    if (difference == 0) {
        return bytes;
    }
    const bool feed = kind == OverrideKind::Feed;
    const std::uint8_t majorIncrease = feed ? 0x91 : 0x9A;
    const std::uint8_t majorDecrease = feed ? 0x92 : 0x9B;
    const std::uint8_t minorIncrease = feed ? 0x93 : 0x9C;
    const std::uint8_t minorDecrease = feed ? 0x94 : 0x9D;
    const int magnitude = std::abs(difference);
    const int majors = magnitude / 10;
    const int minors = magnitude % 10;
    bytes.insert(bytes.end(), static_cast<std::size_t>(majors), difference > 0 ? majorIncrease : majorDecrease);
    bytes.insert(bytes.end(), static_cast<std::size_t>(minors), difference > 0 ? minorIncrease : minorDecrease);
    return bytes;
}

namespace {

// A_AXIS_COMMANDS / Y_AXIS_COMMANDS (no /g flag: first match only).
const boost::regex& axisPattern(char axis) {
    static const boost::regex kA(R"(A(\d+\.\d+)|A (\d+\.\d+)|A(\d+)|A (\d+)|A-(\d+\.\d+)|A-(\d+))");
    static const boost::regex kY(R"(Y(\d+\.\d+)|Y (\d+\.\d+)|Y(\d+)|Y (\d+)|Y-(\d+\.\d+)|Y-(\d+))");
    return axis == 'A' ? kA : kY;
}

}  // namespace

bool hasAxisWord(std::string_view line, char axis) {
    const std::string text(line);
    return boost::regex_search(text, axisPattern(axis));
}

std::string translateAtoY(std::string_view line, TranslationUnits units) {
    const std::string text(line);
    boost::smatch m;
    if (!boost::regex_search(text, m, axisPattern('A'))) {
        return text;
    }
    // data.split("A")[1] -> parseFloat, converted, toFixed(3)
    const std::string word = m[0].str();
    double value = js::parseFloat(std::string_view(word).substr(1));
    if (units == TranslationUnits::ToImperial) {
        value /= 25.4;
    } else if (units == TranslationUnits::ToMetric) {
        value *= 25.4;
    }
    return text.substr(0, static_cast<std::size_t>(m.position())) + "Y" + js::toFixed(value, 3) +
           text.substr(static_cast<std::size_t>(m.position() + m.length()));
}

std::string applyRotaryTranslation(std::string_view line, bool imperial) {
    if (hasAxisWord(line, 'A') && !hasAxisWord(line, 'Y')) {
        return translateAtoY(line, imperial ? TranslationUnits::ToImperial : TranslationUnits::Default);
    }
    return std::string(line);
}

RealtimeExtraction extractRealtimeCommands(std::string_view line) {
    static const boost::regex kToken(R"(\[\\x([0-9a-fA-F]{1,2})\])");
    RealtimeExtraction out;
    const std::string text(line);
    std::string cleaned;
    std::size_t last = 0;
    for (boost::sregex_iterator it(text.begin(), text.end(), kToken), end; it != end; ++it) {
        const boost::smatch& m = *it;
        cleaned += text.substr(last, static_cast<std::size_t>(m.position()) - last);
        out.realtime.push_back(static_cast<std::uint8_t>(js::parseInt(m[1].str(), 16)));
        last = static_cast<std::size_t>(m.position() + m.length());
    }
    cleaned += text.substr(last);
    out.line = std::string(str::trim(cleaned));
    return out;
}

// ---- EventTrigger --------------------------------------------------------------

EventTrigger::EventTrigger(Lookup lookup, Callback callback)
    : lookup_(std::move(lookup)), callback_(std::move(callback)) {}

void EventTrigger::trigger(std::string_view key) const {
    if (key.empty() || !lookup_) {
        return;
    }
    const std::optional<EventConfig> config = lookup_(key);
    if (!config || !config->enabled) {
        return;
    }
    if (callback_) {
        callback_(config->event, config->trigger, config->commands);
    }
}

namespace {
bool isProgramEvent(std::string_view key) {
    return key == kProgramStart || key == kProgramEnd || key == kProgramPause || key == kProgramResume;
}
}  // namespace

bool EventTrigger::hasEnabledEvent(std::string_view key) const {
    if (!isProgramEvent(key) || !lookup_) {
        return false;
    }
    const std::optional<EventConfig> config = lookup_(key);
    return config && config->enabled && !config->commands.empty();
}

std::string EventTrigger::eventCode(std::string_view key) const {
    if (!hasEnabledEvent(key)) {
        return {};
    }
    return lookup_(key)->commands;
}

// ---- ToolChanger -----------------------------------------------------------------

ToolChanger::ToolChanger(runtime::EventLoop& loop, std::function<bool()> isIdle, std::int64_t intervalMs)
    : timers_(loop), isIdle_(std::move(isIdle)), intervalMs_(intervalMs) {}

void ToolChanger::addInterval(std::function<void()> callback) {
    timers_.clear(timer_);
    if (!callback) {
        return;
    }
    timer_ = timers_.interval(intervalMs_, [this, callback = std::move(callback)]() {
        if (isIdle_ && isIdle_()) {
            const auto run = callback;  // clearing the timer destroys this lambda
            clearInterval();
            run();
        }
    });
}

void ToolChanger::clearInterval() {
    timers_.clear(timer_);
}

}  // namespace gs::controller
