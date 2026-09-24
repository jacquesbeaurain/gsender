#include "gs/util/zip.hpp"

#include <algorithm>
#include <array>

namespace gs::zip {
namespace {

void put(std::string& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

constexpr std::uint16_t kVersion = 20;        // 2.0: what readers need for stored entries
constexpr std::uint16_t kUtf8Names = 0x0800;  // general purpose bit 11

}  // namespace

std::uint32_t crc32(std::string_view data) {
    static const std::array<std::uint32_t, 256> kTable = [] {
        std::array<std::uint32_t, 256> table{};
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t c = i;
            for (int bit = 0; bit < 8; ++bit) {
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[i] = c;
        }
        return table;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (const char c : data) {
        crc = kTable[(crc ^ static_cast<unsigned char>(c)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

std::string archive(const std::vector<Entry>& entries, const DosTime& modified) {
    const auto time = static_cast<std::uint32_t>((modified.hour << 11) | (modified.minute << 5) | (modified.second / 2));
    const auto date = static_cast<std::uint32_t>((std::max(modified.year - 1980, 0) << 9) | (modified.month << 5) |
                                                 modified.day);
    std::string out;
    std::string directory;
    for (const Entry& entry : entries) {
        const std::uint32_t crc = crc32(entry.data);
        const auto size = static_cast<std::uint32_t>(entry.data.size());
        const auto nameSize = static_cast<std::uint32_t>(entry.name.size());
        const auto offset = static_cast<std::uint32_t>(out.size());
        // Local file header, then the data.
        put(out, 0x04034b50, 4);
        put(out, kVersion, 2);
        put(out, kUtf8Names, 2);
        put(out, 0, 2);  // stored
        put(out, time, 2);
        put(out, date, 2);
        put(out, crc, 4);
        put(out, size, 4);  // compressed
        put(out, size, 4);  // uncompressed
        put(out, nameSize, 2);
        put(out, 0, 2);  // no extra field
        out += entry.name;
        out += entry.data;
        // Its central directory record.
        put(directory, 0x02014b50, 4);
        put(directory, kVersion, 2);  // made by: MS-DOS / 2.0
        put(directory, kVersion, 2);
        put(directory, kUtf8Names, 2);
        put(directory, 0, 2);
        put(directory, time, 2);
        put(directory, date, 2);
        put(directory, crc, 4);
        put(directory, size, 4);
        put(directory, size, 4);
        put(directory, nameSize, 2);
        put(directory, 0, 2);  // extra
        put(directory, 0, 2);  // comment
        put(directory, 0, 2);  // disk
        put(directory, 0, 2);  // internal attributes
        put(directory, 0, 4);  // external attributes
        put(directory, offset, 4);
        directory += entry.name;
    }
    const auto directoryOffset = static_cast<std::uint32_t>(out.size());
    out += directory;
    // End of central directory.
    put(out, 0x06054b50, 4);
    put(out, 0, 2);
    put(out, 0, 2);
    put(out, static_cast<std::uint32_t>(entries.size()), 2);
    put(out, static_cast<std::uint32_t>(entries.size()), 2);
    put(out, static_cast<std::uint32_t>(directory.size()), 4);
    put(out, directoryOffset, 4);
    put(out, 0, 2);  // no comment
    return out;
}

}  // namespace gs::zip
