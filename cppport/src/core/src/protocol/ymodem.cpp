#include "gs/protocol/ymodem.hpp"

#include <algorithm>
#include <cmath>

namespace gs::protocol {
namespace {

constexpr std::size_t kSohSize = 128;
constexpr std::size_t kStxSize = 1024;
constexpr char kPad = 0x1A;

std::string padded(std::string_view data, std::size_t size, char pad) {
    std::string block(data);
    block.resize(size, pad);
    return block;
}

void appendCrc(std::string& packet, std::string_view data) {
    const std::uint16_t crc = crc16Xmodem(data);
    packet.push_back(static_cast<char>(crc >> 8));
    packet.push_back(static_cast<char>(crc & 0xFF));
}

}  // namespace

std::uint16_t crc16Xmodem(std::string_view data) {
    std::uint16_t crc = 0;
    for (const char c : data) {
        crc ^= static_cast<std::uint16_t>(static_cast<unsigned char>(c) << 8);
        for (int bit = 0; bit < 8; ++bit) {
            crc = (crc & 0x8000) ? static_cast<std::uint16_t>((crc << 1) ^ 0x1021) : static_cast<std::uint16_t>(crc << 1);
        }
    }
    return crc;
}

std::optional<std::string> ymodemHeaderPacket(std::string_view name, std::size_t size) {
    const std::string path = "/" + std::string(name);
    const std::string length = std::to_string(size);
    if (path.size() + length.size() > kSohSize - 1) {
        return std::nullopt;  // "Either filename is too big or the file is extremely large."
    }
    std::string block;
    block.reserve(kSohSize);
    block += path;
    block.push_back('\0');
    block += length;
    block.push_back('\0');
    block.resize(kSohSize, '\0');
    std::string packet{kYmodemSoh, '\0', static_cast<char>(0xFF)};
    packet += block;
    appendCrc(packet, block);
    return packet;
}

std::string ymodemDataPacket(char type, unsigned char seq, std::string_view block) {
    std::string packet{type, static_cast<char>(seq), static_cast<char>(0xFF - seq)};
    packet += block;
    appendCrc(packet, block);
    return packet;
}

YModemBlocks ymodemBlocks(std::string_view data) {
    YModemBlocks out;
    if (data.empty()) {
        return out;
    }
    if (data.size() <= kSohSize) {
        out.blocks.push_back(padded(data, kSohSize, kPad));
        out.lastSoh = true;
        return out;
    }
    if (data.size() <= kStxSize) {
        out.blocks.push_back(padded(data, kStxSize, kPad));
        return out;
    }
    for (std::size_t at = 0; at < data.size(); at += kStxSize) {
        out.blocks.push_back(padded(data.substr(at, kStxSize), kStxSize, '\0'));
    }
    return out;
}

// ---- the sender -----------------------------------------------------------------------------

YModemSender::YModemSender(runtime::EventLoop& loop, Callbacks callbacks)
    : timers_(loop), callbacks_(std::move(callbacks)) {}

void YModemSender::start(std::vector<YModemFile> files) {
    files_ = std::move(files);
    file_ = 0;
    active_ = true;
    waiting_ = Waiting::Nothing;
    if (callbacks_.started) {
        callbacks_.started();
    }
    timer_ = timers_.timeout(500, [this] {
        timer_ = 0;
        listening_ = true;
        if (file_ < files_.size()) {
            sendHeader();
        } else {
            sendEot();  // nothing to send: finish as after the last file
        }
    });
}

void YModemSender::sendHeader() {
    const YModemFile& file = files_[file_];
    const std::optional<std::string> header = ymodemHeaderPacket(file.name, file.data.size());
    if (!header) {
        fail("Couldn't send file. Either filename is too big or the file is extremely large.");
        return;
    }
    callbacks_.write(*header);
    waiting_ = Waiting::Header;
    timer_ = timers_.timeout(5000, [this] {
        timer_ = 0;
        fail("Timeout waiting for control characters: C, ACK, NAK");
    });
}

void YModemSender::receive(std::string_view bytes) {
    for (const char c : bytes) {
        if (!active_) {
            return;
        }
        if (waiting_ == Waiting::Header && (c == kYmodemCrc || c == kYmodemAck || c == kYmodemNak)) {
            timers_.clear(timer_);
            waiting_ = Waiting::Nothing;
            blocks_ = ymodemBlocks(files_[file_].data);
            packet_ = 1;
            timeouts_ = 0;
            if (blocks_.blocks.empty()) {
                sendEot();
            } else {
                sendPacket();
            }
        } else if (waiting_ == Waiting::Packet) {
            if (c == kYmodemAck) {
                timers_.clear(timer_);
                waiting_ = Waiting::Nothing;
                if (callbacks_.progress) {
                    const double share = static_cast<double>(packet_) / static_cast<double>(blocks_.blocks.size());
                    callbacks_.progress(static_cast<int>(std::ceil(share * 100)));
                }
                if (packet_ < blocks_.blocks.size()) {
                    ++packet_;
                    timeouts_ = 0;
                    sendPacket();
                } else {
                    sendEot();
                }
            } else if (c == kYmodemNak) {
                // Retransmitted without counting as a retry.
                timers_.clear(timer_);
                sendPacket();
            } else if (c == kYmodemCan) {
                fail("Operation cancelled by remote device.");
            }
        }
    }
}

void YModemSender::sendPacket() {
    const bool last = packet_ == blocks_.blocks.size();
    const char type = last && blocks_.lastSoh ? kYmodemSoh : kYmodemStx;
    callbacks_.write(ymodemDataPacket(type, static_cast<unsigned char>(packet_ % 256), blocks_.blocks[packet_ - 1]));
    waiting_ = Waiting::Packet;
    timer_ = timers_.timeout(3000, [this] {
        timer_ = 0;
        packetTimedOut();
    });
}

void YModemSender::packetTimedOut() {
    // Upstream gives up after the ninth attempt without an answer.
    if (++timeouts_ >= 9) {
        fail("Packet timed out after 10 retries.");
        return;
    }
    sendPacket();
}

void YModemSender::sendEot() {
    waiting_ = Waiting::Nothing;
    if (file_ < files_.size()) {
        callbacks_.write(std::string(1, kYmodemEot));
    }
    timer_ = timers_.timeout(200, [this] {
        timer_ = 0;
        if (++file_ < files_.size()) {
            sendHeader();
            return;
        }
        timer_ = timers_.timeout(100, [this] {
            timer_ = 0;
            active_ = false;
            listening_ = false;
            if (callbacks_.completed) {
                callbacks_.completed();
            }
        });
    });
}

void YModemSender::fail(const std::string& message) {
    timers_.clear(timer_);
    waiting_ = Waiting::Nothing;
    active_ = false;
    listening_ = false;
    if (callbacks_.failed) {
        callbacks_.failed(message);
    }
}

}  // namespace gs::protocol
