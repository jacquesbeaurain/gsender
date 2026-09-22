#include "gs/controller/streaming.hpp"

#include <gtest/gtest.h>

using namespace gs;
using namespace gs::controller;

namespace {

struct SenderHarness {
    runtime::ManualEventLoop loop;
    std::vector<std::string> written;
    int filterCalls = 0;
    std::function<std::string(std::string)> transform;
    Sender sender;

    explicit SenderHarness(Sender::Protocol protocol = Sender::Protocol::CharacterCounting, int buffer = 30)
        : sender(loop, protocol, buffer, [this](std::string line, const expr::Value&) {
              ++filterCalls;
              return transform ? transform(std::move(line)) : line;
          }) {
        sender.onData = [this](const std::string& line) { written.push_back(line); };
    }

    // Simulates the controller's reaction to an "ok".
    void ok() {
        sender.ack();
        sender.next({.isOk = true});
    }
};

}  // namespace

TEST(Sender, CharacterCountingFillsTheBufferWithoutOverflow) {
    SenderHarness h;  // 30 byte buffer
    ASSERT_TRUE(h.sender.load("job", "G1 X1.0000\nG1 X2.0000\nG1 X3.0000\nG1 X4.0000\n"));
    EXPECT_EQ(h.sender.total(), 4u);
    h.sender.next();
    // 11 + 11 = 22 bytes in flight; a third line would reach 33 >= 30.
    ASSERT_EQ(h.written.size(), 2u);
    EXPECT_EQ(h.written[0], "G1 X1.0000\n");
    EXPECT_EQ(h.sender.dataLength(), 22);

    h.ok();
    EXPECT_EQ(h.written.size(), 3u);
    h.ok();
    h.ok();
    h.ok();
    EXPECT_EQ(h.written.size(), 4u);
    EXPECT_EQ(h.sender.received(), 4u);
    EXPECT_EQ(h.sender.dataLength(), 0);
}

TEST(Sender, EndFiresOnceWhenAllLinesAreAcknowledged) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    int ends = 0;
    h.sender.onEnd = [&](std::int64_t) { ++ends; };
    h.sender.load("job", "G0 X0\nG0 X1\n");
    h.sender.next();
    h.ok();
    EXPECT_EQ(ends, 0);
    h.ok();
    EXPECT_EQ(ends, 1);
    h.sender.next();
    EXPECT_EQ(ends, 1);
}

TEST(Sender, BlankAndCommentOnlyLinesAreSkippedAndAcked) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.transform = [](std::string line) { return line.starts_with(";") ? std::string() : line; };
    h.sender.load("job", "G21\n\n   \n; comment\nG0 X1\n");
    EXPECT_EQ(h.sender.total(), 3u);  // blank lines never become program lines
    h.sender.next();
    ASSERT_EQ(h.written.size(), 2u);
    EXPECT_EQ(h.written[1], "G0 X1\n");
    // The filtered-out comment was acknowledged locally.
    EXPECT_EQ(h.sender.received(), 1u);
}

TEST(Sender, HoldStopsStreamingUntilReleased) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.transform = [&h](std::string line) {
        if (line == "M0") {
            h.sender.hold(HoldReason{"M0", "", ""});
            return std::string("(M0)");
        }
        return line;
    };
    h.sender.load("job", "G0 X1\nM0\nG0 X2\n");
    h.sender.next();
    ASSERT_EQ(h.written.size(), 2u);  // the M0 line itself is still sent
    EXPECT_TRUE(h.sender.isHeld());
    EXPECT_EQ(h.sender.holdReason()->data, "M0");
    h.ok();
    h.ok();
    EXPECT_EQ(h.written.size(), 2u);
    h.sender.unhold();
    h.sender.next();
    EXPECT_EQ(h.written.size(), 3u);
}

TEST(Sender, CachedLineIsNotFilteredTwice) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 12);
    h.sender.load("job", "G1 X1.0000\nG1 X2.0000\n");
    h.sender.next();
    EXPECT_EQ(h.written.size(), 1u);
    EXPECT_EQ(h.filterCalls, 2);  // second line filtered, then cached
    h.sender.next();
    EXPECT_EQ(h.filterCalls, 2);
    h.ok();
    EXPECT_EQ(h.written.size(), 2u);
    EXPECT_EQ(h.filterCalls, 2);
}

TEST(Sender, SendResponseSendsOneLinePerAck) {
    SenderHarness h(Sender::Protocol::SendResponse);
    h.sender.load("job", "G0 X1\nG0 X2\nG0 X3\n");
    h.sender.next();
    EXPECT_EQ(h.written.size(), 1u);
    h.ok();
    EXPECT_EQ(h.written.size(), 2u);
}

TEST(Sender, StartFromLineSkipsAhead) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job", "G0 X1\nG0 X2\nG0 X3\nG0 X4\n");
    h.sender.setStartLine(2);
    h.sender.next({.startFromLine = true});
    ASSERT_EQ(h.written.size(), 2u);
    EXPECT_EQ(h.written[0], "G0 X3\n");
    EXPECT_EQ(h.sender.received(), 2u);
}

TEST(Sender, RewindAndUnload) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job", "G0 X1\nG0 X2\n");
    h.sender.next();
    EXPECT_TRUE(h.sender.rewind());
    EXPECT_EQ(h.sender.sent(), 0u);
    EXPECT_EQ(h.sender.dataLength(), 0);
    h.sender.unload();
    EXPECT_FALSE(h.sender.hasProgram());
    EXPECT_FALSE(h.sender.next());
    EXPECT_FALSE(h.sender.load("empty", ""));
}

