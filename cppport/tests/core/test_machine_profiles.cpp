// Machine profiles and the Config page's EEPROM defaults.

#include "gs/config/machine_profiles.hpp"

#include <gtest/gtest.h>

#include <algorithm>

using namespace gs;
using namespace gs::config;

namespace {

bool contains(const std::vector<std::string>& lines, const std::string& line) {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}

std::ptrdiff_t indexOf(const std::vector<std::string>& lines, const std::string& line) {
    return std::find(lines.begin(), lines.end(), line) - lines.begin();
}

}  // namespace

TEST(MachineProfiles, TheShippedProfilesLoad) {
    ASSERT_GE(machineProfiles().size(), 30u);
    const MachineProfile* mk2 = findMachineProfile(defaultMachineProfileId());
    ASSERT_NE(mk2, nullptr);
    EXPECT_EQ(machineProfileName(*mk2), "LongMill MK2 30x30 (MK2)");
    EXPECT_TRUE(canRestoreDefaults(*mk2));
    const MachineProfile* altmill = findMachineProfile(0);
    ASSERT_NE(altmill, nullptr);
    EXPECT_EQ(machineProfileName(*altmill), "AltMill 4X4");
    EXPECT_EQ(altmill->width, 1260);
    ASSERT_TRUE(altmill->orderedSettings);
    EXPECT_EQ(altmill->orderedSettings->front(), (std::pair<std::string, std::string>{"$462", "8192"}));
    EXPECT_EQ(altmill->orderedSettings->back(), (std::pair<std::string, std::string>{"$744", "15"}));
    EXPECT_EQ(findMachineProfile(999), nullptr);
}

TEST(MachineProfiles, GrblHalBuildsFromTheCutoffMoveTheirSettings) {
    EXPECT_FALSE(usesGrblCoreMigration(-1, ""));
    EXPECT_FALSE(usesGrblCoreMigration(20250626, ""));
    EXPECT_TRUE(usesGrblCoreMigration(20250627, ""));
    EXPECT_FALSE(usesGrblCoreMigration(20250627, "SLB Lite"));  // the board skips it
    EXPECT_EQ(translateGrblCoreKey("$450", 20250627, ""), "$590");
    EXPECT_EQ(translateGrblCoreKey("$450", 20250101, ""), "$450");
    EXPECT_EQ(translateGrblCoreKey("$100", 20250627, ""), "$100");

    protocol::OrderedMap base;
    base.set("$6", "0");
    base.set("$450", "7");
    base.set("$170", "1");
    base.set("$100", "200");
    const SettingPairs ordered{{"$450", "7"}, {"$23", "1"}, {"$170", "2"}};
    const ResolvedDefaults resolved = resolveGrblCoreDefaults(20250627, base, ordered, "");
    // $450 moved to $590 (at the end), $6 overridden, $170 removed.
    const auto& items = resolved.defaults.items();
    EXPECT_EQ(resolved.defaults.get("$6"), "3");
    EXPECT_FALSE(resolved.defaults.has("$450"));
    EXPECT_FALSE(resolved.defaults.has("$170"));
    EXPECT_EQ(resolved.defaults.get("$590"), "7");
    EXPECT_EQ(items[0].first, "$6");
    EXPECT_EQ(items[1].first, "$100");
    EXPECT_EQ(items[2].first, "$590");
    ASSERT_TRUE(resolved.ordered);
    EXPECT_EQ(*resolved.ordered, (SettingPairs{{"$590", "7"}, {"$23", "1"}}));
    // Before the cutoff nothing changes.
    EXPECT_EQ(resolveGrblCoreDefaults(20240101, base, ordered, "").defaults, base);
}

TEST(MachineProfiles, DefaultsDecideWhatCountsAsChanged) {
    const MachineProfile& mk2 = *findMachineProfile(defaultMachineProfileId());
    const BoardContext grbl;
    const std::optional<std::string> resolution = defaultValue(mk2, grbl, "$100");
    ASSERT_TRUE(resolution);
    EXPECT_TRUE(isDefaultValue(*resolution, resolution));
    EXPECT_FALSE(isDefaultValue("123", resolution));
    EXPECT_FALSE(defaultValue(mk2, grbl, "$999"));
    EXPECT_TRUE(isDefaultValue("anything", std::nullopt));  // no known default
    EXPECT_TRUE(isDefaultValue("1.0", std::string("1")));   // numbers by value
    EXPECT_TRUE(isDefaultValue("0.0004", std::string("0"), 6));  // decimals to 3 places
    EXPECT_FALSE(isDefaultValue("0.0006", std::string("0"), 6));
    EXPECT_TRUE(isDefaultValue("abc", std::string("abc")));
}

TEST(MachineProfiles, RestoringDefaultsWritesTheOrderedSettingsLast) {
    const MachineProfile& altmill = *findMachineProfile(0);
    BoardContext hal;
    hal.grblHal = true;
    hal.semver = 20240101;
    const std::vector<std::string> commands = restoreDefaultsCommands(altmill, hal);
    ASSERT_GE(commands.size(), 4u);
    EXPECT_EQ(commands[commands.size() - 3], "$$");
    EXPECT_EQ(commands[commands.size() - 2], "$ES");
    EXPECT_EQ(commands.back(), "$ESH");
    // The ordered ones come after everything else, in their order.
    EXPECT_EQ(commands[commands.size() - 4], "$744=15");
    EXPECT_LT(indexOf(commands, "$462=8192"), indexOf(commands, "$23=1"));
    EXPECT_EQ(std::count_if(commands.begin(), commands.end(),
                            [](const std::string& c) { return c.rfind("$23=", 0) == 0; }),
              1);
    // Grbl: the Grbl defaults, then $$.
    const MachineProfile& mk2 = *findMachineProfile(defaultMachineProfileId());
    const std::vector<std::string> grbl = restoreDefaultsCommands(mk2, BoardContext{});
    EXPECT_EQ(grbl.back(), "$$");
    EXPECT_TRUE(contains(grbl, "$100=" + *defaultValue(mk2, BoardContext{}, "$100")));
}

TEST(MachineProfiles, EepromFilesImportAndExport) {
    protocol::OrderedMap settings;
    settings.set("$0", "10");
    settings.set("$100", "200.000");
    const std::string exported = exportEeprom(settings);
    EXPECT_EQ(exported, R"({"$0":"10","$100":"200.000"})");
    const auto commands = importEepromCommands(exported, nullptr);
    ASSERT_TRUE(commands);
    EXPECT_EQ(*commands, (std::vector<std::string>{"$0=10", "$100=200.000", "$$"}));
    // A profile's ordered settings go first.
    const MachineProfile& altmill = *findMachineProfile(0);
    const auto ordered = importEepromCommands(R"({"$100":"80","$23":"1","$462":8192})", &altmill);
    ASSERT_TRUE(ordered);
    EXPECT_EQ(*ordered, (std::vector<std::string>{"$462=8192", "$23=1", "$100=80", "$$"}));
    EXPECT_FALSE(importEepromCommands(R"({"x":1})", nullptr));
    EXPECT_FALSE(importEepromCommands("[1,2]", nullptr));
    EXPECT_FALSE(importEepromCommands("not json", nullptr));
}
