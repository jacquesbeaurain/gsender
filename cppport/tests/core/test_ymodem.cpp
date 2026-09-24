// YMODEM uploads to a grblHAL SD card: the packets, the sender's handshake
// and retries, and whole uploads to the simulated grblHAL board.

#include "gs/controller/session.hpp"
#include "gs/protocol/ymodem.hpp"
#include "gs/sim/grbl_simulator.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::protocol;

namespace {

std::uint16_t crcOf(const std::string& packet) {
    return static_cast<std::uint16_t>((static_cast<unsigned char>(packet[packet.size() - 2]) << 8) |
                                      static_cast<unsigned char>(packet[packet.size() - 1]));
}

}  // namespace

TEST(YModem, TheChecksumIsCrc16Xmodem) {
    EXPECT_EQ(crc16Xmodem("123456789"), 0x31C3);  // the catalogue's check value
    EXPECT_EQ(crc16Xmodem(""), 0);
}

TEST(YModem, TheHeaderCarriesTheNameAndSize) {
    const std::optional<std::string> header = ymodemHeaderPacket("job.nc", 1234);
    ASSERT_TRUE(header);
    ASSERT_EQ(header->size(), 133u);
    EXPECT_EQ((*header)[0], kYmodemSoh);
    EXPECT_EQ((*header)[1], '\0');
    EXPECT_EQ(static_cast<unsigned char>((*header)[2]), 0xFF);
    const std::string block = header->substr(3, 128);
    EXPECT_EQ(block.substr(0, 13), std::string("/job.nc\0" "1234\0", 13));
    EXPECT_TRUE(std::all_of(block.begin() + 13, block.end(), [](char c) { return c == '\0'; }));
    EXPECT_EQ(crcOf(*header), crc16Xmodem(block));

    // "/" + name + size must leave room for a NUL in the 128 bytes.
    EXPECT_TRUE(ymodemHeaderPacket(std::string(125, 'a'), 1));
    EXPECT_FALSE(ymodemHeaderPacket(std::string(126, 'a'), 1));
}

TEST(YModem, DataPacketsAreNumberedAndChecked) {
    const std::string block(1024, 'x');
    const std::string packet = ymodemDataPacket(kYmodemStx, 3, block);
    ASSERT_EQ(packet.size(), 1029u);
    EXPECT_EQ(packet[0], kYmodemStx);
    EXPECT_EQ(packet[1], 3);
    EXPECT_EQ(static_cast<unsigned char>(packet[2]), 0xFC);
    EXPECT_EQ(packet.substr(3, 1024), block);
    EXPECT_EQ(crcOf(packet), crc16Xmodem(block));
}

TEST(YModem, FilesAreCutAsUpstreamCutsThem) {
    EXPECT_TRUE(ymodemBlocks("").blocks.empty());

    const YModemBlocks small = ymodemBlocks("G0 X1\n");
    ASSERT_EQ(small.blocks.size(), 1u);
    EXPECT_TRUE(small.lastSoh);
    EXPECT_EQ(small.blocks[0], "G0 X1\n" + std::string(122, '\x1A'));

    const YModemBlocks medium = ymodemBlocks(std::string(200, 'm'));
    ASSERT_EQ(medium.blocks.size(), 1u);
    EXPECT_FALSE(medium.lastSoh);
    EXPECT_EQ(medium.blocks[0], std::string(200, 'm') + std::string(824, '\x1A'));

    // Longer: 1024-byte blocks, the last zero-padded - and never SOH.
    const YModemBlocks large = ymodemBlocks(std::string(1030, 'l'));
    ASSERT_EQ(large.blocks.size(), 2u);
    EXPECT_FALSE(large.lastSoh);
    EXPECT_EQ(large.blocks[1], std::string(6, 'l') + std::string(1018, '\0'));
}

namespace {

class YModemSenderTest : public ::testing::Test {
protected:
    YModemSenderTest()
        : sender(loop, YModemSender::Callbacks{
                           [this](std::string_view bytes) { written.emplace_back(bytes); },
                           [this] { ++started; },
                           [this](int percent) { progress.push_back(percent); },
                           [this] { ++completed; },
                           [this](const std::string& message) { failures.push_back(message); },
                       }) {}

    runtime::ManualEventLoop loop;
    std::vector<std::string> written;
    int started = 0;
    std::vector<int> progress;
    int completed = 0;
    std::vector<std::string> failures;
    YModemSender sender;
};

}  // namespace

