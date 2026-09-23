// The calibration tools. The first groups port upstream's
// MovementTuning/tests/movement_tuning.test.tsx and
// Squaring/tests/XY_Squaring.test.tsx.

#include "gs/calibration/calibration.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <string>
#include <vector>

using namespace gs::calibration;

namespace {

using Lines = std::vector<std::string>;

}  // namespace

// ---- movement_tuning.test.tsx ----

TEST(MovementTuning, NewStepsPerMmScalesByMovedOverMeasured) {
    EXPECT_NEAR(newStepsPerMm(100, 100, 102), 98.04, 0.005);
    EXPECT_NEAR(newStepsPerMm(80, 50, 50), 80, 1e-4);
    EXPECT_NEAR(newStepsPerMm(100, 100, 90), 111.11, 0.005);
    EXPECT_NEAR(newStepsPerMm(100, 100, 110), 90.91, 0.005);
    EXPECT_NEAR(newStepsPerMm(80.25, 100, 100), 80.25, 1e-4);
    EXPECT_NEAR(newStepsPerMm(100, 50.5, 51.0), 99.02, 0.005);
    // Rounded to 2 decimals, exactly as Number(x.toFixed(2)).
    EXPECT_EQ(newStepsPerMm(100, 100, 102), 98.04);
    EXPECT_EQ(newStepsPerMm(200, 100, 102), 196.08);
}

TEST(MovementTuning, EdgeCases) {
    EXPECT_EQ(newStepsPerMm(100, 100, 0), 0);  // nothing measured
    EXPECT_NEAR(newStepsPerMm(100, 0.001, 0.001), 100, 1e-4);
    EXPECT_NEAR(newStepsPerMm(0.01, 100, 100), 0.01, 1e-4);
    EXPECT_NEAR(newStepsPerMm(10000, 100, 100), 10000, 0.005);
    EXPECT_TRUE(std::isnan(newStepsPerMm(NAN, 100, 100)));
    EXPECT_TRUE(std::isinf(newStepsPerMm(100, INFINITY, 100)));
    EXPECT_EQ(newStepsPerMm(100, 0, 100), 0);
    EXPECT_EQ(newStepsPerMm(0, 100, 100), 0);
    EXPECT_LE(newStepsPerMm(100, 100, -50), 0);
    EXPECT_EQ(newStepsPerMm(100, -100, 100), -100);
    EXPECT_EQ(newStepsPerMm(-100, 100, 100), -100);
}

TEST(MovementTuning, MovesUpdatesAndDefaults) {
    EXPECT_EQ(stepsSetting('x'), "$100");
    EXPECT_EQ(stepsSetting('Y'), "$101");
    EXPECT_EQ(stepsSetting('z'), "$102");
    EXPECT_EQ(defaultTuningDistance('x', true), 100);
    EXPECT_EQ(defaultTuningDistance('z', true), -50);
    EXPECT_EQ(defaultTuningDistance('y', false), 4);
    EXPECT_EQ(defaultTuningDistance('z', false), -2);
    EXPECT_EQ(tuningMove('x', 100, true), "$J=G21 G91 X100 F1000");
    EXPECT_EQ(tuningMove('z', -50, true), "$J=G21 G91 Z-50 F1000");
    EXPECT_EQ(tuningMove('y', 4, false), "$J=G20 G91 Y4 F39.37");
    EXPECT_EQ(tuningUpdateCommands('x', 98.04), (Lines{"$100=98.04", "$$"}));
    EXPECT_EQ(tuningError(100, 102), -2);
    EXPECT_EQ(tuningError(100, 99.12344), 0.8766);
}

// ---- XY_Squaring.test.tsx ----

TEST(XYSquaring, SquareDiagonalIsTheHypotenuse) {
    EXPECT_NEAR(squareDiagonal({3, 4, 0}), 5, 1e-5);
    EXPECT_NEAR(squareDiagonal({1, 1, 0}), std::sqrt(2.0), 1e-5);
    EXPECT_NEAR(squareDiagonal({300, 300, 0}), std::sqrt(300.0 * 300 + 300 * 300), 0.005);
    EXPECT_NEAR(squareDiagonal({-3, -2, -6}), std::sqrt(9.0 + 4), 0.005);
    EXPECT_NEAR(squareDiagonal({3.2, 5.5, 6.6}), std::sqrt(3.2 * 3.2 + 5.5 * 5.5), 0.005);
    EXPECT_EQ(squareDiagonal({0, 0, 0}), 0);
    EXPECT_EQ(squareDiagonal({3, 4, 0}), squareDiagonal({3, 4, 999}));  // c is ignored
}

