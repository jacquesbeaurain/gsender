#pragma once

// The machine profiles gSender ships (Config/assets/MachineDefaults,
// extracted into resources/data/machine_profiles.json) and what the Config
// page does with them: each machine's EEPROM defaults - for grblHAL through
// the grblCore migration of builds from 20250627 on (utils/
// grblCoreMigration.ts) - which values differ from them, restoring them
// (RestoreDefaultDialog) and importing/exporting EEPROM files (ProfileBar).

#include "gs/protocol/types.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gs::config {

using SettingPairs = std::vector<std::pair<std::string, std::string>>;

struct MachineProfile {
    int id = 0;
    std::string company;
    std::string name;
    std::string type;
    std::string version;
    double width = 0;  // mm
    double depth = 0;
    double height = 0;
    // Grbl's and grblHAL's defaults, when the profile has them.
    std::optional<protocol::OrderedMap> eepromSettings;
    std::optional<protocol::OrderedMap> grblHalEepromSettings;
    // Written last, in this order (e.g. the AltMill's $462-$464 then $23).
    std::optional<SettingPairs> orderedSettings;
};

const std::vector<MachineProfile>& machineProfiles();
const MachineProfile* findMachineProfile(int id);
// The default state's machineProfiles[6]: the LongMill MK2 30x30.
int defaultMachineProfileId();
// humanReadableMachineName(): "LongMill MK2 30x30 (MK2)".
std::string machineProfileName(const MachineProfile& profile);
// The Defaults button: a Sienci Labs machine or one with defaults.
bool canRestoreDefaults(const MachineProfile& profile);

// ---- grblCore migration (grblHAL) ----

// From the cutoff build on (unless the board skips it, the SLB Lite) the
// settings moved and some defaults changed.
bool usesGrblCoreMigration(long long firmwareSemver, std::string_view boardId);
std::string translateGrblCoreKey(const std::string& key, long long firmwareSemver, std::string_view boardId);

struct ResolvedDefaults {
    protocol::OrderedMap defaults;
    std::optional<SettingPairs> ordered;
};
ResolvedDefaults resolveGrblCoreDefaults(long long firmwareSemver, const protocol::OrderedMap& baseDefaults,
                                         const std::optional<SettingPairs>& ordered, std::string_view boardId);

// ---- the connected board ----

struct BoardContext {
    bool grblHal = false;
    long long semver = -1;  // grblHAL build date; -1 unknown
    std::string boardId;    // grblHAL [BOARD:]
};

// The profile's default for a setting as the Config page shows it
// ("Default X"), nullopt where it has none.
std::optional<std::string> defaultValue(const MachineProfile& profile, const BoardContext& board,
                                        const std::string& setting);
// eepromIsDefault(): a setting without a known default counts as default;
// grblHAL integers and decimals compare to 3 decimals; otherwise numbers by
// value, anything else as text.
bool isDefaultValue(const std::string& value, const std::optional<std::string>& fallback, int dataType = -1);

// restoreEEPROMDefaults(): every default "$n=v" (the ordered ones last, in
// order), then $$ - and $ES, $ESH on grblHAL.
std::vector<std::string> restoreDefaultsCommands(const MachineProfile& profile, const BoardContext& board);

// The ProfileBar's import: a JSON object of "$n" settings becomes "$n=v"
// lines (the profile's ordered settings first) and $$; nullopt when the file
// is not such an object.
std::optional<std::vector<std::string>> importEepromCommands(std::string_view json, const MachineProfile* profile);
// exportFirmwareSettings(): the board's settings as a JSON object.
std::string exportEeprom(const protocol::OrderedMap& settings);

}  // namespace gs::config
