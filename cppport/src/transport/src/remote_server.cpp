#include "gs/transport/remote_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>

#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

namespace gs::transport {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using asio::ip::tcp;
using boost::system::error_code;

// Messages queued for one client before it counts as gone.
constexpr std::size_t kMaxQueued = 256;

}  // namespace

// Everything below runs on the I/O thread, apart from the Impl members the
// owner thread calls (which post to it) and `resources` (under its mutex).
struct RemoteServer::Impl : std::enable_shared_from_this<RemoteServer::Impl> {
    class Socket;
    class HttpSession;

    Impl(Dispatcher d, std::atomic<std::size_t>& count) : dispatch(std::move(d)), clientCount(count) {}

    asio::io_context io;
    tcp::acceptor acceptor{io};
    std::thread thread;
    Dispatcher dispatch;
    std::atomic<std::size_t>& clientCount;
    // Cleared by stop(): dispatched callbacks check it on the owner thread.
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);

    std::mutex resourcesMutex;
    std::map<std::string, HttpResource> resources;

    ClientId nextId = 1;
    std::map<ClientId, std::weak_ptr<Socket>> sockets;
    std::map<ClientId, std::weak_ptr<HttpSession>> sessions;  // plain HTTP, to close on stop
    ClientId nextSession = 1;

    std::function<void(ClientId)> connected;
    std::function<void(ClientId)> disconnected;
    std::function<void(ClientId, const std::string&)> message;

    // Hands `fn` to the owner thread, unless the server stops first.
    void toOwner(std::function<void()> fn) {
        dispatch([alive = alive, fn = std::move(fn)] {
            if (alive->load()) {
                fn();
            }
        });
    }

    void accept();
    void closeAll();
    void enqueue(ClientId id, std::shared_ptr<const std::string> text);
};

class RemoteServer::Impl::Socket : public std::enable_shared_from_this<Socket> {
public:
    Socket(std::shared_ptr<Impl> server, tcp::socket socket, ClientId id)
        : server_(std::move(server)), ws_(std::move(socket)), id_(id) {}

    ClientId id() const noexcept { return id_; }

    void run(http::request<http::string_body> request) {
        ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
        // Known from the start, so stop() can close a handshake in progress.
        server_->sockets[id_] = weak_from_this();
        ws_.async_accept(request, [self = shared_from_this()](const error_code& ec) {
            if (ec) {
                self->server_->sockets.erase(self->id_);
                return;
            }
            self->open_ = true;
            ++self->server_->clientCount;
            self->server_->toOwner([server = self->server_, id = self->id_] {
                if (server->connected) {
                    server->connected(id);
                }
            });
            self->read();
        });
    }

    void send(std::shared_ptr<const std::string> text) {
        if (!open_) {
            return;
        }
        if (queue_.size() >= kMaxQueued) {
            close();
            return;
        }
        queue_.push_back(std::move(text));
        if (queue_.size() == 1) {
            write();
        }
    }

    // A client too far behind: the closing handshake, then it is gone.
    void close() {
        if (!open_) {
            return;
        }
        ws_.async_close(websocket::close_code::going_away, [self = shared_from_this()](const error_code&) {});
        finished();
    }

    // The server stops: the connection just ends, so nothing is left pending.
    void abort() {
        error_code ignored;
        beast::get_lowest_layer(ws_).socket().shutdown(tcp::socket::shutdown_both, ignored);
        beast::get_lowest_layer(ws_).close();
        finished();
    }

private:
    void read() {
        ws_.async_read(buffer_, [self = shared_from_this()](const error_code& ec, std::size_t) {
            if (ec) {
                self->finished();
                return;
            }
            if (self->ws_.got_text()) {
                std::string text = beast::buffers_to_string(self->buffer_.data());
                self->server_->toOwner([server = self->server_, id = self->id_, text = std::move(text)] {
                    if (server->message) {
                        server->message(id, text);
                    }
                });
            }
            self->buffer_.consume(self->buffer_.size());
            self->read();
        });
    }

