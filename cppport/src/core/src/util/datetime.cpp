#include "gs/util/datetime.hpp"

#include "gs/util/jsnumber.hpp"

#include <cmath>
#include <cstdio>

namespace gs::util {
namespace {

std::string twoDigits(long long value) {
    char text[32];
    std::snprintf(text, sizeof text, "%02lld", value);
    return text;
}

// The en-US locale's formatDistance tokens.
std::string phrase(const char* one, const char* other, long long count) {
    if (count == 1) {
        return one;
    }
    std::string text = other;
    const std::size_t at = text.find("{}");
    return text.replace(at, 2, std::to_string(count));
}

constexpr long long kMinutesInDay = 1440;
constexpr long long kMinutesInMonth = 43200;
constexpr long long kMinutesInTwoMonths = 86400;

}  // namespace

std::string millisecondsToTimeStamp(double milliseconds, bool shortForm) {
    if (!(milliseconds >= 0)) {
        return "-";
    }
    double seconds = milliseconds / 1000;
    const auto hours = static_cast<long long>(std::floor(seconds / 3600));
    seconds = std::fmod(seconds, 3600);
    const auto minutes = static_cast<long long>(std::floor(seconds / 60));
    const auto wholeSeconds = static_cast<long long>(std::floor(std::fmod(seconds, 60)));
    if (hours >= 24) {
        return twoDigits(hours / 24) + "d " + twoDigits(hours % 24) + "h";
    }
    if (shortForm) {
        if (hours != 0) {
            return twoDigits(hours) + "hr " + twoDigits(minutes) + "m";
        }
        if (minutes != 0) {
            return twoDigits(minutes) + "m " + twoDigits(wholeSeconds) + "s";
        }
        return twoDigits(wholeSeconds) + "s";
    }
    return twoDigits(hours) + ":" + twoDigits(minutes) + ":" + twoDigits(wholeSeconds);
}

std::string timeAgo(std::int64_t thenMs, std::int64_t nowMs) {
    const bool future = thenMs > nowMs;
    const std::int64_t span = future ? thenMs - nowMs : nowMs - thenMs;
    const std::int64_t seconds = span / 1000;  // differenceInSeconds truncates
    const auto minutes = static_cast<long long>(js::mathRound(static_cast<double>(seconds) / 60));
    std::string text;
    if (minutes < 2) {
        text = minutes == 0 ? std::string("less than a minute") : phrase("1 minute", "{} minutes", minutes);
    } else if (minutes < 45) {
        text = phrase("1 minute", "{} minutes", minutes);
    } else if (minutes < 90) {
        text = "about 1 hour";
    } else if (minutes < kMinutesInDay) {
        text = phrase("about 1 hour", "about {} hours",
                      static_cast<long long>(js::mathRound(static_cast<double>(minutes) / 60)));
    } else if (minutes < 2520) {
        text = "1 day";
    } else if (minutes < kMinutesInMonth) {
        text = phrase("1 day", "{} days",
                      static_cast<long long>(js::mathRound(static_cast<double>(minutes) / kMinutesInDay)));
    } else if (minutes < kMinutesInTwoMonths) {
        text = phrase("about 1 month", "about {} months",
                      static_cast<long long>(js::mathRound(static_cast<double>(minutes) / kMinutesInMonth)));
    } else {
        const long long months = minutes / kMinutesInMonth;
        if (months < 12) {
            text = phrase("1 month", "{} months",
                          static_cast<long long>(js::mathRound(static_cast<double>(minutes) / kMinutesInMonth)));
        } else {
            const long long years = months / 12;
            const long long remainder = months % 12;
            text = remainder < 3   ? phrase("about 1 year", "about {} years", years)
                   : remainder < 9 ? phrase("over 1 year", "over {} years", years)
                                   : phrase("almost 1 year", "almost {} years", years + 1);
        }
    }
    return future ? "in " + text : text + " ago";
}

}  // namespace gs::util
