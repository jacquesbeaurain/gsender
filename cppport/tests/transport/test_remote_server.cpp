// The pendant's web server over loopback: pages, WebSocket messages both
// ways, clients coming and going, stop and restart.

#include "gs/transport/network_list.hpp"
#include "gs/transport/remote_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

using namespace gs::transport;
using namespace std::chrono_literals;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
using asio::ip::tcp;

namespace {

// The owner thread: callbacks queue here and the test runs them.
class Pump {
public:
    Dispatcher dispatcher() {
        return [this](std::function<void()> fn) {
            {
                std::lock_guard lock(mutex_);
                queue_.push_back(std::move(fn));
            }
            ready_.notify_one();
        };
    }

    bool runUntil(const std::function<bool()>& done, std::chrono::milliseconds timeout = 5s) {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (!done()) {
            std::function<void()> fn;
            {
                std::unique_lock lock(mutex_);
                if (!ready_.wait_until(lock, deadline, [this] { return !queue_.empty(); })) {
                    return done();
                }
                fn = std::move(queue_.front());
                queue_.pop_front();
            }
            fn();
        }
        return true;
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
};

http::response<http::string_body> get(std::uint16_t port, const std::string& target) {
    asio::io_context io;
    beast::tcp_stream stream(io);
    stream.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
    http::request<http::string_body> request(http::verb::get, target, 11);
    request.set(http::field::host, "127.0.0.1");
    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    boost::system::error_code ignored;
    stream.socket().shutdown(tcp::socket::shutdown_both, ignored);
    return response;
}

// A blocking WebSocket client.
class Client {
public:
    explicit Client(std::uint16_t port) : ws_(io_) {
        beast::get_lowest_layer(ws_).connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
        ws_.handshake("127.0.0.1", RemoteServer::kSocketPath);
    }

    void send(const std::string& text) { ws_.write(asio::buffer(text)); }

    std::string receive() {
        beast::flat_buffer buffer;
        beast::get_lowest_layer(ws_).expires_after(5s);
        ws_.read(buffer);
        return beast::buffers_to_string(buffer.data());
    }

    void close() { ws_.close(websocket::close_code::normal); }

private:
    asio::io_context io_;
    websocket::stream<beast::tcp_stream> ws_;
};

}  // namespace

TEST(RemoteServer, ServesPagesAndNotFound) {
    Pump pump;
    RemoteServer server(pump.dispatcher());
    server.setResource("/", {"text/html; charset=utf-8", "<html>pendant</html>"});
    ASSERT_EQ(server.start("127.0.0.1", 0), "");
    ASSERT_TRUE(server.running());
    ASSERT_NE(server.port(), 0);

    const auto page = get(server.port(), "/?t=1");
    EXPECT_EQ(page.result(), http::status::ok);
    EXPECT_EQ(page.body(), "<html>pendant</html>");
    EXPECT_EQ(page[http::field::content_type], "text/html; charset=utf-8");
    EXPECT_EQ(page[http::field::cache_control], "no-store");

    EXPECT_EQ(get(server.port(), "/missing").result(), http::status::not_found);

    // Changed while running.
    server.setResource("/", {"text/plain", "v2"});
    EXPECT_EQ(get(server.port(), "/").body(), "v2");
}

TEST(RemoteServer, ExchangesWebSocketMessages) {
    Pump pump;
    RemoteServer server(pump.dispatcher());
    std::vector<RemoteServer::ClientId> connected;
    std::vector<RemoteServer::ClientId> disconnected;
    std::vector<std::string> messages;
    server.onClientConnected = [&](RemoteServer::ClientId id) {
        connected.push_back(id);
        server.send(id, "hello " + std::to_string(id));
    };
    server.onClientDisconnected = [&](RemoteServer::ClientId id) { disconnected.push_back(id); };
    server.onMessage = [&](RemoteServer::ClientId, const std::string& text) { messages.push_back(text); };
    ASSERT_EQ(server.start("127.0.0.1", 0), "");

    Client a(server.port());
    ASSERT_TRUE(pump.runUntil([&] { return connected.size() == 1; }));
    EXPECT_EQ(a.receive(), "hello " + std::to_string(connected[0]));
    Client b(server.port());
    ASSERT_TRUE(pump.runUntil([&] { return connected.size() == 2; }));
    EXPECT_EQ(b.receive(), "hello " + std::to_string(connected[1]));
    EXPECT_EQ(server.clientCount(), 2u);

    a.send(R"({"type":"start"})");
    b.send(R"({"type":"stop"})");
    ASSERT_TRUE(pump.runUntil([&] { return messages.size() == 2; }));
    EXPECT_NE(std::find(messages.begin(), messages.end(), R"({"type":"start"})"), messages.end());

    server.broadcast("state 1");
    EXPECT_EQ(a.receive(), "state 1");
    EXPECT_EQ(b.receive(), "state 1");

    a.close();
    ASSERT_TRUE(pump.runUntil([&] { return disconnected.size() == 1; }));
    EXPECT_EQ(disconnected[0], connected[0]);
    EXPECT_EQ(server.clientCount(), 1u);
    server.broadcast("state 2");
    EXPECT_EQ(b.receive(), "state 2");
}

TEST(RemoteServer, StopClosesClientsAndCallsNothingMore) {
    Pump pump;
    RemoteServer server(pump.dispatcher());
    int connected = 0;
    int disconnected = 0;
    server.onClientConnected = [&](RemoteServer::ClientId) { ++connected; };
    server.onClientDisconnected = [&](RemoteServer::ClientId) { ++disconnected; };
    server.setResource("/", {"text/plain", "x"});
    ASSERT_EQ(server.start("127.0.0.1", 0), "");
    const std::uint16_t port = server.port();
    Client client(port);
    ASSERT_TRUE(pump.runUntil([&] { return connected == 1; }));

    server.stop();
    EXPECT_FALSE(server.running());
    EXPECT_EQ(server.clientCount(), 0u);
    EXPECT_THROW(client.receive(), boost::system::system_error);
    pump.runUntil([] { return false; }, 100ms);
    EXPECT_EQ(disconnected, 0);  // stopped: no callbacks

    // A restart serves the same pages again.
    ASSERT_EQ(server.start("127.0.0.1", 0), "");
    EXPECT_EQ(get(server.port(), "/").body(), "x");
}

TEST(RemoteServer, ReportsAddressesItCannotBind) {
    Pump pump;
    RemoteServer first(pump.dispatcher());
    ASSERT_EQ(first.start("127.0.0.1", 0), "");
    RemoteServer second(pump.dispatcher());
    EXPECT_NE(second.start("127.0.0.1", first.port()), "");  // in use
    EXPECT_FALSE(second.running());
    EXPECT_NE(second.start("not an address", 8000), "");
    // An address this computer does not have (TEST-NET-1).
    EXPECT_NE(second.start("192.0.2.1", 0), "");
}

TEST(NetworkList, ListsTheLoopbackAddress) {
    const auto interfaces = listInterfaceAddresses();
    const bool hasLoopback = std::any_of(interfaces.begin(), interfaces.end(), [](const auto& entry) {
        return entry.address == "127.0.0.1" && entry.internal;
    });
    EXPECT_TRUE(hasLoopback);
    // The ranked list has every address once.
    EXPECT_LE(listNetworkAddresses().size(), interfaces.size());
}
