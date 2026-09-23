#pragma once

// G-code interpreter: tracks modal state and position line by line, reports
// motion geometry to a sink, and collects the statistics and time estimates
// gSender shows for a loaded file.
//
// Port of the app's GCodeVirtualizer (visualizer/estimates) merged with the
// server's GcodeToolpath (start-from-line state recovery) so both features
// interpret programs identically. Differences from the JavaScript, all
// deliberate and documented in DEV_WALKTHROUGH.md:
//   * G4 P is seconds (Grbl semantics) rather than milliseconds;
//   * G4 does not overwrite the motion modal;
//   * arc time estimates use the arc length instead of the chord;
//   * the bounding box is taken in the same (G92-offset) frame as the geometry;
//   * axes used on modal-continuation lines ("X10" after "G1 X0") count as used;
//   * a malformed F word does not poison the feed rate with NaN.

#include "gs/gcode/parser.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gs::gcode {

struct Vec4 {
    double x = 0;
    double y = 0;
    double z = 0;
    double a = 0;
};

struct Modal {
    std::string motion = "G0";     // G0, G1, G2, G3, G38.x, G80
    std::string wcs = "G54";       // G54..G59
    std::string plane = "G17";     // G17 XY, G18 ZX, G19 YZ
    std::string units = "G21";     // G20 inch, G21 mm
    std::string distance = "G90";  // G90 absolute, G91 relative
    std::string arc = "G91.1";     // arc IJK distance mode
    std::string feedrate = "G94";  // G93, G94, G95
    std::string cutter = "G40";
    std::string tlo = "G49";       // G43.1, G49
    std::string program = "M0";    // M0, M1, M2, M30
    std::string spindle = "M5";    // M3, M4, M5
    std::string coolant = "M9";    // "M7", "M8", "M7,M8" or "M9"
    double tool = 0;
};

// Receives motion geometry. Positions are millimetres with G92 offsets
// applied. For arcs, coordinates are rotated into the arc plane: x/y are the
// in-plane axes and z is the axis normal to the plane (see Modal::plane).
class GeometrySink {
public:
    virtual ~GeometrySink() = default;
    virtual void addLine(const Modal& modal, const Vec4& from, const Vec4& to) = 0;
    // Rotary moves with a large A change (> 30 degrees).
    virtual void addCurve(const Modal& modal, const Vec4& from, const Vec4& to) {
        addLine(modal, from, to);
    }
    virtual void addArc(const Modal& modal, const Vec4& from, const Vec4& to, const Vec4& center) = 0;
    // Called by drivers such as job::analyzeProgram() before the geometry of a
    // line is reported, with that line's index in the driver's numbering.
    virtual void atLine(std::size_t /*index*/) {}
};

struct AxisLimits {
    double acceleration = 750;  // mm/s^2 (deg/s^2 for A)
    double maxFeed = 4000;      // mm/min (deg/min for A)
};

struct InterpreterOptions {
    AxisLimits x{750, 4000};
    AxisLimits y{750, 4000};
    AxisLimits z{500, 3000};
    AxisLimits a{500, 3000};
    bool atcEnabled = false;  // adds 45 s per M6 to the estimate
    // A fixed rotary stock diameter disables auto-detection from Y/Z extents.
    std::optional<double> rotaryDiameter;
    bool autoDetectRotaryDiameter = true;
};

// Per-line S/T/M/tool-change events for the tool timeline.
struct SpindleToolEvent {
    std::optional<double> S;
    std::optional<double> T;
    std::optional<double> M;
    std::string comment;
};

struct BoundingBox {
    Vec4 min;
    Vec4 max;
};

enum class FileType { Default, Rotary, FourAxis };

// Insertion-ordered set of strings (JavaScript Set semantics).
class OrderedStringSet {
public:
    void add(std::string value);
    const std::vector<std::string>& values() const noexcept { return values_; }
    bool contains(const std::string& value) const { return index_.count(value) != 0; }

private:
    std::vector<std::string> values_;
    std::unordered_set<std::string> index_;
};

class Interpreter {
public:
    explicit Interpreter(InterpreterOptions options = {});

    void setSink(GeometrySink* sink) noexcept { sink_ = sink; }

    // Feeds one program line (no line terminator). Every line counts towards
    // totalLines(), including blank ones.
    void processLine(std::string_view line);

    // Convenience: feeds every line of a program.
    void processProgram(std::string_view text);

