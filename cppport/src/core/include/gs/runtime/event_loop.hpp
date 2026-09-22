#pragma once

// Time and scheduling for the Qt-free core.
//
// The JavaScript controllers are full of setTimeout/setInterval/await delay().
// The core schedules through this interface instead, so the application can run
// it on the Qt event loop while tests drive a ManualEventLoop and advance
// simulated time deterministically.
//
// All callbacks run on the loop's thread. Only post() may be called from other
// threads.

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <vector>

namespace gs::runtime {

using TimerId = std::uint64_t;

class EventLoop {
public:
    virtual ~EventLoop() = default;

    // Monotonic milliseconds (arbitrary epoch).
    virtual std::int64_t nowMs() const = 0;

    virtual TimerId setTimeout(std::int64_t delayMs, std::function<void()> fn) = 0;
    virtual TimerId setInterval(std::int64_t periodMs, std::function<void()> fn) = 0;
    // Cancelling an unknown or finished timer is a no-op.
    virtual void clear(TimerId id) = 0;

    // Queues fn to run on the loop thread; safe to call from any thread.
    virtual void post(std::function<void()> fn) = 0;
};

// Owns a set of timers and cancels them all when destroyed, so an object that
// schedules callbacks capturing `this` can never be called back after death.
class TimerScope {
public:
    explicit TimerScope(EventLoop& loop) : loop_(loop) {}
    ~TimerScope() { clearAll(); }
    TimerScope(const TimerScope&) = delete;
    TimerScope& operator=(const TimerScope&) = delete;

    EventLoop& loop() const noexcept { return loop_; }
    std::int64_t nowMs() const { return loop_.nowMs(); }

    TimerId timeout(std::int64_t delayMs, std::function<void()> fn);
    TimerId interval(std::int64_t periodMs, std::function<void()> fn);
    // Clears the timer and resets `id` to 0.
    void clear(TimerId& id);
    void clearAll();

private:
    EventLoop& loop_;
    std::vector<TimerId> owned_;
};

// Single-threaded loop with simulated time, for tests and simulations.
class ManualEventLoop final : public EventLoop {
public:
    std::int64_t nowMs() const override { return now_; }
    TimerId setTimeout(std::int64_t delayMs, std::function<void()> fn) override;
    TimerId setInterval(std::int64_t periodMs, std::function<void()> fn) override;
    void clear(TimerId id) override;
    void post(std::function<void()> fn) override;

    // Runs posted tasks and every timer due within `ms`, in time order,
    // advancing the clock as it goes. advance(0) runs what is due now.
    void advance(std::int64_t ms);
    // Runs posted tasks (and anything they post) without moving the clock.
    void runPosted();
    // Number of active timers (for leak checks in tests).
    std::size_t activeTimers() const noexcept { return timers_.size(); }

private:
    struct Timer {
        std::int64_t due = 0;
        std::int64_t period = 0;  // 0 = one-shot
        std::uint64_t sequence = 0;
        std::function<void()> fn;
    };

    // Real clocks are never 0, and ported code uses "timestamp > 0" as a flag.
    std::int64_t now_ = 1'000'000;
    TimerId nextId_ = 1;
    std::uint64_t nextSequence_ = 0;
    std::map<TimerId, Timer> timers_;
    std::mutex postedMutex_;
    std::vector<std::function<void()>> posted_;
};

}  // namespace gs::runtime
