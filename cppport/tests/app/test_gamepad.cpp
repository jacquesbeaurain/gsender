// The gamepad service: a pad's buttons running their actions through the
// shortcut manager, the page that captures them, the stops that keep a jog
// from running on, the sticks jogging the simulated machine, and the
// profiles kept in the settings and read from a gSender export.

#include "app_settings.hpp"
#include "gamepad_backend.hpp"
#include "gamepad_service.hpp"
#include "jogger.hpp"
#include "machine.hpp"
#include "qt_event_loop.hpp"
#include "shortcuts.hpp"

#include "gs/controller/controller.hpp"
#include "gs/gamepad/profile.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include "app_test_support.hpp"

#include <boost/json.hpp>

#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <cmath>
#include <map>
#include <memory>
#include <string>

using namespace gs;
using namespace gs::app;
using namespace gs::app::test_support;

namespace {

// Runs Qt events for `ms`.
void pause(int ms) {
    (void)waitFor([] { return false; }, ms);
}

const std::string kPadId = "Test Pad (STANDARD GAMEPAD Vendor: 1234 Product: 5678)";

gamepad::PadState testPad() {
    gamepad::PadState pad;
    pad.id = kPadId;
    pad.standard = true;
    pad.buttons.assign(17, false);
    pad.axes.assign(4, 0.0);
    return pad;
}

// A Machine with a profile for the test pad (A: Jog X+, B: Start Job, X:
// a macro, Y: the 2nd-action button, A's 2nd action Jog X-), shortcut
// handlers that count, and the service over a fake backend.
class GamepadServiceTest : public ::testing::Test {
protected:
    void SetUp() override {
        application();
        machine_ = std::make_unique<Machine>(loop_, (dir_.path() + "/rc").toStdWString());
        AppSettings settings = machine_->settings();
        gamepad::Profile profile;
        profile.ids = {kPadId};
        profile.name = "Test Pad";
        profile.mapping = "standard";
        profile.buttons = gamepad::defaultButtons(17, true);
        profile.buttons[0].primaryAction = "JOG_X_P";
        profile.buttons[0].secondaryAction = "JOG_X_M";
        profile.buttons[1].primaryAction = "START_JOB";
        profile.buttons[2].primaryAction = "a-macro-id";
        profile.modifier = 3;
        settings.gamepadProfiles.push_back(profile);
        machine_->setSettings(settings);

        jogger_ = std::make_unique<Jogger>(*machine_);
        ShortcutScope scope;
        scope.active = [] { return true; };
        shortcuts_ = std::make_unique<ShortcutManager>(*machine_, scope);
        for (const char* id : {"JOG_X_P", "JOG_X_M", "START_JOB", "STOP_CONT_JOG"}) {
            shortcuts_->setHandler(id, [this, id] { ++runs_[id]; });
        }
        // Stands in for a macro (which the manager runs by its id).
        shortcuts_->setHandler("a-macro-id", [this] { ++runs_["a-macro-id"]; });
        service_ = std::make_unique<GamepadService>(*machine_, *jogger_);
        auto fake = std::make_unique<FakeGamepadBackend>();
        pads_ = fake.get();
        service_->setBackend(std::move(fake));
        service_->setShortcutManager(shortcuts_.get());
    }

    void TearDown() override {
        service_.reset();
        shortcuts_.reset();
        jogger_.reset();
        machine_.reset();
    }

    void connectPad() {
        pads_->pads[0] = testPad();
        service_->poll();
    }
    void press(int button, bool down = true) {
        pads_->pads[0]->buttons[static_cast<std::size_t>(button)] = down;
        service_->poll();
    }
    void release(int button) { press(button, false); }
    int runs(const std::string& id) { return runs_[id]; }

    QTemporaryDir dir_;
    QtEventLoop loop_;
    std::unique_ptr<Machine> machine_;
    std::unique_ptr<Jogger> jogger_;
    std::unique_ptr<ShortcutManager> shortcuts_;
    std::unique_ptr<GamepadService> service_;
    FakeGamepadBackend* pads_ = nullptr;
    std::map<std::string, int> runs_;
};

}  // namespace

