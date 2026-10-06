#include "gs/gcode/motion_planner.hpp"

#include "gs/util/jsnumber.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace gs::gcode {
namespace {

constexpr double kMinimumFeedRate = 1;  // mm/min, from grbl config.h
constexpr double kArcAngularTravelEpsilon = 5e-7;
constexpr double kSomeLargeValue = 1e38;

double positiveOr(const std::optional<double>& value, double fallback) {
    return value && std::isfinite(*value) && *value > 0 ? *value : fallback;
}

// Math.max(0, Number(value) || 0)
double nonNegative(double value) {
    return std::isnan(value) ? 0 : std::max(0.0, value);
}

}  // namespace

EstimatorConfig createEstimatorConfig(const EstimatorSettings& s) {
    EstimatorConfig config;
    config.firmware = s.firmware;
    const bool hal = s.firmware == Firmware::GrblHal;
    const EstimatorConfig defaults;
    for (std::size_t i = 0; i < 4; ++i) {
        config.maxRate[i] = positiveOr(s.maxRate[i], defaults.maxRate[i]);
        config.accel[i] = positiveOr(s.accel[i], defaults.accel[i]);
    }
    config.junctionDeviation = positiveOr(s.junctionDeviation, defaults.junctionDeviation);
    config.arcTolerance = positiveOr(s.arcTolerance, defaults.arcTolerance);
    config.plannerBlocks =
        static_cast<int>(std::max(1.0, js::mathRound(positiveOr(s.plannerBlocks, hal ? 34 : 15))));
    config.aUsesYLimits = s.aUsesYLimits;
    config.rotaryFix = hal && s.rotaryFix;
    config.rotaryRevertMetric = s.rotaryRevertMetric;
    config.laserMode = s.laserMode;
    config.toolChangeTime = nonNegative(s.toolChangeTime);
    config.spindleDelay = nonNegative(s.spindleDelay);
    config.serialBytesPerSecond = nonNegative(s.serialBytesPerSecond);
    if (config.aUsesYLimits) {
        config.maxRate[3] = config.maxRate[1];
        config.accel[3] = config.accel[1];
    }
    return config;
}

double trapezoidTime(double length, double accel, double entrySqr, double exitSqr, double nominalSqr) {
    if (length <= 0) {
        return 0;
    }
    const double vi = std::sqrt(entrySqr);
    const double vf = std::sqrt(exitSqr);
    const double accelDist = (nominalSqr - entrySqr) / (2 * accel);
    const double decelDist = (nominalSqr - exitSqr) / (2 * accel);
    if (accelDist + decelDist <= length) {
        const double vn = std::sqrt(nominalSqr);
        return (vn - vi) / accel + (vn - vf) / accel + (length - accelDist - decelDist) / vn;
    }
    // Never reaches nominal speed: accelerate to a peak then decelerate.
    const double peak = std::max({std::sqrt((2 * accel * length + entrySqr + exitSqr) / 2), vi, vf});
    return (peak - vi) / accel + (peak - vf) / accel;
}

MotionPlanner::MotionPlanner(const EstimatorSettings& settings, std::size_t expectedLines)
    : config_(createEstimatorConfig(settings)), cap_(static_cast<std::size_t>(config_.plannerBlocks) + 1) {
    const std::size_t size = std::max<std::size_t>(16, expectedLines);
    lineTime_.reserve(size);
    lineKind_.reserve(size);
    lineBytes_.reserve(size);
    bLine_.resize(cap_);
    bMm_.resize(cap_);
    bAccel_.resize(cap_);
    bNominalSqr_.resize(cap_);
    bMaxEntrySqr_.resize(cap_);
    bRevEntrySqr_.resize(cap_);
    bRapid_.resize(cap_);
}

void MotionPlanner::beginLine(std::size_t bytes) {
    ++line_;
    lineTime_.push_back(0);
    lineKind_.push_back(kLineKindNone);
    lineBytes_.push_back(static_cast<std::uint16_t>(std::min<std::size_t>(bytes, 65535)));
}

