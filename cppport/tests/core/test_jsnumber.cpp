#include "gs/util/jsnumber.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace gs::js;

TEST(JsNumberToString, IntegersAndFractions) {
    EXPECT_EQ(numberToString(0), "0");
    EXPECT_EQ(numberToString(-0.0), "0");
    EXPECT_EQ(numberToString(1), "1");
    EXPECT_EQ(numberToString(100), "100");
    EXPECT_EQ(numberToString(1.5), "1.5");
    EXPECT_EQ(numberToString(-12.5), "-12.5");
    EXPECT_EQ(numberToString(25.4), "25.4");
    EXPECT_EQ(numberToString(123.456), "123.456");
    EXPECT_EQ(numberToString(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(numberToString(1.0 / 3.0), "0.3333333333333333");
    EXPECT_EQ(numberToString(9007199254740992.0), "9007199254740992");
}

TEST(JsNumberToString, ExponentThresholds) {
    EXPECT_EQ(numberToString(1e20), "100000000000000000000");
    EXPECT_EQ(numberToString(1e21), "1e+21");
    EXPECT_EQ(numberToString(1.5e22), "1.5e+22");
    EXPECT_EQ(numberToString(0.000001), "0.000001");
    EXPECT_EQ(numberToString(1.2345e-6), "0.0000012345");
    EXPECT_EQ(numberToString(1e-7), "1e-7");
    EXPECT_EQ(numberToString(1.5e-7), "1.5e-7");
}

TEST(JsNumberToString, SpecialValues) {
    EXPECT_EQ(numberToString(std::numeric_limits<double>::quiet_NaN()), "NaN");
    EXPECT_EQ(numberToString(std::numeric_limits<double>::infinity()), "Infinity");
    EXPECT_EQ(numberToString(-std::numeric_limits<double>::infinity()), "-Infinity");
}

TEST(JsToFixed, RoundsTiesAwayFromZeroOnExactValue) {
    EXPECT_EQ(toFixed(2.5, 0), "3");
    EXPECT_EQ(toFixed(0.5, 0), "1");
    EXPECT_EQ(toFixed(1.5, 0), "2");
    EXPECT_EQ(toFixed(-2.5, 0), "-3");
    EXPECT_EQ(toFixed(0.125, 2), "0.13");
    // 1.005 is really 1.00499999999999989..., so it rounds down.
    EXPECT_EQ(toFixed(1.005, 2), "1.00");
}

TEST(JsToFixed, FormatsLikeJavaScript) {
    EXPECT_EQ(toFixed(0, 2), "0.00");
    EXPECT_EQ(toFixed(-0.0001, 2), "-0.00");
    EXPECT_EQ(toFixed(12.3456, 3), "12.346");
    EXPECT_EQ(toFixed(123.4, 3), "123.400");
    EXPECT_EQ(toFixed(-5.25, 3), "-5.250");
    EXPECT_EQ(toFixed(99.9996, 3), "100.000");
    EXPECT_EQ(toFixed(1e21, 2), "1e+21");
    EXPECT_EQ(toFixed(std::numeric_limits<double>::quiet_NaN(), 2), "NaN");
}

TEST(JsStringToNumber, ValidLiterals) {
    EXPECT_EQ(stringToNumber(""), 0);
    EXPECT_EQ(stringToNumber("   "), 0);
    EXPECT_EQ(stringToNumber("  12.5  "), 12.5);
    EXPECT_EQ(stringToNumber("1."), 1);
    EXPECT_EQ(stringToNumber(".5"), 0.5);
    EXPECT_EQ(stringToNumber("-.5"), -0.5);
    EXPECT_EQ(stringToNumber("+3"), 3);
    EXPECT_EQ(stringToNumber("1e3"), 1000);
    EXPECT_EQ(stringToNumber("0x1A"), 26);
    EXPECT_EQ(stringToNumber("Infinity"), std::numeric_limits<double>::infinity());
    EXPECT_EQ(stringToNumber("1e400"), std::numeric_limits<double>::infinity());
    EXPECT_EQ(stringToNumber("0.000"), 0);
}

TEST(JsStringToNumber, InvalidLiterals) {
    EXPECT_TRUE(std::isnan(stringToNumber("1.2.3")));
    EXPECT_TRUE(std::isnan(stringToNumber("-")));
    EXPECT_TRUE(std::isnan(stringToNumber("--1")));
    EXPECT_TRUE(std::isnan(stringToNumber("abc")));
    EXPECT_TRUE(std::isnan(stringToNumber("1-")));
    EXPECT_TRUE(std::isnan(stringToNumber(".")));
    EXPECT_TRUE(std::isnan(stringToNumber("12px")));
}

TEST(JsParseFloat, TakesLongestNumericPrefix) {
    EXPECT_EQ(parseFloat("3.14abc"), 3.14);
    EXPECT_EQ(parseFloat("  -2.5e2x"), -250);
    EXPECT_EQ(parseFloat(".5"), 0.5);
    EXPECT_EQ(parseFloat("1e"), 1);
    EXPECT_EQ(parseFloat("7."), 7);
    EXPECT_TRUE(std::isnan(parseFloat("abc")));
    EXPECT_TRUE(std::isnan(parseFloat("-.x")));
}

TEST(JsParseInt, ParsesPrefixInRadix) {
    EXPECT_EQ(parseInt("42px"), 42);
    EXPECT_EQ(parseInt("0x1F", 0), 31);
    EXPECT_EQ(parseInt("0x1F", 16), 31);
    EXPECT_EQ(parseInt("-12.7"), -12);
    EXPECT_EQ(parseInt("08"), 8);
    EXPECT_EQ(parseInt("ff", 16), 255);
    EXPECT_TRUE(std::isnan(parseInt("abc")));
}

TEST(JsMathRound, HalvesRoundUp) {
    EXPECT_EQ(mathRound(2.5), 3);
    EXPECT_EQ(mathRound(-2.5), -2);
    EXPECT_EQ(mathRound(0.49999999999999994), 0);
    EXPECT_EQ(mathRound(1.4), 1);
    EXPECT_EQ(mathRound(-1.6), -2);
}
