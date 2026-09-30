#include "gamepad_backend.hpp"

#ifdef GS_HAVE_SDL3
#include <SDL3/SDL.h>
#endif

#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <string>

namespace gs::app {

QString browserStyleId(const QString& name, unsigned vendor, unsigned product) {
    return QStringLiteral("%1 (STANDARD GAMEPAD Vendor: %2 Product: %3)")
        .arg(name)
        .arg(vendor, 4, 16, QLatin1Char('0'))
        .arg(product, 4, 16, QLatin1Char('0'));
}

#ifdef GS_HAVE_SDL3

namespace {

// The standard layout's buttons 0-16 as SDL names them (6 and 7, the
// triggers, are axes in SDL).
constexpr std::array<int, 17> kStandardButtons{
    SDL_GAMEPAD_BUTTON_SOUTH,          SDL_GAMEPAD_BUTTON_EAST,         SDL_GAMEPAD_BUTTON_WEST,
    SDL_GAMEPAD_BUTTON_NORTH,          SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER,
    -1,                                -1,                               SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_START,          SDL_GAMEPAD_BUTTON_LEFT_STICK,    SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_DPAD_UP,        SDL_GAMEPAD_BUTTON_DPAD_DOWN,     SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,     SDL_GAMEPAD_BUTTON_GUIDE,
};

// Chromium's threshold for a trigger to count as pressed (30/255).
constexpr double kTriggerPressed = 30.0 / 255.0;

double axis(SDL_Gamepad* pad, SDL_GamepadAxis which) {
    const double value = SDL_GetGamepadAxis(pad, which) / 32767.0;
    return std::clamp(value, -1.0, 1.0);
}

class SdlGamepadBackend final : public GamepadBackend {
public:
    SdlGamepadBackend() = default;
    ~SdlGamepadBackend() override {
        for (const auto& [id, pad] : open_) {
            SDL_CloseGamepad(pad.handle);
        }
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
    }

    gamepad::PadSlots poll() override {
        SDL_UpdateGamepads();
        int count = 0;
        SDL_JoystickID* ids = SDL_GetGamepads(&count);
        std::map<SDL_JoystickID, bool> present;
        for (int i = 0; i < count; ++i) {
            present[ids[i]] = true;
        }
        SDL_free(ids);

        // Pads that went free their slots.
        for (auto it = open_.begin(); it != open_.end();) {
            if (!present.contains(it->first) || !SDL_GamepadConnected(it->second.handle)) {
                SDL_CloseGamepad(it->second.handle);
                it = open_.erase(it);
            } else {
                ++it;
            }
        }
        // New pads take the first free slot (four, as a browser has).
        for (const auto& [id, _] : present) {
            if (open_.contains(id) || open_.size() >= static_cast<std::size_t>(gamepad::kSlots)) {
                continue;
            }
            SDL_Gamepad* handle = SDL_OpenGamepad(id);
            if (!handle) {
                continue;
            }
            int slot = 0;
            while (std::any_of(open_.begin(), open_.end(), [slot](const auto& p) { return p.second.slot == slot; })) {
                ++slot;
            }
            const char* name = SDL_GetGamepadName(handle);
            const QString padId = browserStyleId(QString::fromUtf8(name ? name : "Gamepad"), SDL_GetGamepadVendor(handle),
                                                 SDL_GetGamepadProduct(handle));
            open_[id] = Open{handle, slot, padId.toStdString()};
        }

        gamepad::PadSlots slots;
        for (const auto& [id, pad] : open_) {
            gamepad::PadState state;
            state.id = pad.id;
            state.buttons.resize(kStandardButtons.size());
            for (std::size_t b = 0; b < kStandardButtons.size(); ++b) {
                if (kStandardButtons[b] >= 0) {
                    state.buttons[b] =
                        SDL_GetGamepadButton(pad.handle, static_cast<SDL_GamepadButton>(kStandardButtons[b]));
                }
            }
            state.buttons[6] = axis(pad.handle, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTriggerPressed;
            state.buttons[7] = axis(pad.handle, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTriggerPressed;
            state.axes = {axis(pad.handle, SDL_GAMEPAD_AXIS_LEFTX), axis(pad.handle, SDL_GAMEPAD_AXIS_LEFTY),
                          axis(pad.handle, SDL_GAMEPAD_AXIS_RIGHTX), axis(pad.handle, SDL_GAMEPAD_AXIS_RIGHTY)};
            slots[static_cast<std::size_t>(pad.slot)] = std::move(state);
        }
        return slots;
    }

private:
    struct Open {
        SDL_Gamepad* handle;
        int slot;
        std::string id;
    };
    std::map<SDL_JoystickID, Open> open_;
};

}  // namespace

std::unique_ptr<GamepadBackend> createSdlGamepadBackend(QString* error) {
    // No window of SDL's own has the focus: without this SDL would think the
    // application is in the background and drop the input. The application
    // decides when gamepad input counts (GamepadService).
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    // SDL would otherwise catch SIGINT/SIGTERM for an SDL_EVENT_QUIT nobody
    // reads, and the application would no longer stop on Ctrl+C or a kill.
    SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        if (error) {
            *error = QString::fromUtf8(SDL_GetError());
        }
        return nullptr;
    }
    // States are polled (SDL_UpdateGamepads); no events queue up unread.
    SDL_SetGamepadEventsEnabled(false);
    SDL_SetJoystickEventsEnabled(false);
    return std::make_unique<SdlGamepadBackend>();
}

#else

std::unique_ptr<GamepadBackend> createSdlGamepadBackend(QString* error) {
    if (error) {
        *error = QStringLiteral("this build has no SDL3");
    }
    return nullptr;
}

#endif

}  // namespace gs::app