    // ---- current state ----
    const Modal& modal() const noexcept { return modal_; }
    const Vec4& position() const noexcept { return position_; }  // without G92 offsets
    const Vec4& g92Offset() const noexcept { return offsets_; }
    bool hasSeenM6() const noexcept { return sawM6_; }
    double feed() const noexcept { return feed_; }

    // ---- results for the most recent processLine() ----
    const LineScan& lastScan() const noexcept { return scan_; }
    bool lastLineHadTokens() const noexcept { return lineHadTokens_; }
    double lastLineTime() const noexcept { return lineTime_; }  // seconds
    std::optional<double> lastSpindleSpeed() const noexcept { return lineSpindle_; }

    // ---- program totals ----
    std::size_t totalLines() const noexcept { return totalLines_; }
    double totalTime() const noexcept { return totalTime_; }  // seconds
    BoundingBox bounds() const noexcept;
    const OrderedStringSet& tools() const noexcept { return tools_; }
    const OrderedStringSet& spindleSpeeds() const noexcept { return spindleSpeeds_; }
    const OrderedStringSet& feedrates() const noexcept { return feedrates_; }
    std::string usedAxes() const;  // e.g. "XYZ", in X Y Z A order
    const std::vector<std::string>& invalidLines() const noexcept { return invalidLines_; }
    // 1-based line numbers of lines containing M6.
    const std::vector<std::size_t>& toolChangeLines() const noexcept { return toolChangeLines_; }
    // Keyed by 1-based line number.
    const std::map<std::size_t, SpindleToolEvent>& spindleToolEvents() const noexcept {
        return events_;
    }
    FileType fileType() const noexcept;
    double rotaryDiameter() const noexcept { return rotaryDiameter_; }

private:
    enum class Cmd : std::uint8_t {
        None, G0, G1, G2, G3, G4, G10, G17, G18, G19, G20, G21,
        G38_2, G38_3, G38_4, G38_5, G43_1, G49,
        G54, G55, G56, G57, G58, G59, G80, G90, G91, G92, G92_1, G93, G94, G95,
        M0, M1, M2, M30, M3, M4, M5, M6, M7, M8, M9
    };

    // Word arguments of one command group, by letter.
    struct Args {
        std::array<std::string_view, 26> values{};
        std::uint32_t mask = 0;
        bool has(char letter) const noexcept;
        std::string_view get(char letter) const noexcept;
        void set(char letter, std::string_view value) noexcept;
        double number(char letter) const;  // NaN when absent or not numeric
    };

    void dispatchGroup(std::size_t begin, std::size_t end);
    void execute(Cmd cmd, const Args& args);
    void linearMove(Cmd cmd, const Args& args);
    void arcMove(Cmd cmd, const Args& args);
    void dwell(const Args& args);
    void setG92(const Args& args);
    void clearG92();
    void setTool(std::string_view code);
    void noteUsedAxes(const Args& args);
    void recordEvent(char kind, double value);

    double translate(double current, double value, bool relative, bool linear) const;
    double translateAxis(double current, const Args& args, char letter) const;
    Vec4 withOffsets(const Vec4& v) const noexcept;
    void updateBounds(const Vec4& p);
    void addMoveTime(const Vec4& from, const Vec4& to);
    void addTime(double seconds);
    static double acceleratedMoveTime(double length, double velocity, double acceleration);

    static Cmd gCommand(double code) noexcept;
    static Cmd mCommand(double code) noexcept;
    static std::string_view commandName(Cmd cmd) noexcept;
    static bool isMotion(Cmd cmd) noexcept;

    InterpreterOptions options_;
    GeometrySink* sink_ = nullptr;

    Modal modal_;
    Cmd motionMode_ = Cmd::G0;  // continuation mode for axis-only lines
    Vec4 position_;
    Vec4 offsets_;
    double feed_ = 0;
    double lastF_ = 0;
    bool sawM6_ = false;
    double rotaryDiameter_ = 50;
    bool autoDetectDiameter_ = true;

    LineScan scan_;
    bool lineHadTokens_ = false;
    double lineTime_ = 0;
    std::optional<double> lineSpindle_;

    std::size_t totalLines_ = 0;
    double totalTime_ = 0;
    std::array<double, 4> minBounds_{};
    std::array<double, 4> maxBounds_{};
    OrderedStringSet tools_;
    OrderedStringSet spindleSpeeds_;
    OrderedStringSet feedrates_;
    std::uint8_t usedAxes_ = 0;  // bit 0..3 = X, Y, Z, A
    std::vector<std::string> invalidLines_;
    std::vector<std::size_t> toolChangeLines_;
    std::map<std::size_t, SpindleToolEvent> events_;
};

}  // namespace gs::gcode
