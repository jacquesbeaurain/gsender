// Remote mode end to end: a pendant (a WebSocket client, as the page is)
// drives the simulated board through RemoteService - jogging, the jog
// watchdog, starting, pausing and stopping a job - and the settings are
// saved and applied as the dialog does.

#include "jogger.hpp"
#include "machine.hpp"
#include "qt_event_loop.hpp"
#include "remote_service.hpp"

#include "gs/controller/controller.hpp"
#include "gs/remote/network.hpp"
#include "gs/sim/grbl_simulator.hpp"
#include "gs/transport/remote_server.hpp"

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/json.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <gtest/gtest.h>

#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

using namespace gs;
using namespace gs::app;
namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace json = boost::json;
using asio::ip::tcp;

namespace {

QGuiApplication& application() {
    static int argc = 1;
    static char name[] = "gs_app_tests";
    static char* argv[] = {name, nullptr};
    static QGuiApplication* app = [] {
        // Another suite in this executable may have made it first.
        if (auto* existing = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
            return existing;
        }
        qputenv("QT_QPA_PLATFORM", "offscreen");
        return new QGuiApplication(argc, argv);
    }();
    return *app;
}

bool waitFor(const std::function<bool()>& done, int timeoutMs = 5000) {
    const QDeadlineTimer deadline(timeoutMs);
    while (!done()) {
        if (deadline.hasExpired()) {
            return false;
        }
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    }
    return true;
}

// The pendant page's side of the WebSocket: messages are read on a thread
// of their own so the test's Qt loop keeps running.
class Pendant {
public:
    explicit Pendant(std::uint16_t port) : ws_(io_) {
        beast::get_lowest_layer(ws_).connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
        ws_.handshake("127.0.0.1", transport::RemoteServer::kSocketPath);
        reader_ = std::thread([this] {
            try {
                for (;;) {
                    beast::flat_buffer buffer;
                    ws_.read(buffer);
                    std::lock_guard lock(mutex_);
                    messages_.push_back(beast::buffers_to_string(buffer.data()));
                }
            } catch (...) {
            }
        });
    }
    ~Pendant() { close(); }

    void send(const std::string& text) { ws_.write(asio::buffer(text)); }

    void close() {
        if (reader_.joinable()) {
            boost::system::error_code ignored;
            beast::get_lowest_layer(ws_).socket().shutdown(tcp::socket::shutdown_both, ignored);
            reader_.join();
            beast::get_lowest_layer(ws_).close();
        }
    }

    // The newest state message.
    std::optional<json::object> state() {
        std::lock_guard lock(mutex_);
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            json::value v = json::parse(*it);
            if (v.as_object().at("type") == "state") {
                return v.as_object();
            }
        }
        return std::nullopt;
    }

private:
    asio::io_context io_;
    websocket::stream<beast::tcp_stream> ws_;
    std::thread reader_;
    std::mutex mutex_;
    std::deque<std::string> messages_;
};

std::string httpGet(std::uint16_t port, const std::string& target) {
    asio::io_context io;
    beast::tcp_stream stream(io);
    stream.connect(tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));
    http::request<http::string_body> request(http::verb::get, target, 11);
    request.set(http::field::host, "127.0.0.1");
    http::write(stream, request);
    beast::flat_buffer buffer;
    http::response<http::string_body> response;
    http::read(stream, buffer, response);
    return response.body();
}

std::uint16_t freePort() {
    asio::io_context io;
    tcp::acceptor acceptor(io, tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    return acceptor.local_endpoint().port();
}

class RemoteTest : public ::testing::Test {
protected:
    void SetUp() override {
        application();
        machine_ = std::make_unique<Machine>(loop_, (dir_.path() + "/rc").toStdWString());
        jogger_ = std::make_unique<Jogger>(*machine_);
        remote_ = std::make_unique<RemoteService>(*machine_, *jogger_);
    }
    void TearDown() override {
        remote_.reset();
        jogger_.reset();
        machine_.reset();
    }

    void connectSimulator() {
        machine_->connectTo(Machine::kSimulatorPort);
        ASSERT_TRUE(waitFor([&] {
            return machine_->isConnected() && machine_->controller()->runner().hasSettings() &&
                   machine_->controller()->state().status.activeState == "Idle";
        }));
        machine_->simulator()->setSpeed(20);
    }

    std::string activeState() { return machine_->controller()->state().status.activeState; }

    QTemporaryDir dir_;
    QtEventLoop loop_;
    std::unique_ptr<Machine> machine_;
    std::unique_ptr<Jogger> jogger_;
    std::unique_ptr<RemoteService> remote_;
};

}  // namespace

TEST_F(RemoteTest, ServesThePendantPage) {
    ASSERT_EQ(remote_->startServer("127.0.0.1", 0), "");
    EXPECT_TRUE(remote_->running());
    const std::string page = httpGet(static_cast<std::uint16_t>(remote_->port()), "/");
    EXPECT_NE(page.find("<title>gSender Remote</title>"), std::string::npos);
    EXPECT_NE(page.find("'/ws'"), std::string::npos);
    EXPECT_EQ(remote_->url().toStdString(), "http://127.0.0.1:" + std::to_string(remote_->port()) + "/#/remote");
}

