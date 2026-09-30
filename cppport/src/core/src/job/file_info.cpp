#include "gs/job/file_info.hpp"

#include "gs/util/jsnumber.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace gs::job {
namespace {

// The numbers of a set of words ("F300", "S12000").
std::vector<double> wordValues(const std::vector<std::string>& words) {
    std::vector<double> values;
    for (const std::string& word : words) {
        const double value = js::stringToNumber(word.size() > 1 ? word.substr(1) : std::string());
        if (std::isfinite(value)) {
            values.push_back(value);
        }
    }
    return values;
}

}  // namespace

std::string fileSizeText(std::size_t bytes) {
    const auto size = static_cast<double>(bytes);
    if (size < 1024) {
        return js::numberToString(size) + " Bytes";
    }
    if (size < 1024 * 1024) {
        return js::toFixed(size / 1024, 0) + " KB";
    }
    return js::toFixed(size / (1024 * 1024), 0) + " MB";
}

std::string shortNumber(double value) {
    if (value == std::floor(value)) {
        return js::numberToString(value);
    }
    std::string text = js::toFixed(value, 2);
    while (text.find('.') != std::string::npos && (text.back() == '0' || text.back() == '.')) {
        text.pop_back();
    }
    return text;
}

std::string feedRangeText(const ProgramAnalysis& analysis, bool metric) {
    std::vector<double> feeds = wordValues(analysis.feedrates);
    if (feeds.empty()) {
        // Math.min() of nothing is Infinity upstream; the panel says none.
        return {};
    }
    // The file's units against the workspace's (convertFeedrate).
    for (double& feed : feeds) {
        if (analysis.fileModal == "G20" && metric) {
            feed *= 25.4;
        } else if (analysis.fileModal == "G21" && !metric) {
            feed /= 25.4;
        }
    }
    const std::string unit = metric ? " mm/min" : " in/min";
    const auto [low, high] = std::minmax_element(feeds.begin(), feeds.end());
    return *low == *high ? shortNumber(*low) + unit : shortNumber(*low) + "-" + shortNumber(*high) + unit;
}

std::string speedRangeText(const ProgramAnalysis& analysis) {
    const std::vector<double> speeds = wordValues(analysis.spindleSpeeds);
    if (speeds.empty()) {
        return {};
    }
    const auto [low, high] = std::minmax_element(speeds.begin(), speeds.end());
    return js::numberToString(*low) + "-" + js::numberToString(*high) + " RPM";
}

}  // namespace gs::job
