#include "gs/protocol/types.hpp"

#include <algorithm>

namespace gs::protocol {

double AxisValues::axis(char name) const noexcept {
    const auto it = std::find(kAxisNames.begin(), kAxisNames.end(), name);
    if (it == kAxisNames.end()) {
        return 0;
    }
    const auto index = static_cast<std::size_t>(it - kAxisNames.begin());
    return index < count ? values[index] : 0;
}

bool AxisValues::has(char name) const noexcept {
    const auto it = std::find(kAxisNames.begin(), kAxisNames.end(), name);
    return it != kAxisNames.end() && static_cast<std::size_t>(it - kAxisNames.begin()) < count;
}

const std::string* OrderedMap::find(std::string_view key) const {
    for (const auto& [name, value] : items_) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

std::string OrderedMap::get(std::string_view key, std::string_view fallback) const {
    const std::string* value = find(key);
    return value ? *value : std::string(fallback);
}

bool OrderedMap::set(std::string_view key, std::string value) {
    for (auto& [name, existing] : items_) {
        if (name == key) {
            if (existing == value) {
                return false;
            }
            existing = std::move(value);
            return true;
        }
    }
    items_.emplace_back(std::string(key), std::move(value));
    return true;
}

std::optional<std::string> InfoValue::option(std::string_view key) const {
    for (const auto& [name, value] : options) {
        if (name == key) {
            return value;
        }
    }
    return std::nullopt;
}

}  // namespace gs::protocol
