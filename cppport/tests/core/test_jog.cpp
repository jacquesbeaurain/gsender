// Ports of src/server/lib/__tests__/{JogStreamer,jog-limits}.test.js.

#include "gs/controller/jog_streamer.hpp"
#include "gs/util/jsnumber.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <regex>

using namespace gs;
using namespace gs::controller;

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

protocol::OrderedMap makeSettings(std::initializer_list<std::pair<const char*, const char*>> items) {
    protocol::OrderedMap map;
    for (const auto& [k, v] : items) {
        map.set(k, v);
    }
    return map;
}

protocol::OrderedMap defaultSettings() {
    return makeSettings({{"$13", "0"},
                         {"$20", "0"},
                         {"$23", "0"},
                         {"$110", "10000"},
                         {"$111", "10000"},
                         {"$112", "3000"},
                         {"$120", "500"},
                         {"$121", "500"},
                         {"$122", "200"},
                         {"$130", "500"},
                         {"$131", "400"},
                         {"$132", "100"}});
}

protocol::AxisValues position(double x, double y, double z) {
    protocol::AxisValues v;
    v.values = {x, y, z, 0, 0, 0};
    v.count = 3;
    return v;
}

std::map<char, double> parseJogLine(const std::string& line) {
    std::map<char, double> axes;
    std::string body = line;
    const std::string prefix = "$J=G21G91";
    if (body.starts_with(prefix)) {
        body = body.substr(prefix.size());
    }
    static const std::regex re(R"(([XYZAF])(-?\d+(?:\.\d+)?))");
    for (auto it = std::sregex_iterator(body.begin(), body.end(), re); it != std::sregex_iterator(); ++it) {
        axes[(*it)[1].str()[0]] = std::stod((*it)[2].str());
    }
    return axes;
}

struct Harness {
    runtime::ManualEventLoop loop;
    std::vector<std::string> lines;
    std::vector<std::string> warnings;
    protocol::OrderedMap settings = defaultSettings();
    JogStatus status{"Jog", position(0, 0, 0), std::nullopt};
    std::function<bool()> canStream = [] { return true; };
    std::unique_ptr<JogStreamer> streamer;

    void build() {
        JogStreamer::Options options;
        options.write = [this](const std::string& line) {
            std::string trimmed = line;
            while (!trimmed.empty() && (trimmed.back() == '\n' || trimmed.back() == '\r')) {
                trimmed.pop_back();
            }
            lines.push_back(trimmed);
        };
        options.getSettings = [this] { return settings; };
        options.getStatus = [this] { return status; };
        options.getHomingFlag = [] { return false; };
        options.canStream = [this] { return canStream(); };
        options.warn = [this](const std::string& m) { warnings.push_back(m); };
        streamer = std::make_unique<JogStreamer>(loop, options);
    }

    Harness() { build(); }

    void runAcked(int ms) {
        std::size_t acked = 0;
        const auto step = [&] {
            while (acked < lines.size()) {
                ++acked;
                streamer->ack();
            }
        };
        step();
        for (int elapsed = 0; elapsed < ms; ++elapsed) {
            loop.advance(1);
            step();
        }
    }

    double total(char axis) const {
        double sum = 0;
        for (const std::string& line : lines) {
            auto parsed = parseJogLine(line);
            if (auto it = parsed.find(axis); it != parsed.end()) {
                sum += it->second;
            }
        }
        return sum;
    }

    double lead() const { return streamer->plan().tLook * streamer->plan().feedrate / 60; }
};

}  // namespace

// ---- computeSegmentPlan -----------------------------------------------------------

TEST(JogPlan, LookaheadCoversDeceleration) {
    const SegmentPlan plan = computeSegmentPlan({1, 0, 0, 0}, 3000, {500, 500, 200, 0}, {10000, 10000, 3000, 0});
    EXPECT_GE(plan.inflight * plan.dt, plan.requiredLook);
    EXPECT_FALSE(plan.starved);
}

