// "Run outline" against golden output from gSender's own Outline.worker.ts
// (tests/data/outline_golden.json, written by tools/gen_outline_fixtures.mjs),
// and robust-predicates' orient2d.

#include "gs/job/outline.hpp"
#include "gs/util/jsnumber.hpp"

#include <gtest/gtest.h>

#include <boost/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

using namespace gs;
using namespace gs::job;
namespace json = boost::json;

namespace {

const json::array& goldenCases() {
    static const json::value data = [] {
        std::ifstream in(GS_TEST_DATA_DIR "/outline_golden.json", std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return json::parse(text.str());
    }();
    return data.at("cases").as_array();
}

// Number(value) of the widget's outlineSpeed: null 0, strings parsed.
double jsNumber(const json::value& v) {
    if (v.is_null()) {
        return 0;
    }
    if (v.is_string()) {
        return js::stringToNumber(std::string(v.as_string()));
    }
    return v.to_number<double>();
}

OutlineInput inputFrom(const json::object& o) {
    OutlineInput in;
    in.mode = *outlineModeFromName(std::string(o.at("mode").as_string()));
    in.isLaser = o.at("isLaser").as_bool();
    in.zTravel = o.at("zTravel").to_number<double>();
    in.outlineSpeed = jsNumber(o.at("outlineSpeed"));
    if (const json::value* vertices = o.if_contains("parsedData")) {
        for (const json::value& v : vertices->as_array()) {
            in.vertices.push_back(static_cast<float>(v.to_number<double>()));
        }
    }
    if (const json::value* bbox = o.if_contains("bbox")) {
        const json::object& min = bbox->at("min").as_object();
        const json::object& max = bbox->at("max").as_object();
        in.bbox.min = {min.at("x").to_number<double>(), min.at("y").to_number<double>(),
                       min.at("z").to_number<double>(), 0};
        in.bbox.max = {max.at("x").to_number<double>(), max.at("y").to_number<double>(),
                       max.at("z").to_number<double>(), 0};
    }
    if (const json::value* content = o.if_contains("content")) {
        in.content = std::string(content->as_string());
    }
    return in;
}

TEST(Outline, MatchesUpstreamForEveryGoldenCase) {
    const json::array& cases = goldenCases();
    ASSERT_GE(cases.size(), 20u);
    for (const json::value& value : cases) {
        const json::object& c = value.as_object();
        const std::string name(c.at("name").as_string());
        const auto actual = outlineProgram(inputFrom(c.at("input").as_object()));
        if (c.contains("error")) {
            EXPECT_FALSE(actual.has_value()) << name;
            continue;
        }
        ASSERT_TRUE(actual.has_value()) << name;
        std::vector<std::string> expected;
        for (const json::value& line : c.at("lines").as_array()) {
            expected.emplace_back(line.as_string());
        }
        const auto [a, e] = std::mismatch(actual->begin(), actual->end(), expected.begin(), expected.end());
        if (a != actual->end() || e != expected.end()) {
            ADD_FAILURE() << name << " line " << (a - actual->begin()) << ":\n  actual:   "
                          << (a != actual->end() ? *a : "<end>")
                          << "\n  expected: " << (e != expected.end() ? *e : "<end>");
        }
    }
}

TEST(Outline, NoVerticesMeansNoDetailedHull) {
    EXPECT_FALSE(outlineProgram(OutlineInput{}).has_value());  // concaveman throws upstream
}

TEST(Orient2d, SignsAndExactCollinearity) {
    EXPECT_LT(orient2d(0, 0, 1, 0, 0, 1), 0);  // counter-clockwise
    EXPECT_GT(orient2d(0, 0, 0, 1, 1, 0), 0);  // clockwise
    EXPECT_EQ(orient2d(0, 0, 1, 1, 2, 2), 0);
    // One ulp above the line y = x: the naive determinant rounds to zero,
    // the adaptive one keeps the exact sign (-12 * 2^-53).
    const double ay = std::nextafter(0.5, 1.0);
    const double naive = (ay - 24) * (12 - 24) - (0.5 - 24) * (12 - 24);
    EXPECT_EQ(naive, 0);
    EXPECT_LT(orient2d(0.5, ay, 12, 12, 24, 24), 0);
}

TEST(Outline, ANegativeLiftIsPrintedAsUpstreamDoes) {
    // Upstream quirk: with homing and the machine at Z0, getZUpTravel(5) is
    // -1, so the outline dips 1 mm and ends with an invalid "Z--1".
    OutlineInput in;
    in.mode = OutlineMode::Square;
    in.zTravel = -1;
    const auto lines = outlineProgram(in);
    ASSERT_TRUE(lines.has_value());
    EXPECT_EQ((*lines)[2], "G21 G91 G0 Z-1");
    EXPECT_EQ((*lines)[lines->size() - 2], "G21 G91 G0 Z--1");
}

TEST(Outline, ModeNamesRoundTrip) {
    EXPECT_EQ(outlineModeFromName("Rapidless Square"), OutlineMode::RapidlessSquare);
    EXPECT_EQ(outlineModeName(OutlineMode::Square), "Square");
    EXPECT_EQ(outlineModeFromName("Circle"), std::nullopt);
}

}  // namespace
