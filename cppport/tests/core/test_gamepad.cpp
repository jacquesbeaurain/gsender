// Gamepads: profiles (lib/gamepad, store/defaultState/gamepad.ts), the
// input listener (gamepad.js), and the Jogging widget's stick jogging
// (JoystickLoop.js, MPGJogManager.ts) driven with fake pads and simulated time.

#include "gs/gamepad/input.hpp"
#include "gs/gamepad/profile.hpp"
#include "gs/gamepad/stick_jog.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <string>
#include <vector>

using namespace gs;
using namespace gs::gamepad;
namespace json = boost::json;

namespace {

const std::string kXboxId = "Xbox 360 Controller (STANDARD GAMEPAD Vendor: 045e Product: 028e)";

std::vector<bool> buttons(std::initializer_list<int> held, int count = 17) {
    std::vector<bool> out(static_cast<std::size_t>(count), false);
    for (const int b : held) {
        out[static_cast<std::size_t>(b)] = true;
    }
    return out;
}

// ---- profiles ----------------------------------------------------------------------------

TEST(GamepadProfile, DefaultsAreUpstreams) {
    const std::vector<Profile> profiles = defaultProfiles();
    ASSERT_EQ(profiles.size(), 2u);
    EXPECT_EQ(profiles[0].name, "Logitech F710 Gamepad");
    EXPECT_EQ(profiles[0].buttons.size(), 16u);  // no Home
    EXPECT_EQ(profiles[1].name, "Xbox Controller");
    ASSERT_EQ(profiles[1].buttons.size(), 17u);
    EXPECT_EQ(profiles[1].buttons[16].label, "Home");
    EXPECT_EQ(profiles[1].buttons[4].label, "LB");
    EXPECT_TRUE(profiles[1].buttons[0].primaryAction.empty());
    const JoystickOptions& j = profiles[1].joystickOptions;
    EXPECT_EQ(j.stick1.horizontal.primaryAction, "x");
    EXPECT_EQ(j.stick1.vertical.primaryAction, "y");
    EXPECT_EQ(j.stick2.horizontal.primaryAction, "a");
    EXPECT_EQ(j.stick2.vertical.primaryAction, "z");
    EXPECT_EQ(j.zeroThreshold, 30);
    EXPECT_EQ(j.movementDistanceOverride, 100);
    EXPECT_FALSE(j.fixedSpeedMode);
    EXPECT_FALSE(profiles[1].lockout);
    EXPECT_FALSE(profiles[1].modifier);
}

TEST(GamepadProfile, FoundByIdThenByVendorAndProduct) {
    const std::vector<Profile> profiles = defaultProfiles();
    EXPECT_EQ(findProfile(profiles, kXboxId), &profiles[1]);
    EXPECT_EQ(findProfile(profiles, "Xbox 360 Controller (XInput STANDARD GAMEPAD)"), &profiles[1]);
    // Firefox and Chrome name the same pad differently; the USB ids agree.
    EXPECT_EQ(findProfile(profiles, "045e-028e-Microsoft X-Box 360 pad"), nullptr);
    EXPECT_EQ(findProfile(profiles, "X360 (STANDARD GAMEPAD Vendor: 045E Product: 028E)"), &profiles[1]);
    EXPECT_EQ(findProfile(profiles, "Some pad (Vendor: 1234 Product: 5678)"), nullptr);
    EXPECT_EQ(vendorProduct("Wireless Gamepad (Vendor: 2563 Product: 0575)"), "2563:0575");
    EXPECT_EQ(vendorProduct("no ids"), "");
}

TEST(GamepadProfile, ReadsUpstreamsJsonAndWritesItBack) {
    // store/gamepad.js's shape: modifier.button false, labels as numbers.
    const json::value upstream = json::parse(R"j({
        "id": ["Pad (STANDARD GAMEPAD Vendor: 0001 Product: 0002)"], "icon": "fas fa-gamepad",
        "active": true, "profileName": "Pad", "shortcuts": {}, "name": "My Pad", "mapping": "standard",
        "buttons": [{"label": 0, "value": 0, "primaryAction": "JOG_X_P", "secondaryAction": null},
                    {"label": "B", "value": 1, "primaryAction": null, "secondaryAction": "macro-1"}],
        "axes": [0, 0, 0, 0],
        "joystickOptions": {"stick1": {"horizontal": {"primaryAction": "x", "secondaryAction": "z", "isReversed": true},
                                       "vertical": {"primaryAction": null, "secondaryAction": "y", "isReversed": false},
                                       "mpgMode": {"primaryAction": null, "secondaryAction": null, "isReversed": false}},
                            "zeroThreshold": 15, "movementDistanceOverride": 50},
        "lockout": {"button": 4, "active": false}, "modifier": {"button": false}})j");
    const Profile p = profileFromJson(upstream);
    EXPECT_EQ(p.ids, std::vector<std::string>{"Pad (STANDARD GAMEPAD Vendor: 0001 Product: 0002)"});
    EXPECT_EQ(p.name, "My Pad");
    ASSERT_EQ(p.buttons.size(), 2u);
    EXPECT_EQ(p.buttons[0].label, "0");
    EXPECT_EQ(p.buttons[0].primaryAction, "JOG_X_P");
    EXPECT_EQ(p.buttons[1].secondaryAction, "macro-1");
    EXPECT_EQ(p.joystickOptions.stick1.horizontal.secondaryAction, "z");
    EXPECT_TRUE(p.joystickOptions.stick1.horizontal.isReversed);
    EXPECT_EQ(p.joystickOptions.stick1.vertical.primaryAction, "");
    EXPECT_EQ(p.joystickOptions.stick2.vertical.primaryAction, "z");  // missing: the default
    EXPECT_EQ(p.joystickOptions.zeroThreshold, 15);
    EXPECT_EQ(p.joystickOptions.movementDistanceOverride, 50);
    EXPECT_EQ(p.lockout, 4);
    EXPECT_FALSE(p.modifier);

