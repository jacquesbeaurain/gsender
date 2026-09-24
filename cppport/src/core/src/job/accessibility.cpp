#include "gs/job/accessibility.hpp"

#include "gs/util/jsnumber.hpp"
#include "gs/util/strings.hpp"

#include <boost/regex.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace gs::job {
namespace {

std::string fixed2(double value) {
    return js::toFixed(value, 2);
}

// Number(s.replace(letter, '')) for each "S1000" / "F500", sorted.
std::vector<double> letterValues(const std::vector<std::string>& words, char letter) {
    std::vector<double> values;
    for (std::string word : words) {
        if (const std::size_t at = word.find(letter); at != std::string::npos) {
            word.erase(at, 1);
        }
        values.push_back(js::stringToNumber(word));
    }
    std::sort(values.begin(), values.end());
    return values;
}

std::string plural(std::size_t count, std::string_view word) {
    return std::to_string(count) + " " + std::string(word) + (count > 1 ? "s" : "");
}

}  // namespace

std::string spokenDuration(double seconds) {
    const auto whole = [](double value) { return js::numberToString(value); };
    if (seconds < 60) {
        return whole(std::ceil(seconds)) + " seconds";
    }
    if (seconds < 3600) {
        return whole(std::floor(seconds / 60)) + " minutes and " + whole(std::ceil(std::fmod(seconds, 60))) +
               " seconds";
    }
    return whole(std::floor(seconds / 3600)) + " hours and " + whole(std::floor(std::fmod(seconds, 3600) / 60)) +
           " minutes";
}

std::string jobSummary(std::string_view name, const ProgramAnalysis& analysis, std::string_view program,
                       std::string_view units) {
    const std::string unit(units);
    std::vector<std::string> parts;
    parts.push_back("File loaded: " + std::string(name) + ".");

    const gcode::BoundingBox& box = analysis.bounds;
    parts.push_back("Dimensions: " + fixed2(box.max.x - box.min.x) + " wide, " + fixed2(box.max.y - box.min.y) +
                    " deep, and " + fixed2(box.max.z - box.min.z) + " high " + unit + ".");
    parts.push_back("X ranges from " + fixed2(box.min.x) + " to " + fixed2(box.max.x) + ".");
    parts.push_back("Y ranges from " + fixed2(box.min.y) + " to " + fixed2(box.max.y) + ".");
    parts.push_back("Minimum Z height is " + fixed2(box.min.z) + " " + unit + ".");

    if (analysis.estimatedTime > 0) {
        parts.push_back("Estimated completion time: " + spokenDuration(analysis.estimatedTime) + ".");
    }

    if (!analysis.tools.empty()) {
        std::vector<std::string> tools;
        for (std::string tool : analysis.tools) {
            if (const std::size_t at = tool.find('T'); at != std::string::npos) {
                tool.erase(at, 1);
            }
            tools.push_back(std::move(tool));
        }
        parts.push_back("Uses " + plural(analysis.tools.size(), "tool") + ": " + str::join(tools, ", ") + ".");
    }

    // The CAM's comments about tools and stock in the first 100 lines: a
    // line's first "(...)" (to its last ")") or ";..." - JavaScript's "."
    // stops at a carriage return.
    static const boost::regex kComment(R"(\(([^\r\n]*)\)|;([^\r\n]*))");
    std::vector<std::string> metadata;
    std::size_t start = 0;
    for (int line = 0; line < 100 && start <= program.size(); ++line) {
        const std::size_t end = std::min(program.find('\n', start), program.size());
        const std::string text(program.substr(start, end - start));
        start = end + 1;
        boost::smatch m;
        if ((text.find('(') == std::string::npos && text.find(';') == std::string::npos) ||
            !boost::regex_search(text, m, kComment)) {
            continue;
        }
        const std::string comment(str::trim(m[1].matched ? m[1].str() : m[2].str()));
        const std::string lower = str::toLower(comment);
        if (lower.find("tool") != std::string::npos || lower.find("stock") != std::string::npos) {
            metadata.push_back(comment);
        }
        if (end == program.size()) {
            break;
        }
    }
    if (!metadata.empty()) {
        parts.push_back("Job metadata: " + str::join(metadata, ", ") + ".");
    }

    // /^M0|^M1/gm: lines starting with M0 or M1 (M03 and M100 among them).
    std::size_t stops = 0;
    for (std::size_t at = 0; at < program.size();) {
        const std::string_view line = program.substr(at, 2);
        if (line == "M0" || line == "M1") {
            ++stops;
        }
        const std::size_t next = program.find_first_of("\r\n", at);
        if (next == std::string_view::npos) {
            break;
        }
        at = next + 1;
    }
    if (stops > 0) {
        parts.push_back("Contains " + plural(stops, "program stop") + " (M0/M1).");
    }

    // /M7|M8/i anywhere.
    bool coolant = false;
    for (std::size_t i = 0; !coolant && i + 1 < program.size(); ++i) {
        coolant = (program[i] == 'M' || program[i] == 'm') && (program[i + 1] == '7' || program[i + 1] == '8');
    }
    if (coolant) {
        parts.push_back("File requests coolant (M7/M8).");
    }

    if (!analysis.spindleSpeeds.empty()) {
        const std::vector<double> speeds = letterValues(analysis.spindleSpeeds, 'S');
        parts.push_back(speeds.front() == speeds.back()
                            ? "Spindle speed: " + js::numberToString(speeds.front()) + " RPM."
                            : "Spindle speed range: " + js::numberToString(speeds.front()) + " to " +
                                  js::numberToString(speeds.back()) + " RPM.");
    }
    if (!analysis.feedrates.empty()) {
        const std::vector<double> feeds = letterValues(analysis.feedrates, 'F');
        parts.push_back(feeds.front() == feeds.back()
                            ? "Feedrate: " + js::numberToString(feeds.front()) + " " + unit + "/min."
                            : "Feedrate range: " + js::numberToString(feeds.front()) + " to " +
                                  js::numberToString(feeds.back()) + " " + unit + "/min.");
    }

    if (!analysis.usedAxes.empty()) {
        std::vector<std::string> axes;
        for (const char axis : analysis.usedAxes) {
            axes.emplace_back(1, str::toUpperAscii(axis));
        }
        parts.push_back("Active axes: " + str::join(axes, ", ") + ".");
    }
    if (!analysis.invalidLines.empty()) {
        parts.push_back("WARNING: " + std::to_string(analysis.invalidLines.size()) +
                        " invalid G-code lines detected.");
    }
    return str::join(parts, " ");
}