TEST_F(YModemSenderTest, SendsEachFileAfterItsHeaderIsAnswered) {
    sender.start({{"a.nc", "G0 X1\n"}, {"b.nc", std::string(2100, 'b')}});
    EXPECT_EQ(started, 1);
    EXPECT_TRUE(sender.active());
    EXPECT_FALSE(sender.listening());  // half a second first
    loop.advance(499);
    EXPECT_TRUE(written.empty());
    loop.advance(1);
    EXPECT_TRUE(sender.listening());
    ASSERT_EQ(written.size(), 1u);
    EXPECT_EQ(written[0], *ymodemHeaderPacket("a.nc", 6));

    // The board acknowledges the header and asks for CRC packets.
    sender.receive(std::string{kYmodemAck, kYmodemCrc});
    ASSERT_EQ(written.size(), 2u);
    EXPECT_EQ(written[1], ymodemDataPacket(kYmodemSoh, 1, "G0 X1\n" + std::string(122, '\x1A')));
    sender.receive(std::string(1, kYmodemAck));
    EXPECT_EQ(progress, std::vector<int>{100});
    ASSERT_EQ(written.size(), 3u);
    EXPECT_EQ(written[2], std::string(1, kYmodemEot));

    // 200 ms later the next file.
    loop.advance(200);
    ASSERT_EQ(written.size(), 4u);
    EXPECT_EQ(written[3], *ymodemHeaderPacket("b.nc", 2100));
    sender.receive(std::string(1, kYmodemCrc));
    for (int packet = 1; packet <= 3; ++packet) {
        ASSERT_EQ(written.size(), 4u + static_cast<std::size_t>(packet));
        EXPECT_EQ(written.back()[0], kYmodemStx);
        EXPECT_EQ(written.back()[1], packet);
        sender.receive(std::string(1, kYmodemAck));
    }
    EXPECT_EQ(progress, (std::vector<int>{100, 34, 67, 100}));
    EXPECT_EQ(written.back(), std::string(1, kYmodemEot));
    loop.advance(299);
    EXPECT_EQ(completed, 0);
    loop.advance(1);
    EXPECT_EQ(completed, 1);
    EXPECT_FALSE(sender.active());
    EXPECT_FALSE(sender.listening());
    EXPECT_TRUE(failures.empty());
    EXPECT_EQ(loop.activeTimers(), 0u);
}

TEST_F(YModemSenderTest, ANakResendsThePacketAsOftenAsItComes) {
    sender.start({{"a.nc", std::string(300, 'a')}});
    loop.advance(500);
    sender.receive(std::string(1, kYmodemCrc));
    ASSERT_EQ(written.size(), 2u);
    for (int nak = 0; nak < 12; ++nak) {
        sender.receive(std::string(1, kYmodemNak));
    }
    ASSERT_EQ(written.size(), 14u);
    EXPECT_TRUE(std::all_of(written.begin() + 2, written.end(), [&](const std::string& p) { return p == written[1]; }));
    sender.receive(std::string(1, kYmodemAck));
    EXPECT_EQ(written.back(), std::string(1, kYmodemEot));
    EXPECT_TRUE(failures.empty());
}

TEST_F(YModemSenderTest, SilenceAndCancellationFailTheUpload) {
    // No answer to the header within 5 s.
    sender.start({{"a.nc", "x"}});
    loop.advance(500 + 4999);
    EXPECT_TRUE(failures.empty());
    loop.advance(1);
    EXPECT_EQ(failures, std::vector<std::string>{"Timeout waiting for control characters: C, ACK, NAK"});
    EXPECT_FALSE(sender.active());

    // A packet is sent nine times, 3 s apart, before giving up.
    failures.clear();
    written.clear();
    sender.start({{"a.nc", "x"}});
    loop.advance(500);
    sender.receive(std::string(1, kYmodemCrc));
    loop.advance(8 * 3000);
    EXPECT_EQ(written.size(), 1u + 9u);
    EXPECT_TRUE(failures.empty());
    loop.advance(3000);
    EXPECT_EQ(failures, std::vector<std::string>{"Packet timed out after 10 retries."});

    // The board cancels.
    failures.clear();
    sender.start({{"a.nc", "x"}});
    loop.advance(500);
    sender.receive(std::string{kYmodemCrc, kYmodemCan});
    EXPECT_EQ(failures, std::vector<std::string>{"Operation cancelled by remote device."});
    EXPECT_EQ(completed, 0);

    // A name the header cannot hold.
    failures.clear();
    sender.start({{std::string(130, 'n'), "x"}});
    loop.advance(500);
    EXPECT_EQ(failures,
              std::vector<std::string>{"Couldn't send file. Either filename is too big or the file is extremely large."});
    EXPECT_EQ(loop.activeTimers(), 0u);
}

// ---- the simulated grblHAL board ---------------------------------------------------------

namespace {

struct HalBoard {
    HalBoard() {
        sim.setGrblHal(true);
        sim.onData = [this](std::string_view bytes) { session.receive(bytes); };
        sim.open();
        session.opened();
        for (int i = 0; i < 200 && !(session.controller() && session.controller()->isReady() &&
                                     session.controller()->runner().hasSettings());
             ++i) {
            loop.advance(50);
        }
        loop.advance(1000);  // the start-up queries
    }

    template <typename Event>
    std::vector<Event> all() const {
        std::vector<Event> found;
        for (const controller::ControllerEvent& event : events) {
            if (const auto* e = std::get_if<Event>(&event)) {
                found.push_back(*e);
            }
        }
        return found;
    }

    runtime::ManualEventLoop loop;
    sim::GrblSimulator sim{loop};
    std::vector<controller::ControllerEvent> events;
    controller::Session session{loop, sim, {}, {}, [this](const controller::ControllerEvent& e) {
                                    events.push_back(e);
                                }};
};

}  // namespace