    const json::value written = profileToJson(p);
    EXPECT_EQ(written.at("buttons").at(0).at("secondaryAction"), nullptr);
    EXPECT_EQ(written.at("lockout").at("button"), 4);
    EXPECT_EQ(written.at("modifier").at("button"), nullptr);
    EXPECT_EQ(profileFromJson(written), p);

    const std::vector<Profile> all = defaultProfiles();
    EXPECT_EQ(profilesFromJson(profilesToJson(all)), all);
}

TEST(GamepadProfile, ImportTakesTheMappingsNotTheIdentity) {
    Profile mine = defaultProfiles()[1];
    Profile theirs = defaultProfiles()[0];
    theirs.buttons[0].primaryAction = "START_JOB";
    theirs.lockout = 5;
    const json::value file = exportProfile(theirs, "2026-09-30T00:00:00.000Z");
    EXPECT_EQ(file.at("version"), "1.0");
    const std::optional<Profile> merged = importProfile(mine, file);
    ASSERT_TRUE(merged);
    EXPECT_EQ(merged->ids, mine.ids);
    EXPECT_EQ(merged->name, mine.name);
    EXPECT_EQ(merged->buttons, theirs.buttons);
    EXPECT_EQ(merged->lockout, 5);
    EXPECT_FALSE(importProfile(mine, json::parse(R"({"version": "1.0"})")));
}