TEST(JogPlan, LowAccelerationNeedsLongerLookahead) {
    const Axes4 maxRate{10000, 10000, 3000, 0};
    const SegmentPlan slow = computeSegmentPlan({1, 0, 0, 0}, 6000, {50, NAN, NAN, NAN}, maxRate);
    const SegmentPlan fast = computeSegmentPlan({1, 0, 0, 0}, 6000, {2000, NAN, NAN, NAN}, maxRate);
    EXPECT_GT(slow.requiredLook, fast.requiredLook);
}

TEST(JogPlan, FlagsUnreachableSpeed) {
    const SegmentPlan plan = computeSegmentPlan({1, 0, 0, 0}, 10000, {100, NAN, NAN, NAN}, {10000, 10000, 3000, 0});
    EXPECT_GT(plan.requiredLook, jog::kLookMax);
    EXPECT_TRUE(plan.starved);
}

TEST(JogPlan, AssumesAccelerationWhenNoneReported) {
    const SegmentPlan plan = computeSegmentPlan({1, 0, 0, 0}, 3000, {NAN, NAN, NAN, NAN}, {NAN, NAN, NAN, NAN});
    EXPECT_EQ(plan.effectiveAccel, jog::kAssumedAccel);
    EXPECT_FALSE(plan.accelReported);
    EXPECT_EQ(plan.feedrate, 3000);
}

TEST(JogPlan, SilentAxisIsNotInfinitelyCapable) {
    // Upstream's version of this test compares against "both axes 500" and
    // has failed since ASSUMED_ACCEL was raised to 1000 (see DEV_WALKTHROUGH).
    // The intent: an unreported Y still constrains the diagonal instead of
    // being dropped (which would give X alone, 500 * sqrt(2) = 707).
    const Axes4 maxRate{10000, 10000, 3000, 0};
    const SegmentPlan partial = computeSegmentPlan({1, 1, 0, 0}, 3000, {500, NAN, NAN, NAN}, maxRate);
    const double xAlone = 500 * std::sqrt(2.0);
    EXPECT_LT(partial.effectiveAccel, xAlone);
    const double expected = 1 / std::sqrt(std::pow(std::sqrt(0.5) / 500, 2) + std::pow(std::sqrt(0.5) / jog::kAssumedAccel, 2));
    EXPECT_NEAR(partial.effectiveAccel, expected, 1e-9);
    EXPECT_TRUE(partial.accelReported);
}

TEST(JogPlan, RespectsSerialRateAndMinimumSegment) {
    const Axes4 accel{500, 500, 200, 0};
    const Axes4 maxRate{10000, 10000, 3000, 0};
    const SegmentPlan fast = computeSegmentPlan({1, 0, 0, 0}, 10000, accel, maxRate);
    EXPECT_GE(fast.dt, 1 / jog::kMaxSegmentRateHz - 1e-9);
    EXPECT_GE(fast.dt, jog::kDtMin - 1e-9);
    const SegmentPlan crawl = computeSegmentPlan({1, 0, 0, 0}, 6, accel, maxRate);
    EXPECT_GE(crawl.segmentLength, jog::kMinSegmentMm - 1e-9);
}

TEST(JogPlan, ClampsDiagonalAndZ) {
    const Axes4 accel{500, 500, 200, 0};
    const SegmentPlan diagonal = computeSegmentPlan({1, 1, 0, 0}, 10000, accel, {10000, 1000, NAN, NAN});
    EXPECT_NEAR(diagonal.feedrate, 1000 * std::sqrt(2.0), 1e-3);
    const SegmentPlan z = computeSegmentPlan({0, 0, -1, 0}, 10000, accel, {10000, 10000, 3000, 0});
    EXPECT_EQ(z.feedrate, 3000);
}

// ---- velocity mode ------------------------------------------------------------------

TEST(JogVelocity, DistanceIsProportionalToTime) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 1200);  // 20 mm/s
    h.runAcked(1000);
    const double segment = h.streamer->plan().segmentLength;
    EXPECT_GT(h.total('X'), 20 + h.lead() - 2 * segment);
    EXPECT_LT(h.total('X'), 20 + h.lead() + 2 * segment);
}

