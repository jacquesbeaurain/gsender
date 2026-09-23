#include "qt_event_loop.hpp"

#include <QMetaObject>
#include <QTimer>

#include <algorithm>
#include <limits>

namespace gs::app {

QtEventLoop::QtEventLoop(QObject* parent) : QObject(parent) {
    clock_.start();
}

QtEventLoop::~QtEventLoop() {
    for (auto& [id, timer] : timers_) {
        timer->stop();
        delete timer;
    }
}

std::int64_t QtEventLoop::nowMs() const {
    return 1'000'000 + clock_.elapsed();
}

runtime::TimerId QtEventLoop::start(std::int64_t ms, bool repeat, std::function<void()> fn) {
    const runtime::TimerId id = nextId_++;
    auto* timer = new QTimer(this);
    timer->setSingleShot(!repeat);
    // The jog streamer ticks every 10 ms; coarse timers would starve it.
    timer->setTimerType(Qt::PreciseTimer);
    connect(timer, &QTimer::timeout, this, [this, id, repeat, fn = std::move(fn)] {
        if (!repeat) {
            // Forget a one-shot before running it: the callback may start or
            // clear timers, including this id.
            const auto it = timers_.find(id);
            if (it == timers_.end()) {
                return;
            }
            it->second->deleteLater();
            timers_.erase(it);
            fn();
            return;
        }
        if (timers_.count(id) != 0) {
            fn();  // may clear itself; the QTimer is deleted later
        }
    });
    const auto interval = std::clamp<std::int64_t>(ms, 0, std::numeric_limits<int>::max());
    timer->start(static_cast<int>(interval));
    timers_.emplace(id, timer);
    return id;
}

runtime::TimerId QtEventLoop::setTimeout(std::int64_t delayMs, std::function<void()> fn) {
    return start(delayMs, false, std::move(fn));
}

runtime::TimerId QtEventLoop::setInterval(std::int64_t periodMs, std::function<void()> fn) {
    return start(std::max<std::int64_t>(periodMs, 1), true, std::move(fn));
}

void QtEventLoop::clear(runtime::TimerId id) {
    const auto it = timers_.find(id);
    if (it == timers_.end()) {
        return;
    }
    it->second->stop();
    it->second->deleteLater();
    timers_.erase(it);
}

void QtEventLoop::post(std::function<void()> fn) {
    QMetaObject::invokeMethod(this, [fn = std::move(fn)] { fn(); }, Qt::QueuedConnection);
}

}  // namespace gs::app