TEST(GamepadProfile, ButtonActions) {
    Profile p = defaultProfiles()[1];
    p.button(0)->primaryAction = "START_JOB";
    p.button(0)->secondaryAction = "STOP_JOB";
    p.button(15)->primaryAction = "JOG_X_P";
    EXPECT_EQ(buttonAction(p, 0, true, buttons({0})), "START_JOB");
    EXPECT_EQ(buttonAction(p, 0, false, buttons({})), std::nullopt);
    EXPECT_EQ(buttonAction(p, 1, true, buttons({1})), std::nullopt);  // unassigned
    // A released jog button stops the jog, whatever else is held.
    EXPECT_EQ(buttonAction(p, 15, true, buttons({15})), "JOG_X_P");
    EXPECT_EQ(buttonAction(p, 15, false, buttons({})), "STOP_CONT_JOG");

    p.modifier = 5;
    EXPECT_EQ(buttonAction(p, 0, true, buttons({0, 5})), "STOP_JOB");
    EXPECT_EQ(buttonAction(p, 0, true, buttons({0})), "START_JOB");

    p.lockout = 4;
    EXPECT_EQ(buttonAction(p, 0, true, buttons({0})), std::nullopt);
    EXPECT_EQ(buttonAction(p, 0, true, buttons({0, 4})), "START_JOB");
    EXPECT_EQ(buttonAction(p, 0, true, buttons({0, 4, 5})), "STOP_JOB");
    EXPECT_EQ(buttonAction(p, 15, false, buttons({})), "STOP_CONT_JOG");  // stopping needs no lockout
}

// ---- input -------------------------------------------------------------------------------

PadState pad(std::vector<double> axes = {0, 0, 0, 0}, std::vector<bool> held = buttons({})) {
    return PadState{kXboxId, true, std::move(held), std::move(axes)};
}

TEST(GamepadInput, StickGeometry) {
    EXPECT_EQ(stickDegrees(1, 0), 0);
    EXPECT_EQ(stickDegrees(0, -1), 90);  // up
    EXPECT_EQ(stickDegrees(-1, 0), 180);
    EXPECT_EQ(stickDegrees(0, 1), 270);
    EXPECT_EQ(stickDegrees(0.5, -0.5), 45);
    EXPECT_EQ(stickDegrees(0, 0), 0);
    EXPECT_EQ(stickDistance(0.3, 0.4), 0.5);
    EXPECT_EQ(stickDistance(1, 1), 1.41);
    EXPECT_EQ(roundAxis(0.12345), 0.12);
    EXPECT_EQ(roundAxis(-0.005), -0.0);  // JS Math.round: halves up
    EXPECT_TRUE(sticksIdle({0.29, -0.3, 0, 0}, 30));
    EXPECT_FALSE(sticksIdle({0.3, 0, 0, 0}, 30));
    EXPECT_TRUE(sticksIdle({0, 0, 0, 0}, 0));
    EXPECT_FALSE(sticksIdle({0.01, 0, 0, 0}, 0));
}

TEST(GamepadInput, ListenerReportsChanges) {
    Listener listener;
    PadSlots slots;
    EXPECT_TRUE(listener.update(slots).empty());

    slots[1] = pad({0.2, 0, 0, 0}, buttons({3}));
    std::vector<Event> events = listener.update(slots);
    ASSERT_EQ(events.size(), 1u);  // the initial values are no change
    EXPECT_EQ(std::get<Connected>(events[0]).index, 1);
    EXPECT_EQ(std::get<Connected>(events[0]).id, kXboxId);
    EXPECT_TRUE(listener.anyConnected());

    EXPECT_TRUE(listener.update(slots).empty());
    slots[1]->axes[0] = 0.2049;  // rounds to the same
    EXPECT_TRUE(listener.update(slots).empty());

    slots[1]->axes[1] = -0.554;
    slots[1]->buttons = buttons({0});
    events = listener.update(slots);
    ASSERT_EQ(events.size(), 3u);
    const auto& axis = std::get<AxisChanged>(events[0]);
    EXPECT_EQ(axis.axis, 1);
    EXPECT_DOUBLE_EQ(axis.value, -0.55);
    EXPECT_DOUBLE_EQ(listener.pad(1)->axes[1], -0.55);
    EXPECT_EQ(std::get<ButtonChanged>(events[1]).button, 0);
    EXPECT_TRUE(std::get<ButtonChanged>(events[1]).pressed);
    EXPECT_EQ(std::get<ButtonChanged>(events[2]).button, 3);
    EXPECT_FALSE(std::get<ButtonChanged>(events[2]).pressed);

    slots[1]->id = "Another pad";
    events = listener.update(slots);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(std::get<Disconnected>(events[0]).id, kXboxId);
    EXPECT_EQ(std::get<Connected>(events[1]).id, "Another pad");

    slots[1].reset();
    events = listener.update(slots);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(std::get<Disconnected>(events[0]).index, 1);
    EXPECT_FALSE(listener.anyConnected());
}

