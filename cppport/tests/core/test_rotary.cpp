// Rotary support (features/Rotary, lib/rotary.tsx): the rotary surfacing
// generator against golden output from gSender's own
// (tests/data/rotary_surfacing_golden.json, tools/gen_rotary_fixtures.mjs),
// upstream's rotary-surfacing-output.test.ts, the probing routines, the mode
// switch and the mounting programs.

#include "gs/rotary/rotary.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::rotary;
namespace json = boost::json;

namespace {

const json::array& goldenCases() {
    static const json::value data = [] {
        std::ifstream in(GS_TEST_DATA_DIR "/rotary_surfacing_golden.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return json::parse(text.str());
    }();
    return data.at("cases").as_array();
}

StockTurningOptions optionsFrom(const json::object& o) {
    StockTurningOptions s;
    s.stockLength = o.at("stockLength").to_number<double>();
    s.stepdown = o.at("stepdown").to_number<double>();
    s.bitDiameter = o.at("bitDiameter").to_number<double>();
    s.spindleRPM = o.at("spindleRPM").to_number<double>();
    s.feedrate = o.at("feedrate").to_number<double>();
    s.stepover = o.at("stepover").to_number<double>();
    s.startHeight = o.at("startHeight").to_number<double>();
    s.finalHeight = o.at("finalHeight").to_number<double>();
    s.enableRehoming = o.at("enableRehoming").as_bool();
    s.shouldDwell = o.at("shouldDwell").as_bool();
    s.toolNumber = static_cast<int>(o.at("toolNumber").to_number<double>());
    return s;
}

std::vector<std::string> split(const std::string& text) {
    std::vector<std::string> lines;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) {
        lines.push_back(line);
    }
    if (!text.empty() && text.back() == '\n') {
        lines.emplace_back();
    }
    return lines;
}

}  // namespace

TEST(RotarySurfacing, MatchesUpstreamForEveryGoldenCase) {
    const json::array& cases = goldenCases();
    ASSERT_GE(cases.size(), 40u);
    for (const json::value& value : cases) {
        const json::object& c = value.as_object();
        const std::string name(c.at("name").as_string());
        const std::vector<std::string> actual =
            split(stockTurningProgram(optionsFrom(c.at("options").as_object()), c.at("units").as_string() == "mm",
                                      c.at("mode").as_string() == "ROTARY"));
        const json::array& expected = c.at("lines").as_array();
        ASSERT_EQ(actual.size(), expected.size()) << name;
        for (std::size_t i = 0; i < expected.size(); ++i) {
            ASSERT_EQ(actual[i], expected[i].as_string()) << name << " line " << i;
        }
    }
}

// rotary-surfacing-output.test.ts
TEST(RotarySurfacing, ToolChangeComesJustBeforeTheSpindle) {
    StockTurningOptions options;
    std::vector<std::string> lines = split(stockTurningProgram(options, true, false));
    EXPECT_TRUE(std::none_of(lines.begin(), lines.end(), [](const std::string& l) { return l.starts_with("M6"); }));
    options.toolNumber = 3;
    lines = split(stockTurningProgram(options, true, false));
    const auto tool = std::find(lines.begin(), lines.end(), "M6 T3");
    const auto spindle = std::find(lines.begin(), lines.end(), "M3 S17000");
    ASSERT_NE(tool, lines.end());
    ASSERT_NE(spindle, lines.end());
    EXPECT_EQ(tool + 1, spindle);
}

TEST(RotarySurfacing, AStepdownThatIsNotPositiveCutsOneLayer) {
    StockTurningOptions options;
    options.stepdown = 0;
    options.startHeight = 60;
    const std::vector<std::string> lines = split(stockTurningProgram(options, true, false));
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "(*** Layer 1 ***)"), 1);
    EXPECT_EQ(std::count(lines.begin(), lines.end(), "(*** Layer 2 ***)"), 0);
}