TEST(JogVelocity, KeepsMotionQueuedAheadOfTheClock) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 1200);
    double minLead = 1e9;
    for (int elapsed = 0; elapsed < 500; ++elapsed) {
        h.loop.advance(1);
        while (h.streamer->ack()) {
        }
        minLead = std::min(minLead, h.streamer->emittedUntil() - static_cast<double>(h.loop.nowMs()));
    }
    EXPECT_GT(minLead, h.streamer->plan().requiredLook * 1000);
}

TEST(JogVelocity, HoldsVelocityAtVeryLowFeedrate) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 60);  // 1 mm/s
    h.runAcked(2000);
    const double segment = h.streamer->plan().segmentLength;
    EXPECT_GT(h.total('X'), 2 + h.lead() - 2 * segment);
    EXPECT_LT(h.total('X'), 2 + h.lead() + 2 * segment);
}

TEST(JogVelocity, DiagonalKeepsVectorMagnitude) {
    Harness h;
    h.streamer->start({1, -1, 0, 0}, 1200);
    h.runAcked(1000);
    const double x = h.total('X');
    const double y = h.total('Y');
    EXPECT_NEAR(std::fabs(x), std::fabs(y), 0.05);
    EXPECT_LT(y, 0);
    const double segment = h.streamer->plan().segmentLength;
    const double magnitude = std::hypot(x, y);
    EXPECT_GT(magnitude, 20 + h.lead() - 2 * segment);
    EXPECT_LT(magnitude, 20 + h.lead() + 2 * segment);
}

TEST(JogVelocity, CarriesSubPrecisionRemainders) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 6);  // 0.1 mm/s
    h.runAcked(10000);
    const double segment = h.streamer->plan().segmentLength;
    EXPECT_GT(h.total('X'), 1 + h.lead() - 2 * segment);
    EXPECT_LT(h.total('X'), 1 + h.lead() + 2 * segment);
    for (const std::string& line : h.lines) {
        EXPECT_NE(parseJogLine(line)['X'], 0) << line;
    }
}

TEST(JogVelocity, ChangesDirectionWithoutStopping) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.runAcked(200);
    const std::size_t before = h.lines.size();
    h.streamer->update({0, 1, 0, 0}, 1200);
    EXPECT_EQ(h.streamer->state(), JogState::Streaming);
    h.runAcked(200);
    ASSERT_GT(h.lines.size(), before);
    auto last = parseJogLine(h.lines.back());
    EXPECT_GT(last['Y'], 0);
    EXPECT_EQ(last.count('X'), 0u);
}

TEST(JogVelocity, KeepsFeedrateWhenABadOneArrives) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.streamer->update({1, 1, 0, 0}, std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(h.streamer->plan().feedrate, 1200);
    h.runAcked(100);
    EXPECT_EQ(parseJogLine(h.lines.back())['F'], 1200);
}

TEST(JogVelocity, UnusableStartFeedrateFallsBackToDefault) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, std::nullopt);
    EXPECT_EQ(h.streamer->plan().feedrate, jog::kDefaultFeedrate);
}

TEST(JogVelocity, AnnouncesStartInRequestedUnits) {
    Harness h;
    std::vector<std::string> started;
    h.streamer->onStart = [&](const std::string& s) { started.push_back(s); };
    h.streamer->start({1, -1, 0, 0}, 1200);
    EXPECT_EQ(started, (std::vector<std::string>{"Started continuous jogging X+ Y- at 1200 mm/min"}));

    Harness imperial;
    std::vector<std::string> startedIn;
    imperial.streamer->onStart = [&](const std::string& s) { startedIn.push_back(s); };
    imperial.streamer->start({1, 0, 0, 0}, 50, JogUnits::Inches);
    EXPECT_EQ(startedIn, (std::vector<std::string>{"Started continuous jogging X+ at 50 in/min"}));
}

