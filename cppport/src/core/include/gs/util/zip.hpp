#pragma once

// A ZIP archive of files stored as they are (no compression) - what the
// diagnostics support file needs (upstream builds it with JSZip).

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gs::zip {

struct Entry {
    std::string name;  // UTF-8, '/' between folders
    std::string data;
};

// The entries' modification time (the archive's local time).
struct DosTime {
    int year = 1980;
    int month = 1;
    int day = 1;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

// CRC-32 (IEEE 802.3), as the format checks entries with.
std::uint32_t crc32(std::string_view data);

std::string archive(const std::vector<Entry>& entries, const DosTime& modified = {});

}  // namespace gs::zip
