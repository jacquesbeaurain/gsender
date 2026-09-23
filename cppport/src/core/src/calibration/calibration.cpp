#include "gs/calibration/calibration.hpp"

#include "gs/controller/jogging.hpp"
#include "gs/util/jsnumber.hpp"
#include "gs/util/units.hpp"

#include <cctype>
#include <cmath>
#include <numbers>

namespace gs::calibration {
namespace {

char upper(char axis) {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(axis)));
}

// Number(value.toFixed(digits)), keeping NaN and the infinities.
double fixed(double value, int digits) {
    return std::isfinite(value) ? js::stringToNumber(js::toFixed(value, digits)) : value;
}

}  // namespace

// ---- Movement Tuning ----------------------------------------------------------------------

std::string stepsSetting(char axis) {
    switch (upper(axis)) {
        case 'Y': return "$101";
        case 'Z': return "$102";
        default: return "$100";
    }
}

double defaultTuningDistance(char axis, bool metric) {
    if (upper(axis) == 'Z') {
        return metric ? -50 : -2;
    }
    return metric ? 100 : 4;
}

double newStepsPerMm(double original, double moved, double measured) {
    if (measured == 0) {
        return 0;
    }
    return fixed(original * (moved / measured), 2);
}

std::string tuningMove(char axis, double distance, bool metric) {
    const double feed = metric ? 1000 : units::convertToImperial(1000);
    return controller::jogCommand({{upper(axis), distance}}, feed, metric);
}

double tuningError(double moved, double measured) {
    return fixed(moved - measured, 4);
}

std::vector<std::string> tuningUpdateCommands(char axis, double stepsPerMm) {
    return {stepsSetting(axis) + "=" + js::numberToString(stepsPerMm), "$$"};
}

// ---- XY Squaring ---------------------------------------------------------------------------

double defaultSquaringDistance(bool metric) {
    return metric ? 300 : 12;
}

std::string squaringMove(char axis, double distance, bool metric) {
    return std::string("G91 ") + (metric ? "G21" : "G20") + " G0 " + upper(axis) + js::numberToString(distance);
}

double squaringAngle(const Triangle& t) {
    const double cosC = (t.a * t.a + t.b * t.b - t.c * t.c) / (2 * t.a * t.b);
    const double degrees = std::acos(cosC) * 180 / std::numbers::pi;
    return 90 - degrees;
}

double squareDiagonal(const Triangle& t) {
    return std::sqrt(t.a * t.a + t.b * t.b);
}

StepsAdjustment stepAdjustment(const Triangle& triangle, const SquaringMoves& moves, double currentX,
                               double currentY) {
    constexpr double kThreshold = 0.001;  // STEP_ADJUSTMENT_THRESHOLD
    const auto adjust = [](bool valid, double current, double moved, double measured) {
        const double steps = valid ? current * (moved / measured) : current;
        const double change = std::fabs((steps - current) / current);
        return StepAdjustment{valid && change > kThreshold, steps};
    };
    return {adjust(moves.x > 0 && triangle.a > 0, currentX, moves.x, triangle.a),
            adjust(moves.y > 0 && triangle.b > 0, currentY, moves.y, triangle.b)};
}

SquaringResult squaringResult(const Triangle& triangle, bool metric) {
    SquaringResult result;
    result.angle = squaringAngle(triangle);
    result.diagonalError = js::toFixed(std::fabs(squareDiagonal(triangle) - triangle.c), 2);
    const double threshold = metric ? 2 : 0.079;  // FM_LOWER_OFFSET_THRESHOLD
    if (std::fabs(result.angle) < 0.1) {
        result.verdict = Squareness::Square;
    } else if (js::stringToNumber(result.diagonalError) <= threshold) {
        result.verdict = Squareness::SlightlyOut;
    } else {
        result.verdict = Squareness::NeedsAdjustment;
    }
    return result;
}

std::vector<std::string> squaringUpdateCommands(const StepsAdjustment& adjustment) {
    return {"$100=" + js::toFixed(adjustment.x.stepsPerMm, 3), "$101=" + js::toFixed(adjustment.y.stepsPerMm, 3),
            "$$"};
}

}  // namespace gs::calibration