TEST_F(GamepadServiceTest, ButtonsRunTheirActionsAndAJogStopsOnRelease) {
    QStringList notices;
    QObject::connect(service_.get(), &GamepadService::notice, [&](const QString& text) { notices.append(text); });
    connectPad();
    ASSERT_EQ(notices.count(), 1);
    EXPECT_EQ(notices.front(), "Test Pad Connected");
    EXPECT_TRUE(service_->anyConnected());
    EXPECT_TRUE(service_->padFor({kPadId}).has_value());

    press(0);
    EXPECT_EQ(runs("JOG_X_P"), 1);
    release(0);
    EXPECT_EQ(runs("STOP_CONT_JOG"), 1);
    // One run per action per 100 ms; the stop is never held back.
    press(0);
    EXPECT_EQ(runs("JOG_X_P"), 1);
    release(0);
    EXPECT_EQ(runs("STOP_CONT_JOG"), 2);

    press(1);
    release(1);
    EXPECT_EQ(runs("START_JOB"), 1);

    // With the 2nd-action button held, A's 2nd action.
    press(3);
    press(0);
    EXPECT_EQ(runs("JOG_X_M"), 1);
    release(0);
    release(3);

    // A macro runs once, after the presses settle.
    press(2);
    release(2);
    press(2);
    release(2);
    EXPECT_EQ(runs("a-macro-id"), 0);
    EXPECT_TRUE(waitFor([&] { return runs("a-macro-id") == 1; }));
    pause(600);
    EXPECT_EQ(runs("a-macro-id"), 1);
}

TEST_F(GamepadServiceTest, AnUnknownPadIsAnnouncedAndDoesNothing) {
    QStringList notices;
    QObject::connect(service_.get(), &GamepadService::notice, [&](const QString& text) { notices.append(text); });
    gamepad::PadState other = testPad();
    other.id = "Other Pad (STANDARD GAMEPAD Vendor: 9999 Product: 0001)";
    pads_->pads[1] = other;
    service_->poll();
    ASSERT_EQ(notices.count(), 1);
    EXPECT_EQ(notices.front(), "New gamepad connected, add it as a profile in your preferences");
    pads_->pads[1]->buttons[0] = true;
    service_->poll();
    EXPECT_EQ(runs("JOG_X_P"), 0);

    pads_->pads[1].reset();
    service_->poll();
    EXPECT_EQ(notices.back(), "Gamepad disconnected");
    EXPECT_FALSE(service_->anyConnected());
}

TEST_F(GamepadServiceTest, ThePageCapturesTheButtons) {
    connectPad();
    int buttons = 0;
    QObject::connect(service_.get(), &GamepadService::buttonChanged, [&] { ++buttons; });
    service_->setCapturing(true);
    press(0);
    EXPECT_EQ(buttons, 1);
    EXPECT_EQ(runs("JOG_X_P"), 0);
    release(0);
    service_->setCapturing(false);
    press(0);
    EXPECT_EQ(runs("JOG_X_P"), 1);
}

TEST_F(GamepadServiceTest, AButtonJogStopsWhenThePadGoesOrTheWindowIsLeft) {
    connectPad();
    press(0);
    ASSERT_EQ(runs("JOG_X_P"), 1);
    pads_->pads[0].reset();
    service_->poll();
    EXPECT_EQ(runs("STOP_CONT_JOG"), 1);

    bool active = true;
    service_->setActiveCheck([&] { return active; });
    connectPad();
    pause(120);  // past the throttle
    press(0);
    ASSERT_EQ(runs("JOG_X_P"), 2);
    active = false;
    service_->poll();
    EXPECT_EQ(runs("STOP_CONT_JOG"), 2);
    // Behind another window the pad does nothing.
    release(0);
    pause(120);
    press(0);
    EXPECT_EQ(runs("JOG_X_P"), 2);
}