// ---- stick jogging ------------------------------------------------------------------------

TEST(GamepadSticks, TapSectors) {
    const StickOptions stick = defaultProfiles()[1].joystickOptions.stick1;
    using E = std::optional<std::pair<char, int>>;
    EXPECT_EQ(tapAxesAndDirection(stick, false, 0), (std::vector<E>{std::pair{'x', 1}}));
    EXPECT_EQ(tapAxesAndDirection(stick, false, 45), (std::vector<E>{std::pair{'x', 1}, std::pair{'y', 1}}));
    EXPECT_EQ(tapAxesAndDirection(stick, false, 90), (std::vector<E>{std::nullopt, std::pair{'y', 1}}));
    EXPECT_EQ(tapAxesAndDirection(stick, false, 180), (std::vector<E>{std::pair{'x', -1}}));
    EXPECT_EQ(tapAxesAndDirection(stick, false, 225), (std::vector<E>{std::pair{'x', -1}, std::pair{'y', -1}}));
    EXPECT_EQ(tapAxesAndDirection(stick, false, 270), (std::vector<E>{std::nullopt, std::pair{'y', -1}}));
    EXPECT_EQ(tapAxesAndDirection(stick, false, 315), (std::vector<E>{std::pair{'x', 1}, std::pair{'y', -1}}));
    EXPECT_TRUE(tapAxesAndDirection(stick, false, 30).empty());  // upstream's gaps

    StickOptions reversed = stick;
    reversed.horizontal.isReversed = true;
    reversed.horizontal.secondaryAction = "z";
    EXPECT_EQ(tapAxesAndDirection(reversed, false, 0), (std::vector<E>{std::pair{'x', -1}}));
    EXPECT_EQ(tapAxesAndDirection(reversed, true, 180), (std::vector<E>{std::pair{'z', 1}}));
    reversed.vertical.primaryAction.clear();
    EXPECT_EQ(tapAxesAndDirection(reversed, false, 90), (std::vector<E>{std::nullopt, std::nullopt}));
}

struct Jog {
    std::string kind;
    controller::JogAxes axes;
    double feedrate = 0;
    bool operator==(const Jog&) const = default;
};

void PrintTo(const Jog& jog, std::ostream* os) {
    *os << jog.kind << '(';
    for (const auto& [axis, value] : jog.axes) {
        *os << axis << value << ' ';
    }
    *os << "F" << jog.feedrate << ')';
}

class StickJoggerTest : public ::testing::Test {
protected:
    StickJoggerTest() {
        context.connected = true;
        context.canJog = true;
        context.speeds = controller::JogSpeeds{5, 2, 5, 3000};
        StickCallbacks callbacks{
            [this](const controller::JogAxes& a, double f) { jogs.push_back({"step", a, f}); },
            [this](const controller::JogAxes& a, double f) { jogs.push_back({"start", a, f}); },
            [this](const controller::JogAxes& a, double f) { jogs.push_back({"update", a, f}); },
            [this](const controller::JogAxes& a, double f) { jogs.push_back({"feed", a, f}); },
            [this] { jogs.push_back({"stop", {}, 0}); },
        };
        jogger = std::make_unique<StickJogger>(
            loop, std::move(callbacks), [this] { return context; },
            [this](int index) { return index == 0 && connected ? std::optional<PadState>(state) : std::nullopt; });
    }

    // Moves the stick(s) and reports the axes that changed, as the listener does.
    void move(std::vector<double> axes) {
        const std::vector<double> before = state.axes;
        state.axes = std::move(axes);
        for (int i = 0; i < 4; ++i) {
            if (state.axes[static_cast<std::size_t>(i)] != before[static_cast<std::size_t>(i)]) {
                jogger->onAxis(0, i, profile);
            }
        }
    }

