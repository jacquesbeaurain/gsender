// The Asio byte link over loopback TCP, serial open failures, and the port
// listing rules. Real serial hardware is outside unit tests.

#include "gs/controller/session.hpp"
#include "gs/transport/asio_link.hpp"
#include "gs/transport/ftp_upload.hpp"
#include "gs/transport/port_list.hpp"

#include <boost/asio.hpp>
#include <gtest/gtest.h>

#include <array>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <functional>
#include <istream>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

using namespace gs;
using namespace gs::transport;
using namespace std::chrono_literals;
namespace asio = boost::asio;
using asio::ip::tcp;

namespace {

// The owner thread's queue: the link posts callbacks here, the test runs them.
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

    // Runs posted callbacks until `done` holds; false on timeout.
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

    // Runs whatever is queued within `duration`.
    void runFor(std::chrono::milliseconds duration) {
        runUntil([] { return false; }, duration);
    }

private:
    std::mutex mutex_;
    std::condition_variable ready_;
    std::deque<std::function<void()>> queue_;
};

// A one-client TCP server on the loopback interface.
class Server {
public:
    Server() : acceptor_(io_, tcp::endpoint(asio::ip::address_v4::loopback(), 0)), socket_(io_) {
        thread_ = std::thread([this] {
            boost::system::error_code ec;
            acceptor_.accept(socket_, ec);
            accepted_ = !ec;
        });
    }
    ~Server() {
        boost::system::error_code ec;
        acceptor_.close(ec);
        if (thread_.joinable()) {
            thread_.join();
        }
        socket_.close(ec);
    }

    std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    bool waitForClient() {
        if (thread_.joinable()) {
            thread_.join();
        }
        return accepted_;
    }

    // Reads until `count` bytes arrived (or the timeout passed).
    std::string read(std::size_t count, std::chrono::milliseconds timeout = 5s) {
        std::string data;
        socket_.non_blocking(true);
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (data.size() < count && std::chrono::steady_clock::now() < deadline) {
            std::array<char, 256> buffer{};
            boost::system::error_code ec;
            const std::size_t n = socket_.read_some(asio::buffer(buffer), ec);
            if (ec == asio::error::would_block) {
                std::this_thread::sleep_for(2ms);
                continue;
            }
            if (ec) {
                break;
            }
            data.append(buffer.data(), n);
        }
        return data;
    }

