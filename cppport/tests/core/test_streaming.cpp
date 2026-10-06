#include "gs/controller/streaming.hpp"

#include <gtest/gtest.h>

#include <array>
#include <optional>

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

// ---- the execution playhead (server/lib/__tests__/SenderProgress.test.js) ----

namespace {

constexpr std::uint8_t kFeed = 1;
constexpr std::uint8_t kRapid = 2;
constexpr std::uint8_t kFixed = 3;

// Loads a job, gives it estimates and starts streaming.
struct ProgressHarness : SenderHarness {
    std::int64_t t0 = 0;

    ProgressHarness(std::string_view gcode, std::vector<float> lineTime, std::vector<std::uint8_t> lineKind)
        : SenderHarness(Sender::Protocol::CharacterCounting, 128) {
        sender.load("job.nc", std::string(gcode));
        sender.setEstimateData(std::move(lineTime), std::move(lineKind));
        t0 = loop.nowMs();
        sender.next();
    }

    void ackAll(std::size_t count) {
        while (sender.received() < count) {
            sender.ack();
        }
        sender.next({.isOk = true});
    }
    void ackAll() { ackAll(sender.sent()); }

    // A status report `seconds` after the start.
    void report(double seconds, std::string state, std::optional<int> planner = std::nullopt,
                std::optional<std::array<int, 3>> ov = std::nullopt) {
        protocol::StatusReport status;
        status.activeState = std::move(state);
        if (planner) {
            status.buf = protocol::BufferState{*planner, 128};
        }
        status.overrides = ov;
        sender.updateProgress(status, t0 + static_cast<std::int64_t>(seconds * 1000));
    }

    double remaining() const { return sender.status().remainingTime; }
    std::int64_t running() const { return sender.status().currentLineRunning; }
};

}  // namespace

TEST(SenderProgress, SmallFilesDontCountDownAsSoonAsTheyAreBuffered) {
    ProgressHarness h("G1 X1\nG1 X2\nG1 X3", {10, 10, 10}, {kFeed, kFeed, kFeed});
    h.ackAll();
    EXPECT_EQ(h.sender.received(), 3u);
    EXPECT_EQ(h.remaining(), 30);
    EXPECT_EQ(h.running(), 0);

    h.report(5, "Run");
    EXPECT_NEAR(h.remaining(), 25, 1e-3);
    EXPECT_EQ(h.running(), 0);

    h.report(12, "Run");
    EXPECT_NEAR(h.remaining(), 18, 1e-3);
    EXPECT_EQ(h.running(), 1);
}

TEST(SenderProgress, TimeDoesntPassWhileHeld) {
    ProgressHarness h("G1 X1\nG1 X2", {10, 10}, {kFeed, kFeed});
    h.ackAll();
    h.report(4, "Run");
    h.report(60, "Hold:0");
    EXPECT_NEAR(h.remaining(), 16, 1e-3);
    h.report(61, "Run");
    EXPECT_NEAR(h.remaining(), 15, 1e-3);
}

TEST(SenderProgress, ThePlayheadCantPassTheLastAckedLine) {
    ProgressHarness h("G1 X1\nG1 X2\nG1 X3", {10, 10, 10}, {kFeed, kFeed, kFeed});
    h.ackAll(1);
    h.report(25, "Run");
    EXPECT_EQ(h.running(), 1);
    EXPECT_NEAR(h.remaining(), 20, 1e-3);
}

TEST(SenderProgress, PlannerOccupancyPullsASlowPlayheadForward) {
    ProgressHarness h("G1 X1\n(comment)\nG1 X2\nG1 X3", {10, 0, 10, 10}, {kFeed, 0, kFeed, kFeed});
    // Learn the empty planner size (15 blocks available).
    h.report(0, "Idle", 15);
    h.ackAll();
    // One block queued: only the last motion line can still be pending.
    h.report(1, "Run", 14);
    EXPECT_EQ(h.running(), 3);
    EXPECT_NEAR(h.remaining(), 10, 1e-3);
}

TEST(SenderProgress, CommentLinesDontCountAsQueuedPlannerBlocks) {
    ProgressHarness h("G1 X1\nG1 X2\n(a)\n(b)\nG1 X3", {10, 10, 0, 0, 10}, {kFeed, kFeed, 0, 0, kFeed});
    h.report(0, "Idle", 15);
    h.ackAll();
    h.report(0.5, "Run", 13);
    // Lines 1 and 4 may both still be queued, so only line 0 must be done.
    EXPECT_EQ(h.running(), 1);
}