TEST(YModemEndToEnd, UploadsReachTheSimulatedCardAndAreListed) {
    HalBoard board;
    ASSERT_NE(board.session.controller(), nullptr);
    controller::Controller& c = *board.session.controller();
    ASSERT_TRUE(c.isGrblHal());
    // The complete report said a card is in: the start-up listed it.
    EXPECT_TRUE(c.state().status.sdCard);
    const auto& info = c.runner().settings().info;
    ASSERT_TRUE(info.contains("NEWOPT"));
    EXPECT_TRUE(info.at("NEWOPT").hasOption("YM"));
    EXPECT_FALSE(info.at("NEWOPT").hasOption("FTP"));

    std::string big;
    for (int i = 0; big.size() < 5000; ++i) {
        big += "G1 X" + std::to_string(i % 50) + " Y" + std::to_string(i % 30) + " F1000\n";
    }
    c.sdUpload({{"small.nc", "G0 X1\n"}, {"big.gcode", big}, {"notes.md", "not listed\n"}});
    for (int i = 0; i < 200 && board.all<controller::YModemCompleted>().empty() &&
                    board.all<controller::YModemFailed>().empty();
         ++i) {
        board.loop.advance(50);
    }
    ASSERT_TRUE(board.all<controller::YModemFailed>().empty()) << board.all<controller::YModemFailed>()[0].message;
    EXPECT_EQ(board.all<controller::YModemStarted>().size(), 1u);
    EXPECT_EQ(board.all<controller::YModemCompleted>().size(), 1u);
    EXPECT_FALSE(c.ymodemActive());
    ASSERT_EQ(board.sim.sdFiles().size(), 3u);
    EXPECT_EQ(board.sim.sdFiles().at("small.nc"), "G0 X1\n");
    EXPECT_EQ(board.sim.sdFiles().at("big.gcode"), big);  // padding cut at the size

    // 150 ms after the upload the card is listed again: CNC files only.
    board.loop.advance(1000);
    std::vector<std::string> names;
    for (const protocol::SdFile& file : c.state().sdcard.files) {
        names.push_back(file.name);
    }
    std::sort(names.begin(), names.end());
    EXPECT_EQ(names, (std::vector<std::string>{"big.gcode", "small.nc"}));

    // Status polling came back: the board still answers as lines.
    const std::size_t reports = board.all<controller::StateChanged>().size();
    c.writeln("G0 X5");
    board.loop.advance(1000);
    EXPECT_GT(board.all<controller::StateChanged>().size(), reports);
    EXPECT_EQ(c.state().status.activeState, "Idle");
}

TEST(YModemEndToEnd, SdFilesRunAndDelete) {
    HalBoard board;
    controller::Controller& c = *board.session.controller();
    board.sim.putSdFile("square.nc", "G21 G90\nG1 X10 F1200\nG1 Y10\nG1 X0\nG1 Y0\n");
    c.sdList();
    board.loop.advance(500);
    ASSERT_EQ(c.state().sdcard.files.size(), 1u);
    EXPECT_EQ(c.state().sdcard.files[0].size, 40);

    c.sdRun("square.nc");
    board.loop.advance(300);
    EXPECT_TRUE(board.sim.isRunningSdFile());
    EXPECT_EQ(c.state().status.activeState, "Run");
    ASSERT_TRUE(c.state().status.sdProgress.name);
    EXPECT_EQ(*c.state().status.sdProgress.name, "square.nc");
    EXPECT_GT(c.state().status.sdProgress.percentage, 0);
    // The run's lines are not answered: no errors, no stray acknowledgements.
    for (int i = 0; i < 100 && board.sim.isRunningSdFile(); ++i) {
        board.loop.advance(100);
    }
    EXPECT_FALSE(board.sim.isRunningSdFile());
    board.loop.advance(500);
    EXPECT_FALSE(c.state().status.sdProgress.name);
    EXPECT_EQ(c.state().status.activeState, "Idle");
    EXPECT_TRUE(board.all<controller::ErrorReported>().empty());

    c.sdDelete("square.nc");
    board.loop.advance(100);
    EXPECT_TRUE(board.sim.sdFiles().empty());
}

TEST(YModemEndToEnd, NoCardNoUpload) {
    HalBoard board;
    controller::Controller& c = *board.session.controller();
    // A complete report without the card (grblHAL sends one on 0x87).
    c.sdUpload({{"a.nc", "G0 X1\n"}});
    board.session.receive("<Idle|MPos:0.000,0.000,0.000|FS:0,0|WCO:0.000,0.000,0.000|FW:grblHAL>\n");
    board.loop.advance(1500);
    const auto failures = board.all<controller::YModemFailed>();
    ASSERT_EQ(failures.size(), 1u);
    EXPECT_EQ(failures[0].message,
              "SD Card not detected, please insert an SD Card in FAT32 format, 32 GB or under, and try again");
    EXPECT_TRUE(board.all<controller::YModemStarted>().empty());
    EXPECT_TRUE(board.sim.sdFiles().empty());
}
