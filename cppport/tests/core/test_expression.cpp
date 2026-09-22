#include "gs/expr/expression.hpp"

#include <gtest/gtest.h>

#include <cmath>

using namespace gs::expr;

namespace {

Value makeScope() {
    Value scope = Value::object();
    installGlobals(scope);
    scope.set("posx", Value("10.000"));
    scope.set("posy", Value("-2.500"));
    scope.set("posz", Value("7.250"));
    scope.set("ymax", Value(50));
    Value modal = Value::object();
    modal.set("wcs", Value("G55"));
    modal.set("units", Value("G21"));
    scope.set("modal", modal);
    scope.set("global", Value::object());
    return scope;
}

double number(std::string_view source, const Value& scope) {
    std::optional<Value> v = evaluate(source, scope);
    EXPECT_TRUE(v.has_value()) << source;
    EXPECT_TRUE(v && v->isNumber()) << source;
    return v && v->isNumber() ? v->asNumber() : std::nan("");
}

std::string text(std::string_view source, const Value& scope) {
    std::optional<Value> v = evaluate(source, scope);
    EXPECT_TRUE(v.has_value()) << source;
    return v ? toString(*v) : std::string("<unresolved>");
}

}  // namespace

TEST(Expression, ArithmeticAndPrecedence) {
    const Value scope = makeScope();
    EXPECT_EQ(number("1 + 2 * 3", scope), 7);
    EXPECT_EQ(number("1 + 2 * 3 - 4 / 2", scope), 5);
    EXPECT_EQ(number("(1 + 2) * 3", scope), 9);
    EXPECT_EQ(number("2 ** 3 ** 2", scope), 512);
    EXPECT_EQ(number("-(3) + +'4'", scope), 1);
    EXPECT_EQ(number("7 % 4", scope), 3);
    EXPECT_EQ(number("5 & 3", scope), 1);
    EXPECT_EQ(number("~0", scope), -1);
    EXPECT_EQ(number("1 << 4", scope), 16);
    EXPECT_EQ(number("-16 >> 2", scope), -4);
}

TEST(Expression, NumericVariablesConvert) {
    const Value scope = makeScope();
    EXPECT_EQ(number("posx - 8", scope), 2);
    EXPECT_EQ(number("posy * 2", scope), -5);
    EXPECT_EQ(text("typeof posx", scope), "number");
    EXPECT_EQ(text("typeof nothing", scope), "undefined");
}

TEST(Expression, StringsTemplatesAndMethods) {
    const Value scope = makeScope();
    EXPECT_EQ(text("'G' + 54", scope), "G54");
    EXPECT_EQ(text("'X' + (1.5).toFixed(2)", scope), "X1.50");
    EXPECT_EQ(text("`X${posx} Y${posy}`", scope), "X10 Y-2.5");
    EXPECT_EQ(text("'a,b,c'.split(',')[1]", scope), "b");
    EXPECT_EQ(text("modal.wcs.toLowerCase()", scope), "g55");
    EXPECT_EQ(number("'hello'.length", scope), 5);
    EXPECT_EQ(text("'abc'.slice(-2)", scope), "bc");
    EXPECT_EQ(text("'7'.padStart(3, '0')", scope), "007");
}

TEST(Expression, ComparisonAndLogic) {
    const Value scope = makeScope();
    EXPECT_EQ(text("posz > 5 ? 'up' : 'down'", scope), "up");
    EXPECT_EQ(text("0 || 'fallback'", scope), "fallback");
    EXPECT_EQ(text("1 && 'second'", scope), "second");
    EXPECT_EQ(text("null ?? 'x'", scope), "x");
    EXPECT_EQ(text("'1' == 1", scope), "true");
    EXPECT_EQ(text("'1' === 1", scope), "false");
    EXPECT_EQ(text("'b' > 'a'", scope), "true");
    // Like gSender's evaluator, `undefined` is only a (missing) variable name.
    EXPECT_FALSE(evaluate("null == undefined", scope));
}

