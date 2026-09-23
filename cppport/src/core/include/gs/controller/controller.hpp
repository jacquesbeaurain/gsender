#pragma once

// The machine controller: streams jobs and commands to a Grbl or grblHAL
// board, interprets its responses and reports state to the UI.
//
// A single class ports both GrblController.js and GrblHalController.js; the
// two share most of their logic and differ in many small ways, each of which
// is marked with an isGrbl()/isGrblHal() branch below and in the .cpp. The
// controller is driven entirely through:
//   * DeviceLink      - bytes to the board (write vs. immediate/realtime),
//   * receiveLine()   - each line from the board,
//   * EventLoop       - every timer (status polling, delayed commands),
//   * ControllerHooks - configuration (macros, event triggers, preferences),
// and reports everything through ControllerEvent callbacks.

#include "gs/controller/events.hpp"
#include "gs/controller/helpers.hpp"
#include "gs/controller/jog_streamer.hpp"
#include "gs/controller/streaming.hpp"
#include "gs/expr/value.hpp"
#include "gs/protocol/firmware_data.hpp"
#include "gs/protocol/runner.hpp"
#include "gs/runtime/event_loop.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::controller {

// How a write reaches the board. Normal writes go through the controller's
// write filter; immediate writes (status polls, overrides) bypass it.
enum class SendKind { Write, Immediate };

class DeviceLink {
public:
    virtual ~DeviceLink() = default;
    virtual bool isOpen() const = 0;
    virtual void send(std::string_view bytes, SendKind kind) = 0;
    virtual bool isNetwork() const { return false; }
};

// User preferences the controller consults ("preferences" in gSender's store).
struct Preferences {
    double spindleDelay = 0;  // seconds to dwell after M3/M4
    bool showLineWarnings = false;
    bool useAaxisForGrbl = false;  // Grbl: send A words instead of Y
};

struct Macro {
    std::string id;
    std::string name;
    std::string content;
};

struct ToolChangeContext {
    std::string option;  // "Ignore", "Pause", "Code", ...
    bool passthrough = false;
    std::string preHook;
    std::string postHook;
    bool skipDialog = false;
    // grblHAL tool remapping ("1" -> "7"); nullopt when never set.
    std::optional<std::map<std::string, std::string>> mappings;
};

struct ControllerHooks {
    std::function<std::optional<Macro>(std::string_view id)> findMacro;
    EventTrigger::Lookup findEvent;
    std::function<void(const std::string& commands)> runSystemCommands;
    std::function<void()> unloadFile;  // engine.unload(): forget the loaded file
    std::shared_ptr<Preferences> preferences;
};

// Arguments of "gcode:start".
struct StartOptions {
    std::size_t lineToStartFrom = 0;
    double zMax = 0;
    double safeHeight = 10;
    std::optional<double> spindleDelay;  // Grbl only; defaults to preferences
};

// Request/reply bookkeeping (actionMask in the JavaScript).
struct ActionMask {
    bool queryParserStateState = false;  // waiting for [GC:...]
    bool queryParserStateReply = false;  // waiting for its ok
    bool queryStatusReport = false;
    bool replyParserState = false;  // echo the next $G reply (user asked)
    bool replyStatusReport = false;  // echo the next status report
    bool alarmCompleteReport = false;  // grblHAL: poll 0x87 for the alarm code
    bool sdAccessory = false;          // grblHAL: SD card queried
};

class Controller {
public:
    Controller(runtime::EventLoop& loop, DeviceLink& link, protocol::Firmware firmware, ControllerHooks hooks,
               ControllerEventSink sink);
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;

    // ---- lifecycle ----
    void open();   // the link is open and the firmware identified
    void close();  // the link closed (or is about to)
    void receiveLine(std::string_view line);

    // ---- state ----
    protocol::Firmware firmware() const noexcept { return firmware_; }
    bool isGrbl() const noexcept { return firmware_ == protocol::Firmware::Grbl; }
    bool isGrblHal() const noexcept { return firmware_ == protocol::Firmware::GrblHal; }
    bool isOpen() const { return link_.isOpen(); }
    bool isReady() const noexcept { return ready_; }
    bool hasHomed() const noexcept { return hasHomedSet_; }
    bool homingFlag() const noexcept { return homingFlagSet_; }
    // grblHAL's spindle list query: "$spindlesh" from the 20231210 build
    // (ATCI support), "$spindles" before.
    std::string spindleListCommand() const {
        return runner_.settings().semver >= 20231210 ? "$spindlesh" : "$spindles";
    }
    // An alarm's description ("9", or "Homing" for Grbl's homing lock):
    // grblHAL's own ($EA) first, then the firmware tables.
    std::optional<protocol::CodeInfo> alarmInfo(const std::string& code) const;
    const protocol::RunnerState& state() const noexcept { return runner_.state(); }
    const protocol::FirmwareSettings& settings() const noexcept { return runner_.settings(); }
    const ToolChangeContext& toolChangeContext() const noexcept { return toolChangeContext_; }
    const ActionMask& actionMask() const noexcept { return actionMask_; }
    ActionMask& actionMask() noexcept { return actionMask_; }
    bool parserStateEnabled() const noexcept { return parserStateEnabled_; }
    std::int64_t timePaused() const noexcept { return timePaused_; }
    std::int64_t senderFinishTime() const noexcept { return senderFinishTime_; }
    const expr::Value& sharedContext() const noexcept { return sharedContext_; }

