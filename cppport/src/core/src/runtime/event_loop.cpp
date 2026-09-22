#include "gs/runtime/event_loop.hpp"

#include <algorithm>
#include <limits>
#include <memory>

namespace gs::runtime {

// ---- TimerScope ----------------------------------------------------------------

TimerId TimerScope::timeout(std::int64_t delayMs, std::function<void()> fn) {
    // Forget the id once it has fired so the list does not grow unbounded.
    auto self = std::make_shared<TimerId>(0);
    const TimerId id = loop_.setTimeout(delayMs, [this, self, fn = std::move(fn)]() {
        std::erase(owned_, *self);
        fn();
    });
    *self = id;
    owned_.push_back(id);
    return id;
}

TimerId TimerScope::interval(std::int64_t periodMs, std::function<void()> fn) {
    const TimerId id = loop_.setInterval(periodMs, std::move(fn));
    owned_.push_back(id);
    return id;
}

void TimerScope::clear(TimerId& id) {
    if (id == 0) {
        return;
    }
    loop_.clear(id);
    std::erase(owned_, id);
    id = 0;
}

void TimerScope::clearAll() {
    for (TimerId id : owned_) {
        loop_.clear(id);
    }
    owned_.clear();
}

// ---- ManualEventLoop --------------------------------------------------------------

TimerId ManualEventLoop::setTimeout(std::int64_t delayMs, std::function<void()> fn) {
    const TimerId id = nextId_++;
    timers_[id] = Timer{now_ + std::max<std::int64_t>(0, delayMs), 0, nextSequence_++, std::move(fn)};
    return id;
}

TimerId ManualEventLoop::setInterval(std::int64_t periodMs, std::function<void()> fn) {
    const TimerId id = nextId_++;
    const std::int64_t period = std::max<std::int64_t>(1, periodMs);
    timers_[id] = Timer{now_ + period, period, nextSequence_++, std::move(fn)};
    return id;
}

void ManualEventLoop::clear(TimerId id) {
    timers_.erase(id);
}

void ManualEventLoop::post(std::function<void()> fn) {
    std::lock_guard lock(postedMutex_);
    posted_.push_back(std::move(fn));
}

void ManualEventLoop::runPosted() {
    while (true) {
        std::vector<std::function<void()>> batch;
        {
            std::lock_guard lock(postedMutex_);
            batch.swap(posted_);
        }
        if (batch.empty()) {
            return;
        }
        for (auto& fn : batch) {
            fn();
        }
    }
}

void ManualEventLoop::advance(std::int64_t ms) {
    const std::int64_t end = now_ + std::max<std::int64_t>(0, ms);
    runPosted();
    while (true) {
        // Earliest due timer; ties break by scheduling order, like Node.
        auto next = timers_.end();
        for (auto it = timers_.begin(); it != timers_.end(); ++it) {
            if (next == timers_.end() || it->second.due < next->second.due ||
                (it->second.due == next->second.due && it->second.sequence < next->second.sequence)) {
                next = it;
            }
        }
        if (next == timers_.end() || next->second.due > end) {
            break;
        }
        now_ = std::max(now_, next->second.due);
        const TimerId id = next->first;
        std::function<void()> fn = next->second.fn;
        if (next->second.period > 0) {
            next->second.due += next->second.period;
            next->second.sequence = nextSequence_++;
        } else {
            timers_.erase(next);
        }
        fn();
        (void)id;
        runPosted();
    }
    now_ = end;
    runPosted();
}

}  // namespace gs::runtime