TEST(JogVelocity, AnnouncesSpeedChangesAtMostOncePerSecond) {
    Harness h;
    std::vector<std::string> changes;
    h.streamer->onFeedrate = [&](const std::string& s) { changes.push_back(s); };
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.streamer->update({1, 0, 0, 0}, 1200);
    h.streamer->update({1, 0, 0, 0}, 2400);
    EXPECT_TRUE(changes.empty());
    h.loop.advance(1000);
    h.streamer->update({1, 0, 0, 0}, 2400);
    h.streamer->update({1, 0, 0, 0}, 3000);
    EXPECT_EQ(changes, (std::vector<std::string>{"Jog speed now 2400 mm/min"}));
}

TEST(JogVelocity, WarnsOnlyAboutReportedAcceleration) {
    // Fast enough that even the assumed 1000 mm/s^2 cannot hold it (upstream
    // used 10000 mm/min, which stopped starving when ASSUMED_ACCEL went up).
    Harness unreported;
    unreported.settings = {};
    unreported.streamer->start({1, 0, 0, 0}, 50000);
    EXPECT_TRUE(unreported.streamer->plan().starved);
    EXPECT_TRUE(unreported.warnings.empty());

    Harness reported;
    reported.settings.set("$120", "50");
    reported.settings.set("$121", "50");
    reported.streamer->start({1, 0, 0, 0}, 10000);
    EXPECT_TRUE(reported.streamer->plan().starved);
    ASSERT_FALSE(reported.warnings.empty());
    EXPECT_NE(reported.warnings.front().find("acceleration"), std::string::npos);
}

TEST(JogVelocity, DeratesZForParity) {
    Harness h;
    h.streamer->start({0, 0, -1, 0}, 1000);
    EXPECT_EQ(parseJogLine(h.lines.front())['F'], 800);
}

// ---- backpressure -------------------------------------------------------------------

TEST(JogBackpressure, NeverExceedsRxBudgetWithoutAcks) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 6000);
    h.loop.advance(2000);
    EXPECT_LE(h.streamer->pendingBytes(), 128 - jog::kRxMarginBytes);
}

TEST(JogBackpressure, NoCatchUpBurstAfterStall) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.loop.advance(1000);
    const double stalled = h.total('X');
    h.runAcked(1000);
    EXPECT_LT(h.total('X') - stalled, 20 + h.lead() + h.streamer->plan().segmentLength);
}

TEST(JogBackpressure, BacksOffWhenPlannerIsFull) {
    Harness h;
    h.status.buf = protocol::BufferState{1, 0};
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.streamer->onStatus(h.status);
    const std::size_t before = h.lines.size();
    h.runAcked(500);
    EXPECT_EQ(h.lines.size(), before);
}

// ---- acks -----------------------------------------------------------------------------

TEST(JogAcks, ClaimsOnlyItsOwnLines) {
    Harness h;
    EXPECT_FALSE(h.streamer->ack());
    h.streamer->start({1, 0, 0, 0}, 1200);
    const std::size_t primed = h.streamer->pendingCount();
    EXPECT_GT(primed, 1u);
    for (std::size_t i = 0; i < primed; ++i) {
        EXPECT_TRUE(h.streamer->ack());
    }
    EXPECT_FALSE(h.streamer->ack());
}

TEST(JogAcks, DrainsAfterStopThenIdles) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 6000);
    h.loop.advance(100);
    const std::size_t outstanding = h.streamer->pendingCount();
    ASSERT_GT(outstanding, 0u);
    h.streamer->stop();
    EXPECT_EQ(h.streamer->state(), JogState::Draining);
    for (std::size_t i = 0; i < outstanding; ++i) {
        EXPECT_TRUE(h.streamer->ack());
    }
    EXPECT_EQ(h.streamer->state(), JogState::Idle);
    EXPECT_FALSE(h.streamer->ack());
}

TEST(JogAcks, GivesUpOnUndeliveredAcks) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 6000);
    h.loop.advance(100);
    h.streamer->stop();
    h.loop.advance(2000);
    EXPECT_EQ(h.streamer->state(), JogState::Idle);
    EXPECT_GT(h.streamer->acksOrphaned(), 0);
}

// ---- travel limits ---------------------------------------------------------------------