TEST(RotaryProbing, RoutinesUseTheReportUnits) {
    const std::vector<std::string> mm = zAxisProbing(false);
    EXPECT_EQ(mm[0], "%PROBE_FAST_FEEDRATE = 150");
    EXPECT_EQ(mm[7], "G21");
    EXPECT_EQ(mm.back(), "G90");
    const std::vector<std::string> inches = zAxisProbing(true);
    EXPECT_EQ(inches[0], "%PROBE_FAST_FEEDRATE = 5.906");
    EXPECT_EQ(inches[3], "%Z_AXIS_LARGE_MOVEMENT = 1.535");
    EXPECT_EQ(inches[7], "G20");
    const std::vector<std::string> y = yAxisAlignmentProbing(true);
    EXPECT_EQ(y[4], "%PROBE_HEIGHT = -0.118");
    EXPECT_NE(std::find(y.begin(), y.end(), "%Y_SEEK_CENTERDIST = [REAR_SEEK_POS - FRONT_SEEK_POS]"), y.end());
    EXPECT_NE(std::find(y.begin(), y.end(), "G10 L20 Y0"), y.end());
}

TEST(RotaryMode, GrblRewritesYForTheRotaryAndBack) {
    protocol::OrderedMap settings;
    settings.set("$101", "200.000");
    settings.set("$111", "4000.000");
    settings.set("$20", "1");
    settings.set("$21", "1");
    const FirmwareValues saved = currentFirmwareValues(settings);
    EXPECT_EQ(saved, (FirmwareValues{{"$101", "200.000"}, {"$111", "4000.000"}, {"$20", "1"}, {"$21", "1"}}));

    ModeSwitch enter{true, false, rotaryFirmwareSettings()};
    EXPECT_EQ(modeSwitchCommands(enter, settings),
              (std::vector<std::string>{"G10 L20 P1 Y0", "$101=19.75308642", "$111=8000.00", "$20=0", "$21=0", "$$",
                                        "G04 P0.5", "G0 G90 Y[posy]"}));
    ModeSwitch leave{false, false, saved};
    EXPECT_EQ(modeSwitchCommands(leave, settings),
              (std::vector<std::string>{"$101=200.000", "$111=4000.000", "$20=1", "$21=1", "$$", "G04 P0.5",
                                        "G0 G90 Y[posy]"}));
}

TEST(RotaryMode, GrblHalSwapsTheAAndYSettings) {
    protocol::OrderedMap settings;
    for (const auto& [key, value] : std::vector<std::pair<std::string, std::string>>{
             {"$101", "200"}, {"$103", "19.753"}, {"$111", "4000"}, {"$113", "8000"}, {"$121", "750"},
             {"$123", "500"}, {"$131", "800"}, {"$133", "360"}}) {
        settings.set(key, value);
    }
    const std::vector<std::string> commands = modeSwitchCommands({true, true, {}}, settings);
    EXPECT_EQ(commands,
              (std::vector<std::string>{"G10 L20 P1 Y0", "$103=200", "$113=4000", "$123=750", "$133=800", "$101=19.753",
                                        "$111=8000", "$121=500", "$131=360", "$$", "G04 P0.5", "G0 G90 Y[posy]"}));
    // A board without A settings: nothing swapped (upstream wrote "undefined").
    protocol::OrderedMap threeAxis;
    threeAxis.set("$101", "200");
    EXPECT_EQ(modeSwitchCommands({true, true, {}}, threeAxis),
              (std::vector<std::string>{"G10 L20 P1 Y0", "$$", "G04 P0.5", "G0 G90 Y[posy]"}));
}

TEST(RotaryMounting, ProgramsFollowTheTrackAndBit) {
    EXPECT_EQ(mountingProgramName({false, true, 10, true}), "DOESNT_LINE_UP_QUARTER");
    EXPECT_EQ(mountingProgramName({false, false, 6, false}), "DOESNT_LINE_UP_EIGHTH");
    EXPECT_EQ(mountingProgramName({true, true, 6, true}), "QUARTER_INCH_SIX_HOLES");
    EXPECT_EQ(mountingProgramName({true, false, 6, false}), "EIGHTH_INCH_SIX_HOLES");
    EXPECT_EQ(mountingProgramName({true, true, 10, true}), "QUARTER_INCH_TEN_HOLES");
    EXPECT_EQ(mountingProgramName({true, false, 10, true}), "EIGHTH_INCH_TEN_HOLES");
    EXPECT_EQ(mountingProgramName({true, true, 10, false}), "QUARTER_INCH_TEN_HOLES_SHORT");
    EXPECT_EQ(mountingProgramName({true, false, 10, false}), "EIGHTH_INCH_TEN_HOLES_SHORT");
    const std::optional<std::string> program = mountingProgram({true, true, 10, false});
    ASSERT_TRUE(program.has_value());
    EXPECT_TRUE(program->starts_with("(10HolesVortexMounting0_25Dia)"));
    EXPECT_NE(program->find("G21"), std::string::npos);
}
