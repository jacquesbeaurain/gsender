#pragma once

// A search through a program's lines, as the G-code editor and Step Through
// run it: the lines (0-based, ascending) that contain the words sought,
// ignoring ASCII case, and a current match that steps forwards and back
// around the ends.

#include <cstddef>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gs::util {

class LineMatches {
public:
    static constexpr std::size_t kNoLimit = std::numeric_limits<std::size_t>::max();

    // The lines containing `needle` (trimmed; empty finds nothing), at most
    // `limit` of them. The first match becomes the current one.
    void find(const std::vector<std::string>& lines, std::string_view needle, std::size_t limit = kNoLimit);
    void clear();

    const std::vector<std::size_t>& lines() const noexcept { return lines_; }
    std::size_t size() const noexcept { return lines_.size(); }
    bool empty() const noexcept { return lines_.empty(); }
    bool contains(std::size_t line) const;

    // The current match (its index among the matches), and its line.
    std::optional<std::size_t> current() const noexcept { return current_; }
    std::optional<std::size_t> currentLine() const;
    void next();      // the one after, the first after the last
    void previous();  // the one before, the last before the first

    // The first match after `line`, or the first of all past the last.
    std::optional<std::size_t> after(std::size_t line) const;

private:
    std::vector<std::size_t> lines_;
    std::optional<std::size_t> current_;
};

}  // namespace gs::util