TEST(JogLimits, StopsAtSoftLimit) {
    Harness h;
    h.settings.set("$20", "1");
    h.status.mpos = position(-495, 0, 0);
    bool limited = false;
    h.streamer->onLimit = [&] { limited = true; };
    h.streamer->start({-1, 0, 0, 0}, 1200);
    h.runAcked(2000);
    EXPECT_TRUE(limited);
    EXPECT_NEAR(h.total('X'), -4, 0.05);
}

TEST(JogLimits, RunsIndefinitelyWithoutSoftLimits) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 6000);
    h.runAcked(3000);
    EXPECT_GT(std::fabs(h.total('X')), 200);
    EXPECT_EQ(h.streamer->state(), JogState::Streaming);
}

// ---- displacement mode ------------------------------------------------------------------

TEST(JogDisplacement, BlendsPulses) {
    Harness h;
    h.streamer->feed({5, 0, 0, 0}, 1200);
    h.runAcked(100);
    EXPECT_EQ(h.streamer->state(), JogState::Streaming);
    h.streamer->feed({5, 0, 0, 0}, 1200);
    h.runAcked(1000);
    EXPECT_NEAR(h.total('X'), 10, 0.05);
}

TEST(JogDisplacement, IdlesWhenTheWheelStops) {
    Harness h;
    bool drained = false;
    h.streamer->onDrained = [&] { drained = true; };
    h.streamer->feed({1, 0, 0, 0}, 1200);
    h.runAcked(1000);
    EXPECT_TRUE(drained);
    EXPECT_EQ(h.streamer->state(), JogState::Idle);
    EXPECT_NEAR(h.total('X'), 1, 0.005);
}

TEST(JogDisplacement, ReversalDiscardsRemainder) {
    Harness h;
    h.streamer->feed({50, 0, 0, 0}, 600);
    h.runAcked(50);
    h.streamer->feed({-1, 0, 0, 0}, 600);
    EXPECT_NEAR(h.streamer->work().X, -1, 1e-3);
}

// ---- safety --------------------------------------------------------------------------------

TEST(JogSafety, RefusesToStartWhenBusy) {
    Harness h;
    h.canStream = [] { return false; };
    EXPECT_FALSE(h.streamer->start({1, 0, 0, 0}, 1200));
    EXPECT_TRUE(h.lines.empty());
}

TEST(JogSafety, AbortsWhenPreconditionsFail) {
    Harness h;
    bool allowed = true;
    h.canStream = [&] { return allowed; };
    h.streamer->start({1, 0, 0, 0}, 1200);
    allowed = false;
    h.loop.advance(50);
    EXPECT_EQ(h.streamer->state(), JogState::Idle);
}

TEST(JogSafety, AbortsOnAlarm) {
    Harness h;
    std::string reason;
    h.streamer->onAbort = [&](const std::string& r) { reason = r; };
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.streamer->onStatus(JogStatus{"Alarm", position(0, 0, 0), std::nullopt});
    EXPECT_EQ(h.streamer->state(), JogState::Idle);
    EXPECT_EQ(reason, "state:Alarm");
    EXPECT_EQ(describeJogStopReason(reason), std::optional<std::string>("the machine went into Alarm"));
}

TEST(JogSafety, WatchdogStopsUnsteeredStream) {
    Harness h;
    h.streamer->start({1, 0, 0, 0}, 1200);
    h.runAcked(static_cast<int>(jog::kMaxStreamDurationMs) + 100);
    EXPECT_EQ(h.streamer->state(), JogState::Idle);
}

TEST(JogSafety, IgnoresUpdateWhenIdle) {
    Harness h;
    EXPECT_FALSE(h.streamer->update({1, 0, 0, 0}, 1200));
    EXPECT_TRUE(h.lines.empty());
}

TEST(JogSafety, StopReasons) {
    EXPECT_EQ(describeJogStopReason("command:gcode"), std::optional<std::string>("another command was sent"));
    EXPECT_EQ(describeJogStopReason("cancel"), std::optional<std::string>("cancelled"));
    EXPECT_EQ(describeJogStopReason("abort"), std::nullopt);
    EXPECT_EQ(describeJogStopReason(""), std::nullopt);
}

// ---- jog-limits.js -------------------------------------------------------------------------