std::size_t MotionPlanner::currentLine() {
    if (line_ < 0) {
        beginLine(0);
    }
    return static_cast<std::size_t>(line_);
}

void MotionPlanner::addTime(std::size_t line, double seconds, std::uint8_t kind) {
    lineTime_[line] += static_cast<float>(seconds);
    const std::uint8_t existing = lineKind_[line];
    // feed wins over rapid wins over fixed when a line mixes them
    if (existing == kLineKindNone || (kind == kLineKindFeed && existing != kLineKindFeed) ||
        (kind == kLineKindRapid && existing == kLineKindFixed)) {
        lineKind_[line] = kind;
    }
}

void MotionPlanner::addLinear(double dx, double dy, double dz, double da, Motion motion, double feed, bool imperial,
                              bool inverseTime) {
    const auto orZero = [](double v) { return std::isnan(v) ? 0.0 : v; };
    const std::array<double, 4> d{orZero(dx), orZero(dy), orZero(dz), orZero(da)};
    const double f = orZero(feed);
    const double rateScale = inverseTime ? 1 : imperial ? 25.4 : 1;
    addMove(currentLine(), d, motion, f * rateScale, inverseTime ? f : 0, imperial);
}

void MotionPlanner::addArc(const std::array<double, 3>& start, const std::array<double, 3>& end,
                           const std::array<double, 2>& center, bool clockwise, const std::array<int, 3>& axes,
                           double feed, bool imperial, bool inverseTime) {
    const std::size_t line = currentLine();
    const double rAxis0 = start[0] - center[0];
    const double rAxis1 = start[1] - center[1];
    const double rtAxis0 = end[0] - center[0];
    const double rtAxis1 = end[1] - center[1];
    const double radius = std::hypot(rAxis0, rAxis1);

    double angularTravel = std::atan2(rAxis0 * rtAxis1 - rAxis1 * rtAxis0, rAxis0 * rtAxis0 + rAxis1 * rtAxis1);
    if (clockwise) {
        if (angularTravel >= -kArcAngularTravelEpsilon) {
            angularTravel -= 2 * std::numbers::pi;
        }
    } else if (angularTravel <= kArcAngularTravelEpsilon) {
        angularTravel += 2 * std::numbers::pi;
    }

    const double feedRaw = std::isnan(feed) ? 0 : feed;
    const double rate = inverseTime ? 0 : feedRaw * (imperial ? 25.4 : 1);
    const double tol = config_.arcTolerance;
    const double segDenominator = std::sqrt(tol * (2 * radius - tol));
    double segments = radius > 0 && segDenominator > 0
                          ? std::floor(std::fabs(0.5 * angularTravel * radius) / segDenominator)
                          : 0;
    if (!std::isfinite(segments) || segments < 1) {
        segments = 1;
    }
    // In inverse time the whole arc takes 1/F minutes, so each chord takes 1/(F*segments).
    const double inverseFeed = inverseTime ? feedRaw * segments : 0;

    const double thetaPerSegment = angularTravel / segments;
    const double linearPerSegment = (end[2] - start[2]) / segments;
    const auto count = static_cast<std::int64_t>(segments);
    double prev0 = start[0];
    double prev1 = start[1];
    for (std::int64_t i = 1; i <= count; ++i) {
        double p0 = 0;
        double p1 = 0;
        if (i == count) {
            p0 = end[0];
            p1 = end[1];
        } else {
            const double theta = thetaPerSegment * static_cast<double>(i);
            const double cosT = std::cos(theta);
            const double sinT = std::sin(theta);
            p0 = center[0] + rAxis0 * cosT - rAxis1 * sinT;
            p1 = center[1] + rAxis0 * sinT + rAxis1 * cosT;
        }
        std::array<double, 4> d{};
        d[static_cast<std::size_t>(axes[0])] = p0 - prev0;
        d[static_cast<std::size_t>(axes[1])] = p1 - prev1;
        d[static_cast<std::size_t>(axes[2])] = linearPerSegment;
        prev0 = p0;
        prev1 = p1;
        addMove(line, d, Motion::Feed, rate, inverseFeed, imperial);
    }
}

