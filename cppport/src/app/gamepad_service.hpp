#pragma once

// gSender's gamepad support (lib/gamepad's GamepadManager, and the Jogging
// widget's stick handling): polls the pads, runs the actions a profile gives
// their buttons - as the keyboard shortcuts run them - and jogs with the
// sticks. The profiles are the settings' (AppSettings::gamepadProfiles).
//
// Beyond upstream, for safety: jogging stops when the pad that drives it
// goes, when the application loses focus (input only counts while it has
// it, as the keyboard shortcuts), and when the machine disconnects.

#include "gamepad_backend.hpp"

#include "gs/gamepad/input.hpp"
#include "gs/gamepad/stick_jog.hpp"

#include <QObject>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <vector>

class QTimer;

namespace gs::app {

class Jogger;
class Machine;
class ShortcutManager;

class GamepadService final : public QObject {
    Q_OBJECT

public:
    static constexpr int kPollMs = 16;               // requestAnimationFrame's pace
    static constexpr std::int64_t kActionThrottleMs = 100;  // runAction's throttle, per action
    static constexpr std::int64_t kMacroDebounceMs = 500;   // macroCallbackDebounce

    GamepadService(Machine& machine, Jogger& jogger, QObject* parent = nullptr);
    ~GamepadService() override;

    // Starts polling `backend` (null: no gamepads).
    void setBackend(std::unique_ptr<GamepadBackend> backend);
    bool available() const noexcept { return backend_ != nullptr; }
    // What runs the buttons' actions.
    void setShortcutManager(ShortcutManager* shortcuts) { shortcuts_ = shortcuts; }
    // Whether input counts now (the window is the active one). Default: always.
    void setActiveCheck(std::function<bool()> active) { active_ = std::move(active); }
    // The profile editor is open (holdListener): buttons run nothing, sticks
    // jog nothing; the signals still report them.
    void setCapturing(bool capturing);
    bool capturing() const noexcept { return capturing_; }

    // One poll now (the timer's; tests call it).
    void poll();

    const gamepad::Listener& listener() const noexcept { return listener_; }
    bool anyConnected() const { return listener_.anyConnected(); }
    // The connected pad whose id is `ids`' (a profile's), if any.
    std::optional<gamepad::PadState> padFor(const std::vector<std::string>& ids) const;
    const gamepad::StickJogger& sticks() const noexcept { return *sticks_; }

Q_SIGNALS:
    void padsChanged();  // a pad connected or disconnected
    void buttonChanged(int index, int button, bool pressed);
    void axisChanged(int index, int axis, double value);
    void notice(const QString& text);  // upstream's toasts

private:
    void handle(const gamepad::Event& event);
    void onButton(const gamepad::ButtonChanged& change);
    void runAction(const std::string& action);
    void stopJogging();
    const gamepad::Profile* profileOf(int index) const;

    Machine& machine_;
    Jogger& jogger_;
    std::unique_ptr<GamepadBackend> backend_;
    QTimer* timer_;
    runtime::TimerScope timers_;
    ShortcutManager* shortcuts_ = nullptr;
    std::function<bool()> active_;
    bool wasActive_ = true;
    bool capturing_ = false;
    gamepad::Listener listener_;
    std::unique_ptr<gamepad::StickJogger> sticks_;
    int stickPad_ = -1;       // the pad whose stick last moved
    std::map<std::string, std::int64_t> lastRun_;  // action -> when it last ran
    runtime::TimerId macroTimer_ = 0;
    std::string pendingMacro_;
    bool buttonJogging_ = false;  // a button's jog runs until its release
};

}  // namespace gs::app
