// The tool change wizards against golden output from gSender's own
// src/app/src/wizards (tests/data/toolchange_golden.json, written by
// tools/gen_toolchange_fixtures.mjs).

#include "gs/toolchange/wizards.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::toolchange;
namespace json = boost::json;

namespace {

const json::array& goldenCases() {
    static const json::value data = [] {
        std::ifstream in(GS_TEST_DATA_DIR "/toolchange_golden.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return json::parse(text.str());
    }();
    return data.at("cases").as_array();
}

double number(const json::value& v) {
    return v.to_number<double>();
}

std::string text(const json::value& v) {
    return std::string(v.as_string());
}

MachinePosition position(const json::value& v) {
    const json::object& o = v.as_object();
    return {number(o.at("x")), number(o.at("y")), number(o.at("z"))};
}

std::vector<std::string> lines(const json::value& v) {
    std::vector<std::string> out;
    for (const json::value& line : v.as_array()) {
        out.push_back(text(line));
    }
    return out;
}

Wizard build(const json::object& in) {
    probe::ProbeSettings settings;
    settings.plateType = *probe::plateTypeFromName(text(in.at("plateType")));
    const json::object& z = in.at("zThickness").as_object();
    settings.zThickness = {number(z.at("standardBlock")), number(z.at("autoZero")), number(z.at("zProbe")),
                           number(z.at("probe3D")),       number(z.at("bitZero")),  number(z.at("bitZeroZOnly"))};
    const json::object& p = in.at("probe").as_object();
    settings.probeFeedrate = number(p.at("probeFeedrate"));
    settings.probeFastFeedrate = number(p.at("probeFastFeedrate"));
    settings.retractionDistance = number(p.at("retractionDistance"));
    settings.zProbeDistance = number(p.at("zProbeDistance"));
    const ProbeSettings probe = toolChangeProbeSettings(settings);

    const json::object& s = in.at("settings").as_object();
    MachineFacts machine;
    machine.reportInches = text(s.at("$13"));
    machine.softLimits = text(s.at("$20"));
    machine.zMaxTravel = text(s.at("$132"));
    machine.machineZ = number(in.at("mposZ"));
    machine.tool = text(in.at("tool"));

    const int count = static_cast<int>(number(in.at("count")));
    const std::string wizard = text(in.at("wizard"));
    const MachinePosition sensor = position(in.at("toolChangePosition"));
    if (wizard == "manual") {
        return standardRezero(probe, machine);
    }
    if (wizard == "semiauto") {
        return flexibleRezero(count, probe, machine);
    }
    if (wizard == "automatic") {
        return fixedToolSensor(count, probe, machine, sensor, position(in.at("manualPosition")),
                               in.at("moveToManualPosition").as_bool());
    }
    return probeToolLength(probe, machine, sensor);
}

void expectLines(const std::vector<std::string>& actual, const std::vector<std::string>& expected,
                 const std::string& where) {
    const std::size_t common = std::min(actual.size(), expected.size());
    for (std::size_t i = 0; i < common; ++i) {
        if (actual[i] != expected[i]) {
            ADD_FAILURE() << where << " line " << i << ":\n  actual:   " << actual[i] << "\n  expected: " << expected[i];
            return;
        }
    }
    EXPECT_EQ(actual.size(), expected.size()) << where;
}

TEST(ToolChangeWizards, MatchUpstreamForEveryGoldenCase) {
    const json::array& cases = goldenCases();
    ASSERT_GE(cases.size(), 30u);
    for (const json::value& value : cases) {
        const json::object& c = value.as_object();
        const std::string name = text(c.at("name"));
        const Wizard w = build(c.at("input").as_object());
        EXPECT_EQ(w.intro, text(c.at("intro"))) << name;
        EXPECT_EQ(w.startDirect, c.at("startDirect").as_bool()) << name;
        expectLines(w.start, lines(c.at("start")), name + " start");
        const json::array& steps = c.at("steps").as_array();
        ASSERT_EQ(w.steps.size(), steps.size()) << name;
        for (std::size_t i = 0; i < steps.size(); ++i) {
            const json::object& step = steps[i].as_object();
            const std::string where = name + " step " + std::to_string(i);
            EXPECT_EQ(w.steps[i].title, text(step.at("title"))) << where;
            const json::array& substeps = step.at("substeps").as_array();
            ASSERT_EQ(w.steps[i].substeps.size(), substeps.size()) << where;
            for (std::size_t j = 0; j < substeps.size(); ++j) {
                const json::object& sub = substeps[j].as_object();
                const WizardSubstep& actual = w.steps[i].substeps[j];
                const std::string at = where + "." + std::to_string(j);
                EXPECT_EQ(actual.title, text(sub.at("title"))) << at;
                EXPECT_EQ(actual.description, text(sub.at("description"))) << at;
                EXPECT_EQ(actual.toolBanner, sub.at("toolBanner").as_bool()) << at;
                const json::array& actions = sub.at("actions").as_array();
                ASSERT_EQ(actual.actions.size(), actions.size()) << at;
                for (std::size_t k = 0; k < actions.size(); ++k) {
                    const json::object& action = actions[k].as_object();
                    EXPECT_EQ(actual.actions[k].label, text(action.at("label"))) << at;
                    expectLines(actual.actions[k].gcode, lines(action.at("lines")), at + " " + actual.actions[k].label);
                }
            }
        }
    }
}

TEST(ToolChangeWizards, EveryWizardEndsByResumingTheJob) {
    const ProbeSettings probe;
    const MachineFacts machine;
    for (const Wizard& w : {standardRezero(probe, machine), flexibleRezero(1, probe, machine),
                            fixedToolSensor(1, probe, machine, {}, {}, false), probeToolLength(probe, machine, {})}) {
        ASSERT_FALSE(w.steps.empty());
        const WizardSubstep& last = w.steps.back().substeps.back();
        ASSERT_EQ(last.actions.size(), 1u) << w.title;
        EXPECT_EQ(last.actions[0].gcode.back(), "%toolchange_complete") << w.title;
    }
}

TEST(ToolChangeWizards, TheProbeThicknessFollowsThePlate) {
    probe::ProbeSettings settings;
    settings.plateType = probe::PlateType::BitZero;
    EXPECT_EQ(toolChangeProbeSettings(settings).zProbeThickness, 15.5);  // flat on the surface
    settings.plateType = probe::PlateType::AutoZero;
    EXPECT_EQ(toolChangeProbeSettings(settings).zProbeThickness, 5);
}

}  // namespace
