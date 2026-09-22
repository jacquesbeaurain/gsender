#include "gs/controller/jog_streamer.hpp"

#include "gs/util/jsnumber.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gs::controller {
namespace {

constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

// JogStreamer.js toNumber(value, fallback): non-finite values take the fallback.
double finiteOr(double value, double fallback) {
    return std::isfinite(value) ? value : fallback;
}

double sign(double v) {
    return v > 0 ? 1.0 : (v < 0 ? -1.0 : 0.0);
}

double roundDecimals(double value) {
    const double factor = std::pow(10.0, jog::kDecimals);
    return js::mathRound(value * factor) / factor;
}

double settingNumber(const protocol::OrderedMap& settings, std::string_view key) {
    const std::string* value = settings.find(key);
    return value ? js::stringToNumber(*value) : kNaN;
}

}  // namespace

SegmentPlan computeSegmentPlan(const Axes4& dir, double feedrate, const Axes4& accelByAxis,
                               const Axes4& maxRateByAxis) {
    const double magnitude = std::hypot(dir.X, dir.Y, dir.Z);
    Axes4 unit;
    for (std::size_t i = 0; i < 3; ++i) {
        unit[i] = magnitude > 0 ? dir[i] / magnitude : 0;
    }

    // A diagonal is limited by whichever axis reaches its own ceiling first.
    // Axes that report nothing are left unclamped: the firmware clamps anyway.
    double resolved = finiteOr(feedrate, jog::kMinFeedrate);
    for (std::size_t i = 0; i < 3; ++i) {
        if (unit[i] == 0) {
            continue;
        }
        const double maxRate = finiteOr(maxRateByAxis[i], 0);
        if (maxRate > 0) {
            resolved = std::min(resolved, maxRate / std::fabs(unit[i]));
        }
    }
    if (dir.A != 0) {
        const double maxRate = finiteOr(maxRateByAxis.A, 0);
        if (maxRate > 0) {
            resolved = std::min(resolved, maxRate);
        }
    }
    resolved = std::max(resolved, jog::kMinFeedrate);
    const double v = resolved / 60;

    // Acceleration along the travel vector; unreported axes count at an
    // assumed (conservative) figure rather than as infinitely capable.
    double inverseSquares = 0;
    bool accelReported = false;
    const auto axisAccel = [&](std::size_t i) {
        const double accel = finiteOr(accelByAxis[i], 0);
        if (accel > 0) {
            accelReported = true;
            return accel;
        }
        return jog::kAssumedAccel;
    };
    for (std::size_t i = 0; i < 3; ++i) {
        if (unit[i] == 0) {
            continue;
        }
        inverseSquares += std::pow(unit[i] / axisAccel(i), 2);
    }
    if (dir.A != 0 && magnitude == 0) {
        inverseSquares += std::pow(1 / axisAccel(3), 2);
    }
    const double effectiveAccel =
        inverseSquares > 0 ? std::max(1 / std::sqrt(inverseSquares), 1.0) : jog::kAssumedAccel;

    const double requiredLook = (jog::kSafetyK * v) / (2 * effectiveAccel);
    const double tLook = std::clamp(requiredLook, jog::kLookMin, jog::kLookMax);

    double dt = std::clamp(tLook / jog::kInflightTarget, jog::kDtMin, jog::kDtMax);
    dt = std::max(dt, jog::kMinSegmentMm / v);          // segments must survive rounding
    dt = std::max(dt, 1 / jog::kMaxSegmentRateHz);      // never flood the serial link

    SegmentPlan plan;
    plan.feedrate = resolved;
    plan.dt = dt;
    plan.segmentLength = v * dt;
    plan.inflight = std::max(1, std::min(static_cast<int>(std::ceil(tLook / dt)), jog::kMaxInflightLines));
    plan.tLook = tLook;
    plan.requiredLook = requiredLook;
    plan.effectiveAccel = effectiveAccel;
    plan.accelReported = accelReported;
    plan.starved = requiredLook > jog::kLookMax;
    return plan;
}

std::optional<std::string> describeJogStopReason(std::string_view reason) {
    if (reason.empty()) {
        return std::nullopt;
    }
    if (reason.starts_with("command:")) {
        return "another command was sent";
    }
    if (reason.starts_with("state:")) {
        return "the machine went into " + std::string(reason.substr(6));
    }
    if (reason == "cancel") return "cancelled";
    if (reason == "close" || reason == "destroy") return "connection closed";
    if (reason == "error") return "the machine reported an error";
    if (reason == "preconditions") return "the machine was busy";
    if (reason == "watchdog") return "no response from the machine";
    if (reason == "workflow") return "a job started";
    return std::nullopt;
}

