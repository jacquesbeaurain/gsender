#include "gs/runtime/event_loop.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace gs::runtime;

TEST(ManualEventLoop, RunsTimersInTimeOrder) {
    ManualEventLoop loop;
    const std::int64_t start = loop.nowMs();
    EXPECT_GT(start, 0);
    std::vector<std::string> order;
    loop.setTimeout(300, [&] { order.push_back("c"); });
    loop.setTimeout(100, [&] { order.push_back("a"); });
    loop.setTimeout(100, [&] { order.push_back("b"); });  // same time: scheduling order
    loop.advance(99);
    EXPECT_TRUE(order.empty());
    loop.advance(1);
    EXPECT_EQ(order, (std::vector<std::string>{"a", "b"}));
    loop.advance(500);
    EXPECT_EQ(order.back(), "c");
    EXPECT_EQ(loop.nowMs() - start, 600);
}

TEST(ManualEventLoop, IntervalsRepeatUntilCleared) {
    ManualEventLoop loop;
    int ticks = 0;
    TimerId id = loop.setInterval(250, [&] { ++ticks; });
    loop.advance(1000);
    EXPECT_EQ(ticks, 4);
    loop.clear(id);
    loop.advance(1000);
    EXPECT_EQ(ticks, 4);
    EXPECT_EQ(loop.activeTimers(), 0u);
}

TEST(ManualEventLoop, TimersScheduledFromCallbacksRunAtTheRightTime) {
    ManualEventLoop loop;
    const std::int64_t start = loop.nowMs();
    std::vector<std::int64_t> times;
    loop.setTimeout(100, [&] {
        times.push_back(loop.nowMs() - start);
        loop.setTimeout(50, [&] { times.push_back(loop.nowMs() - start); });
    });
    loop.advance(1000);
    EXPECT_EQ(times, (std::vector<std::int64_t>{100, 150}));
}

TEST(ManualEventLoop, PostedTasksRunOnAdvance) {
    ManualEventLoop loop;
    int runs = 0;
    loop.post([&] { ++runs; });
    EXPECT_EQ(runs, 0);
    loop.advance(0);
    EXPECT_EQ(runs, 1);
}

TEST(TimerScope, CancelsItsTimersOnDestruction) {
    ManualEventLoop loop;
    int fired = 0;
    {
        TimerScope scope(loop);
        scope.timeout(100, [&] { ++fired; });
        scope.interval(100, [&] { ++fired; });
        EXPECT_EQ(loop.activeTimers(), 2u);
    }
    EXPECT_EQ(loop.activeTimers(), 0u);
    loop.advance(1000);
    EXPECT_EQ(fired, 0);
}

TEST(TimerScope, ClearResetsTheId) {
    ManualEventLoop loop;
    TimerScope scope(loop);
    int fired = 0;
    TimerId id = scope.timeout(100, [&] { ++fired; });
    scope.clear(id);
    EXPECT_EQ(id, 0u);
    loop.advance(200);
    EXPECT_EQ(fired, 0);
    scope.timeout(10, [&] { ++fired; });
    loop.advance(20);
    EXPECT_EQ(fired, 1);
}
