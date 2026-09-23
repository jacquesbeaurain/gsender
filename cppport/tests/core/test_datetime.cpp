// Durations and relative times as gSender words them.

#include "gs/util/datetime.hpp"

#include <gtest/gtest.h>

using gs::util::millisecondsToTimeStamp;
using gs::util::timeAgo;

TEST(DateTime, DurationsReadAsClockTimes) {
    EXPECT_EQ(millisecondsToTimeStamp(0), "00:00:00");
    EXPECT_EQ(millisecondsToTimeStamp(61999), "00:01:01");
    EXPECT_EQ(millisecondsToTimeStamp(3 * 3600000.0 + 25 * 60000 + 7000), "03:25:07");
    EXPECT_EQ(millisecondsToTimeStamp(-1), "-");
    // A day or more: days and hours.
    EXPECT_EQ(millisecondsToTimeStamp(26 * 3600000.0), "01d 02h");
    // The short form drops what is zero at the top.
    EXPECT_EQ(millisecondsToTimeStamp(3 * 3600000.0 + 25 * 60000, true), "03hr 25m");
    EXPECT_EQ(millisecondsToTimeStamp(65000, true), "01m 05s");
    EXPECT_EQ(millisecondsToTimeStamp(9000, true), "09s");
}

TEST(DateTime, TimesAgoAreRoundedAsDateFnsWordsThem) {
    const std::int64_t now = 1'700'000'000'000;
    const auto ago = [now](double seconds) { return timeAgo(now - static_cast<std::int64_t>(seconds * 1000), now); };
    EXPECT_EQ(ago(10), "less than a minute ago");
    EXPECT_EQ(ago(40), "1 minute ago");      // 0.67 min rounds to 1
    EXPECT_EQ(ago(5 * 60), "5 minutes ago");
    EXPECT_EQ(ago(44 * 60), "44 minutes ago");
    EXPECT_EQ(ago(50 * 60), "about 1 hour ago");
    EXPECT_EQ(ago(5 * 3600), "about 5 hours ago");
    EXPECT_EQ(ago(30 * 3600), "1 day ago");
    EXPECT_EQ(ago(3 * 86400), "3 days ago");
    EXPECT_EQ(ago(35 * 86400), "about 1 month ago");
    EXPECT_EQ(ago(100 * 86400), "3 months ago");
    EXPECT_EQ(ago(370 * 86400), "about 1 year ago");
    EXPECT_EQ(ago(1.5 * 365 * 86400), "over 1 year ago");
    EXPECT_EQ(ago(1.9 * 365 * 86400), "almost 2 years ago");
    EXPECT_EQ(timeAgo(now + 5 * 60 * 1000, now), "in 5 minutes");
}
