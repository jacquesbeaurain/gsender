// The file panel's texts for a loaded program.

#include "gs/job/file_info.hpp"

#include <gtest/gtest.h>

using namespace gs::job;

TEST(FileInfo, SizesReadInBytesKilobytesOrMegabytes) {
    EXPECT_EQ(fileSizeText(512), "512 Bytes");
    EXPECT_EQ(fileSizeText(12 * 1024 + 100), "12 KB");
    EXPECT_EQ(fileSizeText(3 * 1024 * 1024 + 600 * 1024), "4 MB");
}

TEST(FileInfo, NumbersKeepAtMostTwoDecimals) {
    EXPECT_EQ(shortNumber(300), "300");
    EXPECT_EQ(shortNumber(11.811), "11.81");
    EXPECT_EQ(shortNumber(2.5), "2.5");
    EXPECT_EQ(shortNumber(1.001), "1");
}

TEST(FileInfo, FeedRatesAreShownInTheWorkspaceUnits) {
    ProgramAnalysis inches;
    inches.fileModal = "G20";
    inches.feedrates = {"F10", "F30"};
    EXPECT_EQ(feedRangeText(inches, true), "254-762 mm/min");
    EXPECT_EQ(feedRangeText(inches, false), "10-30 in/min");

    ProgramAnalysis mm;
    mm.feedrates = {"F300", "F300"};
    EXPECT_EQ(feedRangeText(mm, true), "300 mm/min");
    EXPECT_EQ(feedRangeText(mm, false), "11.81 in/min");

    EXPECT_EQ(feedRangeText(ProgramAnalysis{}, true), "");
}

TEST(FileInfo, SpindleSpeedsReadAsARange) {
    ProgramAnalysis a;
    a.spindleSpeeds = {"S12000", "S1000", "Sx"};
    EXPECT_EQ(speedRangeText(a), "1000-12000 RPM");
    EXPECT_EQ(speedRangeText(ProgramAnalysis{}), "");
}
