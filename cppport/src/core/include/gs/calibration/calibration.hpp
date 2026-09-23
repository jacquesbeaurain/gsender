#pragma once

// The calibration tools' arithmetic and G-code: Movement Tuning (an axis'
// steps/mm from a measured move) and XY Squaring (the angle between X and Y
// from a measured triangle). Ported from src/app/src/features/MovementTuning
// (utils/index.ts, Steps/index.tsx) and features/Squaring (utils/index.ts,
// context/SquaringContext.tsx, Steps/ResultsStep.tsx).

#include <string>
#include <vector>

namespace gs::calibration {

// ---- Movement Tuning ----

// getEEPROMSettingKey(): the steps/mm setting of X, Y or Z - $100, $101, $102.
std::string stepsSetting(char axis);
// The distance offered to move: 100 mm (4 in), Z -50 mm (-2 in) - downwards.
double defaultTuningDistance(char axis, bool metric);
// calculateNewStepsPerMM(): original x moved / measured, to 2 decimals; 0
// when nothing was measured.
double newStepsPerMm(double original, double moved, double measured);
// The move: jogAxis({X: distance}, 1000) - "$J=G21 G91 X100 F1000".
// Deviation: in an inch workspace the feed is 1000 mm/min in inches
// (F39.37); upstream wrote F1000 into the G20 jog (1000 in/min, capped only
// by the machine's max rate).
std::string tuningMove(char axis, double distance, bool metric);
// The "off by" figure: moved - measured to at most 4 decimals
// (toFixedIfNecessary).
double tuningError(double moved, double measured);
// Writing the new value: "$100=98.04" then "$$".
std::vector<std::string> tuningUpdateCommands(char axis, double stepsPerMm);

// ---- XY Squaring ----

// The measured triangle: a = points 1-2 (along X), b = 2-3 (along Y),
// c = 1-3 (the diagonal); workspace units.
struct Triangle {
    double a = 0;
    double b = 0;
    double c = 0;
};

// How far X and Y were moved to mark the points (workspace units).
struct SquaringMoves {
    double x = 0;
    double y = 0;
};

// The distance moved along each axis to mark the points: 300 mm (12 in).
double defaultSquaringDistance(bool metric);
// The move to the next point: "G91 G21 G0 X300" - sent with `gcode`,
// leaving G91 modal (as upstream).
std::string squaringMove(char axis, double distance, bool metric);
// calculateAngle(): how far the corner at point 2 is from 90 degrees (law
// of cosines); positive when the diagonal is shorter than square. NaN for
// sides that make no triangle.
double squaringAngle(const Triangle& triangle);
// calculateHypotenuse(): the diagonal a square machine would have.
double squareDiagonal(const Triangle& triangle);

struct StepAdjustment {
    bool needed = false;
    double stepsPerMm = 0;  // the recommended value (the current one when no data)
};
struct StepsAdjustment {
    StepAdjustment x;
    StepAdjustment y;
};
// determineEEPROMAdjustment(): each axis' steps/mm scaled by moved /
// measured, where both are positive; needed when it changes by more than
// 0.1 %.
StepsAdjustment stepAdjustment(const Triangle& triangle, const SquaringMoves& moves, double currentX,
                               double currentY);

enum class Squareness {
    Square,           // within 0.1 degrees
    SlightlyOut,      // the diagonal within 2 mm (0.079 in) of square
    NeedsAdjustment,  // anything else
};
struct SquaringResult {
    double angle = 0;
    std::string diagonalError;  // |square diagonal - c|, toFixed(2)
    Squareness verdict = Squareness::Square;
};
// ResultsStep's verdict. Upstream fixes the threshold's units when the
// module loads; here it follows the workspace units given.
SquaringResult squaringResult(const Triangle& triangle, bool metric);
// Writing the recommendation: "$100=103.448", "$101=100.000", "$$" - both
// axes, as upstream.
std::vector<std::string> squaringUpdateCommands(const StepsAdjustment& adjustment);

}  // namespace gs::calibration
