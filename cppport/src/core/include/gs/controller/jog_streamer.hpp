#pragma once

// Continuous jogging as a stream of short incremental "$J=" moves that keeps
// the firmware planner filled so the machine holds a constant velocity.
// Port of src/server/lib/JogStreamer.js (see the extensive rationale there);
// driven by the core EventLoop so it can be tested with simulated time.

#include "gs/controller/jog_limits.hpp"
#include "gs/protocol/types.hpp"
#include "gs/runtime/event_loop.hpp"

#include <cstdint>
#include <deque>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace gs::controller {

namespace jog {
inline constexpr std::int64_t kTickMs = 10;
inline constexpr double kDtMin = 0.02;
inline constexpr double kDtMax = 0.12;
inline constexpr double kDtMaxCatchup = 0.25;
inline constexpr double kLookMin = 0.1;
inline constexpr double kLookMax = 0.5;
inline constexpr double kSafetyK = 1.5;
inline constexpr int kInflightTarget = 6;
inline constexpr int kMaxInflightLines = 8;
inline constexpr double kMaxSegmentRateHz = 50;
inline constexpr double kMinSegmentMm = 0.02;
inline constexpr int kPlannerLowWater = 3;
inline constexpr double kOverrunFactor = 2.0;
inline constexpr int kRxMarginBytes = 24;
inline constexpr std::int64_t kMaxStreamDurationMs = 30000;
inline constexpr std::int64_t kDrainTimeoutMs = 1500;
inline constexpr std::int64_t kFeedrateAnnounceIntervalMs = 1000;
inline constexpr double kZFeedrateDerate = 0.8;
inline constexpr int kDecimals = 3;
inline constexpr double kMinMotionMm = 0.0005;
inline constexpr double kMinFeedrate = 1;
inline constexpr double kDefaultFeedrate = 1000;
inline constexpr double kAssumedAccel = 1000;
}  // namespace jog

struct SegmentPlan {
    double feedrate = 0;  // mm/min actually commanded
    double dt = 0;        // seconds of motion per segment
    double segmentLength = 0;
    int inflight = 1;  // target un-acked lines
    double tLook = 0;  // seconds of motion to keep queued
    double requiredLook = 0;
    double effectiveAccel = 0;
    bool accelReported = false;
    bool starved = false;  // acceleration too low to hold this speed
};

SegmentPlan computeSegmentPlan(const Axes4& dir, double feedrate, const Axes4& accelByAxis,
                               const Axes4& maxRateByAxis);

// Plain-language reason for the console, or nullopt to say nothing.
std::optional<std::string> describeJogStopReason(std::string_view reason);

enum class JogState { Idle, Streaming, Draining };
enum class JogMode { None, Velocity, Displacement };
enum class JogUnits { Millimetres, Inches };

// What the streamer needs to know about the machine.
struct JogStatus {
    std::string activeState;
    protocol::AxisValues mpos;
    std::optional<protocol::BufferState> buf;
};

class JogStreamer {
public:
    struct Options {
        std::function<void(const std::string& line)> write;  // line ends with '\n'
        std::function<protocol::OrderedMap()> getSettings;
        std::function<JogStatus()> getStatus;
        std::function<bool()> getHomingFlag;
        std::function<bool()> canStream;
        std::function<std::string(const std::string&)> lineFilter;  // "" drops the line
        // Grbl: $20 == "1"; grblHAL: $20 == "1" && $40 == "0".
        std::function<bool(const protocol::OrderedMap&)> softLimitsEnabled;
        int rxBufferSize = 128;
        std::function<void(const std::string&)> warn;
        std::function<void(const std::string&)> debug;
    };

    JogStreamer(runtime::EventLoop& loop, Options options);

    // Events
    std::function<void(const std::string& summary)> onStart;
    std::function<void(const std::string& summary)> onFeedrate;
    std::function<void()> onStop;
    std::function<void(const std::string& reason)> onAbort;
    std::function<void()> onIdle;
    std::function<void()> onLimit;
    std::function<void()> onDrained;

    // Velocity mode: jog in `direction` (signs only) until stopped.
    bool start(const Axes4& direction, std::optional<double> feedrate = jog::kDefaultFeedrate,
               JogUnits units = JogUnits::Millimetres);
    // Retarget a running stream without a jog cancel.
    bool update(const Axes4& direction, std::optional<double> feedrate = std::nullopt);
    // Displacement mode (handwheel): add distance to the outstanding work.
    bool feed(const Axes4& distances, std::optional<double> feedrate = std::nullopt,
              JogUnits units = JogUnits::Millimetres);
    // Stop producing segments; stays active to absorb in-flight acks. The
    // caller sends the 0x85 jog cancel.
    bool stop();
    bool abort(const std::string& reason = "abort");

    // An "ok" arrived; true when it belonged to a streamed line.
    bool ack();
    // An error arrived while streaming; true when a jog line was outstanding.
    bool onError();
    void onStatus(const JogStatus& status);

    JogState state() const noexcept { return state_; }
    JogMode mode() const noexcept { return mode_; }
    bool isActive() const noexcept { return state_ != JogState::Idle; }
    bool isStreaming() const noexcept { return state_ == JogState::Streaming; }
    const SegmentPlan& plan() const noexcept { return plan_; }
    std::size_t pendingCount() const noexcept { return pending_.size(); }
    int pendingBytes() const noexcept { return pendingBytes_; }
    int acksConsumed() const noexcept { return acksConsumed_; }
    int acksOrphaned() const noexcept { return acksOrphaned_; }
    double emittedUntil() const noexcept { return emittedUntil_; }
    const Axes4& work() const noexcept { return work_; }
    int rxBudget() const noexcept;

private:
    struct Segment {
        int bytes;
        double dt;
    };

    void resetState();
    void setFeedrate(std::optional<double> feedrate, JogUnits units);
    protocol::OrderedMap settings() const;
    void refreshTravelBudget();
    void replan();
    void beginStreaming();
    void stopTimer();
    void settleIfDrained();
    void onTick();
    void pump();
    bool emitSegment();
    void advanceSchedule(double now, double dtEff);
    void onExhausted();
    Axes4 unitVector() const;
    std::string formatLine(const Axes4& distances, double feedrate) const;
    std::string formatSpeed(double mmPerMin) const;
    std::string startedMessage() const;
    void announceFeedrate();
    void warn(const std::string& message) const;

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    Options options_;

    JogState state_ = JogState::Idle;
    JogMode mode_ = JogMode::None;
    JogUnits units_ = JogUnits::Millimetres;
    Axes4 dir_;
    Axes4 work_;
    Axes4 limit_;
    Axes4 residual_;
    double requestedFeedrate_ = 0;
    SegmentPlan plan_;
    std::deque<Segment> pending_;
    int pendingBytes_ = 0;
    double pendingTime_ = 0;
    std::optional<int> plannerFree_;
    runtime::TimerId tick_ = 0;
    double emittedUntil_ = 0;  // ms; fractional like the JavaScript schedule
    std::int64_t startedAt_ = 0;
    std::int64_t deadlineAt_ = 0;
    std::int64_t drainDeadlineAt_ = 0;
    bool warnedStarved_ = false;
    double announcedFeedrate_ = 0;
    std::int64_t announcedAt_ = 0;
    int rxBufferSize_ = 128;
    int acksConsumed_ = 0;
    int acksOrphaned_ = 0;
};

}  // namespace gs::controller
