#pragma once

// The byte link to a board over a serial port or TCP, on its own I/O thread.
// Port of src/server/lib/SerialConnection.js (Boost.Asio instead of
// node-serialport / net.Socket).
//
// Threading: every public member is called on one "owner" thread (the UI
// thread in the application). The I/O happens on an internal thread, and the
// callbacks and completion handlers are handed back to the owner thread
// through the Dispatcher (in the app: the event loop's post()). No callback
// runs after the link has been destroyed.

#include "gs/controller/controller.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace gs::transport {

// Runs a function on the owner thread; must be callable from any thread.
using Dispatcher = std::function<void(std::function<void()>)>;
// Completion of an open: empty on success, otherwise the reason.
using OpenDone = std::function<void(const std::string& error)>;

struct SerialOptions {
    std::string path;  // "COM3", "/dev/ttyUSB0"
    unsigned baudRate = 115200;
    bool rtscts = false;  // hardware flow control
};

struct NetworkOptions {
    std::string host;
    std::uint16_t port = 23;
    std::int64_t connectTimeoutMs = 2000;
};

// SerialConnection.js decides "network" for any path shaped like an IPv4
// address - with the regex's unescaped dots, so "192x168x0x1" counts too.
bool looksLikeIpAddress(std::string_view path);

class AsioLink final : public controller::DeviceLink {
public:
    explicit AsioLink(Dispatcher dispatch);
    ~AsioLink() override;
    AsioLink(const AsioLink&) = delete;
    AsioLink& operator=(const AsioLink&) = delete;

    // Bytes read from the board, as they arrive (not split into lines).
    std::function<void(std::string_view bytes)> onData;
    // The link was lost (the peer closed it or an I/O error); not called for close().
    std::function<void(const std::string& reason)> onClosed;

    // 8 data bits, no parity, one stop bit; DTR and RTS asserted (as
    // node-serialport does), RTS/CTS handshaking with `rtscts`.
    void openSerial(const SerialOptions& options, OpenDone done);
    void openNetwork(const NetworkOptions& options, OpenDone done);
    void close();

    bool isOpen() const override { return open_.load(); }
    bool isNetwork() const override { return network_.load(); }
    // Writes and immediate writes share one queue, so the board sees them in
    // call order - as with node-serialport.
    void send(std::string_view bytes, controller::SendKind kind) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> open_{false};
    std::atomic<bool> network_{false};
};

}  // namespace gs::transport