void MotionPlanner::addSync(double seconds) {
    const std::size_t line = currentLine();
    drain();
    if (seconds > 0) {
        addTime(line, seconds, kLineKindFixed);
    }
}

void MotionPlanner::addDwell(double seconds) {
    addSync(nonNegative(seconds));
}

void MotionPlanner::addToolChange() {
    addSync(config_.toolChangeTime);
}

void MotionPlanner::addSpindleStart() {
    addSync(config_.spindleDelay);
}

void MotionPlanner::addMove(std::size_t line, const std::array<double, 4>& d, Motion motion, double rateMmPerMin,
                            double inverseFeed, bool imperial) {
    const double mm = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2] + d[3] * d[3]);
    if (!(mm > 1e-9)) {
        return;  // grbl discards zero-length blocks
    }

    std::array<double, 4> u{};
    double accel = kSomeLargeValue;
    double rapidRate = kSomeLargeValue;
    for (std::size_t i = 0; i < 4; ++i) {
        u[i] = d[i] / mm;
        if (u[i] != 0) {
            const double inv = 1 / std::fabs(u[i]);
            accel = std::min(accel, config_.accel[i] * inv);
            rapidRate = std::min(rapidRate, config_.maxRate[i] * inv);
        }
    }

    double rate = 0;  // mm/min
    const bool isRapid = motion == Motion::Rapid;
    if (isRapid) {
        rate = rapidRate;
    } else {
        if (inverseFeed > 0) {
            rate = inverseFeed * mm;
        } else if (config_.rotaryFix && d[3] != 0) {
            // grblHAL rotary fix: F applies to the linear component only
            const double linear = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
            if (linear > 1e-9) {
                rate = (rateMmPerMin * mm) / linear;
            } else if (imperial && config_.rotaryRevertMetric) {
                rate = rateMmPerMin / 25.4;
            } else {
                rate = rateMmPerMin;
            }
        } else {
            rate = rateMmPerMin;
        }
        if (!(rate >= kMinimumFeedRate)) {
            rate = kMinimumFeedRate;
        }
        rate = std::min(rate, rapidRate);
    }

    const double nominal = rate / 60;  // mm/s
    const double nominalSqr = nominal * nominal;

    // Junction speed with the previous block (grbl junction deviation).
    double maxEntrySqr = 0;
    if (hasPrev_) {
        std::array<double, 4> ju{};
        double cosTheta = 0;
        for (std::size_t i = 0; i < 4; ++i) {
            cosTheta -= prevUnit_[i] * u[i];
            ju[i] = u[i] - prevUnit_[i];
        }
        double junctionSqr = 0;
        if (cosTheta > 0.999999) {
            junctionSqr = 0;  // full reversal
        } else if (cosTheta < -0.999999) {
            junctionSqr = kSomeLargeValue;  // straight through
        } else {
            const double jl = std::sqrt(ju[0] * ju[0] + ju[1] * ju[1] + ju[2] * ju[2] + ju[3] * ju[3]);
            double junctionAccel = kSomeLargeValue;
            for (std::size_t i = 0; i < 4; ++i) {
                if (ju[i] != 0) {
                    junctionAccel = std::min(junctionAccel, config_.accel[i] / std::fabs(ju[i] / jl));
                }
            }
            const double sinThetaD2 = std::sqrt(0.5 * (1 - cosTheta));
            junctionSqr = (junctionAccel * config_.junctionDeviation * sinThetaD2) / (1 - sinThetaD2);
        }
        maxEntrySqr = std::min({junctionSqr, nominalSqr, prevNominalSqr_});
    }

    prevUnit_ = u;
    prevNominalSqr_ = nominalSqr;
    hasPrev_ = true;

    // Push into the window.
    const std::size_t idx = (head_ + count_) % cap_;
    bLine_[idx] = line;
    bMm_[idx] = mm;
    bAccel_[idx] = accel;
    bNominalSqr_[idx] = nominalSqr;
    bMaxEntrySqr_[idx] = maxEntrySqr;
    bRapid_[idx] = isRapid ? 1 : 0;
    bRevEntrySqr_[idx] = std::min(maxEntrySqr, 2 * accel * mm);
    ++count_;

    // Reverse pass: newest block must be able to stop; propagate backwards
    // until an entry speed stops changing (same early-exit as grbl).
    std::size_t next = idx;
    for (std::ptrdiff_t k = static_cast<std::ptrdiff_t>(count_) - 2; k >= 1; --k) {
        const std::size_t i = (head_ + static_cast<std::size_t>(k)) % cap_;
        const double v = std::min(bMaxEntrySqr_[i], bRevEntrySqr_[next] + 2 * bAccel_[i] * bMm_[i]);
        if (v == bRevEntrySqr_[i]) {
            break;
        }
        bRevEntrySqr_[i] = v;
        next = i;
    }

    if (count_ >= static_cast<std::size_t>(config_.plannerBlocks)) {
        finalizeOldest();
    }
}

