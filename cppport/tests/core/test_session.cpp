// Port of src/server/lib/__tests__/ConnectionFirmwareDetect.test.js, plus
// line framing and the hand-over to the controller.

#include "gs/controller/session.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::controller;
using protocol::Firmware;

namespace {

class FakeLink final : public DeviceLink {
public:
    bool open = true;
    std::vector<std::pair<std::string, SendKind>> sends;

    bool isOpen() const override { return open; }
    void send(std::string_view bytes, SendKind kind) override { sends.emplace_back(std::string(bytes), kind); }
};

class SessionTest : public ::testing::Test {
protected:
    void make(Firmware defaultFirmware = Firmware::Grbl) {
        session = std::make_unique<Session>(loop, link, SessionOptions{defaultFirmware}, ControllerHooks{},
                                            [this](const ControllerEvent& e) { events.push_back(e); });
        session->onLine = [this](std::string_view line) { lines.emplace_back(line); };
        session->onController = [this](Controller& controller, bool detected) {
            found.push_back(controller.firmware());
            detections.push_back(detected);
        };
    }

    void SetUp() override { make(); }

    // Detection queries only: the controller's own $I is a normal write.
    int queries() const {
        return static_cast<int>(std::count_if(link.sends.begin(), link.sends.end(), [](const auto& send) {
            return send.second == SendKind::Immediate && send.first == "$I\n";
        }));
    }

    std::vector<std::string> console() const {
        std::vector<std::string> out;
        for (const ControllerEvent& e : events) {
            if (const auto* output = std::get_if<ConsoleOutput>(&e)) {
                out.push_back(output->text);
            }
        }
        return out;
    }