TEST(Expression, GlobalsAreAvailable) {
    const Value scope = makeScope();
    EXPECT_EQ(number("Math.round(2.5)", scope), 3);
    EXPECT_EQ(number("Math.max(1, 5, 3)", scope), 5);
    EXPECT_EQ(text("Math.PI.toFixed(3)", scope), "3.142");
    EXPECT_EQ(number("Number('12.5')", scope), 12.5);
    EXPECT_EQ(number("parseFloat('3.5mm')", scope), 3.5);
    EXPECT_EQ(number("parseInt('0x10')", scope), 16);
    EXPECT_EQ(text("String(1/4)", scope), "0.25");
    EXPECT_EQ(text("Number.isFinite(1)", scope), "true");
    EXPECT_EQ(text("JSON.stringify({a: 1, b: [1, 'x'], c: null})", scope), R"({"a":1,"b":[1,"x"],"c":null})");
    EXPECT_EQ(number("JSON.parse('{\"x\": 5}').x", scope), 5);
    EXPECT_EQ(number("Object.keys(modal).length", scope), 2);
}

TEST(Expression, UnresolvedCases) {
    const Value scope = makeScope();
    EXPECT_FALSE(evaluate("unknownVar + 1", scope));
    EXPECT_FALSE(evaluate("missing.deep.path", scope));
    EXPECT_FALSE(evaluate("modal.nothing.deeper", scope));
    EXPECT_FALSE(evaluate("foo(", scope));
    EXPECT_FALSE(evaluate("#<_x>+1", scope));
    EXPECT_FALSE(evaluate("this", scope));
    EXPECT_FALSE(evaluate("posx()", scope));
    EXPECT_FALSE(evaluate("a = 1", scope));  // assignments only via %-lines
    // Reading an absent property is fine; it is undefined.
    std::optional<Value> absent = evaluate("modal.nothing", scope);
    ASSERT_TRUE(absent);
    EXPECT_TRUE(absent->isUndefined());
}

TEST(Expression, NumberFormattingFollowsJavaScript) {
    const Value scope = makeScope();
    EXPECT_EQ(text("0.1 + 0.2", scope), "0.30000000000000004");
    EXPECT_EQ(text("1 / 3", scope), "0.3333333333333333");
    EXPECT_EQ(text("1e21", scope), "1e+21");
}

TEST(ExpressionAssign, SetsVariablesAndNestedPaths) {
    const Value scope = makeScope();
    EXPECT_TRUE(evaluateAssignments("X0=posx, Y0=posy", scope));
    ASSERT_TRUE(scope.get("X0").isNumber());
    EXPECT_EQ(scope.get("X0").asNumber(), 10);
    EXPECT_EQ(scope.get("Y0").asNumber(), -2.5);

    EXPECT_TRUE(evaluateAssignments("global.state.workspace=modal.wcs", scope));
    EXPECT_EQ(toString(scope.get("global").get("state").get("workspace")), "G55");

    EXPECT_TRUE(evaluateAssignments("count = 1", scope));
    EXPECT_TRUE(evaluateAssignments("count += 2", scope));
    EXPECT_EQ(scope.get("count").asNumber(), 3);

    EXPECT_TRUE(evaluateAssignments("list[2] = 'c'", scope));
    ASSERT_TRUE(scope.get("list").isArray());
    EXPECT_EQ(scope.get("list").arrayData().items.size(), 3u);

    // An unresolved right-hand side assigns undefined, like lodash _.set.
    EXPECT_TRUE(evaluateAssignments("gone = nothing", scope));
    EXPECT_TRUE(scope.has("gone"));
    EXPECT_TRUE(scope.get("gone").isUndefined());

    EXPECT_FALSE(evaluateAssignments("= broken", scope));
}

TEST(ExpressionTranslate, ReplacesBracketExpressions) {
    const Value scope = makeScope();
    EXPECT_EQ(translateExpressions("G0 X[posx - 8] Y[ymax]", scope), "G0 X2 Y50");
    EXPECT_EQ(translateExpressions("G0 X1", scope), "G0 X1");
    // Firmware-side expressions and unknown names pass through untouched.
    EXPECT_EQ(translateExpressions("G0 X[#<_x>+1]", scope), "G0 X[#<_x>+1]");
    EXPECT_EQ(translateExpressions("G0 X[unknown]", scope), "G0 X[unknown]");
    EXPECT_EQ(translateExpressions("[]", scope), "[]");
    EXPECT_EQ(translateExpressions("G0 X[posx", scope), "G0 X[posx");

    evaluateAssignments("global.state.testWCS=modal.wcs", scope);
    EXPECT_EQ(translateExpressions("[global.state.testWCS]", scope), "G55");
    EXPECT_EQ(translateExpressions("[null]", scope), "null");
}
