// Workspace unit conversions and the DRO's position text (units.ts, the
// jogging widget's convertValue).

#include "gs/util/units.hpp"

#include <gtest/gtest.h>

using namespace gs::units;

namespace {

TEST(Units, ConversionsRoundAsUpstream) {
    EXPECT_EQ(convertToImperial(6.35), 0.25);
    EXPECT_EQ(convertToImperial(150), 5.906);
    EXPECT_EQ(convertToMetric(0.25), 6.35);
    EXPECT_EQ(convertToMetric(1.2), 30.48);
    EXPECT_EQ(convertValue(3000, true, false), 118.11);  // jog feed, mm/min -> in/min
    EXPECT_EQ(convertValue(0.5, true, false), 0.02);
    EXPECT_EQ(convertValue(0.2, false, true), 5.08);
    EXPECT_EQ(convertValue(1.23456, true, true), 1.235);  // same units: just rounded
}

TEST(Units, PositionsShowTwoDecimalsInMmAndThreeInInches) {
    EXPECT_EQ(positionText(12.3456, true), "12.35");
    EXPECT_EQ(positionText(12.345, true), "12.35");  // 12.345 is 12.3450000000000006
    EXPECT_EQ(positionText(-0.001, true), "0.00");  // no negative zero in mm
    EXPECT_EQ(positionText(25.4, false), "1.000");
    EXPECT_EQ(positionText(-0.001, false), "-0.000");  // upstream keeps it in inches
    EXPECT_EQ(positionText(1.23456, true, 4), "1.2346");
    EXPECT_EQ(positionText(25.4, false, 1), "1.0");
}

}  // namespace
