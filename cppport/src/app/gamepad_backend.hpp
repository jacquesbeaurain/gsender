#pragma once

// Where gamepad states come from. gSender polled the browser's
// navigator.getGamepads(); the port polls SDL3's gamepad API, which maps
// pads to the same standard layout. Tests use FakeGamepadBackend.

#include "gs/gamepad/input.hpp"

#include <QString>

#include <memory>

namespace gs::app {

class GamepadBackend {
public:
    virtual ~GamepadBackend() = default;
    // The pads now, in up to four slots (a pad keeps its slot while connected).
    virtual gamepad::PadSlots poll() = 0;
};

// A backend whose pads a test sets.
class FakeGamepadBackend final : public GamepadBackend {
public:
    gamepad::PadSlots pads;
    gamepad::PadSlots poll() override { return pads; }
};

// SDL3's gamepads; null when this build has no SDL3 or SDL cannot start
// (`error` says why).
std::unique_ptr<GamepadBackend> createSdlGamepadBackend(QString* error = nullptr);

// The id a browser gives a standard pad: "<name> (STANDARD GAMEPAD Vendor:
// 045e Product: 028e)" - Chrome's form, so upstream's profiles match.
QString browserStyleId(const QString& name, unsigned vendor, unsigned product);

}  // namespace gs::app
