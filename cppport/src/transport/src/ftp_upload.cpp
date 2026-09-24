#include "gs/transport/ftp_upload.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <cctype>
#include <chrono>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace gs::transport {
namespace {

namespace asio = boost::asio;
using asio::ip::tcp;
using boost::system::error_code;

struct Reply {
    int code = 0;
    std::string text;  // the last line
};

// The control and data connections, each exchange given `timeout` to
// finish: asynchronous operations run to completion or the deadline.
class FtpSession {
public:
    FtpSession(asio::io_context& io, std::chrono::milliseconds timeout, const std::atomic<bool>& cancelled)
        : io_(io), timeout_(timeout), cancelled_(cancelled), control_(io), data_(io) {}

    void connect(const std::string& host, std::uint16_t port) {
        tcp::resolver resolver(io_);
        tcp::resolver::results_type endpoints;
        run([&](auto done) {
            resolver.async_resolve(host, std::to_string(port),
                                   [&endpoints, done](const error_code& ec, tcp::resolver::results_type results) {
                                       endpoints = std::move(results);
                                       done(ec);
                                   });
        });
        run([&](auto done) {
            asio::async_connect(control_, endpoints, [done](const error_code& ec, const tcp::endpoint&) { done(ec); });
        });
    }

    // A reply: "ddd text", or lines from "ddd-" to "ddd ".
    Reply reply() {
        Reply r;
        std::string line = readLine();
        if (line.size() < 3 || !std::isdigit(static_cast<unsigned char>(line[0]))) {
            throw std::runtime_error("unexpected reply \"" + line + "\"");
        }
        r.code = std::stoi(line.substr(0, 3));
        if (line.size() > 3 && line[3] == '-') {
            const std::string end = line.substr(0, 3) + " ";
            do {
                line = readLine();
            } while (!line.starts_with(end));
        }
        r.text = line;
        return r;
    }

    Reply command(const std::string& text) {
        const std::string out = text + "\r\n";
        run([&](auto done) {
            asio::async_write(control_, asio::buffer(out), [done](const error_code& ec, std::size_t) { done(ec); });
        });
        return reply();
    }

    // Passive mode: "227 Entering Passive Mode (h1,h2,h3,h4,p1,p2)". The
    // data connection goes to the control connection's address (the board
    // answers with its own, which a NAT may not reach).
    void openData() {
        const Reply r = command("PASV");
        expect(r, {227});
        static const boost::regex kAddress(R"((\d+),(\d+),(\d+),(\d+),(\d+),(\d+))");
        boost::smatch m;
        if (!boost::regex_search(r.text, m, kAddress)) {
            throw std::runtime_error("cannot read the passive address in \"" + r.text + "\"");
        }
        const auto port = static_cast<std::uint16_t>(std::stoi(m[5].str()) * 256 + std::stoi(m[6].str()));
        const tcp::endpoint endpoint(control_.remote_endpoint().address(), port);
        run([&](auto done) { data_.async_connect(endpoint, [done](const error_code& ec) { done(ec); }); });
    }

    void sendData(std::string_view bytes) {
        run([&](auto done) {
            asio::async_write(data_, asio::buffer(bytes.data(), bytes.size()),
                              [done](const error_code& ec, std::size_t) { done(ec); });
        });
    }

    void closeData() {
        error_code ignored;
        data_.shutdown(tcp::socket::shutdown_both, ignored);
        data_.close(ignored);
    }

    void close() {
        error_code ignored;
        closeData();
        control_.close(ignored);
    }

    static void expect(const Reply& reply, std::initializer_list<int> codes) {
        for (const int code : codes) {
            if (reply.code == code) {
                return;
            }
        }
        throw std::runtime_error(reply.text);
    }

private:
    std::string readLine() {
        std::size_t size = 0;
        run([&](auto done) {
            asio::async_read_until(control_, buffer_, '\n', [&size, done](const error_code& ec, std::size_t n) {
                size = n;
                done(ec);
            });
        });
        std::string line(asio::buffers_begin(buffer_.data()), asio::buffers_begin(buffer_.data()) + size);
        buffer_.consume(size);
        while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
            line.pop_back();
        }
        return line;
    }

