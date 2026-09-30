#include "gs/util/line_matches.hpp"

#include "gs/util/strings.hpp"

#include <algorithm>

namespace gs::util {

void LineMatches::find(const std::vector<std::string>& lines, std::string_view needle, std::size_t limit) {
    clear();
    needle = str::trim(needle);
    if (needle.empty()) {
        return;
    }
    for (std::size_t i = 0; i < lines.size() && lines_.size() < limit; ++i) {
        if (str::icontains(lines[i], needle)) {
            lines_.push_back(i);
        }
    }
    if (!lines_.empty()) {
        current_ = 0;
    }
}

void LineMatches::clear() {
    lines_.clear();
    current_.reset();
}

bool LineMatches::contains(std::size_t line) const {
    return std::binary_search(lines_.begin(), lines_.end(), line);
}

std::optional<std::size_t> LineMatches::currentLine() const {
    return current_ ? std::optional<std::size_t>(lines_[*current_]) : std::nullopt;
}

void LineMatches::next() {
    if (current_) {
        current_ = (*current_ + 1) % lines_.size();
    }
}

void LineMatches::previous() {
    if (current_) {
        current_ = *current_ > 0 ? *current_ - 1 : lines_.size() - 1;
    }
}

std::optional<std::size_t> LineMatches::after(std::size_t line) const {
    if (lines_.empty()) {
        return std::nullopt;
    }
    const auto it = std::upper_bound(lines_.begin(), lines_.end(), line);
    return it != lines_.end() ? *it : lines_.front();
}

}  // namespace gs::util
