#pragma once

// YMODEM uploads to a grblHAL SD card (server/lib/YModemUSB.js, sendFiles):
// per file a header packet (its "/name" and size), data packets of 128 or
// 1024 bytes with CRC-16/XMODEM - resent on NAK and after 3 s without an
// answer, cancelled by CAN - then EOT. grblHAL's protocol layer takes the
// header's SOH as the start of an upload; there is no command for it.

#include "gs/runtime/event_loop.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::protocol {

inline constexpr char kYmodemSoh = 0x01;  // a 128-byte packet
inline constexpr char kYmodemStx = 0x02;  // a 1024-byte packet
inline constexpr char kYmodemEot = 0x04;
inline constexpr char kYmodemAck = 0x06;
inline constexpr char kYmodemNak = 0x15;
inline constexpr char kYmodemCan = 0x18;
inline constexpr char kYmodemCrc = 'C';

// CRC-16/XMODEM (polynomial 0x1021, initial 0, not reflected).
std::uint16_t crc16Xmodem(std::string_view data);

// createHeaderPacket(): block 0 of 128 bytes - "/name", NUL, the size in
// decimal, NUL, zeros - framed as SOH 00 FF <block> <CRC, big-endian>.
// nullopt when the name and size do not fit in the block.
std::optional<std::string> ymodemHeaderPacket(std::string_view name, std::size_t size);
// createDataPacket(): <type> <seq> <0xFF - seq> <block> <CRC of the block>.
std::string ymodemDataPacket(char type, unsigned char seq, std::string_view block);

struct YModemBlocks {
    std::vector<std::string> blocks;  // padded to their packet size
    bool lastSoh = false;             // the last block is 128 bytes
};
// The file as the sender cuts it: up to 128 bytes one SOH block padded with
// 0x1A, up to 1024 one STX block padded with 0x1A, longer files 1024-byte
// STX blocks, the last padded with 0x00. (Upstream means a short last block
// to go as SOH, but its test reads the whole buffer's size, so it never
// does.) Empty files have no blocks.
YModemBlocks ymodemBlocks(std::string_view data);

struct YModemFile {
    std::string name;
    std::string data;
};

class YModemSender {
public:
    struct Callbacks {
        std::function<void(std::string_view bytes)> write;
        std::function<void()> started;
        std::function<void(int percent)> progress;  // of the file being sent
        std::function<void()> completed;
        std::function<void(const std::string& message)> failed;
    };

    YModemSender(runtime::EventLoop& loop, Callbacks callbacks);

    // sendFiles(): half a second, then per file its header - answered by C,
    // ACK or NAK within 5 s - its packets, EOT and 200 ms; done 100 ms after
    // the last.
    void start(std::vector<YModemFile> files);
    // The board's answers while the transfer runs.
    void receive(std::string_view bytes);
    // From start() until completed or failed.
    bool active() const noexcept { return active_; }
    // From the first header on: the board's bytes are the transfer's, not
    // lines (upstream swaps its line reader for a byte reader then).
    bool listening() const noexcept { return listening_; }

private:
    enum class Waiting { Nothing, Header, Packet };

    void sendHeader();
    void sendPacket();
    void packetTimedOut();
    void sendEot();
    void fail(const std::string& message);

    runtime::TimerScope timers_;
    Callbacks callbacks_;
    std::vector<YModemFile> files_;
    std::size_t file_ = 0;
    YModemBlocks blocks_;
    std::size_t packet_ = 0;  // 1-based
    int timeouts_ = 0;        // of the packet being sent
    Waiting waiting_ = Waiting::Nothing;
    runtime::TimerId timer_ = 0;
    bool active_ = false;
    bool listening_ = false;
};

}  // namespace gs::protocol