JogStreamer::JogStreamer(runtime::EventLoop& loop, Options options)
    : loop_(loop), timers_(loop), options_(std::move(options)) {
    rxBufferSize_ = options_.rxBufferSize;
    if (!options_.softLimitsEnabled) {
        options_.softLimitsEnabled = [](const protocol::OrderedMap& s) { return s.get("$20") == "1"; };
    }
    if (!options_.lineFilter) {
        options_.lineFilter = [](const std::string& line) { return line; };
    }
    resetState();
}

int JogStreamer::rxBudget() const noexcept {
    return std::max(rxBufferSize_ - jog::kRxMarginBytes, 32);
}

void JogStreamer::resetState() {
    state_ = JogState::Idle;
    mode_ = JogMode::None;
    dir_ = {};
    work_ = {};
    limit_ = {kInfinity, kInfinity, kInfinity, kInfinity};
    residual_ = {};
    requestedFeedrate_ = 0;
    plan_ = {};
    pending_.clear();
    pendingBytes_ = 0;
    pendingTime_ = 0;
    plannerFree_.reset();
    emittedUntil_ = 0;
    startedAt_ = 0;
    deadlineAt_ = 0;
    drainDeadlineAt_ = 0;
    warnedStarved_ = false;
    announcedFeedrate_ = 0;
    announcedAt_ = 0;
}

void JogStreamer::warn(const std::string& message) const {
    if (options_.warn) {
        options_.warn(message);
    }
}

protocol::OrderedMap JogStreamer::settings() const {
    return options_.getSettings ? options_.getSettings() : protocol::OrderedMap{};
}

bool JogStreamer::start(const Axes4& direction, std::optional<double> feedrate, JogUnits units) {
    if (options_.canStream && !options_.canStream()) {
        warn("Refusing to start a jog stream: preconditions not met");
        return false;
    }
    Axes4 dir;
    for (std::size_t i = 0; i < 4; ++i) {
        dir[i] = sign(direction[i]);
    }
    if (dir == Axes4{}) {
        return false;
    }

    resetState();
    mode_ = JogMode::Velocity;
    dir_ = dir;
    for (std::size_t i = 0; i < 4; ++i) {
        work_[i] = dir[i] == 0 ? kNaN : dir[i] * kInfinity;
    }
    // start() defaults an absent feedrate, but an explicit null is invalid.
    setFeedrate(feedrate.has_value() ? feedrate : std::optional<double>(kNaN), units);
    refreshTravelBudget();
    replan();
    beginStreaming();
    if (onStart) {
        onStart(startedMessage());
    }
    return true;
}

bool JogStreamer::update(const Axes4& direction, std::optional<double> feedrate) {
    if (state_ != JogState::Streaming) {
        return false;
    }
    Axes4 dir;
    for (std::size_t i = 0; i < 4; ++i) {
        dir[i] = sign(direction[i]);
    }
    if (dir == Axes4{}) {
        return false;
    }
    const bool changedDirection = dir != dir_;
    dir_ = dir;
    for (std::size_t i = 0; i < 4; ++i) {
        work_[i] = dir[i] == 0 ? kNaN : dir[i] * kInfinity;
    }
    if (feedrate.has_value()) {
        setFeedrate(feedrate, units_);
    }
    if (changedDirection) {
        residual_ = {};
        refreshTravelBudget();
    }
    replan();
    announceFeedrate();
    deadlineAt_ = loop_.nowMs() + jog::kMaxStreamDurationMs;
    return true;
}

bool JogStreamer::feed(const Axes4& distances, std::optional<double> feedrate, JogUnits units) {
    if (distances == Axes4{}) {
        return false;
    }
    const double scale = units == JogUnits::Millimetres ? 1.0 : 25.4;
    const bool starting = state_ != JogState::Streaming;

    if (starting) {
        if (options_.canStream && !options_.canStream()) {
            warn("Refusing to start a jog stream: preconditions not met");
            return false;
        }
        resetState();
        mode_ = JogMode::Displacement;
        work_ = {};
        setFeedrate(feedrate.value_or(jog::kDefaultFeedrate), units);
    } else if (mode_ != JogMode::Displacement) {
        return false;  // a handwheel pulse mid velocity-jog is meaningless
    } else if (feedrate.has_value()) {
        setFeedrate(feedrate, units);
    }

    for (std::size_t i = 0; i < 4; ++i) {
        // Reversing discards the opposing remainder so the wheel feels responsive.
        if (distances[i] != 0 && sign(distances[i]) != sign(work_[i])) {
            work_[i] = 0;
            residual_[i] = 0;
        }
        work_[i] += distances[i] * scale;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        dir_[i] = sign(work_[i]);
    }

    refreshTravelBudget();
    replan();

    if (starting) {
        beginStreaming();
        if (onStart) {
            onStart(startedMessage());
        }
    } else {
        deadlineAt_ = loop_.nowMs() + jog::kMaxStreamDurationMs;
    }
    return true;
}

