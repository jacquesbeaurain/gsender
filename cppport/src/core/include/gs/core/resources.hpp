#pragma once

// Data files compiled into gs_core (see cmake/GsEmbed.cmake).

#include <optional>
#include <string_view>
#include <vector>

namespace gs::resources {

// Contents of an embedded file, by path relative to cppport/resources.
std::optional<std::string_view> find(std::string_view name);

// Every embedded resource path.
std::vector<std::string_view> names();

}  // namespace gs::resources
