// Jogging helpers from gSender's Jogging feature: the step command, the limit
// filter, the presets and JogHelper's tap/hold timing.

#include "gs/controller/jogging.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace gs;
using namespace gs::controller;

namespace {

TEST(JogCommand, UsesTheWorkspaceUnitsAndJsNumbers) {
    EXPECT_EQ(jogCommand({{'X', 5}}, 3000, true), "$J=G21 G91 X5 F3000");
    EXPECT_EQ(jogCommand({{'X', 0.5}, {'Y', -0.5}}, 1000, false), "$J=G20 G91 X0.5 Y-0.5 F1000");
    EXPECT_EQ(jogCommand({{'z', 0.1}}, 196.85, true), "$J=G21 G91 Z0.1 F196.85");
}

TEST(JogLimits, BlockOnlyMovesTowardsATriggeredSwitch) {
    const JogAxes xy{{'X', -5}, {'Y', 5}};
    EXPECT_EQ(filterAxesForLimits(xy, "X", false), xy);  // the protection is off
    EXPECT_EQ(filterAxesForLimits(xy, "X", true), (JogAxes{{'Y', 5}}));
    EXPECT_EQ(filterAxesForLimits(xy, "Y", true), xy);  // Y+ moves away from the Y- switch
    EXPECT_EQ(filterAxesForLimits({{'Z', 2}}, "Z", true), std::nullopt);  // Z homes up
    EXPECT_EQ(filterAxesForLimits({{'Z', -2}}, "Z", true), (JogAxes{{'Z', -2}}));
    EXPECT_EQ(filterAxesForLimits({{'x', -1}}, "XP", true), std::nullopt);  // lower-case keys too
}

TEST(JogPresets, DefaultsAndCycleOrder) {
    EXPECT_EQ(defaultJogSpeeds(JogPreset::Rapid), (JogSpeeds{20, 10, 20, 5000}));
    EXPECT_EQ(defaultJogSpeeds(JogPreset::Normal), (JogSpeeds{5, 2, 5, 3000}));
    EXPECT_EQ(defaultJogSpeeds(JogPreset::Precise), (JogSpeeds{0.5, 0.1, 0.5, 1000}));
    EXPECT_EQ(nextJogPreset(JogPreset::Rapid), JogPreset::Normal);
    EXPECT_EQ(nextJogPreset(JogPreset::Precise), JogPreset::Rapid);
}

class JogHelperTest : public ::testing::Test {
protected:
    JogHelper::Callbacks callbacks() {
        return {
            [this](const JogAxes& axes, double feed) { calls.push_back("jog " + describe(axes, feed)); },
            [this](const JogAxes& axes, double feed) { calls.push_back("start " + describe(axes, feed)); },
            [this] { calls.emplace_back("stop"); },
        };
    }

    static std::string describe(const JogAxes& axes, double feed) {
        return jogCommand(axes, feed, true);
    }

    runtime::ManualEventLoop loop;
    std::vector<std::string> calls;
};

TEST_F(JogHelperTest, ATapJogsOneStep) {
    JogHelper helper(loop, callbacks());
    helper.keyDown({{'X', 5}}, 3000);
    loop.advance(100);
    helper.keyUp();
    loop.advance(1000);
    EXPECT_EQ(calls, (std::vector<std::string>{"jog $J=G21 G91 X5 F3000"}));
}

TEST_F(JogHelperTest, AHoldJogsContinuouslyUntilRelease) {
    JogHelper helper(loop, callbacks());
    helper.keyDown({{'Y', -5}}, 3000);
    loop.advance(249);
    EXPECT_TRUE(calls.empty());
    loop.advance(1);
    helper.keyDown({{'Y', -5}}, 3000);  // auto-repeat is ignored
    loop.advance(2000);
    helper.keyUp();
    EXPECT_EQ(calls, (std::vector<std::string>{"start $J=G21 G91 Y-5 F3000", "stop"}));
    EXPECT_FALSE(helper.isPressed());
}

TEST_F(JogHelperTest, StepsAreThrottledTo150Ms) {
    JogHelper helper(loop, callbacks());
    for (int i = 0; i < 3; ++i) {
        helper.keyDown({{'Z', 2}}, 1000);
        loop.advance(40);
        helper.keyUp();
        loop.advance(10);
    }
    EXPECT_EQ(calls.size(), 1u);  // taps 50 ms apart
    loop.advance(150);
    helper.keyDown({{'Z', 2}}, 1000);
    helper.keyUp();
    EXPECT_EQ(calls.size(), 2u);
}

TEST_F(JogHelperTest, AReleaseWithoutAPressDoesNothing) {
    JogHelper helper(loop, callbacks(), 300);
    helper.keyUp();
    loop.advance(1000);
    EXPECT_TRUE(calls.empty());
    helper.keyDown({{'X', 1}}, 500);
    loop.advance(299);
    helper.keyUp();  // still under the custom threshold: a step
    EXPECT_EQ(calls, (std::vector<std::string>{"jog $J=G21 G91 X1 F500"}));
}

}  // namespace

TEST(JogInput, NudgesByTheLeadingDigit) {
    // The presets' values (Normal 5 mm, 2 mm, 3000 mm/min; Precise 0.5, 0.1).
    EXPECT_EQ(jogInputNudge(5, true), 6);
    EXPECT_EQ(jogInputNudge(5, false), 4);
    EXPECT_EQ(jogInputNudge(3000, true), 4000);
    EXPECT_EQ(jogInputNudge(3000, false), 2000);
    EXPECT_EQ(jogInputNudge(0.5, true), 0.6);
    EXPECT_EQ(jogInputNudge(0.5, false), 0.4);
    // A digit finer where the step would pass the leading one.
    EXPECT_EQ(jogInputNudge(0.1, false), 0.09);
    EXPECT_EQ(jogInputNudge(110, false), 100);
    EXPECT_EQ(jogInputNudge(1, false), 0.9);
    EXPECT_EQ(jogInputNudge(0.02, true), 0.03);
    EXPECT_EQ(jogInputNudge(0.001, false), 0.001);  // the step would go under 0.001: none, it stays
    // Zero: + gives 0.1, - stays.
    EXPECT_EQ(jogInputNudge(0, true), 0.1);
    EXPECT_EQ(jogInputNudge(0, false), 0);
    // Larger values round to their second digit (Math.round: 21.5 -> 22).
    EXPECT_EQ(jogInputNudge(115, true), 220);
    EXPECT_EQ(jogInputNudge(45.1, true), 55);
    EXPECT_EQ(jogInputNudge(45.1, false), 35);
    // Floating point stays clean (0.7 + 0.1).
    EXPECT_EQ(jogInputNudge(0.7, true), 0.8);
}
