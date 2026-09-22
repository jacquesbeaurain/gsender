#pragma once

// Small pieces of controller behaviour ported from src/server/lib and
// src/server/controllers: override byte sequences, A->Y rotary translation,
// realtime command tokens, event triggers and the tool-change idle wait.

#include "gs/runtime/event_loop.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::controller {

// ---- runOverride.js ----

enum class OverrideKind { Feed, Spindle };

// Realtime bytes that move an override by `difference` percent: +/-10% steps
// first, then +/-1% steps.
std::vector<std::uint8_t> overrideBytes(int difference, OverrideKind kind);

// ---- gcode-translation.js ----

enum class TranslationUnits { Default, ToImperial, ToMetric };

// Does the line carry an A (resp. Y) axis word? (A_AXIS_COMMANDS / Y_AXIS_COMMANDS)
bool hasAxisWord(std::string_view line, char axis);

// Rewrites the first A word as a Y word (grbl has no rotary axis of its own).
std::string translateAtoY(std::string_view line, TranslationUnits units = TranslationUnits::Default);

// Applies the rotary rewrite when the line has an A word and no Y word.
std::string applyRotaryTranslation(std::string_view line, bool imperial);

// ---- extract-realtime-commands.js ----

// Strips literal "[\xNN]" tokens from a macro line, returning the bytes to
// send immediately and the remaining line (trimmed).
struct RealtimeExtraction {
    std::string line;
    std::vector<std::uint8_t> realtime;
};
RealtimeExtraction extractRealtimeCommands(std::string_view line);

// ---- EventTrigger.js ----

inline constexpr std::string_view kProgramStart = "gcode:start";
inline constexpr std::string_view kProgramEnd = "gcode:stop";
inline constexpr std::string_view kProgramPause = "gcode:pause";
inline constexpr std::string_view kProgramResume = "gcode:resume";
inline constexpr std::string_view kControllerReady = "controller:ready";
inline constexpr std::string_view kFileUnload = "file:unload";
inline constexpr std::string_view kFeedHold = "feedhold";
inline constexpr std::string_view kCycleStart = "cyclestart";
inline constexpr std::string_view kHoming = "homing";
inline constexpr std::string_view kSleep = "sleep";
inline constexpr std::string_view kMacroRun = "macro:run";
inline constexpr std::string_view kMacroLoad = "macro:load";

// One configured event trigger ("events" in the config store).
struct EventConfig {
    std::string event;
    std::string trigger;   // "gcode" or "system"
    std::string commands;  // G-code lines, or a shell command for "system"
    bool enabled = false;
};

class EventTrigger {
public:
    using Lookup = std::function<std::optional<EventConfig>(std::string_view key)>;
    using Callback = std::function<void(const std::string& event, const std::string& trigger, const std::string& commands)>;

    EventTrigger(Lookup lookup, Callback callback);

    void trigger(std::string_view key) const;
    // Only the four program events can gate job control.
    bool hasEnabledEvent(std::string_view key) const;
    std::string eventCode(std::string_view key) const;

private:
    Lookup lookup_;
    Callback callback_;
};

// ---- ToolChanger.js ----

// Runs a callback once the machine reports Idle (polled every 200 ms).
class ToolChanger {
public:
    ToolChanger(runtime::EventLoop& loop, std::function<bool()> isIdle, std::int64_t intervalMs = 200);
    void addInterval(std::function<void()> callback);
    void clearInterval();

private:
    runtime::TimerScope timers_;
    std::function<bool()> isIdle_;
    std::int64_t intervalMs_;
    runtime::TimerId timer_ = 0;
};

}  // namespace gs::controller
