#pragma once

// Folds parsed firmware lines into accumulated machine state.
//
// Port of GrblRunner / GrblHalRunner. The JavaScript runners emitted an event
// per line type and signalled "state changed" by replacing the state object
// (the controllers compared object identity); here parse() returns the typed
// line plus flags, and revision counters change whenever state or settings do.

#include "gs/protocol/line_parser.hpp"
#include "gs/protocol/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::protocol {

enum class Firmware { Grbl, GrblHal };

std::string_view firmwareName(Firmware firmware) noexcept;  // "Grbl" / "grblHAL"

struct SdCardState {
    bool mounted = false;
    std::vector<SdFile> files;
    bool operator==(const SdCardState&) const = default;
};

struct RunnerState {
    MachineStatus status;
    ParserState parserState;
    AxesInfo axes;       // grblHAL
    SdCardState sdcard;  // grblHAL
    bool operator==(const RunnerState&) const = default;
};

// Result of feeding one line to the runner.
struct RunnerEvent {
    ResponseLine line;
    std::string raw;
    // Grbl: the status just entered Alarm (the "startupAlarm" event).
    bool enteredAlarm = false;
    // grblHAL: a [VER:] line carries the firmware build date ("startup" with semver).
    std::optional<long long> semver;
};

class Runner {
public:
    explicit Runner(Firmware firmware);

    Firmware firmware() const noexcept { return firmware_; }

    // Parses one line; trailing whitespace is ignored. Returns nullopt for an
    // empty line (nothing is emitted, as in the JavaScript).
    std::optional<RunnerEvent> parse(std::string_view line);

    const RunnerState& state() const noexcept { return state_; }
    const FirmwareSettings& settings() const noexcept { return settings_; }

    // Incremented whenever state()/settings() change.
    std::uint64_t stateRevision() const noexcept { return stateRevision_; }
    std::uint64_t settingsRevision() const noexcept { return settingsRevision_; }

    // ---- queries used by the controllers ----
    const AxisValues& machinePosition() const noexcept { return state_.status.mpos; }
    const AxisValues& workPosition() const noexcept { return state_.status.wpos; }
    const ModalState& modal() const noexcept { return state_.parserState.modal; }
    int tool() const;  // Number(parserstate.tool) || 0
    std::string currentFeedrate() const { return "F" + state_.parserState.feedrate; }
    std::string currentSpindleRate() const { return state_.parserState.spindle; }
    bool isAlarm() const noexcept { return state_.status.activeState == "Alarm"; }
    bool isIdle() const noexcept { return state_.status.activeState == "Idle"; }
    bool isCheck() const noexcept { return state_.status.activeState == "Check"; }
    bool hasSettings() const noexcept { return !settings_.settings.empty(); }
    std::string setting(std::string_view key, std::string_view fallback = {}) const {
        return settings_.settings.get(key, fallback);
    }
    bool hasAxs() const noexcept { return !state_.axes.letters.empty(); }
    bool isSdMounted() const noexcept { return state_.status.sdCard; }

    // ---- mutations the controllers perform directly ----
    void setActiveState(std::string state);  // e.g. "Home" when homing starts
    void setSpindleModal(std::string spindle);
    void setTool(std::string tool);
    // $13 changes are applied immediately when written (writeFilter in gSender).
    void setSetting(std::string_view key, std::string value);
    void deleteSettings();
    void clearSdFiles();
    void setSdStatus(bool mounted);
    // grblHAL without [AXS:]: guess XYZABC letters from the reported axis count.
    std::optional<std::string> setInferredAxesFromStatus();

private:
    void handleStatus(StatusReport& report, RunnerEvent& event);
    void handleCompleteStatus(StatusReport& report);
    void handleParserState(const ParserStateLine& line);
    void handleParameters(const ParametersLine& line);
    void deriveMissingPosition(StatusReport& report) const;
    void changeState(RunnerState next);
    void changeSettings(FirmwareSettings next);

    Firmware firmware_;
    RunnerState state_;
    FirmwareSettings settings_;
    std::uint64_t stateRevision_ = 0;
    std::uint64_t settingsRevision_ = 0;
};

}  // namespace gs::protocol
