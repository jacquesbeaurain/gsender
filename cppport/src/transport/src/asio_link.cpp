#include "gs/transport/asio_link.hpp"

#include <boost/asio.hpp>
#include <boost/regex.hpp>

#include <array>
#include <chrono>
#include <deque>
#include <thread>

namespace gs::transport {
namespace {

namespace asio = boost::asio;
using asio::ip::tcp;
using boost::system::error_code;

}  // namespace

bool looksLikeIpAddress(std::string_view path) {
    // new RegExp(`^${ip}.${ip}.${ip}.${ip}$`) - the dots are not escaped.
    static const boost::regex kPattern(
        "^(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?).(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)."
        "(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?).(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$");
    return boost::regex_match(std::string(path), kPattern);
}

struct AsioLink::Impl {
    Impl(AsioLink& link, Dispatcher dispatcher)
        : owner(link), dispatch(std::move(dispatcher)), work(asio::make_work_guard(io)) {
        thread = std::thread([this] { io.run(); });
    }

    // Hands `fn` to the owner thread; dropped once the link is destroyed.
    void deliver(std::function<void()> fn) const {
        dispatch([token = std::weak_ptr<int>(alive), fn = std::move(fn)] {
            if (token.lock()) {
                fn();
            }
        });
    }

    // ---- everything below runs on the I/O thread ----

    void closeHandles() {
        error_code ignored;
        if (serial) {
            serial->cancel(ignored);
            serial->close(ignored);
            serial.reset();
        }
        if (socket) {
            socket->shutdown(tcp::socket::shutdown_both, ignored);
            socket->close(ignored);
            socket.reset();
        }
        outbox.clear();
        writing = false;
    }

    // An I/O error or the peer closing: tear down and tell the owner.
    void lost(const error_code& ec) {
        ++generation;
        closeHandles();
        owner.open_ = false;
        owner.network_ = false;
        const std::string reason = ec == asio::error::eof ? std::string("the device closed the connection") : ec.message();
        deliver([this, reason] {
            if (owner.onClosed) {
                owner.onClosed(reason);
            }
        });
    }

    void startRead(std::uint64_t gen) {
        const auto handler = [this, gen](const error_code& ec, std::size_t size) {
            if (gen != generation) {
                return;  // closed or reopened meanwhile
            }
            if (ec) {
                lost(ec);
                return;
            }
            deliver([this, bytes = std::string(buffer.data(), size)] {
                if (owner.onData) {
                    owner.onData(bytes);
                }
            });
            startRead(gen);
        };
        if (serial) {
            serial->async_read_some(asio::buffer(buffer), handler);
        } else if (socket) {
            socket->async_read_some(asio::buffer(buffer), handler);
        }
    }

    void writeNext(std::uint64_t gen) {
        writing = true;
        const auto handler = [this, gen](const error_code& ec, std::size_t) {
            if (gen != generation) {
                return;
            }
            if (ec) {
                lost(ec);
                return;
            }
            outbox.pop_front();
            if (outbox.empty()) {
                writing = false;
            } else {
                writeNext(gen);
            }
        };
        if (serial) {
            asio::async_write(*serial, asio::buffer(outbox.front()), handler);
        } else if (socket) {
            asio::async_write(*socket, asio::buffer(outbox.front()), handler);
        } else {
            writing = false;
        }
    }

    AsioLink& owner;
    Dispatcher dispatch;
    std::shared_ptr<int> alive = std::make_shared<int>(0);  // reset first on destruction

    asio::io_context io;
    asio::executor_work_guard<asio::io_context::executor_type> work;
    std::thread thread;

