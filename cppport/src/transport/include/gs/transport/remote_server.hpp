#pragma once

// The wireless pendant's web server (upstream: remote mode restarted the
// app's Express/socket.io server on the chosen address and port). Boost.Beast
// on its own I/O thread: plain HTTP GETs for a fixed set of pages, and
// WebSocket clients on /ws that exchange text messages.
//
// Threading: as AsioLink - every public member is called on the owner thread,
// the callbacks come back to it through the Dispatcher, and none runs after
// stop() or the server's destruction.

#include "gs/transport/asio_link.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace gs::transport {

struct HttpResource {
    std::string contentType;  // "text/html; charset=utf-8"
    std::string body;
};

class RemoteServer {
public:
    using ClientId = std::uint64_t;

    // The WebSocket endpoint's path.
    static constexpr const char* kSocketPath = "/ws";

    explicit RemoteServer(Dispatcher dispatch);
    ~RemoteServer();
    RemoteServer(const RemoteServer&) = delete;
    RemoteServer& operator=(const RemoteServer&) = delete;

    std::function<void(ClientId id)> onClientConnected;
    std::function<void(ClientId id)> onClientDisconnected;
    std::function<void(ClientId id, const std::string& text)> onMessage;

    // What a GET of `path` returns (the query string is ignored). May be
    // changed while running.
    void setResource(const std::string& path, HttpResource resource);

    // Binds `host`:`port` (port 0: any free one) and starts serving. Empty
    // on success, else why not ("address not available", "address already in
    // use", ...). A running server is stopped first.
    std::string start(const std::string& host, std::uint16_t port);
    // Closes the listener and every client; waits for the I/O thread.
    void stop();
    bool running() const noexcept { return running_; }
    std::uint16_t port() const noexcept { return port_; }  // the bound port

    // Text messages to one WebSocket client, or all of them. A client that
    // falls far behind (a phone gone to sleep) is dropped rather than
    // queueing without end.
    void send(ClientId id, std::string text);
    void broadcast(std::string text);
    std::size_t clientCount() const noexcept { return clients_.load(); }

private:
    struct Impl;
    Dispatcher dispatch_;
    std::atomic<std::size_t> clients_{0};
    std::shared_ptr<Impl> impl_;  // shared with the sessions on the I/O thread
    bool running_ = false;
    std::uint16_t port_ = 0;
};

}  // namespace gs::transport