TEST_F(GamepadServiceTest, AStickJogsTheMachineWhileHeld) {
    machine_->connectTo(Machine::kSimulatorPort);
    ASSERT_TRUE(waitFor([&] {
        return machine_->isConnected() && machine_->controller()->state().status.activeState == "Idle";
    }));
    machine_->simulator()->setSpeed(5);
    const double startX = machine_->machinePositionMm()[0];
    connectPad();

    // Stick 1 left: X- (upstream's default: stick 1 left/right is X), held
    // past 600 ms it streams: further than a step goes.
    pads_->pads[0]->axes[0] = -1.0;
    const double step = jogger_->speeds().xyStep;
    ASSERT_TRUE(waitFor([&] { return machine_->machinePositionMm()[0] < startX - step - 2; }));

    // Let go: the jog stops.
    pads_->pads[0]->axes[0] = 0.0;
    double last = machine_->machinePositionMm()[0];
    ASSERT_TRUE(waitFor([&] {
        pause(300);
        const double now = machine_->machinePositionMm()[0];
        const bool still = std::abs(now - last) < 1e-6;
        last = now;
        return still;
    }));
    EXPECT_EQ(machine_->controller()->state().status.activeState, "Idle");
}

TEST_F(GamepadServiceTest, TheSdlBackendStartsWithoutPads) {
    QString error;
    std::unique_ptr<GamepadBackend> sdl = createSdlGamepadBackend(&error);
    if (!sdl) {
        GTEST_SKIP() << error.toStdString();
    }
    for (const auto& pad : sdl->poll()) {
        EXPECT_FALSE(pad.has_value());  // no pads in the test environment
    }
}

TEST(GamepadSettings, ProfilesAreKeptAndReadFromAGSenderExport) {
    application();
    QTemporaryDir dir;
    QtEventLoop loop;
    const std::wstring file = (dir.path() + "/rc").toStdWString();
    {
        Machine machine(loop, file);
        EXPECT_EQ(machine.settings().gamepadProfiles.size(), 2U);  // gSender's own
        AppSettings settings = machine.settings();
        settings.gamepadProfiles.resize(1);
        settings.gamepadProfiles[0].name = "Renamed";
        settings.gamepadProfiles[0].lockout = 4;
        settings.gamepadProfiles[0].joystickOptions.zeroThreshold = 20;
        machine.setSettings(settings);
    }
    Machine machine(loop, file);
    ASSERT_EQ(machine.settings().gamepadProfiles.size(), 1U);
    EXPECT_EQ(machine.settings().gamepadProfiles[0].name, "Renamed");
    EXPECT_EQ(machine.settings().gamepadProfiles[0].lockout, std::optional<int>(4));
    EXPECT_EQ(machine.settings().gamepadProfiles[0].joystickOptions.zeroThreshold, 20);

    const std::optional<GSenderSettings> read = readGSenderSettings(boost::json::parse(R"j({
        "settings": {"workspace": {"gamepad": {"profiles": [{
            "id": ["Pad (STANDARD GAMEPAD Vendor: 0001 Product: 0002)"],
            "name": "From gSender",
            "mapping": "standard",
            "buttons": [{"label": "A", "value": 0, "primaryAction": "JOG_X_P", "secondaryAction": null}],
            "joystickOptions": {"zeroThreshold": 25},
            "lockout": {"button": null, "active": false},
            "modifier": {"button": 0}
        }]}}}
    })j"));
    ASSERT_TRUE(read);
    ASSERT_EQ(read->settings.gamepadProfiles.size(), 1U);
    const gamepad::Profile& p = read->settings.gamepadProfiles[0];
    EXPECT_EQ(p.name, "From gSender");
    EXPECT_EQ(p.buttons.at(0).primaryAction, "JOG_X_P");
    EXPECT_EQ(p.modifier, std::optional<int>(0));
    EXPECT_FALSE(p.lockout);
    EXPECT_EQ(p.joystickOptions.zeroThreshold, 25);
}