bool JogStreamer::stop() {
    if (state_ != JogState::Streaming) {
        return false;
    }
    state_ = JogState::Draining;
    drainDeadlineAt_ = loop_.nowMs() + jog::kDrainTimeoutMs;
    if (onStop) {
        onStop();
    }
    settleIfDrained();
    return true;
}

bool JogStreamer::abort(const std::string& reason) {
    if (state_ == JogState::Idle) {
        return false;
    }
    stopTimer();
    const bool hadPending = !pending_.empty();
    resetState();
    if (onAbort) {
        onAbort(reason);
    }
    if (options_.debug) {
        options_.debug("Jog stream aborted (" + reason + ")");
    }
    return hadPending;
}

bool JogStreamer::ack() {
    if (pending_.empty()) {
        return false;
    }
    const Segment segment = pending_.front();
    pending_.pop_front();
    pendingBytes_ -= segment.bytes;
    pendingTime_ -= segment.dt;
    ++acksConsumed_;
    settleIfDrained();
    return true;
}

bool JogStreamer::onError() {
    if (!isActive() || pending_.empty()) {
        return false;
    }
    acksOrphaned_ += static_cast<int>(pending_.size());
    return true;
}

void JogStreamer::onStatus(const JogStatus& status) {
    if (!isActive()) {
        return;
    }
    static constexpr std::string_view kBlocking[] = {"Alarm", "Hold", "Door", "Check", "Home", "Sleep"};
    if (std::find(std::begin(kBlocking), std::end(kBlocking), status.activeState) != std::end(kBlocking)) {
        abort("state:" + status.activeState);
        return;
    }
    plannerFree_ = status.buf ? std::optional<int>(status.buf->planner) : std::nullopt;
    // grblHAL's receive buffer is larger than the floor we assume.
    if (status.buf && status.buf->rx > rxBufferSize_) {
        rxBufferSize_ = status.buf->rx;
    }
    if (state_ == JogState::Streaming) {
        refreshTravelBudget();
        replan();  // settings can arrive mid-jog
    }
}

void JogStreamer::setFeedrate(std::optional<double> feedrate, JogUnits units) {
    units_ = units;
    const double value = feedrate.value_or(kNaN);
    if (!std::isfinite(value) || value <= 0) {
        warn("Ignoring invalid jog feedrate");
        if (requestedFeedrate_ == 0) {
            requestedFeedrate_ = jog::kDefaultFeedrate;
        }
        return;
    }
    requestedFeedrate_ = units == JogUnits::Millimetres ? value : value * 25.4;
}

void JogStreamer::refreshTravelBudget() {
    const protocol::OrderedMap s = settings();
    const JogStatus status = options_.getStatus ? options_.getStatus() : JogStatus{};
    const bool homed = options_.getHomingFlag ? options_.getHomingFlag() : false;
    limit_ = computeTravelBudget(dir_, s, status.mpos, homed, options_.softLimitsEnabled(s));
}

void JogStreamer::replan() {
    const protocol::OrderedMap s = settings();
    const auto figure = [&s](std::string_view key, std::string_view fallbackKey) {
        return s.has(key) ? settingNumber(s, key) : settingNumber(s, fallbackKey);
    };
    // 3-axis grbl has no rotary settings; Y stands in for A.
    const Axes4 accel{settingNumber(s, "$120"), settingNumber(s, "$121"), settingNumber(s, "$122"),
                      figure("$123", "$121")};
    const Axes4 maxRate{settingNumber(s, "$110"), settingNumber(s, "$111"), settingNumber(s, "$112"),
                        figure("$113", "$111")};
    plan_ = computeSegmentPlan(dir_, requestedFeedrate_, accel, maxRate);

    if (plan_.starved && plan_.accelReported && !warnedStarved_) {
        warnedStarved_ = true;
        warn("Jog feedrate " + js::numberToString(js::mathRound(plan_.feedrate)) +
             " exceeds what this machine's acceleration can hold at a constant speed; the firmware will "
             "decelerate between segments.");
    }
}