    // Starts an operation and runs the context until it is done; closes
    // everything on the deadline.
    template <typename Start>
    void run(Start start) {
        if (cancelled_) {
            throw std::runtime_error("cancelled");
        }
        std::optional<error_code> result;
        start([&result](const error_code& ec) { result = ec; });
        io_.restart();
        io_.run_for(timeout_);
        if (!result) {
            close();
            io_.restart();
            io_.run();  // the cancelled handlers
            throw std::runtime_error(cancelled_ ? "cancelled" : "timed out");
        }
        if (*result) {
            throw std::runtime_error(result->message());
        }
    }

    asio::io_context& io_;
    std::chrono::milliseconds timeout_;
    const std::atomic<bool>& cancelled_;
    tcp::socket control_;
    tcp::socket data_;
    asio::streambuf buffer_;
};

}  // namespace

struct FtpUploader::Impl {
    explicit Impl(Dispatcher dispatcher) : dispatch(std::move(dispatcher)) {}

    void deliver(std::function<void()> fn) const {
        dispatch([token = std::weak_ptr<int>(alive), fn = std::move(fn)] {
            if (token.lock()) {
                fn();
            }
        });
    }

    Dispatcher dispatch;
    std::shared_ptr<int> alive = std::make_shared<int>(0);
    std::thread thread;
    std::mutex mutex;
    asio::io_context* io = nullptr;  // the running upload's, to stop it
    std::atomic<bool> cancelled{false};
};

FtpUploader::FtpUploader(Dispatcher dispatch) : impl_(std::make_unique<Impl>(std::move(dispatch))) {}

FtpUploader::~FtpUploader() {
    impl_->alive.reset();
    impl_->cancelled = true;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->io) {
            impl_->io->stop();
        }
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
}

bool FtpUploader::upload(FtpOptions options, std::vector<FtpFile> files, Callbacks callbacks) {
    if (active_.exchange(true)) {
        return false;
    }
    if (impl_->thread.joinable()) {
        impl_->thread.join();  // the last upload's thread, finished
    }
    impl_->thread = std::thread([this, options = std::move(options), files = std::move(files),
                                 callbacks = std::move(callbacks)] {
        Impl& d = *impl_;
        asio::io_context io;
        {
            std::lock_guard lock(d.mutex);
            d.io = &io;
        }
        FtpSession session(io, std::chrono::milliseconds(options.timeoutMs), d.cancelled);
        d.deliver([callbacks] {
            if (callbacks.started) {
                callbacks.started();
            }
        });
        std::string failure;
        try {
            session.connect(options.host, options.port);
            FtpSession::expect(session.reply(), {220});
            Reply login = session.command("USER " + options.user);
            if (login.code == 331) {
                login = session.command("PASS " + options.password);
            }
            FtpSession::expect(login, {230, 202});
            FtpSession::expect(session.command("TYPE I"), {200});
            for (const FtpFile& file : files) {
                session.openData();
                FtpSession::expect(session.command("STOR " + file.name), {125, 150});
                constexpr std::size_t kChunk = 16 * 1024;
                int reported = -1;
                for (std::size_t sent = 0; sent < file.data.size() || reported < 0;) {
                    const std::size_t size = std::min(kChunk, file.data.size() - sent);
                    session.sendData(std::string_view(file.data).substr(sent, size));
                    sent += size;
                    const int percent =
                        file.data.empty() ? 100 : static_cast<int>(sent * 100 / file.data.size());
                    if (percent != reported) {
                        reported = percent;
                        d.deliver([callbacks, percent] {
                            if (callbacks.progress) {
                                callbacks.progress(percent);
                            }
                        });
                    }
                }
                session.closeData();
                FtpSession::expect(session.reply(), {226, 250});
            }
            try {
                session.command("QUIT");
            } catch (const std::exception&) {
                // Done already; the board may just hang up.
            }
        } catch (const std::exception& e) {
            failure = e.what();
            if (failure.empty()) {
                failure = "the transfer failed";
            }
        }
        session.close();
        {
            std::lock_guard lock(d.mutex);
            d.io = nullptr;
        }
        active_ = false;
        d.deliver([callbacks, failure, host = options.host] {
            if (!failure.empty()) {
                if (callbacks.failed) {
                    callbacks.failed("FTP upload to " + host + " failed: " + failure);
                }
            } else if (callbacks.completed) {
                callbacks.completed();
            }
        });
    });
    return true;
}

}  // namespace gs::transport