    void write() {
        ws_.text(true);
        ws_.async_write(asio::buffer(*queue_.front()), [self = shared_from_this()](const error_code& ec, std::size_t) {
            if (ec) {
                self->finished();
                return;
            }
            self->queue_.pop_front();
            if (!self->queue_.empty()) {
                self->write();
            }
        });
    }

    // Once per client: forgets it and tells the owner.
    void finished() {
        if (!open_) {
            return;
        }
        open_ = false;
        queue_.clear();
        server_->sockets.erase(id_);
        --server_->clientCount;
        server_->toOwner([server = server_, id = id_] {
            if (server->disconnected) {
                server->disconnected(id);
            }
        });
    }

    std::shared_ptr<Impl> server_;
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::deque<std::shared_ptr<const std::string>> queue_;
    ClientId id_;
    bool open_ = false;
};

class RemoteServer::Impl::HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(std::shared_ptr<Impl> server, tcp::socket socket, ClientId id)
        : server_(std::move(server)), stream_(std::move(socket)), id_(id) {}

    void run() {
        server_->sessions[id_] = weak_from_this();
        read();
    }

    void close() {
        error_code ignored;
        stream_.socket().shutdown(tcp::socket::shutdown_both, ignored);
        stream_.close();
    }

private:
    void read() {
        request_ = {};
        stream_.expires_after(std::chrono::seconds(30));
        http::async_read(stream_, buffer_, request_, [self = shared_from_this()](const error_code& ec, std::size_t) {
            if (ec) {
                self->done();
                return;
            }
            self->handle();
        });
    }

    void handle() {
        std::string target(request_.target());
        if (const auto q = target.find_first_of("?#"); q != std::string::npos) {
            target.resize(q);
        }
        if (websocket::is_upgrade(request_)) {
            if (target == kSocketPath) {
                stream_.expires_never();
                const ClientId id = server_->nextId++;
                auto socket = std::make_shared<Socket>(server_, stream_.release_socket(), id);
                server_->sessions.erase(id_);
                socket->run(std::move(request_));
                return;
            }
            respond(http::status::not_found, "text/plain", "Not found");
            return;
        }
        if (request_.method() != http::verb::get) {
            respond(http::status::method_not_allowed, "text/plain", "Method not allowed");
            return;
        }
        HttpResource resource;
        bool found = false;
        {
            std::lock_guard lock(server_->resourcesMutex);
            if (const auto it = server_->resources.find(target); it != server_->resources.end()) {
                resource = it->second;
                found = true;
            }
        }
        if (!found) {
            respond(http::status::not_found, "text/plain", "Not found");
            return;
        }
        respond(http::status::ok, resource.contentType, std::move(resource.body));
    }

    void respond(http::status status, const std::string& contentType, std::string body) {
        auto response = std::make_shared<http::response<http::string_body>>(status, request_.version());
        response->set(http::field::server, "gSender");
        response->set(http::field::content_type, contentType);
        response->set(http::field::cache_control, "no-store");
        response->keep_alive(request_.keep_alive());
        response->body() = std::move(body);
        response->prepare_payload();
        http::async_write(stream_, *response,
                          [self = shared_from_this(), response](const error_code& ec, std::size_t) {
                              if (ec || !response->keep_alive()) {
                                  self->close();
                                  self->done();
                                  return;
                              }
                              self->read();
                          });
    }

    void done() { server_->sessions.erase(id_); }

    std::shared_ptr<Impl> server_;
    beast::tcp_stream stream_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> request_;
    ClientId id_;
};

void RemoteServer::Impl::accept() {
    acceptor.async_accept([self = shared_from_this()](const error_code& ec, tcp::socket socket) {
        if (ec) {
            if (ec == asio::error::operation_aborted || !self->acceptor.is_open()) {
                return;
            }
        } else {
            std::make_shared<HttpSession>(self, std::move(socket), self->nextSession++)->run();
        }
        self->accept();
    });
}