std::optional<std::string> ProgressAnnouncer::update(double percent, int increment) {
    const int current = static_cast<int>(std::floor(percent));
    const int step = std::max(increment, 1);
    if (current >= last_ + step && current < 100) {
        last_ = current / step * step;
        return "Job progress: " + std::to_string(current) + "%";
    }
    if (current == 100 && last_ != 100) {
        last_ = 100;
        return std::string("Job complete: 100%");
    }
    if (current < last_) {
        last_ = 0;
    }
    return std::nullopt;
}

// ---- tones --------------------------------------------------------------------------------

namespace {

// AudioParam.exponentialRampToValueAtTime between two times.
double exponentialRamp(double from, double to, double t0, double t1, double t) {
    if (t <= t0) {
        return from;
    }
    if (t >= t1) {
        return to;
    }
    return from * std::pow(to / from, (t - t0) / (t1 - t0));
}

}  // namespace

std::vector<float> audioCueSamples(AudioCue cue, int sampleRate) {
    const double duration = cue == AudioCue::Success ? 0.2 : cue == AudioCue::Alarm ? 0.4 : 0.15;
    const auto count = static_cast<std::size_t>(std::lround(duration * sampleRate));
    std::vector<float> samples(count);
    double phase = 0;  // turns
    for (std::size_t i = 0; i < count; ++i) {
        const double t = static_cast<double>(i) / sampleRate;
        double frequency = 0;
        double gain = 0;
        double value = 0;
        switch (cue) {
            case AudioCue::Success:
                frequency = exponentialRamp(880, 1760, 0, 0.1, t);
                gain = exponentialRamp(0.1, 0.01, 0, 0.2, t);
                value = std::sin(2 * std::numbers::pi * phase);
                break;
            case AudioCue::Alarm:
                frequency = t < 0.1 ? 440 : t < 0.2 ? 220 : 440;
                gain = t < 0.3 ? 0.1 : 0.1 * (0.4 - t) / 0.1;
                value = phase < 0.5 ? 1.0 : -1.0;
                break;
            case AudioCue::Info:
                frequency = 660;
                gain = exponentialRamp(0.1, 0.01, 0, 0.15, t);
                value = phase < 0.25 ? 4 * phase : phase < 0.75 ? 2 - 4 * phase : 4 * phase - 4;
                break;
        }
        samples[i] = static_cast<float>(value * gain);
        phase += frequency / sampleRate;
        phase -= std::floor(phase);
    }
    return samples;
}

std::string wavFile(const std::vector<float>& samples, int sampleRate) {
    std::string out;
    const auto put = [&out](std::uint32_t value, int bytes) {
        for (int i = 0; i < bytes; ++i) {
            out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
        }
    };
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * 2);
    out += "RIFF";
    put(36 + dataBytes, 4);
    out += "WAVEfmt ";
    put(16, 4);                                          // the format chunk's size
    put(1, 2);                                           // PCM
    put(1, 2);                                           // mono
    put(static_cast<std::uint32_t>(sampleRate), 4);      // samples per second
    put(static_cast<std::uint32_t>(sampleRate) * 2, 4);  // bytes per second
    put(2, 2);                                           // bytes per frame
    put(16, 2);                                          // bits per sample
    out += "data";
    put(dataBytes, 4);
    for (const float sample : samples) {
        const auto value = static_cast<std::int16_t>(std::lround(std::clamp(sample, -1.0f, 1.0f) * 32767));
        put(static_cast<std::uint16_t>(value), 2);
    }
    return out;
}

}  // namespace gs::job