    runtime::ManualEventLoop loop;
    FakeLink link;
    std::vector<ControllerEvent> events;
    std::vector<std::string> lines;
    std::vector<Firmware> found;
    std::vector<bool> detections;
    std::unique_ptr<Session> session;
};

TEST_F(SessionTest, SendsTheFirstQueryImmediately) {
    session->opened();
    EXPECT_EQ(queries(), 1);
    EXPECT_TRUE(session->isDetecting());
}

TEST_F(SessionTest, SendsExactlyMaxAttemptsQueriesThenFallsBackOnce) {
    session->opened();
    loop.advance(kFirmwareDetectIntervalMs * (kFirmwareDetectMaxAttempts + 20));
    EXPECT_EQ(queries(), kFirmwareDetectMaxAttempts);
    EXPECT_EQ(found, std::vector<Firmware>{Firmware::Grbl});
    EXPECT_EQ(detections, std::vector<bool>{false});
    EXPECT_EQ(session->firmware(), Firmware::Grbl);
    ASSERT_NE(session->controller(), nullptr);
    EXPECT_FALSE(session->isDetecting());
}

TEST_F(SessionTest, TheDefaultFirmwareIsConfigurable) {
    make(Firmware::GrblHal);
    session->opened();
    loop.advance(kFirmwareDetectIntervalMs * (kFirmwareDetectMaxAttempts + 1));
    EXPECT_EQ(found, std::vector<Firmware>{Firmware::GrblHal});
}

TEST_F(SessionTest, NoPointlessQueryOnTheTickItGivesUp) {
    session->opened();
    loop.advance(kFirmwareDetectIntervalMs * (kFirmwareDetectMaxAttempts - 1));
    EXPECT_EQ(queries(), kFirmwareDetectMaxAttempts);
    loop.advance(kFirmwareDetectIntervalMs);
    EXPECT_EQ(queries(), kFirmwareDetectMaxAttempts);
    EXPECT_EQ(found.size(), 1u);
}

TEST_F(SessionTest, PollingStopsOnceTheFirmwareIdentifiesItself) {
    session->opened();
    session->receive("[VER:1.1f.20230131:]\r\n");
    session->receive("[OPT:VNMSL,35,1024,3,0]\r\n");
    session->receive("[FIRMWARE:grblHAL]\r\n");
    EXPECT_EQ(found, std::vector<Firmware>{Firmware::GrblHal});
    EXPECT_EQ(detections, std::vector<bool>{true});
    loop.advance(kFirmwareDetectIntervalMs * 20);
    EXPECT_EQ(queries(), 1);
}

TEST_F(SessionTest, PlainGrblIsIdentifiedWithoutTheDefault) {
    make(Firmware::GrblHal);
    session->opened();
    session->receive("Grbl 1.1f ['$' for help]\r\n");
    EXPECT_EQ(found, std::vector<Firmware>{Firmware::Grbl});
}

TEST_F(SessionTest, FluidNcIsTreatedAsGrbl) {
    session->opened();
    session->receive("Grbl 3.7 [FluidNC v3.7.8 (wifi) '$' for help]\n");
    EXPECT_EQ(found, std::vector<Firmware>{Firmware::Grbl});
}

TEST_F(SessionTest, DetectionStopsWhenThePortGoesAway) {
    session->opened();
    link.open = false;  // unplugged mid-detection
    loop.advance(kFirmwareDetectIntervalMs * 20);
    EXPECT_EQ(queries(), 1);
    EXPECT_FALSE(session->isDetecting());
    EXPECT_TRUE(found.empty());
}

TEST_F(SessionTest, ReopeningStartsDetectionWithAFreshBudget) {
    session->opened();
    loop.advance(kFirmwareDetectIntervalMs * (kFirmwareDetectMaxAttempts + 5));
    EXPECT_EQ(session->detectionAttempts(), kFirmwareDetectMaxAttempts);

    session->closed();
    EXPECT_EQ(session->controller(), nullptr);
    EXPECT_FALSE(session->firmware().has_value());
    link.sends.clear();
    session->opened();
    loop.advance(kFirmwareDetectIntervalMs * (kFirmwareDetectMaxAttempts + 5));
    EXPECT_EQ(queries(), kFirmwareDetectMaxAttempts);
}

TEST_F(SessionTest, ClosingClearsTheBudgetAndTheTimer) {
    session->opened();
    session->closed();
    EXPECT_EQ(session->detectionAttempts(), 0);
    EXPECT_FALSE(session->isDetecting());
    loop.advance(kFirmwareDetectIntervalMs * 20);
    EXPECT_EQ(queries(), 1);
}

TEST_F(SessionTest, ClosingClosesTheController) {
    session->opened();
    session->receive("Grbl 1.1f ['$' for help]\n");
    ASSERT_NE(session->controller(), nullptr);
    session->closed();
    EXPECT_EQ(session->controller(), nullptr);
    EXPECT_TRUE(std::any_of(events.begin(), events.end(),
                            [](const ControllerEvent& e) { return std::holds_alternative<ControllerClosed>(e); }));
}

TEST_F(SessionTest, LinesAreFramedAcrossChunksAndReachTheController) {
    session->opened();
    session->receive("Grbl 1.1f ['$' for help]\r\n<Idle|MPos:0.000,0");
    ASSERT_NE(session->controller(), nullptr);
    session->receive(".000,0.000|FS:0,0>\r");
    EXPECT_EQ(session->controller()->state().status.activeState, "");  // no newline yet
    session->receive("\n[MSG:Hello]\n");
    EXPECT_EQ(session->controller()->state().status.activeState, "Idle");
    const auto output = console();
    EXPECT_TRUE(std::find(output.begin(), output.end(), "[MSG:Hello]") != output.end());
    EXPECT_EQ(lines, (std::vector<std::string>{"Grbl 1.1f ['$' for help]\r", "<Idle|MPos:0.000,0.000,0.000|FS:0,0>\r",
                                               "[MSG:Hello]"}));
}

TEST_F(SessionTest, TheIdentifyingLineIsNotPassedToTheController) {
    session->opened();
    session->receive("[FIRMWARE:grblHAL]\n[MSG:after]\n");
    const auto output = console();
    EXPECT_TRUE(std::find(output.begin(), output.end(), "[FIRMWARE:grblHAL]") == output.end());
    EXPECT_TRUE(std::find(output.begin(), output.end(), "[MSG:after]") != output.end());
}

TEST_F(SessionTest, TheNewControllerIsOpened) {
    session->opened();
    session->receive("[FIRMWARE:grblHAL]\n");
    link.sends.clear();
    loop.advance(500);
    // grblHAL's open(): a complete status report request, then $I.
    std::vector<std::string> writes;
    for (const auto& [bytes, kind] : link.sends) {
        if (kind == SendKind::Write) {
            writes.push_back(bytes);
        }
    }
    EXPECT_EQ(writes, (std::vector<std::string>{"\x87", "$I\n"}));
}

}  // namespace
