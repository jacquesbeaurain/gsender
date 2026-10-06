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
#include <QObject>
#include <QStringList>
#include <QTemporaryDir>
#include <QVariantList>
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
        return newest([](const json::object& o) { return o.at("type") == "state"; });
    }
    // The newest `model` message for a model.
    std::optional<json::object> model(const std::string& name) {
        return newest([&](const json::object& o) { return o.at("type") == "model" && o.at("model").as_string() == name; });
    }
    // The reply to call `id`.
    std::optional<json::object> result(std::int64_t id) {
        return newest([&](const json::object& o) { return o.at("type") == "result" && o.at("id").as_int64() == id; });
    }
    int modelMessages(const std::string& name) {
        std::lock_guard lock(mutex_);
        int count = 0;
        for (const std::string& text : messages_) {
            const json::value v = json::parse(text);
            count += v.as_object().at("type") == "model" && v.as_object().at("model").as_string() == name;
        }
        return count;
    }

private:
    std::optional<json::object> newest(const std::function<bool(const json::object&)>& wanted) {
        std::lock_guard lock(mutex_);
        for (auto it = messages_.rbegin(); it != messages_.rend(); ++it) {
            json::value v = json::parse(*it);
            if (wanted(v.as_object())) {
                return v.as_object();
            }
        }
        return std::nullopt;
    }

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

// A view model as the tool pages use them: properties that notify, methods
// the pendant may call (and one it may not), a property it may set.
class FakeModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int speed READ speed NOTIFY changed)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY changed)
    Q_PROPERTY(QString locked READ locked WRITE setLocked NOTIFY changed)
    Q_PROPERTY(QVariantList items READ items NOTIFY changed)

public:
    int speed() const { return speed_; }
    QString search() const { return search_; }
    QString locked() const { return locked_; }
    QVariantList items() const { return {QVariantMap{{"id", 1}, {"name", "one"}}, QStringLiteral("two")}; }
    void setSearch(const QString& text) {
        search_ = text;
        Q_EMIT changed();
    }
    void setLocked(const QString& text) {
        locked_ = text;
        Q_EMIT changed();
    }

    Q_INVOKABLE void setSpeed(int speed) {
        speed_ = speed;
        Q_EMIT changed();
    }
    Q_INVOKABLE QString echo(const QString& text, double times) const { return text + QString::number(times); }
    Q_INVOKABLE QVariantMap pair(bool flag) const { return {{"flag", flag}}; }
    Q_INVOKABLE void secret() { secretCalls_++; }

    int secretCalls_ = 0;

Q_SIGNALS:
    void changed();

private:
    int speed_ = 0;
    QString search_;
    QString locked_ = QStringLiteral("fixed");
};

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