    runtime::ManualEventLoop loop;
    StickContext context;
    Profile profile = defaultProfiles()[1];
    PadState state = pad();
    bool connected = true;
    std::vector<Jog> jogs;
    std::unique_ptr<StickJogger> jogger;
};

TEST_F(StickJoggerTest, AShortPushStepsOnce) {
    move({1, 0, 0, 0});
    loop.advance(50);  // the throttle
    EXPECT_TRUE(jogger->isRunning());
    loop.advance(100);
    move({0, 0, 0, 0});
    loop.advance(50);
    EXPECT_EQ(jogs, (std::vector<Jog>{{"step", {{'X', 5}}, 3000}}));
    EXPECT_FALSE(jogger->isRunning());
    loop.advance(2000);
    EXPECT_EQ(jogs.size(), 1u);
}

TEST_F(StickJoggerTest, AShortDiagonalPushStepsBothAxes) {
    move({0.7, -0.7, 0, 0});  // up and right
    loop.advance(50);
    move({0, 0, 0, 0});
    loop.advance(50);
    EXPECT_EQ(jogs, (std::vector<Jog>{{"step", {{'X', 5}, {'Y', 5}}, 3000}}));
}

TEST_F(StickJoggerTest, HoldingStreamsAtTheStickSpeed) {
    move({1, 0, 0, 0});
    loop.advance(50);
    loop.advance(599);
    EXPECT_TRUE(jogs.empty());
    loop.advance(1);  // held 600 ms: smoothed 0.35 of full deflection
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0], (Jog{"start", {{'X', 1}}, 1050}));
    loop.advance(50);  // 0.35 + 0.35 * 0.65 = 0.5775 (x 3000 a hair under 1732.5)
    ASSERT_EQ(jogs.size(), 2u);
    EXPECT_EQ(jogs[1], (Jog{"update", {{'X', 1}}, 1732}));
    EXPECT_TRUE(jogger->isStreaming());

    move({0, 0, 0, 0});  // let go: the next update sees the stick idle
    loop.advance(50);
    EXPECT_EQ(jogs.back(), (Jog{"stop", {}, 0}));
    const std::size_t count = jogs.size();
    loop.advance(1000);
    EXPECT_EQ(jogs.size(), count);
    EXPECT_FALSE(jogger->isRunning());
}

TEST_F(StickJoggerTest, AMostlyStraightStickJogsOneAxis) {
    move({1, -0.3, 0, 0});  // 17 degrees: the XY sector, but X dominates
    loop.advance(650);
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0].kind, "start");
    EXPECT_EQ(jogs[0].axes, (controller::JogAxes{{'X', 1}}));
}

TEST_F(StickJoggerTest, FixedSpeedModeJogsAtTheFullSpeed) {
    profile.joystickOptions.fixedSpeedMode = true;
    profile.joystickOptions.movementDistanceOverride = 50;
    move({0.5, 0, 0, 0});
    loop.advance(650);
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0], (Jog{"start", {{'X', 1}}, 1500}));
}

TEST_F(StickJoggerTest, TheRightStickJogsZAndA) {
    move({0, 0, 0, -1});  // right stick up: Z+
    loop.advance(650);
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0], (Jog{"start", {{'Z', 1}}, 3000}));
    // The left stick is ignored while the right one jogs.
    move({1, 0, 0, -1});
    loop.advance(50);
    EXPECT_EQ(jogs.back().axes, (controller::JogAxes{{'Z', 1}}));
}

TEST_F(StickJoggerTest, TheRotaryStepsYInRotaryMode) {
    context.rotaryMode = true;
    move({0, 0, -1, 0});  // right stick left: A-
    loop.advance(50);
    move({0, 0, 0, 0});
    loop.advance(50);
    EXPECT_EQ(jogs, (std::vector<Jog>{{"step", {{'Y', -5}}, 3000}}));
}

