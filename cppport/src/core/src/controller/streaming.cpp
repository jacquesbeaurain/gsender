#include "gs/controller/streaming.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <algorithm>
#include <cmath>

namespace gs::controller {
namespace {

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
    estimateData_.clear();
    countdownQueue_.clear();
    totalSentToQueue_ = 0;
    queueDone_ = true;

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
    estimateData_.clear();
    countdownQueue_.clear();
    totalSentToQueue_ = 0;
    queueDone_ = true;
    isRotaryFile_ = false;
    timers_.clear(countdownTimer_);
    timers_.clear(checkTimer_);
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
        remainingTime_ = estimatedTime_ / (ovF_ / 100);
        countdownQueue_.clear();
        totalSentToQueue_ = 0;
        queueDone_ = true;
        countdownPaused_ = false;
        if (options.startFromLine) {
            // Catch the estimate up for the skipped lines.
            totalSentToQueue_ = received_;
            for (std::size_t i = 0; i <= received_ && i < estimateData_.size(); ++i) {
                remainingTime_ -= estimateData_[i] / (ovF_ / 100);
            }
        }
        // Starts the countdown, and restarts it whenever it runs dry while
        // lines are still being acknowledged.
        timers_.clear(checkTimer_);
        checkTimer_ = timers_.interval(100, [this]() {
            if (!countdownQueue_.empty() && queueDone_) {
                queueDone_ = false;
                fakeCountdown();
            }
        });
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

    if (received_ > 0 && estimatedTime_ > 0 && received_ < estimateData_.size()) {
        for (std::size_t i = totalSentToQueue_; i <= received_; ++i) {
            countdownQueue_.push_back(i < estimateData_.size() ? estimateData_[i] : 0.0);
            ++totalSentToQueue_;
        }
    }

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

void Sender::fakeCountdown() {
    // Skip lines that take no time.
    while (timer_ == 0) {
        if (countdownQueue_.empty()) {
            stopCountdown();
            return;
        }
        timer_ = countdownQueue_.front() / (ovF_ / 100);
        countdownQueue_.pop_front();
    }
    if (timer_ < 1) {
        countdownTimer_ = timers_.timeout(static_cast<std::int64_t>(timer_ * 1000), [this]() {
            countdownTimer_ = 0;
            if (!countdownPaused_) {
                remainingTime_ -= timer_;
                remainingTime_ = js::stringToNumber(js::toFixed(remainingTime_, 4));
                timer_ = 0;
                updateElapsedTime();
                markChanged();
                fakeCountdown();
            } else {
                queueDone_ = true;
            }
        });
    } else {
        countdownTimer_ = timers_.interval(1000, [this]() {
            if (countdownPaused_) {
                return;
            }
            timer_ -= 1;
            remainingTime_ -= 1;
            updateElapsedTime();
            if (timer_ < 1) {
                timers_.clear(countdownTimer_);
                fakeCountdown();
            }
            markChanged();
        });
    }
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
    countdownQueue_.clear();
    totalSentToQueue_ = 0;
    timers_.clear(checkTimer_);
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

void Sender::setEstimateData(std::vector<double> estimates) {
    estimateData_ = std::move(estimates);
}

void Sender::setEstimatedTime(double seconds) {
    remainingTime_ = seconds;
    estimatedTime_ = seconds;
}

void Sender::setOvF(double ovF) {
    if (ovF <= 0) {
        return;
    }
    if (ovF_ != 100) {
        remainingTime_ *= ovF_ / 100;  // back to 100%
    }
    remainingTime_ /= ovF / 100;
    ovF_ = ovF;
}

void Sender::resumeCountdown() {
    countdownPaused_ = false;
}

void Sender::pauseCountdown() {
    countdownPaused_ = true;
}

void Sender::stopCountdown() {
    timers_.clear(countdownTimer_);
    queueDone_ = true;
    remainingTime_ -= timer_;
}

void Sender::updateElapsedTime() {
    const std::int64_t now = loop_.nowMs();
    elapsedTime_ = now - startTime_;
    timeRunning_ = elapsedTime_ - timePaused_;
}

std::int64_t Sender::currentLineRunning() const noexcept {
    return static_cast<std::int64_t>(totalSentToQueue_) - static_cast<std::int64_t>(countdownQueue_.size());
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
