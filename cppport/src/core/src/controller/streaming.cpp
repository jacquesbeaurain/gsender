#include "gs/controller/streaming.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <cmath>

namespace gs::controller {
namespace {

// Matches gcode::LineKind.
constexpr std::uint8_t kLineKindNone = 0;
constexpr std::uint8_t kLineKindFeed = 1;
constexpr std::uint8_t kLineKindRapid = 2;
constexpr std::uint8_t kLineKindFixed = 3;

std::size_t remainingIndexForKind(std::uint8_t kind) {
    return kind == kLineKindRapid ? 1 : kind == kLineKindFixed ? 2 : 0;
}

// rotary.js: does the program (comments removed) mention an A word at all?
bool isRotaryProgram(std::string_view gcode) {
    for (std::string_view line : str::splitLines(gcode)) {
        bool inParen = false;
        for (char c : line) {
            if (inParen) {
                inParen = c != ')';
                continue;
            }
            if (c == '(') {
                inParen = true;
            } else if (c == ';') {
                break;
            } else if (c == 'A') {
                return true;
            }
        }
    }
    return false;
}

}  // namespace

// ---- Sender --------------------------------------------------------------------

Sender::Sender(runtime::EventLoop& loop, Protocol protocol, int bufferSize, DataFilter filter)
    : loop_(loop), timers_(loop), protocol_(protocol), filter_(std::move(filter)) {
    if (bufferSize > 0) {
        bufferSize_ = bufferSize;
    }
}

void Sender::setBufferSize(int size) {
    if (size <= 0) {
        return;
    }
    bufferSize_ = std::max(size, dataLength_);
}

std::string_view Sender::line(std::size_t index) const {
    if (index >= lines_.size()) {
        return {};
    }
    return std::string_view(gcode_).substr(lines_[index].first, lines_[index].second);
}

bool Sender::load(std::string name, std::string gcode, expr::Value context) {
    if (gcode.empty()) {
        return false;
    }
    // Keep only lines with content, trimmed, as views into the program text.
    std::vector<std::pair<std::size_t, std::size_t>> lines;
    for (std::string_view raw : str::splitLines(gcode)) {
        const std::string_view trimmed = str::trim(raw);
        if (!trimmed.empty()) {
            lines.emplace_back(static_cast<std::size_t>(trimmed.data() - gcode.data()), trimmed.size());
        }
    }

    dataLength_ = 0;
    inFlight_.clear();
    pendingLine_.clear();
    hasPendingLine_ = false;
    hold_ = false;
    holdReason_.reset();
    name_ = std::move(name);
    isRotaryFile_ = isRotaryProgram(gcode);
    gcode_ = std::move(gcode);
    lines_ = std::move(lines);
    context_ = context.isObject() ? std::move(context) : expr::Value::object();
    sent_ = 0;
    received_ = 0;
    startTime_ = 0;
    finishTime_ = 0;
    elapsedTime_ = 0;
    timePaused_ = 0;
    timeRunning_ = 0;
    remainingTime_ = 0;
    toolChanges_ = 0;
    estimatedTime_ = 0;
    clearEstimateData();

    if (onRequestData) {
        onRequestData();
    }
    markChanged();
    return true;
}

void Sender::unload() {
    dataLength_ = 0;
    inFlight_.clear();
    pendingLine_.clear();
    hasPendingLine_ = false;
    hold_ = false;
    holdReason_.reset();
    name_.clear();
    gcode_.clear();
    lines_.clear();
    context_ = expr::Value::object();
    sent_ = 0;
    received_ = 0;
    startTime_ = 0;
    finishTime_ = 0;
    elapsedTime_ = 0;
    timePaused_ = 0;
    timeRunning_ = 0;
    remainingTime_ = 0;
    toolChanges_ = 0;
    estimatedTime_ = 0;
    clearEstimateData();
    isRotaryFile_ = false;
    markChanged();
}

bool Sender::ack() {
    if (gcode_.empty() || received_ >= sent_) {
        return false;
    }
    ++received_;
    markChanged();
    return true;
}

void Sender::setStartLine(std::size_t line) {
    sent_ = line;
    received_ = line;
}

void Sender::process(bool isOk) {
    if (protocol_ == Protocol::CharacterCounting) {
        // Only an "ok" frees the oldest line's bytes from the RX buffer.
        if (!inFlight_.empty() && isOk) {
            dataLength_ -= inFlight_.front();
            inFlight_.pop_front();
        }
        while (!hold_ && sent_ < lines_.size()) {
            // A line cached from an earlier pass is not filtered again - the
            // filter has side effects (tool remapping, events).
            if (!hasPendingLine_) {
                std::string text(line(sent_));
                pendingLine_ = filter_ ? filter_(std::move(text), context_) : std::move(text);
                hasPendingLine_ = true;
            }
            // The newline counts against the RX buffer too.
            if (!pendingLine_.empty() &&
                dataLength_ + static_cast<int>(pendingLine_.size()) + 1 >= bufferSize_) {
                break;
            }
            ++sent_;
            markChanged();
            hasPendingLine_ = false;
            if (pendingLine_.empty()) {
                ack();
                continue;
            }
            std::string out = std::move(pendingLine_) + "\n";
            pendingLine_.clear();
            dataLength_ += static_cast<int>(out.size());
            inFlight_.push_back(static_cast<int>(out.size()));
            if (onData) {
                onData(out);
            }
        }
        return;
    }

    // send-response: one line per acknowledgement
    while (!hold_ && sent_ < lines_.size()) {
        std::string text(line(sent_));
        std::string filtered = filter_ ? filter_(std::move(text), context_) : std::move(text);
        ++sent_;
        markChanged();
        if (filtered.empty()) {
            ack();
            continue;
        }
        if (onData) {
            onData(filtered + "\n");
        }
        break;
    }
}

bool Sender::next(const NextOptions& options) {
    if (gcode_.empty()) {
        return false;
    }
    const std::int64_t now = loop_.nowMs();

    const auto handleStart = [&]() {
        startTime_ = now;
        finishTime_ = 0;
        elapsedTime_ = 0;
        timePaused_ = 0;
        timeRunning_ = 0;
        lastProgressTick_ = now;
        jobActive_ = true;
        resetPlayhead();
        // Start from line: everything before the start line counts as done.
        if (options.startFromLine) {
            advancePlayheadTo(received_);
        }
        updateRemainingTime();
        if (onStart) {
            onStart(startTime_);
        }
        markChanged();
    };

    if (options.startFromLine) {
        handleStart();
    } else if (!lines_.empty() && sent_ == 0) {
        received_ = 0;
        handleStart();
    }

    if (options.timePaused) {
        // One second is spent holding and unholding.
        timePaused_ += options.timePaused - 1000;
    }

    process(options.isOk);
    updateElapsedTime();

    if (received_ >= lines_.size() || options.forceEnd) {
        if (finishTime_ == 0) {
            finishTime_ = now;
            if (onEnd) {
                onEnd(finishTime_);
            }
            markChanged();
        }
    }
    return true;
}

bool Sender::rewind() {
    if (gcode_.empty()) {
        return false;
    }
    dataLength_ = 0;
    inFlight_.clear();
    pendingLine_.clear();
    hasPendingLine_ = false;
    hold_ = false;
    holdReason_.reset();
    sent_ = 0;
    received_ = 0;
    toolChanges_ = 0;
    // remainingTime is left as-is so a finished job keeps showing 0.
    jobActive_ = false;
    execLine_ = 0;
    execFrac_ = 0;
    markChanged();
    return true;
}

void Sender::hold(std::optional<HoldReason> reason) {
    if (hold_) {
        return;
    }
    hold_ = true;
    holdReason_ = std::move(reason);
    markChanged();
}

void Sender::unhold() {
    if (!hold_) {
        return;
    }
    hold_ = false;
    holdReason_.reset();
    markChanged();
}

bool Sender::peek() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

int Sender::incrementToolChanges() {
    ++toolChanges_;
    markChanged();
    return toolChanges_;
}

void Sender::setEstimateData(std::vector<float> lineTime, std::vector<std::uint8_t> lineKind,
                             double estimatedTime) {
    lineTime_ = std::move(lineTime);
    lineKind_ = std::move(lineKind);
    lineKind_.resize(lineTime_.size(), kLineKindNone);
    double total = 0;
    for (const float time : lineTime_) {
        total += time;
    }
    estimatedTime_ = std::isfinite(estimatedTime) && estimatedTime != 0 ? estimatedTime : total;
    resetPlayhead();
    if (jobActive_) {
        advancePlayheadTo(received_);
    }
    updateRemainingTime();
    markChanged();
}

void Sender::clearEstimateData() {
    jobActive_ = false;
    lineTime_.clear();
    lineKind_.clear();
    resetPlayhead();
    lastProgressTick_ = 0;
    idleReports_ = 0;
}

void Sender::setOvF(double ovF) {
    if (ovF > 0) {
        ovF_ = ovF;
        updateRemainingTime();
    }
}

void Sender::resetPlayhead() {
    execLine_ = 0;
    execFrac_ = 0;
    remaining_ = {};
    for (std::size_t i = 0; i < lineTime_.size(); ++i) {
        remaining_[remainingIndexForKind(lineKind_[i])] += lineTime_[i];
    }
}

double Sender::lineRate(std::uint8_t kind) const {
    if (kind == kLineKindFixed) {
        return 1;
    }
    const double ov = kind == kLineKindRapid ? ovR_ : ovF_;
    return std::max(ov != 0 && std::isfinite(ov) ? ov : 100, 1.0) / 100;
}

// Marks `fraction` (0..1) of line `line` executed.
void Sender::consumeLine(std::size_t line, double fraction) {
    const double time = line < lineTime_.size() ? lineTime_[line] : 0;
    if (time > 0 && fraction > 0) {
        double& left = remaining_[remainingIndexForKind(lineKind_[line])];
        left = std::max(0.0, left - time * fraction);
    }
}

// Jumps the playhead forward to the start of `line`.
void Sender::advancePlayheadTo(std::size_t line) {
    const std::size_t target = std::min(line, lines_.size());
    if (target <= execLine_) {
        return;
    }
    consumeLine(execLine_, 1 - execFrac_);
    for (std::size_t i = execLine_ + 1; i < target; ++i) {
        consumeLine(i, 1);
    }
    execLine_ = target;
    execFrac_ = 0;
}

// Runs the playhead for `seconds` of wall time, never past `limit` lines.
void Sender::advancePlayheadBy(double seconds, std::size_t limit) {
    double dt = seconds;
    while (dt > 0 && execLine_ < limit) {
        const std::size_t i = execLine_;
        const std::uint8_t kind = i < lineKind_.size() ? lineKind_[i] : kLineKindNone;
        const double duration = (i < lineTime_.size() ? lineTime_[i] : 0) / lineRate(kind);
        const double left = duration * (1 - execFrac_);
        if (dt >= left) {
            dt -= left;
            consumeLine(i, 1 - execFrac_);
            ++execLine_;
            execFrac_ = 0;
        } else {
            const double fraction = dt / duration;
            consumeLine(i, fraction);
            execFrac_ += fraction;
            dt = 0;
        }
    }
}

// The highest line that must have finished executing, from planner
// occupancy: the last `queuedBlocks` motion lines received may still be in
// the planner.
std::size_t Sender::executedLowerBound(int queuedBlocks) const {
    std::size_t line = received_;
    int queued = queuedBlocks;
    while (queued > 0 && line > execLine_) {
        --line;
        const std::uint8_t kind = line < lineKind_.size() ? lineKind_[line] : kLineKindNone;
        if (kind != kLineKindNone && kind != kLineKindFixed) {
            --queued;
        }
    }
    return line;
}

void Sender::updateRemainingTime() {
    const double remaining =
        remaining_[0] / lineRate(kLineKindFeed) + remaining_[1] / lineRate(kLineKindRapid) + remaining_[2];
    remainingTime_ = std::max(0.0, js::toFixedNumber(remaining, 3));
}

void Sender::updateProgress(const protocol::StatusReport& status, std::int64_t now) {
    const double dtSeconds =
        lastProgressTick_ != 0 ? std::max<double>(0, static_cast<double>(now - lastProgressTick_) / 1000) : 0;
    lastProgressTick_ = now;

    if (status.overrides) {
        if ((*status.overrides)[0] > 0) {
            ovF_ = (*status.overrides)[0];
        }
        if ((*status.overrides)[1] > 0) {
            ovR_ = (*status.overrides)[1];
        }
    }
    const std::optional<int> planner = status.buf ? std::optional<int>(status.buf->planner) : std::nullopt;
    if (planner && *planner > plannerSize_) {
        plannerSize_ = *planner;
    }

    if (!jobActive_) {
        return;
    }

    const std::size_t prevLine = execLine_;
    const double prevRemaining = js::mathRound(remainingTime_);
    const std::int64_t prevElapsed = elapsedTime_ / 1000;

    const std::size_t received = std::min(received_, lines_.size());
    idleReports_ = status.activeState == "Idle" ? idleReports_ + 1 : 0;
    if (status.activeState == "Run") {
        advancePlayheadBy(dtSeconds, received);
    }
    if (planner && plannerSize_ > 0) {
        advancePlayheadTo(executedLowerBound(std::max(0, plannerSize_ - *planner)));
    } else if (idleReports_ >= 2) {
        // Without buffer reports, a sustained Idle means everything acked
        // has run. A single Idle isn't enough: grbl acks lines into the
        // planner before it starts the cycle.
        advancePlayheadTo(received);
    }
    updateRemainingTime();

    // Runs until the workflow stops, i.e. past the last ack while the
    // machine finishes the buffered moves.
    updateElapsedTime();

    if (execLine_ != prevLine || js::mathRound(remainingTime_) != prevRemaining ||
        elapsedTime_ / 1000 != prevElapsed) {
        markChanged();
    }
}

std::string Sender::estimateAccuracy() {
    if (estimatedTime_ == 0 || startTime_ == 0) {
        return {};
    }
    updateElapsedTime();
    const double running = static_cast<double>(timeRunning_) / 1000;
    const double paused = static_cast<double>(timePaused_) / 1000;
    const double ratio = running > 0 ? estimatedTime_ / running : 0;
    return "Job time: estimated=" + js::toFixed(estimatedTime_, 1) + "s actual=" + js::toFixed(running, 1) +
           "s paused=" + js::toFixed(paused, 1) + "s ratio=" + js::toFixed(ratio, 3) +
           " ovF=" + js::numberToString(ovF_) + " ovR=" + js::numberToString(ovR_) +
           " lines=" + std::to_string(lines_.size());
}

void Sender::updateElapsedTime() {
    const std::int64_t now = loop_.nowMs();
    elapsedTime_ = now - startTime_;
    timeRunning_ = elapsedTime_ - timePaused_;
}

std::int64_t Sender::currentLineRunning() const noexcept {
    return static_cast<std::int64_t>(execLine_);
}

SenderStatus Sender::status() const {
    SenderStatus s;
    s.characterCounting = protocol_ == Protocol::CharacterCounting;
    s.hold = hold_;
    s.holdReason = holdReason_;
    s.name = name_;
    s.size = gcode_.size();
    s.total = lines_.size();
    s.sent = sent_;
    s.received = received_;
    s.startTime = startTime_;
    s.finishTime = finishTime_;
    s.elapsedTime = elapsedTime_;
    s.timePaused = timePaused_;
    s.timeRunning = timeRunning_;
    s.remainingTime = remainingTime_;
    s.toolChanges = toolChanges_;
    s.estimatedTime = estimatedTime_;
    s.ovF = ovF_;
    s.ovR = ovR_;
    s.isRotaryFile = isRotaryFile_;
    s.currentLineRunning = currentLineRunning();
    return s;
}

// ---- Feeder --------------------------------------------------------------------

Feeder::Feeder(DataFilter filter) : filter_(std::move(filter)) {}

void Feeder::feed(const std::vector<std::string>& commands, expr::Value context) {
    if (queue_.empty()) {
        pending_ = false;
    }
    if (commands.empty()) {
        return;
    }
    if (!context.isObject()) {
        context = expr::Value::object();
    }
    // One context object is shared by every line of a batch (a macro), so
    // "%X0=posx" on one line is visible to the lines after it.
    for (const std::string& command : commands) {
        queue_.push_back(Item{command, context});
    }
    changed_ = true;
}

void Feeder::hold(std::optional<HoldReason> reason) {
    if (hold_) {
        return;
    }
    hold_ = true;
    holdReason_ = std::move(reason);
    changed_ = true;
}

void Feeder::unhold() {
    if (!hold_) {
        return;
    }
    hold_ = false;
    holdReason_.reset();
    changed_ = true;
}

void Feeder::clear() {
    queue_.clear();
    pending_ = false;
    outstanding_ = 0;
    changed_ = true;
}

void Feeder::reset() {
    hold_ = false;
    holdReason_.reset();
    queue_.clear();
    pending_ = false;
    outstanding_ = 0;
    changed_ = true;
}

bool Feeder::next() {
    if (queue_.empty() && !hold_) {
        pending_ = false;
        if (onComplete) {
            onComplete();
        }
        return pending_;
    }

    while (!hold_ && !queue_.empty()) {
        Item item = std::move(queue_.front());
        queue_.pop_front();
        std::string command = filter_ ? filter_(std::move(item.command), item.context) : std::move(item.command);
        if (command.empty()) {
            continue;  // blank after filtering
        }
        pending_ = true;
        ++outstanding_;
        if (onData) {
            onData(command, item.context);
        }
        changed_ = true;
        break;
    }

    if (queue_.empty() && !hold_) {
        pending_ = false;
        if (onComplete) {
            onComplete();
        }
    }
    return pending_;
}

std::vector<std::string> Feeder::queuedCommands() const {
    std::vector<std::string> out;
    out.reserve(queue_.size());
    for (const Item& item : queue_) {
        out.push_back(item.command);
    }
    return out;
}

bool Feeder::peek() {
    const bool changed = changed_;
    changed_ = false;
    return changed;
}

void Feeder::ack() {
    if (outstanding_ > 0) {
        --outstanding_;
    }
}

FeederStatus Feeder::status() const {
    return FeederStatus{hold_, holdReason_, queue_.size(), pending_, changed_};
}

// ---- Workflow ------------------------------------------------------------------

std::string_view workflowStateName(WorkflowState state) noexcept {
    switch (state) {
        case WorkflowState::Running: return "running";
        case WorkflowState::Paused: return "paused";
        case WorkflowState::Idle: return "idle";
    }
    return "idle";
}

void Workflow::start() {
    if (state_ != WorkflowState::Running) {
        state_ = WorkflowState::Running;
        if (onStart) {
            onStart();
        }
    }
}

void Workflow::stop() {
    if (state_ != WorkflowState::Idle) {
        state_ = WorkflowState::Idle;
        if (onStop) {
            onStop();
        }
    }
}

void Workflow::stopTesting() {
    state_ = WorkflowState::Idle;
    if (onStop) {
        onStop();
    }
}

void Workflow::resumeTesting() {
    if (state_ == WorkflowState::Paused) {
        state_ = WorkflowState::Running;
        if (onResume) {
            onResume();
        }
    }
}

void Workflow::pause(std::optional<HoldReason> reason) {
    if (state_ == WorkflowState::Running) {
        state_ = WorkflowState::Paused;
    }
    if (onPause) {
        onPause(reason);
    }
}

void Workflow::resume() {
    if (state_ == WorkflowState::Paused) {
        state_ = WorkflowState::Running;
    }
    if (onResume) {
        onResume();
    }
}

}  // namespace gs::controller
