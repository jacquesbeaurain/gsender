// Probe routines against golden output from gSender's own Probing.ts
// (tests/data/probing_golden.json, written by tools/gen_probe_fixtures.mjs).

#include "gs/probe/probing.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace gs::probe;
namespace json = boost::json;

namespace {

const json::array& goldenCases() {
    static const json::value data = [] {
        std::ifstream in(GS_TEST_DATA_DIR "/probing_golden.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return json::parse(text.str());
    }();
    return data.at("cases").as_array();
}

double number(const json::object& o, std::string_view key) {
    const json::value* v = o.if_contains(key);
    return v ? v->to_number<double>() : 0.0;  // missing keys are undefined, i.e. falsy
}

std::string text(const json::object& o, std::string_view key) {
    return std::string(o.at(key).as_string());
}

Axes axesFrom(const json::value& v) {
    const json::object& o = v.as_object();
    return {o.at("x").as_bool(), o.at("y").as_bool(), o.at("z").as_bool()};
}

PlateThickness thicknessFrom(const json::value& v) {
    const json::object& o = v.as_object();
    return {number(o, "standardBlock"), number(o, "autoZero"), number(o, "zProbe"),
            number(o, "probe3D"),       number(o, "bitZero"),  number(o, "bitZeroZOnly")};
}

ProbingOptions optionsFrom(const json::object& c) {
    const json::object& o = c.at("options").as_object();
    const json::object& machine = c.at("machine").as_object();
    ProbingOptions p;
    p.modal = text(o, "modal");
    p.metric = text(o, "units") == "mm";
    p.toolDiameter = number(o, "toolDiameter");
    p.tipDiameter3D = number(o, "tipDiameter3D");
    p.zRetractNormal = number(o, "zRetractNormal");
    p.zRetractAuto = number(o, "zRetractAuto");
    p.xyRetract3D = number(o, "xyRetract3D");
    p.retract = number(o, "retract");
    p.axes = axesFrom(o.at("axes"));
    const json::object& distances = o.at("probeDistances").as_object();
    p.probeDistanceX = number(distances, "x");
    p.probeDistanceY = number(distances, "y");
    p.probeDistanceZ = number(distances, "z");
    p.probeFast = number(o, "probeFast");
    p.probeSlow = number(o, "probeSlow");
    p.zThickness = thicknessFrom(o.at("zThickness"));
    p.xyThickness = number(o, "xyThickness");
    p.probeMovementSpeed = number(o, "probeMovementSpeed");
    p.probeMovementSpeedAuto = number(o, "probeMovementSpeedAuto");
    p.grblHal = text(o, "firmware") == "grblHAL";
    p.reportInches = text(o, "$13") == "1";
    p.plateType = *plateTypeFromName(text(o, "plateType"));
    p.probeType = *probeTypeFromName(text(o, "probeType"));
    p.homingEnabled = o.at("homingEnabled").as_bool();
    p.zMaxTravel = std::stod(text(machine, "$132"));
    p.machineZ = number(machine, "mposZ");
    return p;
}

std::vector<std::string> lines(const json::value& v) {
    std::vector<std::string> out;
    for (const json::value& line : v.as_array()) {
        out.emplace_back(line.as_string());
    }
    return out;
}

// Reports the first differing line rather than two 50-line blobs.
void expectSameCode(const std::vector<std::string>& actual, const std::vector<std::string>& expected,
                    const std::string& name) {
    const std::size_t common = std::min(actual.size(), expected.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (actual[i] != expected[i]) {
            ADD_FAILURE() << name << " line " << i << ":\n  actual:   " << actual[i] << "\n  expected: " << expected[i];
            return;
        }
    }
    EXPECT_EQ(actual.size(), expected.size()) << name;
}

TEST(Probing, MatchesUpstreamForEveryGoldenCase) {
    const json::array& cases = goldenCases();
    ASSERT_GE(cases.size(), 100u);
    for (const json::value& value : cases) {
        const json::object& c = value.as_object();
        const int direction = static_cast<int>(c.at("direction").as_int64());
        expectSameCode(probeCode(optionsFrom(c), direction), lines(c.at("code")), text(c, "name"));
    }
}

TEST(Probing, BuildsTheWidgetsOptionsFromStoredSettings) {
    for (const json::value& value : goldenCases()) {
        const json::object& c = value.as_object();
        const json::object& s = c.at("settings").as_object();
        const json::object& profile = s.at("profile").as_object();
        ProbeSettings settings;
        settings.plateType = *plateTypeFromName(text(profile, "touchplateType"));
        settings.zThickness = thicknessFrom(profile.at("zThickness"));
        settings.xyThickness = number(profile, "xyThickness");
        settings.probeFeedrate = number(s, "probeFeedrate");
        settings.probeFastFeedrate = number(s, "probeFastFeedrate");
        settings.retractionDistance = number(s, "retractionDistance");
        settings.zRetractNormal = number(s, "zRetractNormal");
        settings.zRetractAuto = number(s, "zRetractAuto");
        settings.zProbeDistance = number(s, "zProbeDistance");
        settings.tipDiameter3D = number(s, "tipDiameter3D");
        settings.xyRetract3D = number(s, "xyRetract3D");
        settings.probeMovementSpeed = number(s, "probeMovementSpeed");
        const json::object& machineJson = c.at("machine").as_object();
        MachineFacts machine;
        machine.grblHal = text(s, "firmware") == "grblHAL";
        machine.reportInches = text(s, "$13");
        machine.homing = text(s, "$22");
        machine.zMaxTravel = text(machineJson, "$132");
        machine.machineZ = number(machineJson, "mposZ");

        const ProbingOptions built =
            makeProbingOptions(settings, text(s, "units") == "mm", axesFrom(s.at("axes")),
                               *probeTypeFromName(text(s, "probeType")), number(s, "toolDiameter"), machine);
        const ProbingOptions expected = optionsFrom(c);
        const std::string name = text(c, "name");
        EXPECT_EQ(built.modal, expected.modal) << name;
        EXPECT_EQ(built.probeDistanceZ, expected.probeDistanceZ) << name;
        EXPECT_EQ(built.xyThickness, expected.xyThickness) << name;
        EXPECT_EQ(built.zThickness.standardBlock, expected.zThickness.standardBlock) << name;
        EXPECT_EQ(built.zThickness.bitZero, expected.zThickness.bitZero) << name;
        EXPECT_EQ(built.retract, expected.retract) << name;
        EXPECT_EQ(built.probeMovementSpeed, expected.probeMovementSpeed) << name;
        EXPECT_EQ(built.homingEnabled, expected.homingEnabled) << name;
        const int direction = static_cast<int>(c.at("direction").as_int64());
        expectSameCode(probeCode(built, direction), lines(c.at("code")), name);
    }
}

TEST(Probing, CornersCycleClockwise) {
    EXPECT_EQ(nextCorner(kBottomLeft), kTopLeft);
    EXPECT_EQ(nextCorner(kTopLeft), kTopRight);
    EXPECT_EQ(nextCorner(kTopRight), kBottomRight);
    EXPECT_EQ(nextCorner(kBottomRight), kBottomLeft);
}

TEST(Probing, OffersOnlyZForAZProbe) {
    ASSERT_EQ(probeCommands(PlateType::ZProbe).size(), 1u);
    EXPECT_EQ(probeCommands(PlateType::ZProbe)[0].id, "Z Touch");
    const std::vector<ProbeCommand> block = probeCommands(PlateType::StandardBlock);
    ASSERT_EQ(block.size(), 5u);
    EXPECT_EQ(block[1].id, "XYZ Touch");
    EXPECT_TRUE(block[1].needsTool);
    EXPECT_FALSE(probeCommands(PlateType::Probe3D)[1].needsTool);  // the tip diameter is a setting
}

TEST(Probing, NoAxesMeansNoCode) {
    ProbingOptions options;
    EXPECT_TRUE(probeCode(options, kBottomLeft).empty());
    options.plateType = PlateType::AutoZero;
    options.probeType = ProbeType::Auto;
    EXPECT_TRUE(probeCode(options, kBottomLeft).empty());
}

TEST(Probing, NamesRoundTrip) {
    for (const PlateType type : {PlateType::StandardBlock, PlateType::AutoZero, PlateType::ZProbe,
                                 PlateType::Probe3D, PlateType::BitZero}) {
        EXPECT_EQ(plateTypeFromName(plateTypeName(type)), type);
    }
    EXPECT_EQ(plateTypeFromName("Touch Plate"), std::nullopt);
    EXPECT_EQ(probeTypeFromName("Tip"), ProbeType::Tip);
}

}  // namespace
