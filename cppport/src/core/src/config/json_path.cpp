#include "gs/config/json_path.hpp"

#include <boost/json.hpp>

#include <charconv>
#include <optional>

namespace gs::config {
namespace {

namespace json = boost::json;

// lodash isIndex: a non-negative integer without leading zeros.
std::optional<std::size_t> asIndex(std::string_view key) {
    if (key.empty() || (key.size() > 1 && key.front() == '0')) {
        return std::nullopt;
    }
    std::size_t index = 0;
    const auto [end, ec] = std::from_chars(key.data(), key.data() + key.size(), index);
    if (ec != std::errc() || end != key.data() + key.size()) {
        return std::nullopt;
    }
    return index;
}

const json::value* child(const json::value& parent, std::string_view key) {
    if (const json::object* object = parent.if_object()) {
        return object->if_contains(key);
    }
    if (const json::array* array = parent.if_array()) {
        if (const auto index = asIndex(key); index && *index < array->size()) {
            return &(*array)[*index];
        }
    }
    return nullptr;
}

}  // namespace

std::vector<std::string> toPath(std::string_view path) {
    std::vector<std::string> keys;
    if (path.find_first_of(".[") == std::string_view::npos) {
        keys.emplace_back(path);  // a plain key
        return keys;
    }
    std::size_t i = 0;
    if (!path.empty() && path.front() == '.') {
        keys.emplace_back();  // lodash: a leading dot is an empty key
    }
    while (i < path.size()) {
        const char c = path[i];
        if (c == '.') {
            ++i;
            continue;
        }
        if (c == '[') {
            const std::size_t close = path.find(']', i + 1);
            if (close == std::string_view::npos) {
                keys.emplace_back(path.substr(i + 1));
                break;
            }
            std::string_view inner = path.substr(i + 1, close - i - 1);
            const char quote = inner.empty() ? '\0' : inner.front();
            if ((quote == '"' || quote == '\'') && inner.size() >= 2 && inner.back() == quote) {
                // Quoted key: backslash escapes the next character.
                std::string key;
                for (std::size_t k = 1; k + 1 < inner.size(); ++k) {
                    if (inner[k] == '\\' && k + 2 < inner.size()) {
                        ++k;
                    }
                    key.push_back(inner[k]);
                }
                keys.push_back(std::move(key));
            } else {
                keys.emplace_back(inner);
            }
            i = close + 1;
            continue;
        }
        const std::size_t end = path.find_first_of(".[", i);
        keys.emplace_back(path.substr(i, end == std::string_view::npos ? std::string_view::npos : end - i));
        i = end == std::string_view::npos ? path.size() : end;
    }
    return keys;
}

const json::value* findPath(const json::value& root, std::string_view path) {
    const json::value* current = &root;
    for (const std::string& key : toPath(path)) {
        current = child(*current, key);
        if (!current) {
            return nullptr;
        }
    }
    return current;
}

json::value* findPath(json::value& root, std::string_view path) {
    return const_cast<json::value*>(findPath(static_cast<const json::value&>(root), path));
}

bool hasPath(const json::value& root, std::string_view path) {
    return findPath(root, path) != nullptr;
}

void setPath(json::value& root, std::string_view path, json::value value) {
    const std::vector<std::string> keys = toPath(path);
    if (keys.empty() || !root.is_structured()) {
        return;
    }
    json::value* current = &root;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const std::string& key = keys[i];
        const bool last = i + 1 == keys.size();
        json::value* slot = nullptr;
        if (json::object* object = current->if_object()) {
            slot = &(*object)[key];
        } else if (json::array* array = current->if_array()) {
            const auto index = asIndex(key);
            if (!index) {
                return;  // a named property on an array does not survive JSON
            }
            if (*index >= array->size()) {
                array->resize(*index + 1);  // holes are null
            }
            slot = &(*array)[*index];
        } else {
            return;
        }
        if (last) {
            *slot = std::move(value);
            return;
        }
        if (!slot->is_structured()) {
            if (asIndex(keys[i + 1])) {
                *slot = json::array();
            } else {
                *slot = json::object();
            }
        }
        current = slot;
    }
}

bool unsetPath(json::value& root, std::string_view path) {
    std::vector<std::string> keys = toPath(path);
    if (keys.empty()) {
        return false;
    }
    const std::string last = std::move(keys.back());
    keys.pop_back();
    json::value* parent = &root;
    for (const std::string& key : keys) {
        parent = const_cast<json::value*>(child(*parent, key));
        if (!parent) {
            return false;
        }
    }
    if (json::object* object = parent->if_object()) {
        object->erase(last);
        return true;
    }
    if (json::array* array = parent->if_array()) {
        if (const auto index = asIndex(last); index && *index < array->size()) {
            (*array)[*index] = nullptr;
        }
        return true;
    }
    return true;
}

}  // namespace gs::config
