#pragma once

// lodash-style property paths into a Boost.JSON document, as gSender's config
// store used them (_.get, _.set, _.has, _.unset): "state.controller",
// "macros[0].name", 'events["gcode:start"]'.

#include <boost/json/value.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace gs::config {

// _.toPath: "a.b[0]['c.d']" -> {"a", "b", "0", "c.d"}.
std::vector<std::string> toPath(std::string_view path);

// The value at `path`, or nullptr when any part of it is missing.
const boost::json::value* findPath(const boost::json::value& root, std::string_view path);
boost::json::value* findPath(boost::json::value& root, std::string_view path);

bool hasPath(const boost::json::value& root, std::string_view path);

// _.set: missing or scalar intermediates become containers - an array when
// the next key is an index, an object otherwise. Arrays grow with nulls.
void setPath(boost::json::value& root, std::string_view path, boost::json::value value);

// _.unset: removes an object member; an array element becomes null (a hole).
// Returns false when the parent does not exist.
bool unsetPath(boost::json::value& root, std::string_view path);

}  // namespace gs::config
