#pragma once

// The core's EventLoop on the Qt event loop: timers are QTimers owned by this
// object, post() queues onto its thread (callable from any thread - the
// transport's I/O thread delivers through it).

#include "gs/runtime/event_loop.hpp"

#include <QElapsedTimer>
#include <QObject>

#include <unordered_map>

class QTimer;

namespace gs::app {

class QtEventLoop final : public QObject, public runtime::EventLoop {
    Q_OBJECT

public:
    explicit QtEventLoop(QObject* parent = nullptr);
    ~QtEventLoop() override;

    // Milliseconds since construction, offset so it is never 0 (ported code
    // uses "timestamp > 0" as a flag).
    std::int64_t nowMs() const override;
    runtime::TimerId setTimeout(std::int64_t delayMs, std::function<void()> fn) override;
    runtime::TimerId setInterval(std::int64_t periodMs, std::function<void()> fn) override;
    void clear(runtime::TimerId id) override;
    void post(std::function<void()> fn) override;

    std::size_t activeTimers() const noexcept { return timers_.size(); }

private:
    runtime::TimerId start(std::int64_t ms, bool repeat, std::function<void()> fn);

    QElapsedTimer clock_;
    runtime::TimerId nextId_ = 1;
    std::unordered_map<runtime::TimerId, QTimer*> timers_;
};

}  // namespace gs::app