    protocol::Runner& runner() noexcept { return runner_; }
    const protocol::Runner& runner() const noexcept { return runner_; }
    Sender& sender() noexcept { return *sender_; }
    Feeder& feeder() noexcept { return *feeder_; }
    Workflow& workflow() noexcept { return workflow_; }
    JogStreamer& jogStreamer() noexcept { return *jogStreamer_; }
    ToolChanger& toolChanger() noexcept { return *toolChanger_; }

    // ---- program ----
    // "gcode:load": returns the sender status, or an error message.
    struct LoadResult {
        bool ok = false;
        std::string error;
        SenderStatus status;
    };
    LoadResult loadProgram(const std::string& name, std::string gcode, expr::Value context = expr::Value::object());
    // loadFile(gcode, meta, refresh): skipped while a job runs when refreshing.
    void loadFile(const std::string& name, std::string gcode, bool refresh = false);
    void unloadProgram();                        // "gcode:unload"
    void start(const StartOptions& options = {});  // "gcode:start"
    void stop(bool force = false);               // "gcode:stop"
    void pause();                                // "gcode:pause"
    void resume(bool ignoreEvents = false);      // "gcode:resume"
    void testProgram();                          // "gcode:test" (check mode)
    void updateEstimateData(std::vector<double> estimates, double estimatedTime);

    // ---- command queue ----
    void gcode(const std::vector<std::string>& commands, expr::Value context = expr::Value::object());
    void gcode(const std::string& commands, expr::Value context = expr::Value::object());
    void feederFeed(const std::vector<std::string>& commands, expr::Value context = expr::Value::object());
    void feederStart();
    void feederStop();
    // "gcode:safe": run in the preferred units, restoring the device units.
    void gcodeSafe(const std::vector<std::string>& commands, const std::string& preferredUnits);

    // ---- machine ----
    void feedHold();
    void cycleStart();
    void feedHoldAlt();    // grblHAL
    void cycleStartAlt();  // grblHAL
    void statusReport();
    void home(std::optional<char> axis = std::nullopt);
    void sleep();
    void unlock();
    void populateConfig();
    void reset();
    void resetSoft();   // grblHAL
    void resetLimit();
    void restart();     // grblHAL: $REBOOT
    void checkStateUpdate();
    void feedOverride(int value);
    void spindleOverride(int value);
    void rapidOverride(int value);
    void laserTestOn(double power, double duration);
    void laserTestOff();
    void laserPowerChange(double power, double maxS);
    void spindleSpeedChange(double speed);
    void realtimeReport();        // grblHAL
    void errorClear();            // grblHAL
    void toolChangeAcknowledge(); // grblHAL
    void virtualStopToggle();     // grblHAL
    void setRotaryMode(bool enabled);  // grblHAL
    void resetRunnerSettings();        // grblHAL

    // ---- jogging ----
    void jogStart(const Axes4& direction, double feedrate = 1000, JogUnits units = JogUnits::Millimetres);
    void jogUpdate(const Axes4& direction, std::optional<double> feedrate);
    void jogFeed(const Axes4& distances, std::optional<double> feedrate, JogUnits units = JogUnits::Millimetres);
    void jogStop();
    void jogCancel();

    // ---- macros, tool changes, wizards ----
    bool runMacro(std::string_view id, expr::Value context = expr::Value::object());
    std::optional<LoadResult> loadMacro(std::string_view id, expr::Value context = expr::Value::object());
    void setToolChangeContext(const ToolChangeContext& context);
    void toolChangePre();
    void toolChangePost();
    // Runs `gcode` once the board is idle; `started` is told when it has
    // been queued (a port addition, so a UI can hold its actions until then).
    void wizardStart(const std::string& gcode, std::function<void()> started = {});
    void wizardStep(int step, int substep);

    // ---- grblHAL SD card ----
    void sdMount();
    void sdList(bool all = false);
    void sdRead(const std::string& fileName);
    void sdRun(const std::string& path);
    void sdDelete(const std::string& path);

