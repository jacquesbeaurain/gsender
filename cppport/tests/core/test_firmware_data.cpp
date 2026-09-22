#include "gs/protocol/firmware_data.hpp"
#include "gs/core/resources.hpp"

#include <gtest/gtest.h>

using namespace gs::protocol;

TEST(FirmwareTables, ResourcesAreEmbedded) {
    const auto names = gs::resources::names();
    EXPECT_NE(std::find(names.begin(), names.end(), "data/grbl.json"), names.end());
    EXPECT_NE(std::find(names.begin(), names.end(), "data/grblhal.json"), names.end());
    EXPECT_FALSE(gs::resources::find("data/does-not-exist.json"));
}

TEST(FirmwareTables, GrblCodes) {
    const FirmwareTables& grbl = FirmwareTables::get(Firmware::Grbl);
    ASSERT_NE(grbl.error("9"), nullptr);
    EXPECT_EQ(grbl.error("9")->message, "G-code lock");
    ASSERT_NE(grbl.alarm("1"), nullptr);
    EXPECT_EQ(grbl.alarm("1")->message, "Hard limit");
    ASSERT_NE(grbl.alarm("Homing"), nullptr);
    EXPECT_EQ(grbl.alarm("Homing")->message, "Homing required");
    EXPECT_EQ(grbl.error("999"), nullptr);
}

TEST(FirmwareTables, SettingsMetadata) {
    const FirmwareTables& grbl = FirmwareTables::get(Firmware::Grbl);
    const SettingInfo* stepPulse = grbl.setting("$0");
    ASSERT_NE(stepPulse, nullptr);
    EXPECT_EQ(stepPulse->message, "Step pulse time");
    EXPECT_EQ(stepPulse->inputType, "number");
    EXPECT_TRUE(stepPulse->raw.contains("max"));

    const FirmwareTables& hal = FirmwareTables::get(Firmware::GrblHal);
    EXPECT_GT(hal.settings().size(), 100u);
    EXPECT_GT(hal.errors().size(), grbl.errors().size());
}
