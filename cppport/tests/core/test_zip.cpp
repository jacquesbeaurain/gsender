// The stored-ZIP writer behind the diagnostics support file.

#include "gs/util/zip.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <string>

using namespace gs;

namespace {

std::uint32_t read(const std::string& data, std::size_t at, int bytes) {
    std::uint32_t value = 0;
    for (int i = bytes - 1; i >= 0; --i) {
        value = (value << 8) | static_cast<unsigned char>(data[at + static_cast<std::size_t>(i)]);
    }
    return value;
}

// Reads a stored archive back through its central directory.
std::map<std::string, std::string> unzip(const std::string& bytes) {
    std::map<std::string, std::string> files;
    const std::size_t end = bytes.size() - 22;
    EXPECT_EQ(read(bytes, end, 4), 0x06054b50u);
    const std::uint32_t count = read(bytes, end + 10, 2);
    std::size_t at = read(bytes, end + 16, 4);
    for (std::uint32_t i = 0; i < count; ++i) {
        EXPECT_EQ(read(bytes, at, 4), 0x02014b50u);
        const std::uint32_t crc = read(bytes, at + 16, 4);
        const std::uint32_t size = read(bytes, at + 24, 4);
        const std::uint32_t nameSize = read(bytes, at + 28, 2);
        const std::uint32_t local = read(bytes, at + 42, 4);
        const std::string name = bytes.substr(at + 46, nameSize);
        EXPECT_EQ(read(bytes, local, 4), 0x04034b50u);
        EXPECT_EQ(read(bytes, local + 8, 2), 0u);  // stored
        const std::uint32_t localName = read(bytes, local + 26, 2);
        const std::string data = bytes.substr(local + 30 + localName, size);
        EXPECT_EQ(zip::crc32(data), crc) << name;
        files[name] = data;
        at += 46 + nameSize;
    }
    return files;
}

}  // namespace

TEST(Zip, TheChecksumIsCrc32) {
    EXPECT_EQ(zip::crc32("123456789"), 0xCBF43926u);  // the catalogue's check value
    EXPECT_EQ(zip::crc32(""), 0u);
}

TEST(Zip, StoredFilesComeBackAsTheyWent) {
    std::string binary;
    for (int i = 0; i < 1000; ++i) {
        binary.push_back(static_cast<char>(i * 7));
    }
    const std::string bytes =
        zip::archive({{"report.pdf", binary}, {"settings.json", "{\"a\": 1}\n"}, {"empty.nc", ""}},
                     {2026, 9, 24, 14, 5, 9});
    const std::map<std::string, std::string> files = unzip(bytes);
    ASSERT_EQ(files.size(), 3u);
    EXPECT_EQ(files.at("report.pdf"), binary);
    EXPECT_EQ(files.at("settings.json"), "{\"a\": 1}\n");
    EXPECT_EQ(files.at("empty.nc"), "");
    // DOS time and date: 14:05:08 (two-second steps), 2026-09-24.
    EXPECT_EQ(read(bytes, 10, 2), (14u << 11) | (5u << 5) | 4u);
    EXPECT_EQ(read(bytes, 12, 2), (46u << 9) | (9u << 5) | 24u);
    EXPECT_EQ(unzip(zip::archive({})).size(), 0u);
}