TEST_F(StickJoggerTest, TheModifierSwitchesToTheSecondActions) {
    profile.modifier = 5;
    profile.joystickOptions.stick1.horizontal.secondaryAction = "z";
    state.buttons = buttons({5});
    move({1, 0, 0, 0});
    loop.advance(50);
    move({0, 0, 0, 0});
    loop.advance(50);
    EXPECT_EQ(jogs, (std::vector<Jog>{{"step", {{'Z', 2}}, 3000}}));
}

TEST_F(StickJoggerTest, TheLockoutButtonMustBeHeld) {
    profile.lockout = 4;
    move({1, 0, 0, 0});
    loop.advance(1000);
    EXPECT_TRUE(jogs.empty());
    EXPECT_FALSE(jogger->isRunning());

    state.buttons = buttons({4});
    move({0.99, 0, 0, 0});
    loop.advance(650);
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0].kind, "start");
    state.buttons = buttons({});  // let go of the lockout mid-jog
    loop.advance(50);
    EXPECT_EQ(jogs.back().kind, "stop");
    EXPECT_FALSE(jogger->isRunning());
}

TEST_F(StickJoggerTest, NothingWhileDisconnected) {
    context.connected = false;
    move({1, 0, 0, 0});
    loop.advance(1000);
    EXPECT_TRUE(jogs.empty());
}

TEST_F(StickJoggerTest, CancelStopsWithoutAStep) {
    move({1, 0, 0, 0});
    loop.advance(100);
    jogger->cancel();  // a short push, but the pad went
    loop.advance(1000);
    EXPECT_TRUE(jogs.empty());

    move({0, 0, 0, 0});
    loop.advance(50);
    move({1, 0, 0, 0});
    loop.advance(700);
    ASSERT_EQ(jogs.size(), 2u);
    jogger->cancel();
    EXPECT_EQ(jogs.back().kind, "stop");
    loop.advance(1000);
    EXPECT_EQ(jogs.size(), 3u);
}

TEST_F(StickJoggerTest, AnMpgStickIsAHandwheel) {
    profile.joystickOptions.stick1.mpgMode.primaryAction = "z";
    move({1, 0, 0, 0});  // 0 degrees: the reference
    loop.advance(50);
    EXPECT_TRUE(jogs.empty());
    move({0.7, 0.7, 0, 0});  // clockwise to 315: half a pulse
    loop.advance(50);
    EXPECT_TRUE(jogs.empty());
    move({0, 1, 0, 0});  // 270: a quarter turn clockwise
    loop.advance(50);
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0], (Jog{"feed", {{'Z', 2}}, 3000}));
    move({-1, 0, 0, 0});  // on to 180
    loop.advance(50);
    ASSERT_EQ(jogs.size(), 2u);
    EXPECT_EQ(jogs[1], (Jog{"feed", {{'Z', 2}}, 3000}));
    move({0, 1, 0, 0});  // back to 270: counter-clockwise, but only one quarter
    loop.advance(50);
    ASSERT_EQ(jogs.size(), 3u);
    EXPECT_EQ(jogs[2], (Jog{"feed", {{'Z', -2}}, 3000}));
    loop.advance(400);  // idle: the jog ends
    EXPECT_EQ(jogs.back().kind, "stop");
    EXPECT_FALSE(jogger->isRunning());
}

TEST_F(StickJoggerTest, AnMpgOnAOnGrblNeedsRotaryMode) {
    profile.joystickOptions.stick1.mpgMode.primaryAction = "a";
    move({1, 0, 0, 0});
    loop.advance(50);
    move({0, 1, 0, 0});
    loop.advance(50);
    EXPECT_TRUE(jogs.empty());
    context.rotaryMode = true;
    move({1, 0, 0, 0});
    loop.advance(50);
    move({0, 1, 0, 0});
    loop.advance(50);
    ASSERT_EQ(jogs.size(), 1u);
    EXPECT_EQ(jogs[0].axes, (controller::JogAxes{{'Y', 5}}));
}

}  // namespace
