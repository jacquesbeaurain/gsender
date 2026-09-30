#include "gamepad_service.hpp"

#include "jogger.hpp"
#include "machine.hpp"
#include "shortcuts.hpp"

#include "gs/controller/controller.hpp"
#include "gs/gamepad/profile.hpp"

#include <QTimer>

namespace gs::app {

namespace {

const std::string kStopJog = "STOP_CONT_JOG";

}  // namespace

GamepadService::GamepadService(Machine& machine, Jogger& jogger, QObject* parent)
    : QObject(parent), machine_(machine), jogger_(jogger), timer_(new QTimer(this)), timers_(machine.eventLoop()) {
    timer_->setInterval(kPollMs);
    connect(timer_, &QTimer::timeout, this, &GamepadService::poll);

    // In rotary mode the rotary is Y: the stream follows the step and the
    // handwheel there (upstream streamed an A the board may not have).
    const auto rotary = [this](const controller::JogAxes& axes) {
        controller::JogAxes out = axes;
        if (machine_.rotaryMode()) {
            for (auto& [axis, value] : out) {
                if (axis == 'A') {
                    axis = 'Y';
                }
            }
        }
        return out;
    };
    gamepad::StickCallbacks callbacks{
        [this](const controller::JogAxes& distances, double feedrate) { jogger_.stepBy(distances, feedrate); },
        [this, rotary](const controller::JogAxes& directions, double feedrate) {
            jogger_.startStream(rotary(directions), feedrate);
        },
        [this, rotary](const controller::JogAxes& directions, double feedrate) {
            jogger_.updateStream(rotary(directions), feedrate);
        },
        [this](const controller::JogAxes& distances, double feedrate) { jogger_.feedStream(distances, feedrate); },
        [this] { jogger_.stopStream(); },
    };
    const auto context = [this] {
        gamepad::StickContext c;
        c.connected = machine_.isConnected();
        c.canJog = jogger_.canJog();
        c.speeds = jogger_.speeds();
        c.rotaryMode = machine_.rotaryMode();
        const controller::Controller* controller = machine_.controller();
        c.grbl = !controller || controller->isGrbl();
        return c;
    };
    sticks_ = std::make_unique<gamepad::StickJogger>(machine_.eventLoop(), std::move(callbacks), context,
                                                     [this](int index) { return listener_.pad(index); });

    connect(&machine_, &Machine::connectionChanged, this, [this] {
        if (!machine_.controller()) {
            sticks_->cancel();
            buttonJogging_ = false;
        }
    });
}

GamepadService::~GamepadService() {
    sticks_->cancel();
}

void GamepadService::setBackend(std::unique_ptr<GamepadBackend> backend) {
    backend_ = std::move(backend);
    if (backend_) {
        timer_->start();
    } else {
        timer_->stop();
    }
}

void GamepadService::setCapturing(bool capturing) {
    capturing_ = capturing;
    if (capturing) {
        stopJogging();
    }
}

std::optional<gamepad::PadState> GamepadService::padFor(const std::vector<std::string>& ids) const {
    gamepad::Profile profile;
    profile.ids = ids;
    const std::vector<gamepad::Profile> one{profile};
    for (int index = 0; index < gamepad::kSlots; ++index) {
        const std::optional<gamepad::PadState>& pad = listener_.pad(index);
        if (pad && gamepad::findProfile(one, pad->id)) {
            return pad;
        }
    }
    return std::nullopt;
}

const gamepad::Profile* GamepadService::profileOf(int index) const {
    const std::optional<gamepad::PadState>& pad = listener_.pad(index);
    return pad ? gamepad::findProfile(machine_.settings().gamepadProfiles, pad->id) : nullptr;
}

void GamepadService::poll() {
    if (!backend_) {
        return;
    }
    const bool active = !active_ || active_();
    if (!active && wasActive_) {
        stopJogging();  // never leave a jog running behind another window
    }
    wasActive_ = active;
    for (const gamepad::Event& event : listener_.update(backend_->poll())) {
        handle(event);
    }
}

void GamepadService::handle(const gamepad::Event& event) {
    const bool acting = wasActive_ && !capturing_;
    if (const auto* connected = std::get_if<gamepad::Connected>(&event)) {
        const gamepad::Profile* profile = profileOf(connected->index);
        Q_EMIT notice(profile ? tr("%1 Connected").arg(QString::fromStdString(profile->name))
                              : tr("New gamepad connected, add it as a profile in your preferences"));
        Q_EMIT padsChanged();
    } else if (const auto* gone = std::get_if<gamepad::Disconnected>(&event)) {
        if (gone->index == stickPad_) {
            sticks_->cancel();
            stickPad_ = -1;
        }
        if (buttonJogging_) {
            runAction(kStopJog);
        }
        Q_EMIT notice(tr("Gamepad disconnected"));
        Q_EMIT padsChanged();
    } else if (const auto* button = std::get_if<gamepad::ButtonChanged>(&event)) {
        Q_EMIT buttonChanged(button->index, button->button, button->pressed);
        if (acting) {
            onButton(*button);
        }
    } else if (const auto* axis = std::get_if<gamepad::AxisChanged>(&event)) {
        Q_EMIT axisChanged(axis->index, axis->axis, axis->value);
        const gamepad::Profile* profile = profileOf(axis->index);
        if (acting && profile) {
            stickPad_ = axis->index;
            sticks_->onAxis(axis->index, axis->axis, *profile);
        }
    }
}

void GamepadService::onButton(const gamepad::ButtonChanged& change) {
    const gamepad::Profile* profile = profileOf(change.index);
    const std::optional<gamepad::PadState>& pad = listener_.pad(change.index);
    if (!profile || !pad) {
        return;
    }
    if (const std::optional<std::string> action =
            gamepad::buttonAction(*profile, change.button, change.pressed, pad->buttons)) {
        runAction(*action);
    }
}

void GamepadService::runAction(const std::string& action) {
    if (!shortcuts_) {
        return;
    }
    const QString id = QString::fromStdString(action);
    if (action == kStopJog) {
        // Never throttled: a stop must not be lost to a press just before it.
        buttonJogging_ = false;
        shortcuts_->run(id);
        return;
    }
    if (findShortcutAction(id)) {
        // throttle(100, {trailing: false}), one per action.
        const std::int64_t now = timers_.nowMs();
        if (const auto last = lastRun_.find(action); last != lastRun_.end() && now - last->second < kActionThrottleMs) {
            return;
        }
        lastRun_[action] = now;
        if (action.find("JOG") != std::string::npos) {
            buttonJogging_ = true;
        }
        shortcuts_->run(id);
        return;
    }
    // Anything else is a macro: debounced, the last one pressed runs.
    pendingMacro_ = action;
    timers_.clear(macroTimer_);
    macroTimer_ = timers_.timeout(kMacroDebounceMs, [this] {
        macroTimer_ = 0;
        if (shortcuts_) {
            shortcuts_->run(QString::fromStdString(pendingMacro_));
        }
    });
}

void GamepadService::stopJogging() {
    sticks_->cancel();
    if (buttonJogging_) {
        runAction(kStopJog);
    }
}

}  // namespace gs::app