TEST_F(RemoteTest, APendantUsesABoundModelOnlyThroughItsAllowedMembers) {
    FakeModel* model = nullptr;
    remote_->bindModel("fake", {[&] { return model = new FakeModel; }, {"setSpeed", "echo", "pair"}, {"search"}});
    ASSERT_EQ(remote_->startServer("127.0.0.1", 0), "");
    Pendant pendant(static_cast<std::uint16_t>(remote_->port()));
    ASSERT_TRUE(waitFor([&] { return remote_->clientCount() == 1; }));

    // Nothing is made, or sent, until a pendant asks.
    EXPECT_EQ(model, nullptr);
    pendant.send(R"({"type":"subscribe","model":"nope"})");
    pendant.send(R"({"type":"subscribe","model":"fake"})");
    ASSERT_TRUE(waitFor([&] { return pendant.model("fake").has_value(); }));
    EXPECT_FALSE(pendant.model("nope"));
    const json::object first = pendant.model("fake")->at("properties").as_object();
    EXPECT_EQ(first.at("speed").as_int64(), 0);
    EXPECT_EQ(first.at("items").as_array().size(), 2u);
    EXPECT_EQ(first.at("items").as_array()[0].as_object().at("name").as_string(), "one");
    EXPECT_FALSE(first.contains("objectName"));

    // A call changes the model and the new properties follow; the reply
    // carries the method's result.
    pendant.send(R"({"type":"call","model":"fake","method":"setSpeed","args":[12000],"id":1})");
    ASSERT_TRUE(waitFor([&] { return pendant.model("fake")->at("properties").as_object().at("speed").as_int64() == 12000; }));
    ASSERT_TRUE(waitFor([&] { return pendant.result(1).has_value(); }));
    EXPECT_TRUE(pendant.result(1)->at("ok").as_bool());
    pendant.send(R"({"type":"call","model":"fake","method":"echo","args":["x",2.5],"id":2})");
    ASSERT_TRUE(waitFor([&] { return pendant.result(2).has_value(); }));
    EXPECT_EQ(pendant.result(2)->at("value").as_string(), "x2.5");
    pendant.send(R"({"type":"call","model":"fake","method":"pair","args":[true],"id":3})");
    ASSERT_TRUE(waitFor([&] { return pendant.result(3).has_value(); }));
    EXPECT_TRUE(pendant.result(3)->at("value").as_object().at("flag").as_bool());

    // Not on the list, wrong arguments, an unknown model: refused, said so.
    pendant.send(R"({"type":"call","model":"fake","method":"secret","id":4})");
    pendant.send(R"({"type":"call","model":"fake","method":"setSpeed","args":["fast"],"id":5})");
    pendant.send(R"({"type":"call","model":"fake","method":"setSpeed","args":[1,2],"id":6})");
    pendant.send(R"({"type":"call","model":"other","method":"setSpeed","id":7})");
    pendant.send(R"({"type":"call","model":"fake","method":"destroyed","id":8})");
    for (int id = 4; id <= 8; ++id) {
        ASSERT_TRUE(waitFor([&] { return pendant.result(id).has_value(); })) << id;
        EXPECT_FALSE(pendant.result(id)->at("ok").as_bool()) << id;
    }
    EXPECT_EQ(model->secretCalls_, 0);
    EXPECT_EQ(model->speed(), 12000);

    // Properties: only the listed ones are writable.
    pendant.send(R"({"type":"set","model":"fake","property":"locked","value":"hacked"})");
    pendant.send(R"({"type":"set","model":"fake","property":"search","value":"baud"})");
    ASSERT_TRUE(waitFor([&] { return pendant.model("fake")->at("properties").as_object().at("search").as_string() == "baud"; }));
    EXPECT_EQ(model->locked().toStdString(), "fixed");

    // Unsubscribed, a pendant hears no more.
    pendant.send(R"({"type":"unsubscribe","model":"fake"})");
    waitFor([] { return false; }, 100);
    const int heard = pendant.modelMessages("fake");
    model->setSpeed(5);
    waitFor([] { return false; }, 200);
    EXPECT_EQ(pendant.modelMessages("fake"), heard);
}

TEST_F(RemoteTest, AModelIsPushedOncePerBurstAndOnlyToItsSubscribers) {
    FakeModel* model = nullptr;
    remote_->bindModel("fake", {[&] { return model = new FakeModel; }, {}, {}});
    ASSERT_EQ(remote_->startServer("127.0.0.1", 0), "");
    Pendant watching(static_cast<std::uint16_t>(remote_->port()));
    Pendant other(static_cast<std::uint16_t>(remote_->port()));
    ASSERT_TRUE(waitFor([&] { return remote_->clientCount() == 2; }));
    watching.send(R"({"type":"subscribe","model":"fake"})");
    ASSERT_TRUE(waitFor([&] { return watching.modelMessages("fake") == 1; }));

    for (int i = 1; i <= 20; ++i) {
        model->setSpeed(i);  // a burst: one message, with the last value
    }
    ASSERT_TRUE(waitFor([&] { return watching.modelMessages("fake") == 2; }));
    EXPECT_EQ(watching.model("fake")->at("properties").as_object().at("speed").as_int64(), 20);
    model->setSpeed(20);  // no change: nothing sent
    waitFor([] { return false; }, 200);
    EXPECT_EQ(watching.modelMessages("fake"), 2);
    EXPECT_EQ(other.modelMessages("fake"), 0);
}

#include "test_remote.moc"
