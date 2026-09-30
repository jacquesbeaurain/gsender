#pragma once

// Duration and relative-time texts as gSender shows them: lib/datetime.ts's
// convertMillisecondsToTimeStamp() and date-fns' formatDistanceToNow() with
// its suffix, in English.

#include <cstdint>
#include <string>

namespace gs::util {

// "HH:MM:SS"; a day or more "DDd HHh"; the short form "HHhr MMm", "MMm SSs"
// or "SSs". Negative: "-".
std::string millisecondsToTimeStamp(double milliseconds, bool shortForm = false);

// "less than a minute ago", "5 minutes ago", "about 2 hours ago", "3 days
// ago", "about 1 month ago", "over 1 year ago"...; "in ..." for a time still
// to come. Months are counted as 30 days (date-fns counts calendar months).
std::string timeAgo(std::int64_t thenMs, std::int64_t nowMs);

// A job's duration as the stats write it (getTimeString): "1h 2m 3s",
// "2m 3s" or "3s", whole seconds.
std::string durationText(std::int64_t milliseconds);

// Info's formatEstimatedTime: "42s" under a minute, "5m 12s" under an hour,
// else "2h 5m".
std::string estimatedTimeText(double seconds);

// "H:MM:SS", as the job's progress shows its times (rounded; never below 0).
std::string clockText(double seconds);

}  // namespace gs::util
