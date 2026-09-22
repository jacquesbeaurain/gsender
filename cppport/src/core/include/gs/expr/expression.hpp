#pragma once

// gSender's macro expression language: a safe JavaScript-expression subset.
//
// Ports evaluate-expression.js, evaluate-assignment-expression.js and
// translate-expression.js. Supported: literals, template strings, arrays,
// objects, identifiers (looked up in a scope object), member access, calls of
// built-in functions (Math.*, Number(), String(), JSON.*, string/number/array
// methods), unary/binary/logical/conditional operators. Anything else - or any
// runtime error such as reading a property of undefined - makes the whole
// expression "unresolved", which is how the JavaScript behaved (it caught the
// exception and returned undefined).
//
// Faithful quirks: an identifier whose value converts to a number (e.g. the
// string "12.345" in posx) evaluates as that number.

#include "gs/expr/value.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace gs::expr {

// Evaluates `source` with `scope` (an object Value) providing the variables.
std::optional<Value> evaluate(std::string_view source, const Value& scope);

// Runs a "%"-line: one or more comma separated assignments such as
// "X0=posx, global.state.wcs=modal.wcs". Assignments write into `scope`,
// creating intermediate objects as needed. Returns false when the source does
// not parse.
bool evaluateAssignments(std::string_view source, const Value& scope);

// Replaces every "[expression]" in `line` with its value; unresolved
// expressions are left untouched (so firmware-side bracket expressions pass
// through).
std::string translateExpressions(std::string_view line, const Value& scope);

// Adds the JavaScript globals gSender exposes to macros (Math, Number, String,
// Boolean, JSON, Date, Object, parseFloat, parseInt) to `scope`.
void installGlobals(const Value& scope);

}  // namespace gs::expr