TEST(SenderProgress, ASingleIdleReportDoesntFinishTheJobWithoutBufferInfo) {
    ProgressHarness h("G1 X1\nG1 X2", {10, 10}, {kFeed, kFeed});
    h.ackAll();
    h.report(0.2, "Idle");
    EXPECT_EQ(h.remaining(), 20);
    h.report(0.4, "Idle");
    EXPECT_EQ(h.remaining(), 0);
    EXPECT_EQ(h.running(), 2);
}

TEST(SenderProgress, FeedAndRapidOverridesScaleTheirOwnLinesOnly) {
    ProgressHarness h("G1 X1\nG0 X2\nG4 P5", {10, 10, 5}, {kFeed, kRapid, kFixed});
    h.ackAll();
    h.report(0, "Run", std::nullopt, std::array<int, 3>{200, 50, 100});
    // 10/2 + 10/0.5 + 5
    EXPECT_NEAR(h.remaining(), 30, 1e-3);
    // the feed line now runs at double speed
    h.report(4, "Run");
    EXPECT_NEAR(h.remaining(), 26, 1e-3);
    EXPECT_EQ(h.running(), 0);
    h.report(6, "Run");
    EXPECT_EQ(h.running(), 1);
}

TEST(SenderProgress, AStoppedJobStopsTheClocks) {
    ProgressHarness h("G1 X1\nG1 X2", {10, 10}, {kFeed, kFeed});
    h.ackAll(1);
    h.report(3, "Run");
    h.sender.rewind();  // workflow stop
    const SenderStatus before = h.sender.status();
    h.report(30, "Run");
    EXPECT_EQ(h.remaining(), before.remainingTime);
    EXPECT_EQ(h.sender.status().elapsedTime, before.elapsedTime);
}

TEST(SenderProgress, TheFeedOverrideCommandAppliesBeforeAStatusReportConfirmsIt) {
    ProgressHarness h("G1 X1", {10}, {kFeed});
    h.sender.setOvF(50);
    EXPECT_NEAR(h.remaining(), 20, 1e-3);
}

TEST(SenderProgress, StartFromLineCountsEarlierLinesAsDone) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job.nc", "G1 X1\nG1 X2\nG1 X3");
    h.sender.setEstimateData({10, 10, 10}, {kFeed, kFeed, kFeed});
    h.sender.setStartLine(2);
    h.sender.next({.startFromLine = true});
    EXPECT_NEAR(h.sender.status().remainingTime, 10, 1e-3);
    EXPECT_EQ(h.sender.currentLineRunning(), 2);
}

TEST(SenderProgress, LineIndexesSkipBlankLinesOfAnyLineEndingLikeTheEstimator) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job.nc", "G1 X1\r\n\r\n  \rG1 X2\r(c)\n\nG1 X3");
    ASSERT_EQ(h.sender.total(), 4u);
    EXPECT_EQ(h.sender.line(0), "G1 X1");
    EXPECT_EQ(h.sender.line(1), "G1 X2");
    EXPECT_EQ(h.sender.line(2), "(c)");
    EXPECT_EQ(h.sender.line(3), "G1 X3");
}

TEST(SenderProgress, TheGivenEstimatedTimeWinsOverTheLinesSum) {
    SenderHarness h(Sender::Protocol::CharacterCounting, 128);
    h.sender.load("job.nc", "G1 X1\nG1 X2");
    h.sender.setEstimateData({1.5F, 2.5F}, {kFeed, kRapid}, 4.5);
    EXPECT_EQ(h.sender.status().estimatedTime, 4.5);
    EXPECT_EQ(h.sender.status().remainingTime, 4);  // the lines' own time
    h.sender.setEstimateData({1.5F, 2.5F});
    EXPECT_EQ(h.sender.status().estimatedTime, 4);
}

TEST(SenderProgress, TheJobsEndReportsTheEstimatesAccuracy) {
    ProgressHarness h("G1 X1", {10}, {kFeed});
    h.loop.advance(20000);
    EXPECT_EQ(h.sender.estimateAccuracy(),
              "Job time: estimated=10.0s actual=20.0s paused=0.0s ratio=0.500 ovF=100 ovR=100 lines=1");
    SenderHarness none;
    none.sender.load("job.nc", "G1 X1");
    none.sender.next();
    EXPECT_EQ(none.sender.estimateAccuracy(), "");
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
