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

}  // namespace gs::util