void JogStreamer::beginStreaming() {
    state_ = JogState::Streaming;
    startedAt_ = loop_.nowMs();
    deadlineAt_ = startedAt_ + jog::kMaxStreamDurationMs;
    // The start message already carries the speed.
    announcedFeedrate_ = js::mathRound(plan_.feedrate);
    announcedAt_ = startedAt_;
    emittedUntil_ = static_cast<double>(startedAt_);
    pump();
    tick_ = timers_.interval(jog::kTickMs, [this]() { onTick(); });
}

void JogStreamer::stopTimer() {
    timers_.clear(tick_);
}

void JogStreamer::settleIfDrained() {
    if (state_ != JogState::Draining) {
        return;
    }
    if (pending_.empty()) {
        stopTimer();
        resetState();
        if (onIdle) {
            onIdle();
        }
        return;
    }
    if (loop_.nowMs() > drainDeadlineAt_) {
        acksOrphaned_ += static_cast<int>(pending_.size());
        warn("Timed out waiting for " + std::to_string(pending_.size()) + " jog ack(s); clearing");
        stopTimer();
        resetState();
        if (onIdle) {
            onIdle();
        }
    }
}

void JogStreamer::onTick() {
    if (state_ == JogState::Draining) {
        settleIfDrained();
        return;
    }
    if (state_ != JogState::Streaming) {
        return;
    }
    if (options_.canStream && !options_.canStream()) {
        abort("preconditions");
        return;
    }
    if (loop_.nowMs() > deadlineAt_) {
        abort("watchdog");
        return;
    }
    pump();
}

void JogStreamer::pump() {
    const double leadMs = plan_.tLook * 1000;
    for (int i = 0; i < jog::kMaxInflightLines; ++i) {
        if (state_ != JogState::Streaming) {
            return;
        }
        if (emittedUntil_ - static_cast<double>(loop_.nowMs()) >= leadMs) {
            return;
        }
        if (!emitSegment()) {
            return;
        }
    }
}

bool JogStreamer::emitSegment() {
    const double now = static_cast<double>(loop_.nowMs());
    const double owed = std::max(0.0, now - emittedUntil_);
    const double dt = plan_.dt;
    const double feedrate = plan_.feedrate;

    // Backpressure: a tripped gate forfeits the owed time instead of banking
    // it, so resuming never bursts catch-up motion.
    constexpr int kEstimatedBytes = 40;
    const bool blocked = pendingBytes_ + kEstimatedBytes > rxBudget() ||
                         pendingTime_ > plan_.tLook * jog::kOverrunFactor ||
                         static_cast<int>(pending_.size()) >= plan_.inflight ||
                         (plannerFree_ && *plannerFree_ < jog::kPlannerLowWater);
    if (blocked) {
        emittedUntil_ = std::max(emittedUntil_, now);
        return false;
    }

    // A late tick is covered by one longer segment.
    const double dtEff = std::min(dt + owed / 1000, jog::kDtMaxCatchup);
    const double v = feedrate / 60;
    const Axes4 unit = unitVector();

    Axes4 distances;
    bool exhausted = true;
    for (std::size_t i = 0; i < 4; ++i) {
        if (unit[i] == 0) {
            continue;
        }
        double distance = unit[i] * v * dtEff;
        if (std::isfinite(work_[i])) {
            distance = sign(distance) * std::min(std::fabs(distance), std::fabs(work_[i]));
        }
        if (std::isfinite(limit_[i])) {
            distance = sign(distance) * std::min(std::fabs(distance), std::fabs(limit_[i]));
        }
        if (std::fabs(distance) > jog::kMinMotionMm) {
            exhausted = false;
        }
        distances[i] = distance;
    }
    if (exhausted) {
        onExhausted();
        return false;
    }

    // Carry sub-precision remainders forward instead of losing them.
    Axes4 commanded;
    bool hasMotion = false;
    for (std::size_t i = 0; i < 4; ++i) {
        if (distances[i] == 0) {
            continue;
        }
        const double raw = residual_[i] + distances[i];
        const double out = roundDecimals(raw);
        residual_[i] = raw - out;
        commanded[i] = out;
        hasMotion = hasMotion || out != 0;
    }
    if (!hasMotion) {
        return false;  // accumulate into the next tick
    }

    const std::string filtered = options_.lineFilter(formatLine(commanded, feedrate));
    if (filtered.empty()) {
        advanceSchedule(now, dtEff);
        return true;
    }
    if (options_.write) {
        options_.write(filtered + "\n");
    }
    const int bytes = static_cast<int>(filtered.size()) + 1;
    pending_.push_back({bytes, dtEff});
    pendingBytes_ += bytes;
    pendingTime_ += dtEff;

    for (std::size_t i = 0; i < 4; ++i) {
        if (commanded[i] == 0) {
            continue;
        }
        if (std::isfinite(work_[i])) {
            work_[i] -= commanded[i];
        }
        if (std::isfinite(limit_[i])) {
            limit_[i] -= commanded[i];
        }
    }
    advanceSchedule(now, dtEff);
    return true;
}