    // I/O thread state
    std::unique_ptr<asio::serial_port> serial;
    std::unique_ptr<tcp::socket> socket;
    std::deque<std::string> outbox;
    bool writing = false;
    std::uint64_t generation = 0;  // bumped by every open/close/loss; stale handlers bail out
    std::array<char, 4096> buffer{};
};

AsioLink::AsioLink(Dispatcher dispatch) : impl_(std::make_unique<Impl>(*this, std::move(dispatch))) {}

AsioLink::~AsioLink() {
    impl_->alive.reset();  // nothing reaches the owner any more
    impl_->work.reset();
    impl_->io.stop();
    if (impl_->thread.joinable()) {
        impl_->thread.join();
    }
    // Destroying impl_ closes the handles; pending handlers are discarded.
}

void AsioLink::openSerial(const SerialOptions& options, OpenDone done) {
    asio::post(impl_->io, [this, options, done = std::move(done)] {
        Impl& d = *impl_;
        ++d.generation;
        d.closeHandles();
        open_ = false;
        auto port = std::make_unique<asio::serial_port>(d.io);
        error_code ec;
        port->open(options.path, ec);
        using Port = asio::serial_port;
        if (!ec) {
            port->set_option(Port::baud_rate(options.baudRate), ec);
        }
        if (!ec) {
            port->set_option(Port::character_size(8), ec);
        }
        if (!ec) {
            port->set_option(Port::parity(Port::parity::none), ec);
        }
        if (!ec) {
            port->set_option(Port::stop_bits(Port::stop_bits::one), ec);
        }
        if (!ec) {
            // Asio's flow-control option also asserts DTR (and RTS unless it
            // handshakes) - which resets Arduino-based boards, as on gSender.
            port->set_option(Port::flow_control(options.rtscts ? Port::flow_control::hardware
                                                               : Port::flow_control::none),
                             ec);
        }
        if (ec) {
            d.deliver([done, message = "Cannot open " + options.path + ": " + ec.message()] { done(message); });
            return;
        }
        d.serial = std::move(port);
        network_ = false;
        open_ = true;
        d.startRead(d.generation);
        d.deliver([done] { done({}); });
    });
}

void AsioLink::openNetwork(const NetworkOptions& options, OpenDone done) {
    asio::post(impl_->io, [this, options, done = std::move(done)] {
        Impl& d = *impl_;
        ++d.generation;
        d.closeHandles();
        open_ = false;

        struct Attempt {
            explicit Attempt(asio::io_context& io) : socket(io), resolver(io), timer(io) {}
            tcp::socket socket;
            tcp::resolver resolver;
            asio::steady_timer timer;
            bool finished = false;
        };
        auto attempt = std::make_shared<Attempt>(d.io);
        const std::uint64_t gen = d.generation;
        const auto finish = [this, attempt, gen, done](const std::string& error) {
            Impl& impl = *impl_;
            if (attempt->finished) {
                return;
            }
            attempt->finished = true;
            attempt->timer.cancel();
            if (gen != impl.generation) {
                impl.deliver([done] { done("the connection attempt was cancelled"); });
                return;
            }
            if (!error.empty()) {
                impl.deliver([done, error] { done(error); });
                return;
            }
            impl.socket = std::make_unique<tcp::socket>(std::move(attempt->socket));
            error_code ignored;
            impl.socket->set_option(tcp::no_delay(true), ignored);
            network_ = true;
            open_ = true;
            impl.startRead(gen);
            impl.deliver([done] { done({}); });
        };

        attempt->timer.expires_after(std::chrono::milliseconds(options.connectTimeoutMs));
        attempt->timer.async_wait([attempt, finish](const error_code& ec) {
            if (!ec) {
                attempt->resolver.cancel();
                error_code ignored;
                attempt->socket.close(ignored);
                finish("Connection timeout");
            }
        });
        attempt->resolver.async_resolve(
            options.host, std::to_string(options.port),
            [attempt, finish](const error_code& ec, const tcp::resolver::results_type& endpoints) {
                if (ec) {
                    finish(ec.message());
                    return;
                }
                asio::async_connect(attempt->socket, endpoints,
                                    [finish](const error_code& connectError, const tcp::endpoint&) {
                                        finish(connectError ? connectError.message() : std::string());
                                    });
            });
    });
}

void AsioLink::close() {
    open_ = false;
    network_ = false;
    asio::post(impl_->io, [impl = impl_.get()] {
        ++impl->generation;
        impl->closeHandles();
    });
}

void AsioLink::send(std::string_view bytes, controller::SendKind) {
    if (!open_) {
        return;
    }
    asio::post(impl_->io, [this, data = std::string(bytes)]() mutable {
        Impl& d = *impl_;
        if (!open_) {
            return;
        }
        d.outbox.push_back(std::move(data));
        if (!d.writing) {
            d.writeNext(d.generation);
        }
    });
}

}  // namespace gs::transport