// Execute the oldest block with the lookahead currently in the window.
void MotionPlanner::finalizeOldest() {
    const std::size_t i = head_;
    const double mm = bMm_[i];
    const double accel = bAccel_[i];
    const double entrySqr = tailEntrySqr_;
    double exitSqr = 0;
    if (count_ > 1) {
        const std::size_t n = (i + 1) % cap_;
        exitSqr = std::min(bRevEntrySqr_[n], entrySqr + 2 * accel * mm);
    }
    const double time = trapezoidTime(mm, accel, entrySqr, exitSqr, bNominalSqr_[i]);
    addTime(bLine_[i], time, bRapid_[i] ? kLineKindRapid : kLineKindFeed);

    tailEntrySqr_ = exitSqr;
    head_ = (i + 1) % cap_;
    --count_;
    if (count_ > 0) {
        // the next block's entry is now fixed
        bRevEntrySqr_[head_] = exitSqr;
    }
}

void MotionPlanner::drain() {
    while (count_ > 0) {
        finalizeOldest();
    }
    tailEntrySqr_ = 0;
    hasPrev_ = false;
    prevNominalSqr_ = 0;
}

const EstimateResult& MotionPlanner::finish() {
    if (result_) {
        return *result_;
    }
    drain();
    EstimateResult result;
    result.lineTime = lineTime_;
    result.lineKind = lineKind_;
    if (config_.serialBytesPerSecond > 0) {
        applySerialThroughput(result.lineTime, config_.serialBytesPerSecond);
    }
    double total = 0;
    for (const float t : result.lineTime) {
        total += t;
    }
    result.totalTime = total;
    result_ = std::move(result);
    return *result_;
}

// Queue model of streaming over a slow serial link: a line can't start until
// its bytes have arrived, and bytes can't be sent until the line
// `plannerBlocks` earlier has completed (buffer slot free).
void MotionPlanner::applySerialThroughput(std::vector<float>& lineTime, double bps) const {
    const auto window = static_cast<std::size_t>(config_.plannerBlocks);
    std::vector<double> completions(window, 0.0);
    double arrival = 0;
    double completion = 0;
    for (std::size_t i = 0; i < lineTime.size(); ++i) {
        const std::uint16_t bytes = lineBytes_[i];
        if (bytes > 0) {
            if (i >= window) {
                arrival = std::max(arrival, completions[i % window]);
            }
            arrival += (bytes + 1) / bps;
        }
        const double start = std::max(completion, arrival);
        const double done = start + lineTime[i];
        lineTime[i] = static_cast<float>(done - completion);
        completion = done;
        completions[i % window] = done;
    }
}

}  // namespace gs::gcode
