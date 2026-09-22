#pragma once

// The three components the controllers stream through:
//
//   Sender   - streams a loaded program with Grbl's character-counting (or
//              send-response) protocol and runs the remaining-time countdown.
//   Feeder   - one-command-at-a-time queue for console input, macros and
//              internally generated G-code; holds on M0/M1/M6.
//   Workflow - idle / running / paused job state.
//
// Ports of src/server/lib/{Sender,Feeder,Workflow}.js. Their EventEmitter
// events are std::function callbacks; unset callbacks are skipped.

#include "gs/expr/value.hpp"
#include "gs/runtime/event_loop.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::controller {

// Why the sender or feeder is holding.
struct HoldReason {
    std::string data;     // "M0", "M1", "M6", "%wait", "%toolchange", ...
    std::string comment;  // comment text from the line that caused the hold
    std::string err;      // set when a job paused because of a firmware error
    bool operator==(const HoldReason&) const = default;
};

// ---- Sender --------------------------------------------------------------------

struct SenderStatus {
    bool characterCounting = true;
    bool hold = false;
    std::optional<HoldReason> holdReason;
    std::string name;
    std::size_t size = 0;  // program text length
    std::size_t total = 0;
    std::size_t sent = 0;
    std::size_t received = 0;
    std::int64_t startTime = 0;
    std::int64_t finishTime = 0;
    std::int64_t elapsedTime = 0;
    std::int64_t timePaused = 0;
    std::int64_t timeRunning = 0;
    double remainingTime = 0;  // seconds
    int toolChanges = 0;
    double estimatedTime = 0;  // seconds
    double ovF = 100;          // feed override used for the estimate
    bool isRotaryFile = false;
    std::int64_t currentLineRunning = 0;
    bool operator==(const SenderStatus&) const = default;
};

class Sender {
public:
    enum class Protocol { SendResponse, CharacterCounting };

    // Transforms each line before it is sent; may return "" to skip it (the
    // skipped line is acknowledged immediately). May re-enter the sender
    // (e.g. hold()).
    using DataFilter = std::function<std::string(std::string line, const expr::Value& context)>;

    Sender(runtime::EventLoop& loop, Protocol protocol, int bufferSize, DataFilter filter = {});

    std::function<void(const std::string& line)> onData;  // line ends with '\n'
    std::function<void(std::int64_t startTime)> onStart;
    std::function<void(std::int64_t finishTime)> onEnd;
    std::function<void()> onRequestData;  // estimate data wanted for a new program

    // Loads a program; returns false for an empty program.
    bool load(std::string name, std::string gcode, expr::Value context = expr::Value::object());
    void unload();

    // An acknowledgement ("ok"/"error") arrived for the oldest sent line.
    bool ack();
    // Fast-forwards to `line` (start from line).
    void setStartLine(std::size_t line);

    struct NextOptions {
        bool startFromLine = false;
        std::int64_t timePaused = 0;  // ms spent paused, reported on resume
        bool forceEnd = false;
        bool isOk = false;  // this call follows an "ok" (frees RX buffer space)
    };
    // Sends as many lines as the protocol allows.
    bool next(const NextOptions& options);
    bool next() { return next(NextOptions{}); }
    bool rewind();

    void hold(std::optional<HoldReason> reason = std::nullopt);
    void unhold();

    // True once per change since the last call (drives status broadcasts).
    bool peek();

    int incrementToolChanges();
    void setEstimateData(std::vector<double> estimates);
    void setEstimatedTime(double seconds);
    void setOvF(double ovF);
    void resumeCountdown();
    void pauseCountdown();
    void stopCountdown();
    bool isCountdownRunning() const noexcept { return !countdownPaused_; }

    SenderStatus status() const;

    // ---- state queries ----
    Protocol protocol() const noexcept { return protocol_; }
    int bufferSize() const noexcept { return bufferSize_; }
    // The buffer can grow but never shrink below what is in flight.
    void setBufferSize(int size);
    int dataLength() const noexcept { return dataLength_; }
    bool hasProgram() const noexcept { return !gcode_.empty(); }
    const std::string& name() const noexcept { return name_; }
    std::size_t total() const noexcept { return lines_.size(); }
    std::size_t sent() const noexcept { return sent_; }
    std::size_t received() const noexcept { return received_; }
    bool isHeld() const noexcept { return hold_; }
    const std::optional<HoldReason>& holdReason() const noexcept { return holdReason_; }
    std::int64_t finishTime() const noexcept { return finishTime_; }
    // Line `index` of the program (trimmed), or "" when out of range.
    std::string_view line(std::size_t index) const;
    const expr::Value& context() const noexcept { return context_; }
    std::int64_t currentLineRunning() const noexcept;

private:
    void process(bool isOk);
    void fakeCountdown();
    void updateElapsedTime();
    void markChanged() noexcept { changed_ = true; }

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    Protocol protocol_;
    DataFilter filter_;