    void write(std::string_view text) { asio::write(socket_, asio::buffer(text.data(), text.size())); }
    void closeClient() {
        boost::system::error_code ec;
        socket_.shutdown(tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    asio::io_context io_;
    tcp::acceptor acceptor_;
    tcp::socket socket_;
    std::thread thread_;
    bool accepted_ = false;
};

// Opens `link` to `port` on the loopback interface and returns the error.
std::string connect(Pump& pump, AsioLink& link, std::uint16_t port) {
    std::optional<std::string> result;
    link.openNetwork({"127.0.0.1", port, 2000}, [&](const std::string& error) { result = error; });
    EXPECT_TRUE(pump.runUntil([&] { return result.has_value(); }));
    return result.value_or("timed out");
}

// ---- rules ---------------------------------------------------------------------------

TEST(Transport, AddressesThatLookLikeIpv4GoToTheNetwork) {
    EXPECT_TRUE(looksLikeIpAddress("192.168.5.1"));
    EXPECT_TRUE(looksLikeIpAddress("10.0.0.254"));
    EXPECT_FALSE(looksLikeIpAddress("COM3"));
    EXPECT_FALSE(looksLikeIpAddress("256.1.1.1"));
    EXPECT_FALSE(looksLikeIpAddress("1.2.3"));
    // Upstream's unescaped dots (checked against the JavaScript regex):
    EXPECT_TRUE(looksLikeIpAddress("192x168x5x1"));
    EXPECT_TRUE(looksLikeIpAddress("192.168.1"));
}

TEST(Transport, UsbIdsAreReadFromPnpIds) {
    EXPECT_EQ(usbId("USB\\VID_2341&PID_0043\\7543334323135171E0D1", "VID"), "2341");
    EXPECT_EQ(usbId("USB\\VID_2341&PID_0043\\7543334323135171E0D1", "PID"), "0043");
    EXPECT_EQ(usbId("FTDIBUS\\VID_0403+PID_6001+A50285BIA\\0000", "PID"), "6001");
    EXPECT_EQ(usbId("usb\\vid_1a86&pid_7523", "VID"), "1A86");
    EXPECT_EQ(usbId("ACPI\\PNP0501\\1", "VID"), "");
}

TEST(Transport, RecognizedPortsNeedAKnownVendorAndProduct) {
    EXPECT_TRUE(isRecognizedPort("2341", "0043"));  // Arduino Uno
    EXPECT_TRUE(isRecognizedPort("1a86", "7523"));  // CH340, any case
    EXPECT_TRUE(isRecognizedPort("0403", "6001"));  // FTDI
    EXPECT_FALSE(isRecognizedPort("1234", "0043"));
    EXPECT_FALSE(isRecognizedPort("2341", "FFFF"));
    EXPECT_FALSE(isRecognizedPort("", ""));
}

TEST(Transport, ListingPortsReportsComPorts) {
    for (const SerialPortInfo& port : listSerialPorts()) {
        EXPECT_TRUE(port.path.starts_with("COM")) << port.path;
    }
}

// ---- the link --------------------------------------------------------------------------

TEST(AsioLink, ExchangesBytesOverTcp) {
    Pump pump;
    Server server;
    AsioLink link(pump.dispatcher());
    std::string received;
    link.onData = [&](std::string_view bytes) { received.append(bytes); };

    EXPECT_EQ(connect(pump, link, server.port()), "");
    ASSERT_TRUE(server.waitForClient());
    EXPECT_TRUE(link.isOpen());
    EXPECT_TRUE(link.isNetwork());

    link.send("$I\n", controller::SendKind::Write);
    EXPECT_EQ(server.read(3), "$I\n");

    server.write("[VER:1.1f.20170801:]\r\nok\r\n");
    EXPECT_TRUE(pump.runUntil([&] { return received.size() >= 26; }));
    EXPECT_EQ(received, "[VER:1.1f.20170801:]\r\nok\r\n");
}

TEST(AsioLink, WritesAndImmediateWritesKeepTheirOrder) {
    Pump pump;
    Server server;
    AsioLink link(pump.dispatcher());
    ASSERT_EQ(connect(pump, link, server.port()), "");
    ASSERT_TRUE(server.waitForClient());
    link.send("G1 X1\n", controller::SendKind::Write);
    link.send("?", controller::SendKind::Immediate);
    link.send("\x85", controller::SendKind::Immediate);
    link.send("G1 X2\n", controller::SendKind::Write);
    EXPECT_EQ(server.read(14), "G1 X1\n?\x85G1 X2\n");
}

TEST(AsioLink, APeerThatClosesIsReportedOnce) {
    Pump pump;
    Server server;
    AsioLink link(pump.dispatcher());
    int closedCount = 0;
    std::string reason;
    link.onClosed = [&](const std::string& why) {
        ++closedCount;
        reason = why;
    };
    ASSERT_EQ(connect(pump, link, server.port()), "");
    ASSERT_TRUE(server.waitForClient());
    server.closeClient();
    EXPECT_TRUE(pump.runUntil([&] { return closedCount > 0; }));
    pump.runFor(50ms);
    EXPECT_EQ(closedCount, 1);
    EXPECT_FALSE(reason.empty());
    EXPECT_FALSE(link.isOpen());
}

TEST(AsioLink, ADeliberateCloseIsSilent) {
    Pump pump;
    Server server;
    AsioLink link(pump.dispatcher());
    bool closed = false;
    link.onClosed = [&](const std::string&) { closed = true; };
    ASSERT_EQ(connect(pump, link, server.port()), "");
    ASSERT_TRUE(server.waitForClient());
    link.close();
    EXPECT_FALSE(link.isOpen());  // immediately
    EXPECT_EQ(server.read(1, 1s), "");  // the server sees the end of the stream
    pump.runFor(50ms);
    EXPECT_FALSE(closed);
    link.send("ignored\n", controller::SendKind::Write);  // dropped
}

TEST(AsioLink, ARefusedConnectionFails) {
    std::uint16_t port = 0;
    {
        asio::io_context io;
        tcp::acceptor probe(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
        port = probe.local_endpoint().port();
    }  // nothing listens there now
    Pump pump;
    AsioLink link(pump.dispatcher());
    // Windows retries a refused loopback connect for about 2 s, so the
    // short timeout usually ends it; elsewhere the refusal does.
    std::optional<std::string> result;
    const auto started = std::chrono::steady_clock::now();
    link.openNetwork({"127.0.0.1", port, 200}, [&](const std::string& error) { result = error; });
    ASSERT_TRUE(pump.runUntil([&] { return result.has_value(); }, 2s));
    EXPECT_NE(*result, "");
    EXPECT_LT(std::chrono::steady_clock::now() - started, 1500ms);
    EXPECT_FALSE(link.isOpen());
}

TEST(AsioLink, AMissingSerialPortFails) {
    Pump pump;
    AsioLink link(pump.dispatcher());
    std::optional<std::string> result;
    link.openSerial({"COM250", 115200, false}, [&](const std::string& error) { result = error; });
    ASSERT_TRUE(pump.runUntil([&] { return result.has_value(); }));
    EXPECT_NE(result->find("COM250"), std::string::npos) << *result;
    EXPECT_FALSE(link.isOpen());
}

TEST(AsioLink, NoCallbackRunsAfterTheLinkIsGone) {
    Pump pump;
    Server server;
    bool called = false;
    {
        AsioLink link(pump.dispatcher());
        link.onData = [&](std::string_view) { called = true; };
        ASSERT_EQ(connect(pump, link, server.port()), "");
        ASSERT_TRUE(server.waitForClient());
        server.write("ok\r\n");
        std::this_thread::sleep_for(50ms);  // the data is queued, not yet run
    }
    pump.runFor(50ms);
    EXPECT_FALSE(called);
}

TEST(AsioLink, FeedsASessionThatIdentifiesTheFirmware) {
    Pump pump;
    Server server;
    runtime::ManualEventLoop loop;
    AsioLink link(pump.dispatcher());
    controller::Session session(loop, link, {}, {}, {});
    link.onData = [&](std::string_view bytes) { session.receive(bytes); };

    ASSERT_EQ(connect(pump, link, server.port()), "");
    ASSERT_TRUE(server.waitForClient());
    session.opened();
    EXPECT_EQ(server.read(3), "$I\n");  // the first detection query
    server.write("\r\nGrbl 1.1h ['$' for help]\r\n");
    EXPECT_TRUE(pump.runUntil([&] { return session.controller() != nullptr; }));
    EXPECT_EQ(session.firmware(), protocol::Firmware::Grbl);
}

// ---- FTP uploads ------------------------------------------------------------------------------

// grblHAL's FTP server as far as an upload needs it: one session, passive
// data connections on the loopback interface.
class FakeFtpServer {
public:
    explicit FakeFtpServer(std::string password = "grblHAL")
        : password_(std::move(password)), acceptor_(io_, tcp::endpoint(asio::ip::address_v4::loopback(), 0)) {
        thread_ = std::thread([this] { serve(); });
    }
    ~FakeFtpServer() {
        boost::system::error_code ec;
        acceptor_.close(ec);
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    std::uint16_t port() const { return acceptor_.local_endpoint().port(); }
    // After the session (joins the server).
    const std::map<std::string, std::string>& files() {
        if (thread_.joinable()) {
            thread_.join();
        }
        return files_;
    }
    const std::vector<std::string>& commands() {
        if (thread_.joinable()) {
            thread_.join();
        }
        return commands_;
    }

private:
    void serve() {
        boost::system::error_code ec;
        tcp::socket control(io_);
        acceptor_.accept(control, ec);
        if (ec) {
            return;
        }
        const auto say = [&](std::string_view text) { asio::write(control, asio::buffer(text.data(), text.size()), ec); };
        say("220-grblHAL FTP\r\n220 ready\r\n");
        asio::streambuf buffer;
        std::optional<tcp::acceptor> passive;
        while (true) {
            asio::read_until(control, buffer, "\r\n", ec);
            if (ec) {
                return;
            }
            std::istream in(&buffer);
            std::string line;
            std::getline(in, line);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            commands_.push_back(line);
            if (line.starts_with("USER ")) {
                say("331 Password required\r\n");
            } else if (line.starts_with("PASS ")) {
                say(line.substr(5) == password_ ? "230 Logged in\r\n" : "530 Login incorrect.\r\n");
            } else if (line == "TYPE I") {
                say("200 Type set to I\r\n");
            } else if (line == "PASV") {
                passive.emplace(io_, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
                const std::uint16_t data = passive->local_endpoint().port();
                say("227 Entering Passive Mode (127,0,0,1," + std::to_string(data / 256) + "," +
                    std::to_string(data % 256) + ")\r\n");
            } else if (line.starts_with("STOR ") && passive) {
                say("150 Opening data connection\r\n");
                tcp::socket data(io_);
                passive->accept(data, ec);
                std::string received;
                std::array<char, 4096> chunk{};
                while (true) {
                    const std::size_t n = data.read_some(asio::buffer(chunk), ec);
                    received.append(chunk.data(), n);
                    if (ec) {
                        break;
                    }
                }
                files_[line.substr(5)] = received;
                passive.reset();
                say("226 Transfer complete\r\n");
            } else if (line == "QUIT") {
                say("221 Bye\r\n");
                return;
            } else {
                say("502 Not implemented\r\n");
            }
        }
    }

    std::string password_;
    asio::io_context io_;
    tcp::acceptor acceptor_;
    std::thread thread_;
    std::map<std::string, std::string> files_;
    std::vector<std::string> commands_;
};

struct UploadWatch {
    FtpUploader::Callbacks callbacks() {
        return {[this] { ++started; }, [this](int percent) { progress.push_back(percent); },
                [this] { ++completed; }, [this](const std::string& message) { failures.push_back(message); }};
    }
    bool done() const { return completed + static_cast<int>(failures.size()) > 0; }

    int started = 0;
    std::vector<int> progress;
    int completed = 0;
    std::vector<std::string> failures;
};

TEST(FtpUpload, SendsTheFilesToTheBoardsCard) {
    Pump pump;
    FakeFtpServer server;
    FtpUploader uploader(pump.dispatcher());
    UploadWatch watch;
    std::string big;
    for (int i = 0; big.size() < 40000; ++i) {
        big += "G1 X" + std::to_string(i % 100) + " Y" + std::to_string(i % 70) + "\n";
    }
    ASSERT_TRUE(uploader.upload({"127.0.0.1", server.port()}, {{"job.nc", big}, {"empty.nc", ""}}, watch.callbacks()));
    EXPECT_TRUE(uploader.active());
    EXPECT_FALSE(uploader.upload({"127.0.0.1", server.port()}, {}, watch.callbacks()));  // one at a time
    ASSERT_TRUE(pump.runUntil([&] { return watch.done(); }, 10s));
    EXPECT_TRUE(watch.failures.empty()) << watch.failures.front();
    EXPECT_EQ(watch.started, 1);
    EXPECT_EQ(watch.completed, 1);
    EXPECT_FALSE(uploader.active());
    ASSERT_GE(watch.progress.size(), 3u);  // job.nc in 16 KB pieces, then empty.nc
    EXPECT_EQ(watch.progress[0], 40);
    EXPECT_EQ(watch.progress.back(), 100);
    const std::map<std::string, std::string>& files = server.files();
    ASSERT_EQ(files.size(), 2u);
    EXPECT_EQ(files.at("job.nc"), big);
    EXPECT_EQ(files.at("empty.nc"), "");
    const std::vector<std::string>& commands = server.commands();
    EXPECT_EQ(std::vector<std::string>(commands.begin(), commands.begin() + 3),
              (std::vector<std::string>{"USER grblHAL", "PASS grblHAL", "TYPE I"}));
    EXPECT_EQ(commands.back(), "QUIT");
}

TEST(FtpUpload, ARefusedLoginOrNoServerFails) {
    Pump pump;
    {
        FakeFtpServer server("secret");
        FtpUploader uploader(pump.dispatcher());
        UploadWatch watch;
        ASSERT_TRUE(uploader.upload({"127.0.0.1", server.port()}, {{"job.nc", "G0 X1\n"}}, watch.callbacks()));
        ASSERT_TRUE(pump.runUntil([&] { return watch.done(); }, 10s));
        ASSERT_EQ(watch.failures.size(), 1u);
        EXPECT_EQ(watch.failures[0], "FTP upload to 127.0.0.1 failed: 530 Login incorrect.");
        EXPECT_EQ(watch.completed, 0);
    }
    // Nothing listens on a port just freed.
    std::uint16_t closed = 0;
    {
        asio::io_context io;
        tcp::acceptor probe(io, tcp::endpoint(asio::ip::address_v4::loopback(), 0));
        closed = probe.local_endpoint().port();
    }
    FtpUploader uploader(pump.dispatcher());
    UploadWatch watch;
    ASSERT_TRUE(uploader.upload({"127.0.0.1", closed, "grblHAL", "grblHAL", 3000}, {{"job.nc", "x"}}, watch.callbacks()));
    ASSERT_TRUE(pump.runUntil([&] { return watch.done(); }, 10s));
    ASSERT_EQ(watch.failures.size(), 1u);
    EXPECT_TRUE(watch.failures[0].starts_with("FTP upload to 127.0.0.1 failed: ")) << watch.failures[0];
}

}  // namespace
