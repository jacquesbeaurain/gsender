#pragma once

// A machine connection: identifies the firmware on a freshly opened link,
// then runs the Controller for it. Port of src/server/lib/Connection.js
// (line framing and firmware detection) and the "firmwareFound" handling in
// services/cncengine/CNCEngine.js.
//
// The owner opens the transport, then calls opened(); feeds everything the
// transport reads to receive(); and calls closed() when it goes away.

#include "gs/controller/controller.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace gs::controller {

inline constexpr std::int64_t kFirmwareDetectIntervalMs = 800;
inline constexpr int kFirmwareDetectMaxAttempts = 7;

struct SessionOptions {
    // Assumed when the board never identifies itself.
    protocol::Firmware defaultFirmware = protocol::Firmware::Grbl;
};

class Session {
public:
    Session(runtime::EventLoop& loop, DeviceLink& link, SessionOptions options, ControllerHooks hooks,
            ControllerEventSink sink);
    ~Session();
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    // Every complete line read from the link, before any interpretation.
    std::function<void(std::string_view line)> onLine;
    // The firmware is known and its controller exists; `detected` is false
    // when the default was assumed.
    std::function<void(Controller& controller, bool detected)> onController;

    void opened();
    void receive(std::string_view bytes);
    void closed();

    Controller* controller() noexcept { return controller_.get(); }
    std::optional<protocol::Firmware> firmware() const noexcept { return firmware_; }
    bool isDetecting() const noexcept { return detectTimer_ != 0; }
    int detectionAttempts() const noexcept { return attempts_; }

private:
    void handleLine(std::string_view line);
    void startDetection();
    void stopDetection();
    void sendQuery();
    void firmwareFound(protocol::Firmware firmware, bool detected);

    runtime::EventLoop& loop_;
    runtime::TimerScope timers_;
    DeviceLink& link_;
    SessionOptions options_;
    ControllerHooks hooks_;
    ControllerEventSink sink_;

    std::string partial_;  // bytes after the last newline
    std::optional<protocol::Firmware> firmware_;
    runtime::TimerId detectTimer_ = 0;
    int attempts_ = 0;
    std::unique_ptr<Controller> controller_;
};

}  // namespace gs::controller
