// Rectangular grid capture (the feature/probe-mesh-capture branch's
// useMeshCapture.ts): its CSV tests, ported, and the run's lines against a
// scripted board.

#include "gs/probe/grid_capture.hpp"

#include <gtest/gtest.h>

#include <string>
#include <vector>

using namespace gs::probe;

namespace {

// Probed in a serpentine, so the second row arrives right to left.
const std::vector<CapturedPoint> kPoints = {
    {0, 0, 0, 0, -1.5},
    {1, 0, 10, 0, -1.25},
    {1, 1, 10, 10, -2},
    {0, 1, 0, 10, -1.75},
};

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    for (std::size_t end; (end = text.find('\n', start)) != std::string::npos; start = end + 1) {
        lines.push_back(text.substr(start, end - start));
    }
    lines.push_back(text.substr(start));
    return lines;
}

// A capture whose sends are kept, one batch per send.
struct Harness {
    std::vector<std::vector<std::string>> sent;
    GridCapture capture{[this](std::vector<std::string> lines) { sent.push_back(std::move(lines)); }};

    static CaptureSetup setup() {
        CaptureSetup s;
        s.work = {5, 5, 20};
        s.machine = {105, 205, -10};  // work offset (100, 200, -30)
        s.feedrate = 150;
        s.probeDistance = 30;
        return s;
    }
};

}  // namespace

TEST(GridCaptureCsv, WritesAHeaderAndOneRowPerPoint) {
    const auto lines = split(pointsToCsv(kPoints, true));
    EXPECT_EQ(lines[0], "X,Y,Z");
    EXPECT_EQ(lines.size(), kPoints.size() + 1);
}

TEST(GridCaptureCsv, EmitsGridOrderRegardlessOfTheOrderProbed) {
    EXPECT_EQ(pointsToCsv(kPoints, true),
              "X,Y,Z\n0.000,0.000,-1.500\n10.000,0.000,-1.250\n0.000,10.000,-1.750\n10.000,10.000,-2.000");
}

TEST(GridCaptureCsv, WritesInchesWithAnExtraDecimalPlace) {
    const auto lines = split(pointsToCsv({{0, 0, 25.4, 0, -1.27}}, false));
    EXPECT_EQ(lines[1], "1.0000,0.0000,-0.0500");
}

TEST(GridCaptureCsv, HandlesAnEmptyCapture) {
    EXPECT_EQ(pointsToCsv({}, true), "X,Y,Z");
}

TEST(GridCapture, ParsesProbeReports) {
    auto r = parseProbeReport("[PRB:1.000,-2.500,-10.125:1]");
    ASSERT_TRUE(r);
    EXPECT_DOUBLE_EQ(r->position[0], 1);
    EXPECT_DOUBLE_EQ(r->position[1], -2.5);
    EXPECT_DOUBLE_EQ(r->position[2], -10.125);
    EXPECT_TRUE(r->contact);
    r = parseProbeReport("[PRB:0.000,0.000,-40.000,12.000:0]\r");  // grblHAL with A
    ASSERT_TRUE(r);
    EXPECT_FALSE(r->contact);
    EXPECT_FALSE(parseProbeReport("[TLO:0.000]"));
    EXPECT_FALSE(parseProbeReport("ok"));
}

TEST(GridCapture, VisitsTheGridInASerpentine) {
    const std::vector<std::pair<int, int>> expected = {{0, 0}, {1, 0}, {2, 0}, {2, 1}, {1, 1}, {0, 1}};
    EXPECT_EQ(serpentine(3, 2), expected);
    EXPECT_TRUE(serpentine(0, 3).empty());
}