void JogStreamer::advanceSchedule(double now, double dtEff) {
    emittedUntil_ = std::max(emittedUntil_, now) + dtEff * 1000;
}

void JogStreamer::onExhausted() {
    state_ = JogState::Draining;
    drainDeadlineAt_ = loop_.nowMs() + jog::kDrainTimeoutMs;
    if (mode_ == JogMode::Displacement) {
        // The wheel stopped: let the last segment finish so a further pulse
        // blends straight in, with no jog cancel.
        if (onDrained) {
            onDrained();
        }
    } else if (onLimit) {
        // Velocity mode only runs out of distance at a soft limit.
        onLimit();
    }
    settleIfDrained();
}

Axes4 JogStreamer::unitVector() const {
    const Axes4& source = mode_ == JogMode::Displacement ? work_ : dir_;
    const double x = std::isfinite(source.X) ? source.X : 0;
    const double y = std::isfinite(source.Y) ? source.Y : 0;
    const double z = std::isfinite(source.Z) ? source.Z : 0;
    const double magnitude = mode_ == JogMode::Displacement ? std::hypot(x, y, z) : std::hypot(dir_.X, dir_.Y, dir_.Z);
    Axes4 unit;
    if (magnitude > 0) {
        if (mode_ == JogMode::Displacement) {
            unit.X = x / magnitude;
            unit.Y = y / magnitude;
            unit.Z = z / magnitude;
        } else {
            unit.X = dir_.X / magnitude;
            unit.Y = dir_.Y / magnitude;
            unit.Z = dir_.Z / magnitude;
        }
    }
    // A is rotary; it does not share the linear feedrate vector.
    unit.A = sign(mode_ == JogMode::Displacement ? source.A : dir_.A);
    return unit;
}

std::string JogStreamer::formatLine(const Axes4& distances, double feedrate) const {
    std::string words;
    for (std::size_t i = 0; i < 4; ++i) {
        if (distances[i] != 0) {
            words += kJogAxes[i];
            words += js::numberToString(distances[i]);
        }
    }
    // Parity with the previous handlers, which slowed any move involving Z.
    const double derated = distances.Z != 0 ? feedrate * jog::kZFeedrateDerate : feedrate;
    return "$J=G21G91" + words + "F" + js::numberToString(roundDecimals(derated));
}

std::string JogStreamer::formatSpeed(double mmPerMin) const {
    if (units_ == JogUnits::Inches) {
        const double inches = mmPerMin / 25.4;
        return js::numberToString(js::mathRound(inches * 10) / 10) + " in/min";
    }
    return js::numberToString(js::mathRound(mmPerMin)) + " mm/min";
}

std::string JogStreamer::startedMessage() const {
    std::string active;
    for (std::size_t i = 0; i < 4; ++i) {
        if (dir_[i] == 0) {
            continue;
        }
        if (!active.empty()) {
            active += ' ';
        }
        active += kJogAxes[i];
        active += dir_[i] > 0 ? '+' : '-';
    }
    const double speed = plan_.feedrate > 0 ? plan_.feedrate : requestedFeedrate_;
    return "Started continuous jogging " + (active.empty() ? std::string("-") : active) + " at " + formatSpeed(speed);
}

void JogStreamer::announceFeedrate() {
    const double feedrate = js::mathRound(plan_.feedrate);
    if (feedrate == announcedFeedrate_) {
        return;
    }
    const std::int64_t now = loop_.nowMs();
    if (now - announcedAt_ < jog::kFeedrateAnnounceIntervalMs) {
        return;
    }
    announcedFeedrate_ = feedrate;
    announcedAt_ = now;
    if (onFeedrate) {
        onFeedrate("Jog speed now " + formatSpeed(feedrate));
    }
}

}  // namespace gs::controller
