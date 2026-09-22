#include "gs/gcode/interpreter.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace gs::gcode;

namespace {

struct Segment {
    std::string motion;
    Vec4 from;
    Vec4 to;
};

struct Arc {
    std::string motion;
    std::string plane;
    Vec4 from;
    Vec4 to;
    Vec4 center;
};

class RecordingSink : public GeometrySink {
public:
    void addLine(const Modal& modal, const Vec4& from, const Vec4& to) override {
        lines.push_back({modal.motion, from, to});
    }
    void addArc(const Modal& modal, const Vec4& from, const Vec4& to, const Vec4& center) override {
        arcs.push_back({modal.motion, modal.plane, from, to, center});
    }
    std::vector<Segment> lines;
    std::vector<Arc> arcs;
};

Interpreter run(std::string_view program, RecordingSink* sink = nullptr, InterpreterOptions options = {}) {
    Interpreter interpreter(options);
    interpreter.setSink(sink);
    interpreter.processProgram(program);
    return interpreter;
}

}  // namespace

TEST(GcodeInterpreter, LinearMovesInMillimetres) {
    RecordingSink sink;
    Interpreter vm = run("G21 G90\nG0 X10 Y5\nG1 Z-2 F300\nX20\n", &sink);

    ASSERT_EQ(sink.lines.size(), 3u);
    EXPECT_EQ(sink.lines[0].motion, "G0");
    EXPECT_DOUBLE_EQ(sink.lines[0].to.x, 10);
    EXPECT_DOUBLE_EQ(sink.lines[0].to.y, 5);
    EXPECT_EQ(sink.lines[1].motion, "G1");
    EXPECT_DOUBLE_EQ(sink.lines[1].to.z, -2);
    // Axis-only line continues the active motion mode.
    EXPECT_EQ(sink.lines[2].motion, "G1");
    EXPECT_DOUBLE_EQ(sink.lines[2].to.x, 20);
    EXPECT_DOUBLE_EQ(vm.position().x, 20);
    EXPECT_EQ(vm.usedAxes(), "XYZ");
    EXPECT_EQ(vm.totalLines(), 4u);
}

TEST(GcodeInterpreter, AxisWordsAfterModalGCodeStillMove) {
    // The cncjs interpreter attached X10 to G90 and dropped the move.
    RecordingSink sink;
    Interpreter vm = run("G0 G90 X10\n", &sink);
    ASSERT_FALSE(sink.lines.empty());
    EXPECT_DOUBLE_EQ(sink.lines.back().to.x, 10);
    EXPECT_DOUBLE_EQ(vm.position().x, 10);
}

TEST(GcodeInterpreter, InchesAndRelativeDistance) {
    Interpreter vm = run("G20 G91\nG0 X1 Y2\nG0 X1\n");
    EXPECT_DOUBLE_EQ(vm.position().x, 50.8);
    EXPECT_DOUBLE_EQ(vm.position().y, 50.8);
    EXPECT_EQ(vm.modal().units, "G20");
    EXPECT_EQ(vm.modal().distance, "G91");
}

TEST(GcodeInterpreter, G92OffsetsShiftGeometryNotPosition) {
    RecordingSink sink;
    Interpreter vm = run("G0 X10\nG92 X0\nG0 X5\n", &sink);
    ASSERT_EQ(sink.lines.size(), 2u);
    // The second move is drawn in the original frame: from 10 to 15.
    EXPECT_DOUBLE_EQ(sink.lines[1].from.x, 10);
    EXPECT_DOUBLE_EQ(sink.lines[1].to.x, 15);
    EXPECT_DOUBLE_EQ(vm.position().x, 5);
    EXPECT_DOUBLE_EQ(vm.g92Offset().x, 10);

    vm.processLine("G92.1");
    EXPECT_DOUBLE_EQ(vm.position().x, 15);
    EXPECT_DOUBLE_EQ(vm.g92Offset().x, 0);
}

TEST(GcodeInterpreter, ArcsReportCenterAndPlane) {
    RecordingSink sink;
    Interpreter vm = run("G17 G0 X10 Y0\nG2 X0 Y-10 I-10 J0 F600\n", &sink);
    ASSERT_EQ(sink.arcs.size(), 1u);
    const Arc& arc = sink.arcs[0];
    EXPECT_EQ(arc.motion, "G2");
    EXPECT_EQ(arc.plane, "G17");
    EXPECT_DOUBLE_EQ(arc.center.x, 0);
    EXPECT_DOUBLE_EQ(arc.center.y, 0);
    EXPECT_DOUBLE_EQ(vm.position().x, 0);
    EXPECT_DOUBLE_EQ(vm.position().y, -10);
}

TEST(GcodeInterpreter, RadiusArcsComputeCenter) {
    RecordingSink sink;
    run("G0 X0 Y0\nG2 X10 Y0 R5 F600\n", &sink);
    ASSERT_EQ(sink.arcs.size(), 1u);
    EXPECT_NEAR(sink.arcs[0].center.x, 5, 1e-9);
    EXPECT_NEAR(sink.arcs[0].center.y, 0, 1e-9);
}

