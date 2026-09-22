#pragma once

// Internal: built-in members of primitive values ("abc".length, (1.5).toFixed).

#include "gs/expr/value.hpp"

#include <optional>
#include <string_view>

namespace gs::expr::detail {

// Property `name` of a string, number, boolean or array. nullopt when the
// property does not exist (the caller then yields undefined).
std::optional<Value> primitiveMember(const Value& base, std::string_view name);

}  // namespace gs::expr::detail