TEST(JogLimitsMath, AxisTravelLimitMatchesGrblHalLegacy) {
    const auto legacyHal = [](int direction, double position, double maxTravel) {
        const double offset = -1;
        double v;
        if (position == 0) {
            v = (maxTravel + offset) * direction;
        } else if (direction == 1) {
            v = position + offset;
        } else {
            v = -1 * (maxTravel - position + offset);
        }
        return js::stringToNumber(js::toFixed(v, 2));
    };
    for (double position : {0.0, 1.0, 12.5, 50.0, 99.0, 100.0}) {
        for (int direction : {1, -1}) {
            EXPECT_EQ(axisTravelLimit(direction, position, 100), legacyHal(direction, position, 100));
        }
    }
    EXPECT_EQ(axisTravelLimit(1, 0, 100), 99);
}

TEST(JogLimitsMath, LegacyGrblZSpecialCase) {
    for (double mposZ : {-30.0, -1.0, -99.0}) {
        EXPECT_EQ(axisTravelLimit(1, std::fabs(mposZ), 100), std::fabs(mposZ + 1));
        EXPECT_EQ(axisTravelLimit(-1, std::fabs(mposZ), 100), -1 * (100 - 1) - mposZ);
    }
}

TEST(JogLimitsMath, TravelBudget) {
    const protocol::OrderedMap settings =
        makeSettings({{"$13", "0"}, {"$23", "0"}, {"$130", "500"}, {"$131", "400"}, {"$132", "100"}});
    const protocol::AxisValues mpos = position(-100, -200, -30);
    const Axes4 direction{1, -1, -1, 1};

    const Axes4 off = computeTravelBudget(direction, settings, mpos, false, false);
    EXPECT_EQ(off, (Axes4{kInf, kInf, kInf, kInf}));

    const Axes4 on = computeTravelBudget(direction, settings, mpos, false, true);
    EXPECT_EQ(on.A, kInf);
    EXPECT_EQ(on.X, axisTravelLimit(1, 100, 500));
    EXPECT_EQ(on.Y, axisTravelLimit(-1, 200, 400));
    EXPECT_EQ(on.Z, axisTravelLimit(-1, 30, 100));

    const Axes4 xOnly = computeTravelBudget({1, 0, 0, 0}, settings, mpos, false, true);
    EXPECT_EQ(xOnly.Y, kInf);
    EXPECT_EQ(xOnly.Z, kInf);
    EXPECT_NE(xOnly.X, kInf);

    for (const char* mask : {"0", "1", "2", "3"}) {
        protocol::OrderedMap homed = settings;
        homed.set("$23", mask);
        const Axes4 budget = computeTravelBudget(direction, homed, mpos, true, true);
        const auto [xMax, yMax] = axisMaximumLocation(js::stringToNumber(mask));
        EXPECT_EQ(budget.X, determineMaxMovement(100, 1, xMax, 500));
        EXPECT_EQ(budget.Y, determineMaxMovement(200, -1, yMax, 400));
    }

    protocol::OrderedMap imperial = settings;
    imperial.set("$13", "1");
    const Axes4 inches = computeTravelBudget({0, 0, -1, 0}, imperial, position(0, 0, -1), false, true);
    EXPECT_EQ(inches.Z, axisTravelLimit(-1, 25.4, 100));
}

TEST(Homing, MachineZeroFlags) {
    protocol::OrderedMap settings = makeSettings({{"$23", "3"}});
    EXPECT_TRUE(determineMachineZeroFlagSet(position(-0.5, 0.2, 0), settings));
    EXPECT_FALSE(determineMachineZeroFlagSet(position(-10, 0, 0), settings));
    settings.set("$23", "0");  // back-right homing never sets the flag
    EXPECT_FALSE(determineMachineZeroFlagSet(position(0, 0, 0), settings));

    EXPECT_FALSE(determineHalMachineZeroFlag({}));
    EXPECT_TRUE(determineHalMachineZeroFlag(makeSettings({{"$22", "15"}})));
    EXPECT_FALSE(determineHalMachineZeroFlag(makeSettings({{"$22", "7"}})));
}