TEST(GcodeInterpreter, FullCircleCountsArcLengthAndExtents) {
    // A full circle has a zero-length chord; time must follow the circumference.
    Interpreter vm = run("G0 X10 Y0\nG1 F600\nG3 X10 Y0 I-10 J0\n");
    const double circumference = 2 * 3.14159265358979 * 10;
    EXPECT_GT(vm.totalTime(), circumference / 10.0 * 0.95);
    const BoundingBox box = vm.bounds();
    EXPECT_NEAR(box.min.x, -10, 1e-9);
    EXPECT_NEAR(box.max.y, 10, 1e-9);
    EXPECT_NEAR(box.min.y, -10, 1e-9);
}

TEST(GcodeInterpreter, ArcPlanesAreRotated) {
    RecordingSink sink;
    run("G18\nG0 X10 Z0\nG2 X0 Z10 I-10 K0 F100\n", &sink);
    ASSERT_EQ(sink.arcs.size(), 1u);
    // ZX plane: in-plane x is Z, y is X.
    EXPECT_EQ(sink.arcs[0].plane, "G18");
    EXPECT_DOUBLE_EQ(sink.arcs[0].from.x, 0);   // Z
    EXPECT_DOUBLE_EQ(sink.arcs[0].from.y, 10);  // X
    EXPECT_DOUBLE_EQ(sink.arcs[0].to.x, 10);
    EXPECT_DOUBLE_EQ(sink.arcs[0].to.y, 0);
}

TEST(GcodeInterpreter, LinearMoveTimeUsesTrapezoidalProfile) {
    // 100 mm at F600 (10 mm/s) with 750 mm/s^2: 2*(v/a + (L/2 - v^2/2a)/v).
    Interpreter vm = run("G1 X100 F600\n");
    const double v = 10;
    const double a = 750;
    const double expected = 2 * (v / a + (50 - 0.5 * v * (v / a)) / v);
    EXPECT_NEAR(vm.totalTime(), expected, 1e-9);
    EXPECT_NEAR(vm.lastLineTime(), expected, 1e-9);
}

TEST(GcodeInterpreter, RapidsUseMaxFeed) {
    InterpreterOptions options;
    options.x.maxFeed = 6000;  // 100 mm/s
    options.y.maxFeed = 6000;
    Interpreter vm = run("G0 X100\n", nullptr, options);
    EXPECT_GT(vm.totalTime(), 1.0);
    EXPECT_LT(vm.totalTime(), 1.2);
}

TEST(GcodeInterpreter, DwellIsSeconds) {
    Interpreter vm = run("G4 P2\nG4 P0.5\n");
    EXPECT_DOUBLE_EQ(vm.totalTime(), 2.5);
    EXPECT_EQ(vm.modal().motion, "G0");  // G4 is not a motion mode
}

TEST(GcodeInterpreter, CollectsToolsFeedsSpindlesAndEvents) {
    Interpreter vm = run("T2 M6 (Tool 2 - endmill)\nS12000 M3\nG1 X1 F500\nG1 X2 F800.0\n");
    EXPECT_EQ(vm.tools().values(), (std::vector<std::string>{"T2"}));
    EXPECT_EQ(vm.spindleSpeeds().values(), (std::vector<std::string>{"S12000"}));
    EXPECT_EQ(vm.feedrates().values(), (std::vector<std::string>{"F500", "F800.0"}));
    EXPECT_TRUE(vm.hasSeenM6());
    EXPECT_EQ(vm.toolChangeLines(), (std::vector<std::size_t>{1}));
    EXPECT_EQ(vm.modal().spindle, "M3");
    EXPECT_DOUBLE_EQ(vm.modal().tool, 2);

    const auto& events = vm.spindleToolEvents();
    ASSERT_TRUE(events.count(1));
    EXPECT_EQ(events.at(1).T, 2);
    EXPECT_EQ(events.at(1).M, 6);
    EXPECT_EQ(events.at(1).comment, "Tool 2 - endmill");
    ASSERT_TRUE(events.count(2));
    EXPECT_EQ(events.at(2).S, 12000);
    EXPECT_EQ(events.at(2).M, 3);
}

TEST(GcodeInterpreter, CoolantAndModalGroups) {
    Interpreter vm = run("M7\nM8\nG55 G18 G93\n");
    EXPECT_EQ(vm.modal().coolant, "M7,M8");
    EXPECT_EQ(vm.modal().wcs, "G55");
    EXPECT_EQ(vm.modal().plane, "G18");
    EXPECT_EQ(vm.modal().feedrate, "G93");
    vm.processLine("M9");
    EXPECT_EQ(vm.modal().coolant, "M9");
}

TEST(GcodeInterpreter, RotaryFilesAreClassified) {
    EXPECT_EQ(run("G1 X10 A90 F100\n").fileType(), FileType::Rotary);
    EXPECT_EQ(run("G1 Y10 A90 F100\n").fileType(), FileType::FourAxis);
    EXPECT_EQ(run("G1 X10 Y10 F100\n").fileType(), FileType::Default);
}

TEST(GcodeInterpreter, InvalidLinesAreRecorded) {
    Interpreter vm = run("G1 X5 E0.2\nG1 X6\n");
    ASSERT_EQ(vm.invalidLines().size(), 1u);
    EXPECT_EQ(vm.invalidLines()[0], "G1 X5 E0.2");
}

TEST(GcodeInterpreter, AtcToolChangesAddTime) {
    InterpreterOptions options;
    options.atcEnabled = true;
    Interpreter vm = run("M6 T1\n", nullptr, options);
    EXPECT_DOUBLE_EQ(vm.totalTime(), 45);
}
