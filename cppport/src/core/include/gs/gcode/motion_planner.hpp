#pragma once

// Streaming job time estimator modelled on the grbl / grblHAL motion planner.
// Port of app/lib/timeEstimator/MotionPlanner.ts (upstream a440be05b).
//
// Moves are fed in file order and pushed through a rolling window the size of
// the firmware planner buffer. A block's velocity profile is only fixed once
// the window is full, which is when the firmware would start executing it
// with the same lookahead - this is what slows dense short-segment toolpaths
// down the same way the real machine does. Profiles use grbl's junction
// deviation cornering model and per-axis rate/acceleration limits
// (plan_buffer_line).
//
// All time is attributed to the sender line index (non-blank lines only, as
// the Sender keeps them) so the Sender can map execution progress to time.

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace gs::gcode {

// How a line's time scales with the overrides.
enum LineKind : std::uint8_t {
    kLineKindNone = 0,
    kLineKindFeed = 1,
    kLineKindRapid = 2,
    kLineKindFixed = 3,  // dwell / tool change - not scaled by overrides
};

enum class Motion { Rapid, Feed };

enum class Firmware { Grbl, GrblHal };

struct EstimatorConfig {
    Firmware firmware = Firmware::Grbl;
    // X, Y, Z, A max rates in units/min ($110-$113)
    std::array<double, 4> maxRate{4000, 4000, 3000, 3000};
    // X, Y, Z, A accelerations in units/sec^2 ($120-$123)
    std::array<double, 4> accel{750, 750, 500, 500};
    double junctionDeviation = 0.01;  // $11, mm
    double arcTolerance = 0.002;      // $12, mm
    int plannerBlocks = 15;           // usable planner blocks (lookahead window)
    // grbl without 4-axis firmware: gSender remaps A to Y, so A runs on Y's limits
    bool aUsesYLimits = false;
    // grblHAL $701 bit0 + A flagged rotary in $376: feed applies to the linear axes only
    bool rotaryFix = false;
    // grblHAL $701 bit1: pure rotary F is not inch-converted in G20
    bool rotaryRevertMetric = false;
    bool laserMode = false;          // spindle changes don't sync the planner in laser mode
    double toolChangeTime = 0;       // seconds added per M6 (ATC)
    double spindleDelay = 0;         // seconds added when the spindle is turned on
    // serial throughput floor in bytes/sec (0 disables), models buffer starvation
    double serialBytesPerSecond = 0;
};

// Partial config: unset (or non-positive) values take the defaults, as
// createEstimatorConfig() fills them.
struct EstimatorSettings {
    Firmware firmware = Firmware::Grbl;
    std::array<std::optional<double>, 4> maxRate;
    std::array<std::optional<double>, 4> accel;
    std::optional<double> junctionDeviation;
    std::optional<double> arcTolerance;
    std::optional<double> plannerBlocks;
    bool aUsesYLimits = false;
    bool rotaryFix = false;
    bool rotaryRevertMetric = false;
    bool laserMode = false;
    double toolChangeTime = 0;
    double spindleDelay = 0;
    double serialBytesPerSecond = 0;
    bool operator==(const EstimatorSettings&) const = default;
};

EstimatorConfig createEstimatorConfig(const EstimatorSettings& settings = {});

// Time to traverse a block with a trapezoidal (or triangular) velocity
// profile. Speeds are passed squared, as the planner stores them.
double trapezoidTime(double length, double accel, double entrySqr, double exitSqr, double nominalSqr);

struct EstimateResult {
    std::vector<float> lineTime;  // seconds per sender line
    std::vector<std::uint8_t> lineKind;
    double totalTime = 0;
};

class MotionPlanner {
public:
    explicit MotionPlanner(const EstimatorSettings& settings = {}, std::size_t expectedLines = 1024);

    const EstimatorConfig& config() const noexcept { return config_; }

    // Start a new (non-blank) sender line. `bytes` is what gets streamed for it.
    void beginLine(std::size_t bytes = 0);

    // Linear move. Deltas are machine units: mm for XYZ, degrees for A.
    // `feed` is the raw programmed F word (units/min, or 1/min in inverse time).
    void addLinear(double dx, double dy, double dz, double da, Motion motion, double feed, bool imperial = false,
                   bool inverseTime = false);

    // Arc move, expanded into the same chords grbl's mc_arc() generates.
    // Positions are in plane order: [axis0, axis1, linear] with the machine
    // axis indices given by `axes`. `center` is absolute.
    void addArc(const std::array<double, 3>& start, const std::array<double, 3>& end,
                const std::array<double, 2>& center, bool clockwise, const std::array<int, 3>& axes, double feed,
                bool imperial = false, bool inverseTime = false);

    // Planner sync (buffer drains to a full stop), plus optional fixed time.
    void addSync(double seconds = 0);
    void addDwell(double seconds);
    void addToolChange();
    void addSpindleStart();

    // Flush the planner and return per-line times. Idempotent.
    const EstimateResult& finish();

private:
    std::size_t currentLine();
    void addTime(std::size_t line, double seconds, std::uint8_t kind);
    void addMove(std::size_t line, const std::array<double, 4>& d, Motion motion, double rateMmPerMin,
                 double inverseFeed, bool imperial);
    void finalizeOldest();
    void drain();
    void applySerialThroughput(std::vector<float>& lineTime, double bps) const;

    EstimatorConfig config_;

    // per sender line output
    std::vector<float> lineTime_;
    std::vector<std::uint8_t> lineKind_;
    std::vector<std::uint16_t> lineBytes_;
    std::ptrdiff_t line_ = -1;  // current sender line index

    // planner window (ring buffer)
    std::size_t cap_;
    std::size_t head_ = 0;  // index of oldest (executing) block
    std::size_t count_ = 0;
    std::vector<std::size_t> bLine_;
    std::vector<double> bMm_;
    std::vector<double> bAccel_;
    std::vector<double> bNominalSqr_;
    std::vector<double> bMaxEntrySqr_;
    std::vector<double> bRevEntrySqr_;  // entry limit from reverse pass (window ends at rest)
    std::vector<std::uint8_t> bRapid_;

    double tailEntrySqr_ = 0;  // fixed entry speed of the oldest block
    bool hasPrev_ = false;
    std::array<double, 4> prevUnit_{};
    double prevNominalSqr_ = 0;

    std::optional<EstimateResult> result_;
};

}  // namespace gs::gcode