void RemoteServer::Impl::closeAll() {
    error_code ignored;
    acceptor.close(ignored);
    for (auto& [id, weak] : std::map(sockets)) {
        if (auto socket = weak.lock()) {
            socket->abort();
        }
    }
    for (auto& [id, weak] : std::map(sessions)) {
        if (auto session = weak.lock()) {
            session->close();
        }
    }
}

void RemoteServer::Impl::enqueue(ClientId id, std::shared_ptr<const std::string> text) {
    if (id == 0) {
        for (auto& [clientId, weak] : std::map(sockets)) {
            if (auto socket = weak.lock()) {
                socket->send(text);
            }
        }
        return;
    }
    if (const auto it = sockets.find(id); it != sockets.end()) {
        if (auto socket = it->second.lock()) {
            socket->send(std::move(text));
        }
    }
}

RemoteServer::RemoteServer(Dispatcher dispatch)
    : dispatch_(std::move(dispatch)), impl_(std::make_shared<Impl>(dispatch_, clients_)) {}

RemoteServer::~RemoteServer() {
    stop();
}

void RemoteServer::setResource(const std::string& path, HttpResource resource) {
    std::lock_guard lock(impl_->resourcesMutex);
    impl_->resources[path] = std::move(resource);
}

std::string RemoteServer::start(const std::string& host, std::uint16_t port) {
    stop();
    Impl& impl = *impl_;
    impl.connected = [this](ClientId id) {
        if (onClientConnected) {
            onClientConnected(id);
        }
    };
    impl.disconnected = [this](ClientId id) {
        if (onClientDisconnected) {
            onClientDisconnected(id);
        }
    };
    impl.message = [this](ClientId id, const std::string& text) {
        if (onMessage) {
            onMessage(id, text);
        }
    };

    error_code ec;
    const asio::ip::address address = asio::ip::make_address(host, ec);
    if (ec) {
        return "invalid address " + host;
    }
    const tcp::endpoint endpoint(address, port);
    impl.acceptor.open(endpoint.protocol(), ec);
#ifndef _WIN32
    // A restart can rebind at once. (Windows' SO_REUSEADDR would let two
    // servers share the port, so it keeps the default there.)
    if (!ec) {
        impl.acceptor.set_option(asio::socket_base::reuse_address(true), ec);
    }
#endif
    if (!ec) {
        impl.acceptor.bind(endpoint, ec);
    }
    if (!ec) {
        impl.acceptor.listen(asio::socket_base::max_listen_connections, ec);
    }
    if (ec) {
        error_code ignored;
        impl.acceptor.close(ignored);
        return ec.message();
    }
    port_ = impl.acceptor.local_endpoint().port();
    impl.accept();
    impl.thread = std::thread([raw = &impl] { raw->io.run(); });
    running_ = true;
    return {};
}

void RemoteServer::stop() {
    if (!running_) {
        return;
    }
    impl_->alive->store(false);
    // Closing every socket completes every pending operation, so the loop
    // runs out of work and the thread ends.
    asio::post(impl_->io, [impl = impl_.get()] { impl->closeAll(); });
    impl_->thread.join();
    impl_->sockets.clear();
    impl_->sessions.clear();
    // A new Impl for the next start(); the old one goes with its last session.
    auto fresh = std::make_shared<Impl>(dispatch_, clients_);
    {
        std::lock_guard lock(impl_->resourcesMutex);
        fresh->resources = impl_->resources;
    }
    impl_ = std::move(fresh);
    clients_ = 0;
    running_ = false;
    port_ = 0;
}

void RemoteServer::send(ClientId id, std::string text) {
    if (!running_ || id == 0) {
        return;
    }
    asio::post(impl_->io, [impl = impl_.get(), id, text = std::make_shared<const std::string>(std::move(text))] {
        impl->enqueue(id, text);
    });
}

void RemoteServer::broadcast(std::string text) {
    if (!running_) {
        return;
    }
    asio::post(impl_->io, [impl = impl_.get(), text = std::make_shared<const std::string>(std::move(text))] {
        impl->enqueue(0, text);
    });
}

}  // namespace gs::transport