TEST(GridCapture, ProbesEachPointAndRetractsAtTheEnd) {
    Harness h;
    ASSERT_TRUE(h.capture.startGrid(Harness::setup(), {10, 2, 20, 2}));
    // Each batch ends at the probe.
    ASSERT_EQ(h.sent.size(), 1u);
    EXPECT_EQ(h.sent[0], (std::vector<std::string>{"G21 G90 G0 Z20.000", "G21 G90 G0 X5.000 Y5.000",
                                                   "G21 G90 G38.2 Z-10.000 F150.000"}));
    EXPECT_FALSE(h.capture.onLine("ok"));
    // Machine coordinates back to work ones.
    EXPECT_TRUE(h.capture.onLine("[PRB:105.000,205.000,-35.000:1]"));
    ASSERT_EQ(h.sent.size(), 2u);
    EXPECT_EQ(h.sent[1][1], "G21 G90 G0 X15.000 Y5.000");
    h.capture.onLine("[PRB:115.000,205.000,-35.500:1]");
    // The second row runs back from +X.
    EXPECT_EQ(h.sent[2][1], "G21 G90 G0 X15.000 Y25.000");
    h.capture.onLine("[PRB:115.000,225.000,-36.000:1]");
    EXPECT_EQ(h.sent[3][1], "G21 G90 G0 X5.000 Y25.000");
    h.capture.onLine("[PRB:105.000,225.000,-36.500:1]");
    EXPECT_EQ(h.capture.status(), GridCapture::Status::Done);
    ASSERT_EQ(h.sent.size(), 5u);
    EXPECT_EQ(h.sent[4], std::vector<std::string>{"G21 G90 G0 Z20.000"});
    EXPECT_EQ(h.capture.captured(), 4);
    EXPECT_EQ(pointsToCsv(h.capture.points(), true),
              "X,Y,Z\n5.000,5.000,-5.000\n15.000,5.000,-5.500\n5.000,25.000,-6.500\n15.000,25.000,-6.000");
}

TEST(GridCapture, WritesInchWorkspacesAndReadsInchReports) {
    Harness h;
    CaptureSetup setup = Harness::setup();
    setup.inches = true;
    setup.reportInches = true;
    setup.work = {0, 0, 25.4};
    setup.machine = {0, 0, 0};
    ASSERT_TRUE(h.capture.startGrid(setup, {25.4, 1, 25.4, 1}));
    EXPECT_EQ(h.sent[0][0], "G20 G90 G0 Z1.0000");
    EXPECT_EQ(h.sent[0][2], "G20 G90 G38.2 Z-0.1811 F5.9055");
    h.capture.onLine("[PRB:0.0000,0.0000,-0.5000:1]");
    ASSERT_EQ(h.capture.points().size(), 1u);
    EXPECT_DOUBLE_EQ(h.capture.points()[0].z, 25.4 - 12.7);
}

TEST(GridCapture, StopsGracefullyAfterTheProbeInFlight) {
    Harness h;
    ASSERT_TRUE(h.capture.startGrid(Harness::setup(), {10, 4, 10, 4}));
    h.capture.onLine("[PRB:105.000,205.000,-35.000:1]");
    h.capture.stop();
    EXPECT_TRUE(h.capture.running());
    h.capture.onLine("[PRB:115.000,205.000,-35.000:1]");
    EXPECT_EQ(h.capture.status(), GridCapture::Status::Stopped);
    EXPECT_EQ(h.capture.captured(), 2);
    EXPECT_EQ(h.capture.total(), 16);
    EXPECT_EQ(h.sent.back(), std::vector<std::string>{"G21 G90 G0 Z20.000"});
    EXPECT_EQ(h.capture.points().size(), 2u);
}

TEST(GridCapture, FailsOnAMissOrAnAlarmWithoutSendingMore) {
    Harness h;
    ASSERT_TRUE(h.capture.startGrid(Harness::setup(), {10, 2, 10, 1}));
    h.capture.onLine("[PRB:105.000,205.000,-60.000:0]");
    EXPECT_EQ(h.capture.status(), GridCapture::Status::Failed);
    EXPECT_EQ(h.capture.error(), "No contact at X5.000 Y5.000 - nothing within 30.000 below the start height");
    EXPECT_EQ(h.sent.size(), 1u);

    ASSERT_TRUE(h.capture.startGrid(Harness::setup(), {10, 2, 10, 1}));
    h.capture.onLine("ALARM:5\r\n");
    EXPECT_EQ(h.capture.status(), GridCapture::Status::Failed);
    EXPECT_EQ(h.capture.error(), "ALARM:5");
    EXPECT_EQ(h.sent.size(), 2u);
    h.capture.onLine("[PRB:105.000,205.000,-35.000:1]");  // not running: ignored
    EXPECT_TRUE(h.capture.points().empty());
}

TEST(GridCapture, ANewGridReplacesTheLastAndClearForgetsIt) {
    Harness h;
    for (int run = 0; run < 2; ++run) {
        ASSERT_TRUE(h.capture.startGrid(Harness::setup(), {10, 1, 10, 1}));
        EXPECT_FALSE(h.capture.startGrid(Harness::setup(), {10, 2, 10, 2}));  // one run at a time
        h.capture.onLine("[PRB:105.000,205.000,-35.000:1]");
    }
    EXPECT_EQ(h.capture.points().size(), 1u);
    h.capture.clear();
    EXPECT_TRUE(h.capture.points().empty());
}