    // ---- raw writes (console) ----
    void write(std::string_view data);
    void writeln(std::string_view data, bool echo = false);
    // A console line typed by the user: remembered for error attribution and
    // ends an active jog stream first.
    void writeConsoleLine(std::string_view data);

    // The 250 ms status/parser-state poll. On by default; tests (and anything
    // that needs the link to itself, like a firmware transfer) can pause it.
    void setPollingEnabled(bool enabled);
    bool pollingEnabled() const noexcept { return queryTimer_ != 0; }

private:
    struct StartupStep {
        std::string commands;  // several commands are separated by newlines
        bool marksSdQueried = false;
    };

    void wireStreaming();
    void wireJogStreamer();
    std::string feederFilter(std::string line, const expr::Value& context);
    std::string senderFilter(std::string line, const expr::Value& context);
    void handle(const protocol::RunnerEvent& event);
    void onStatus(const protocol::StatusReport& report, const std::string& raw);
    void onOk(const std::string& raw);
    void onError(const std::string& message, const std::string& raw);
    void onAlarm(const std::string& message, const std::string& raw);
    void onStartupAlarm(const std::string& raw);
    void onParserState(const std::string& raw);
    void onSetting(const protocol::SettingLine& line, const std::string& raw);
    void onStartup(const std::string& raw, std::optional<long long> semver);
    void queryTick();
    void queryStatusReport();
    void queryParserState();
    void initController();
    void runStartupStep(std::shared_ptr<const std::vector<StartupStep>> steps, std::size_t index);
    void populateContext(const expr::Value& context) const;
    void clearActionValues();
    bool beginCommand(std::string_view name);
    void writeFiltered(std::string_view data);
    void writeImmediate(std::string_view data);
    void writeImmediateBytes(const std::vector<std::uint8_t>& bytes);
    void debounce(runtime::TimerId& timer, std::int64_t delayMs, std::function<void()> fn);
    void updateSpindleModal(const std::string& modal);
    void runPreChangeHook(const std::string& comment = {});
    void runPostChangeHook();
    void consumeFeederCallback();
    void report(ControllerEvent event) const;
    void emitState(std::optional<std::string> tool = std::nullopt);
    std::optional<protocol::CodeInfo> errorInfo(int code) const;
    std::pair<std::string, std::string> errorOrigin(bool checkHoming);
    Preferences preferences() const;
    // grblHAL [AXS:] probing
    void resetAxsProbe();
    void startAxsProbe();
    void resolveAxsProbe();

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    DeviceLink& link_;
    protocol::Firmware firmware_;
    ControllerHooks hooks_;
    ControllerEventSink sink_;

    protocol::Runner runner_;
    std::unique_ptr<Sender> sender_;
    std::unique_ptr<Feeder> feeder_;
    Workflow workflow_;
    std::unique_ptr<JogStreamer> jogStreamer_;
    std::unique_ptr<ToolChanger> toolChanger_;
    EventTrigger eventTrigger_;

    expr::Value sharedContext_ = expr::Value::object();
    expr::Value globals_ = expr::Value::object();
    ToolChangeContext toolChangeContext_;
    ActionMask actionMask_;
    std::function<void()> feederCallback_;

    bool ready_ = false;
    bool initialized_ = false;
    bool homingStarted_ = false;
    bool homingFlagSet_ = false;
    bool hasHomedSet_ = false;
    bool jogAnnounced_ = false;
    bool parserStateEnabled_ = false;
    bool alarmActive_ = false;
    bool isInRotaryMode_ = false;
    std::optional<std::string> consoleInput_;  // inAppConsoleInput

    std::int64_t timePaused_ = 0;
    std::int64_t queryStatusReportTime_ = 0;
    std::int64_t queryParserStateTime_ = 0;
    std::int64_t senderFinishTime_ = 0;
    std::int64_t lastParserQuery_ = 0;
    runtime::TimerId parserQueryTrailing_ = 0;
    runtime::TimerId queryTimer_ = 0;
    runtime::TimerId programResumeTimer_ = 0;
    runtime::TimerId axsProbeTimer_ = 0;
    runtime::TimerId descriptionsDebounce_ = 0;
    runtime::TimerId groupsDebounce_ = 0;

    // Snapshot of what was last reported, for change detection.
    std::uint64_t reportedStateRevision_ = ~std::uint64_t{0};
    std::uint64_t reportedSettingsRevision_ = ~std::uint64_t{0};
    protocol::AxisValues reportedWorkPosition_;
    std::string reportedActiveState_;

    // grblHAL [AXS:] probe
    int axsQueryCount_ = 0;
    std::int64_t axsQueryLastTime_ = 0;
    bool axsProbePending_ = false;
    enum class AxsSupport { Unknown, Supported, Unsupported } axsSupport_ = AxsSupport::Unknown;
};

}  // namespace gs::controller