TEST(Sender, BufferNeverShrinksBelowInFlightData) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job", "G1 X1.0000\n");
    h.sender.next();
    h.sender.setBufferSize(5);
    EXPECT_EQ(h.sender.bufferSize(), 11);
    h.sender.setBufferSize(1016);
    EXPECT_EQ(h.sender.bufferSize(), 1016);
}

TEST(Sender, CountdownFollowsEstimates) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job", "G1 X1\nG1 X2\nG1 X3\n");
    h.sender.setEstimateData({2.0, 3.0, 1.0});
    h.sender.setEstimatedTime(6.0);
    h.sender.next();
    EXPECT_DOUBLE_EQ(h.sender.status().remainingTime, 6.0);
    h.ok();  // received=1 queues estimates 0..1
    h.loop.advance(100);   // the check interval starts the countdown
    h.loop.advance(2000);  // first line's two seconds elapse
    EXPECT_NEAR(h.sender.status().remainingTime, 4.0, 1e-9);
    EXPECT_GE(h.sender.currentLineRunning(), 1);
}

TEST(Sender, OverrideRescalesRemainingTime) {
    SenderHarness h;
    h.sender.load("job", "G1 X1\n");
    h.sender.setEstimatedTime(100);
    h.sender.setOvF(200);
    EXPECT_DOUBLE_EQ(h.sender.status().remainingTime, 50);
    h.sender.setOvF(50);
    EXPECT_DOUBLE_EQ(h.sender.status().remainingTime, 200);
}

TEST(Sender, PeekReportsChangesOnce) {
    SenderHarness h;
    h.sender.load("job", "G0 X1\n");
    EXPECT_TRUE(h.sender.peek());
    EXPECT_FALSE(h.sender.peek());
}

// ---- Feeder --------------------------------------------------------------------

TEST(Feeder, SendsOneCommandPerAck) {
    std::vector<std::string> sent;
    int completes = 0;
    Feeder feeder;
    feeder.onData = [&](const std::string& c, const expr::Value&) { sent.push_back(c); };
    feeder.onComplete = [&] { ++completes; };

    feeder.feed({"G21", "G0 X1", "G0 X2"});
    feeder.next();
    EXPECT_EQ(sent, (std::vector<std::string>{"G21"}));
    EXPECT_TRUE(feeder.isPending());
    EXPECT_TRUE(feeder.hasOutstanding());

    feeder.ack();
    feeder.next();
    feeder.ack();
    feeder.next();
    EXPECT_EQ(sent.size(), 3u);
    EXPECT_EQ(completes, 1);  // the queue drained with the last send
    feeder.ack();
    feeder.next();
    EXPECT_EQ(completes, 2);
    EXPECT_FALSE(feeder.hasOutstanding());
}

TEST(Feeder, HoldBlocksUntilUnhold) {
    std::vector<std::string> sent;
    Feeder feeder([&](std::string c, const expr::Value&) {
        if (c == "M0") {
            return std::string("(M0)");
        }
        return c;
    });
    feeder.onData = [&](const std::string& c, const expr::Value&) { sent.push_back(c); };
    feeder.feed({"G0 X1", "G0 X2"});
    feeder.hold(HoldReason{"M0", "change tool", ""});
    EXPECT_FALSE(feeder.next());
    EXPECT_TRUE(sent.empty());
    EXPECT_EQ(feeder.status().holdReason->comment, "change tool");
    feeder.unhold();
    feeder.next();
    EXPECT_EQ(sent.size(), 1u);
}

TEST(Feeder, FilteredOutCommandsAreSkipped) {
    std::vector<std::string> sent;
    Feeder feeder([](std::string c, const expr::Value&) { return c.starts_with("%") ? std::string() : c; });
    feeder.onData = [&](const std::string& c, const expr::Value&) { sent.push_back(c); };
    feeder.feed({"%x=1", "G0 X1"});
    feeder.next();
    EXPECT_EQ(sent, (std::vector<std::string>{"G0 X1"}));
}

TEST(Feeder, BatchSharesOneContext) {
    std::vector<expr::Value> contexts;
    Feeder feeder;
    feeder.onData = [&](const std::string&, const expr::Value& ctx) { contexts.push_back(ctx); };
    feeder.feed({"A", "B"});
    feeder.next();
    feeder.ack();
    feeder.next();
    ASSERT_EQ(contexts.size(), 2u);
    EXPECT_TRUE(contexts[0].sameAs(contexts[1]));
}

TEST(Feeder, ResetClearsEverything) {
    Feeder feeder;
    feeder.feed({"A", "B"});
    feeder.hold();
    feeder.reset();
    EXPECT_EQ(feeder.size(), 0u);
    EXPECT_FALSE(feeder.isHeld());
    EXPECT_FALSE(feeder.isPending());
}

// ---- Workflow ------------------------------------------------------------------

TEST(Workflow, Transitions) {
    Workflow workflow;
    std::vector<std::string> events;
    workflow.onStart = [&] { events.push_back("start"); };
    workflow.onStop = [&] { events.push_back("stop"); };
    workflow.onPause = [&](const std::optional<HoldReason>& r) { events.push_back(r ? "pause:" + r->data : "pause"); };
    workflow.onResume = [&] { events.push_back("resume"); };

    workflow.stop();  // already idle: no event
    workflow.start();
    workflow.start();  // already running: no event
    workflow.pause(HoldReason{"M6", "", ""});
    EXPECT_TRUE(workflow.isPaused());
    workflow.resume();
    EXPECT_TRUE(workflow.isRunning());
    workflow.stop();
    workflow.pause();  // notifies even when idle (as in gSender)
    EXPECT_TRUE(workflow.isIdle());
    EXPECT_EQ(events, (std::vector<std::string>{"start", "pause:M6", "resume", "stop", "pause"}));
    EXPECT_EQ(workflowStateName(WorkflowState::Paused), "paused");
}