TEST(XYSquaring, AngleIsTheCornersDeviationFromNinetyDegrees) {
    EXPECT_NEAR(squaringAngle({3, 4, 5}), 0, 0.05);
    EXPECT_GT(squaringAngle({300, 300, 400}), 0);  // diagonal short
    EXPECT_LT(squaringAngle({300, 300, 450}), 0);  // diagonal long
    EXPECT_TRUE(std::isnan(squaringAngle({0, 0, 0})));
    for (const auto& [a, b] : {std::pair{300.0, 300.0}, {0.3, 0.4}, {5.5, 6.6}}) {
        EXPECT_NEAR(std::fabs(squaringAngle({a, b, std::sqrt(a * a + b * b)})), 0, 1e-5) << a << " " << b;
    }
    EXPECT_FALSE(std::isnan(squaringAngle({100, 150, 180})));
}

TEST(XYSquaring, StepAdjustmentNeedsMovesAndMeasurements) {
    StepsAdjustment r = stepAdjustment({300, 300, 424}, {0, 0}, 100, 100);
    EXPECT_FALSE(r.x.needed);
    EXPECT_FALSE(r.y.needed);
    r = stepAdjustment({0, 0, 0}, {300, 300}, 100, 100);
    EXPECT_FALSE(r.x.needed);
    EXPECT_FALSE(r.y.needed);
    r = stepAdjustment({0, 0, 0}, {0, 0}, 100, 100);  // the current values
    EXPECT_EQ(r.x.stepsPerMm, 100);
    EXPECT_EQ(r.y.stepsPerMm, 100);
}

TEST(XYSquaring, StepAdjustmentScalesByMovedOverMeasured) {
    StepsAdjustment r = stepAdjustment({290, 300, 424}, {300, 300}, 100, 100);
    EXPECT_TRUE(r.x.needed);
    EXPECT_NEAR(r.x.stepsPerMm, 100 * (300.0 / 290), 1e-3);
    r = stepAdjustment({300, 300, 424}, {300, 300}, 100, 100);
    EXPECT_FALSE(r.x.needed);
    EXPECT_FALSE(r.y.needed);
    r = stepAdjustment({290, 290, 424}, {300, 300}, 80, 160);
    EXPECT_NEAR(r.x.stepsPerMm, 80 * (300.0 / 290), 1e-3);
    EXPECT_NEAR(r.y.stepsPerMm, 160 * (300.0 / 290), 1e-3);
    r = stepAdjustment({290, 0, 424}, {300, 0}, 100, 100);
    EXPECT_TRUE(r.x.needed);
    EXPECT_FALSE(r.y.needed);
    r = stepAdjustment({0, 290, 424}, {0, 300}, 100, 100);
    EXPECT_FALSE(r.x.needed);
    EXPECT_TRUE(r.y.needed);
}

TEST(XYSquaring, AngleAndDiagonalAgree) {
    const double a = 300;
    const double b = 300;
    const double c = std::sqrt(a * a + b * b);
    EXPECT_LT(std::fabs(squaringAngle({a, b, c})), 0.1);
    EXPECT_NEAR(std::fabs(squareDiagonal({a, b, c}) - c), 0, 1e-5);
    EXPECT_GT(std::fabs(squaringAngle({300, 300, 450})), 0.1);
}

// ---- the results step and the G-code ----

TEST(XYSquaring, VerdictsFollowTheAngleThenTheDiagonal) {
    SquaringResult r = squaringResult({300, 300, 424.26}, true);
    EXPECT_EQ(r.verdict, Squareness::Square);
    EXPECT_EQ(r.diagonalError, "0.00");
    r = squaringResult({300, 300, 425.5}, true);  // 1.24 mm off: slight
    EXPECT_EQ(r.verdict, Squareness::SlightlyOut);
    EXPECT_EQ(r.diagonalError, "1.24");
    r = squaringResult({300, 300, 430}, true);
    EXPECT_EQ(r.verdict, Squareness::NeedsAdjustment);
    EXPECT_EQ(r.diagonalError, "5.74");
    EXPECT_NEAR(r.angle, -1.56, 0.01);
    // Inches: 0.079 in is the slight limit.
    EXPECT_EQ(squaringResult({12, 12, 17.0}, false).verdict, Squareness::SlightlyOut);
    EXPECT_EQ(squaringResult({12, 12, 17.1}, false).verdict, Squareness::NeedsAdjustment);
}

TEST(XYSquaring, MovesAndUpdates) {
    EXPECT_EQ(defaultSquaringDistance(true), 300);
    EXPECT_EQ(defaultSquaringDistance(false), 12);
    EXPECT_EQ(squaringMove('x', 300, true), "G91 G21 G0 X300");
    EXPECT_EQ(squaringMove('Y', 12, false), "G91 G20 G0 Y12");
    const StepsAdjustment r = stepAdjustment({290, 300, 424}, {300, 300}, 100, 100);
    EXPECT_EQ(squaringUpdateCommands(r), (Lines{"$100=103.448", "$101=100.000", "$$"}));
}