TEST_F(RemoteTest, APendantSeesTheMachineAndJogsIt) {
    connectSimulator();
    ASSERT_EQ(remote_->startServer("127.0.0.1", 0), "");
    Pendant pendant(static_cast<std::uint16_t>(remote_->port()));
    ASSERT_TRUE(waitFor([&] { return remote_->clientCount() == 1 && pendant.state(); }));
    EXPECT_TRUE(pendant.state()->at("connected").as_bool());
    EXPECT_EQ(pendant.state()->at("activeState").as_string(), "Idle");
    EXPECT_TRUE(pendant.state()->at("canJog").as_bool());

    // A tap: one step along X, and the DRO follows on the pendant.
    pendant.send(R"({"type":"jogPress","x":1})");
    pendant.send(R"({"type":"jogRelease"})");
    ASSERT_TRUE(waitFor([&] { return machine_->workPositionMm()[0] > 0.5 && activeState() == "Idle"; }));
    ASSERT_TRUE(waitFor([&] {
        const auto s = pendant.state();
        return s && s->at("wpos").as_array()[0].to_number<double>() > 0.5;
    }));

    // Presets come back in the state.
    pendant.send(R"({"type":"preset","preset":"Precise"})");
    ASSERT_TRUE(waitFor([&] { return pendant.state()->at("jogPreset").as_string() == "Precise"; }));
}

TEST_F(RemoteTest, AHeldJogStopsWhenThePendantGoesQuietOrAway) {
    connectSimulator();
    machine_->simulator()->setSpeed(1);
    ASSERT_EQ(remote_->startServer("127.0.0.1", 0), "");
    {
        Pendant pendant(static_cast<std::uint16_t>(remote_->port()));
        ASSERT_TRUE(waitFor([&] { return remote_->clientCount() == 1; }));
        // Held, and then nothing: no release, no pings.
        pendant.send(R"({"type":"jogPress","y":1})");
        ASSERT_TRUE(waitFor([&] { return jogger_->isPressed(); }));
        ASSERT_TRUE(waitFor([&] { return !jogger_->isPressed(); }, 3000));  // the watchdog
        ASSERT_TRUE(waitFor([&] { return activeState() == "Idle"; }));

        // Held while the pendant keeps pinging: still going.
        pendant.send(R"({"type":"jogPress","x":1})");
        for (int i = 0; i < 6; ++i) {
            waitFor([] { return false; }, 250);
            pendant.send(R"({"type":"ping"})");
        }
        EXPECT_TRUE(jogger_->isPressed());
    }
    // The pendant disconnected mid-hold.
    ASSERT_TRUE(waitFor([&] { return remote_->clientCount() == 0 && !jogger_->isPressed(); }));
    ASSERT_TRUE(waitFor([&] { return activeState() == "Idle"; }));
}

TEST_F(RemoteTest, APendantStartsPausesAndStopsTheJob) {
    connectSimulator();
    machine_->simulator()->setSpeed(1);
    machine_->loadProgram("rect.nc", "G21 G90\nG1 X30 F300\nG1 Y30\nG1 X0\nG1 Y0\n");
    ASSERT_TRUE(waitFor([&] { return !machine_->isAnalyzing(); }));
    ASSERT_EQ(remote_->startServer("127.0.0.1", 0), "");
    Pendant pendant(static_cast<std::uint16_t>(remote_->port()));
    ASSERT_TRUE(waitFor([&] {
        const auto s = pendant.state();
        return s && s->at("canRun").as_bool() && s->at("fileName").as_string() == "rect.nc";
    }));

    pendant.send(R"({"type":"start"})");
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->workflow().isRunning() && activeState() == "Run"; }));
    ASSERT_TRUE(waitFor([&] { return pendant.state()->at("workflow").as_string() == "running"; }));
    EXPECT_FALSE(remote_->state().canJog);  // no jogging during a job
    pendant.send(R"({"type":"jogPress","x":1})");

    pendant.send(R"({"type":"pause"})");
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->workflow().isPaused(); }));
    ASSERT_TRUE(waitFor([&] { return pendant.state()->at("workflow").as_string() == "paused"; }));
    EXPECT_FALSE(jogger_->isPressed());

    pendant.send(R"({"type":"stop"})");
    ASSERT_TRUE(waitFor([&] { return machine_->controller()->workflow().isIdle(); }));
    ASSERT_TRUE(waitFor([&] { return pendant.state()->at("workflow").as_string() == "idle"; }));
}

TEST_F(RemoteTest, SettingsAreValidatedSavedAndApplied) {
    const remote::RemoteSettings defaults = remote_->settings();
    EXPECT_EQ(defaults.port, 8000);
    EXPECT_FALSE(defaults.headlessStatus);

    EXPECT_EQ(remote_->apply({"127.0.0.1", 80, true, false}).toStdString(),
              "Invalid Port Number - Must be between 1025 and 65535");
    EXPECT_FALSE(remote_->running());

    const std::uint16_t port = freePort();
    ASSERT_EQ(remote_->apply({"127.0.0.1", port, true, false}), "");
    EXPECT_TRUE(remote_->running());
    EXPECT_EQ(remote_->port(), port);
    EXPECT_TRUE(remote_->settings().headlessStatus);
    EXPECT_EQ(remote_->settings().ip, "127.0.0.1");

    ASSERT_EQ(remote_->apply({"127.0.0.1", port, false, false}), "");
    EXPECT_FALSE(remote_->running());
    EXPECT_FALSE(remote_->settings().headlessStatus);

    // Another service starts from what was saved.
    ASSERT_EQ(remote_->apply({"127.0.0.1", port, true, false}), "");
    remote_->stopServer();
    remote_->startFromSettings();
    EXPECT_TRUE(remote_->running());

    // An address it cannot bind switches remote mode off, flagged.
    transport::RemoteServer squatter([](std::function<void()> fn) { fn(); });
    const std::uint16_t taken = freePort();
    ASSERT_EQ(squatter.start("127.0.0.1", taken), "");
    EXPECT_NE(remote_->apply({"127.0.0.1", taken, true, false}), "");
    EXPECT_FALSE(remote_->running());
    EXPECT_FALSE(remote_->settings().headlessStatus);
    EXPECT_TRUE(remote_->settings().error);
}