    // character-counting state
    int bufferSize_ = 128;
    int dataLength_ = 0;
    std::deque<int> inFlight_;
    std::string pendingLine_;  // filtered line waiting for buffer space
    bool hasPendingLine_ = false;

    bool hold_ = false;
    std::optional<HoldReason> holdReason_;
    std::string name_;
    std::string gcode_;
    std::vector<std::pair<std::size_t, std::size_t>> lines_;  // offset/length into gcode_
    expr::Value context_ = expr::Value::object();
    std::size_t sent_ = 0;
    std::size_t received_ = 0;
    std::int64_t startTime_ = 0;
    std::int64_t finishTime_ = 0;
    std::int64_t elapsedTime_ = 0;
    std::int64_t timePaused_ = 0;
    std::int64_t timeRunning_ = 0;
    double remainingTime_ = 0;
    int toolChanges_ = 0;
    double estimatedTime_ = 0;
    std::vector<double> estimateData_;
    double ovF_ = 100;
    std::deque<double> countdownQueue_;
    std::size_t totalSentToQueue_ = 0;
    bool queueDone_ = true;
    double timer_ = 0;
    bool countdownPaused_ = false;
    bool isRotaryFile_ = false;
    bool changed_ = false;
    runtime::TimerId countdownTimer_ = 0;
    runtime::TimerId checkTimer_ = 0;
};

// ---- Feeder --------------------------------------------------------------------

struct FeederStatus {
    bool hold = false;
    std::optional<HoldReason> holdReason;
    std::size_t queue = 0;
    bool pending = false;
    bool changed = false;
    bool operator==(const FeederStatus&) const = default;
};

class Feeder {
public:
    // Transforms each command before it is written; "" skips it. The filter may
    // re-enter the feeder (hold(), reset(), feed()).
    using DataFilter = std::function<std::string(std::string command, const expr::Value& context)>;

    explicit Feeder(DataFilter filter = {});

    std::function<void(const std::string& command, const expr::Value& context)> onData;
    std::function<void()> onComplete;  // queue drained and not holding

    void feed(const std::vector<std::string>& commands, expr::Value context = expr::Value::object());
    void hold(std::optional<HoldReason> reason = std::nullopt);
    void unhold();
    void clear();
    void reset();
    std::size_t size() const noexcept { return queue_.size(); }
    // The commands waiting to be sent, oldest first.
    std::vector<std::string> queuedCommands() const;
    // Sends the next command unless holding; returns whether one is pending.
    bool next();
    bool isPending() const noexcept { return pending_; }
    bool isHeld() const noexcept { return hold_; }
    const std::optional<HoldReason>& holdReason() const noexcept { return holdReason_; }
    bool peek();
    void ack();
    bool hasOutstanding() const noexcept { return outstanding_ > 0; }
    int outstanding() const noexcept { return outstanding_; }
    FeederStatus status() const;

private:
    struct Item {
        std::string command;
        expr::Value context;
    };

    DataFilter filter_;
    std::deque<Item> queue_;
    bool hold_ = false;
    std::optional<HoldReason> holdReason_;
    bool pending_ = false;
    bool changed_ = false;
    int outstanding_ = 0;
};

// ---- Workflow ------------------------------------------------------------------

enum class WorkflowState { Idle, Running, Paused };

std::string_view workflowStateName(WorkflowState state) noexcept;  // "idle", "running", "paused"

class Workflow {
public:
    std::function<void()> onStart;
    std::function<void()> onStop;
    std::function<void(const std::optional<HoldReason>& reason)> onPause;
    std::function<void()> onResume;

    WorkflowState state() const noexcept { return state_; }
    bool isRunning() const noexcept { return state_ == WorkflowState::Running; }
    bool isPaused() const noexcept { return state_ == WorkflowState::Paused; }
    bool isIdle() const noexcept { return state_ == WorkflowState::Idle; }

    void start();
    void stop();
    void stopTesting();  // like stop(), but always notifies
    void resumeTesting();
    // Notifies even when not running (the JavaScript did too).
    void pause(std::optional<HoldReason> reason = std::nullopt);
    void resume();

private:
    WorkflowState state_ = WorkflowState::Idle;
};

}  // namespace gs::controller
