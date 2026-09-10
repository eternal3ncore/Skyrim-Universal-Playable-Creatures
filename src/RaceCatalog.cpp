#include "RaceCatalog.h"

#include <nlohmann/json.hpp>

namespace UPC::RaceCatalog
{
    namespace
    {
        constexpr auto kRaceConfigDir = "Data/SKSE/Plugins/UniversalPlayableCreatures"sv;
        constexpr auto kAttackFamilyConfigDir = "Data/SKSE/Plugins/UniversalPlayableCreatures/AttackFamilies"sv;

        std::unordered_map<const RE::TESRace*, HandPolicy> g_spellHands;
        std::unordered_map<const RE::TESRace*, WeaponVisibilityPolicy> g_weaponVisibility;
        std::unordered_map<const RE::TESRace*, std::string> g_attackFamilyNames;
        std::unordered_map<std::string, AttackFamilyProfile> g_attackFamilies;
        std::unordered_set<const RE::TESRace*> g_enabledRaces;
        std::unordered_set<const RE::TESRace*> g_seenRaces;
        std::unordered_set<const RE::TESRace*> g_combatWorkaroundRaces;
        std::size_t g_entryCount = 0;
        std::size_t g_resolvedCount = 0;


        std::string Lower(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::vector<std::string> ParseStringArray(
            const nlohmann::json& object,
            std::string_view key,
            std::string_view profileName)
        {
            std::vector<std::string> out;
            const auto it = object.find(std::string(key));
            if (it == object.end()) {
                return out;
            }
            if (!it->is_array()) {
                logger::warn(
                    "Attack family [{}] field '{}' must be an array of strings",
                    profileName, key);
                return out;
            }
            for (const auto& value : *it) {
                if (!value.is_string()) {
                    logger::warn(
                        "Attack family [{}] field '{}' contains a non-string value; skipped",
                        profileName, key);
                    continue;
                }
                auto marker = Lower(value.get<std::string>());
                if (!marker.empty()) {
                    out.push_back(std::move(marker));
                }
            }
            return out;
        }

        void LoadAttackFamilies(const std::filesystem::path& path)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                logger::warn(
                    "Attack-family catalog not found: {}; configured races will use generic BGSAttackData discovery",
                    path.string());
                return;
            }

            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            nlohmann::json root;
            try {
                root = nlohmann::json::parse(text, nullptr, true, true);
            } catch (const std::exception& e) {
                logger::error("Attack-family catalog parse failed [{}]: {}", path.filename().string(), e.what());
                return;
            }

            const auto familiesIt = root.find("attackFamilies");
            if (familiesIt == root.end() || !familiesIt->is_array()) {
                logger::warn("Attack-family catalog [{}] has no object array named 'attackFamilies'", path.filename().string());
                return;
            }

            std::size_t loaded = 0;
            for (const auto& item : *familiesIt) {
                if (!item.is_object()) {
                    logger::warn("Attack-family catalog [{}] skipped non-object entry", path.filename().string());
                    continue;
                }

                const auto nameIt = item.find("name");
                if (nameIt == item.end() || !nameIt->is_string() || nameIt->get<std::string>().empty()) {
                    logger::warn("Attack-family catalog [{}] skipped entry without non-empty string 'name'", path.filename().string());
                    continue;
                }

                AttackFamilyProfile profile;
                profile.name = nameIt->get<std::string>();
                profile.eventPrefixes = ParseStringArray(item, "eventPrefixes", profile.name);
                profile.swimmingPrefixes = ParseStringArray(item, "swimmingPrefixes", profile.name);
                profile.powerMarkers = ParseStringArray(item, "powerMarkers", profile.name);
                profile.bashMarkers = ParseStringArray(item, "bashMarkers", profile.name);
                profile.blockAttackMarkers = ParseStringArray(item, "blockAttackMarkers", profile.name);
                profile.counterAttackMarkers = ParseStringArray(item, "counterAttackMarkers", profile.name);
                profile.excludedAttackMarkers = ParseStringArray(item, "excludedAttackMarkers", profile.name);

                if (const auto weaponsIt = item.find("weaponFamilies"); weaponsIt != item.end()) {
                    if (!weaponsIt->is_object()) {
                        logger::warn("Attack family [{}] field 'weaponFamilies' must be an object", profile.name);
                    } else {
                        profile.oneHandMarkers = ParseStringArray(*weaponsIt, "oneHand", profile.name);
                        profile.twoHandMarkers = ParseStringArray(*weaponsIt, "twoHand", profile.name);
                        profile.unarmedMarkers = ParseStringArray(*weaponsIt, "unarmed", profile.name);
                        profile.bowMarkers = ParseStringArray(*weaponsIt, "bow", profile.name);
                        profile.staffMarkers = ParseStringArray(*weaponsIt, "staff", profile.name);
                    }
                }

                if (const auto redispatchIt = item.find("redispatchMatchedEvents"); redispatchIt != item.end()) {
                    if (redispatchIt->is_boolean()) {
                        profile.redispatchMatchedEvents = redispatchIt->get<bool>();
                    } else {
                        logger::warn(
                            "Attack family [{}] invalid redispatchMatchedEvents type; using false",
                            profile.name);
                    }
                }

                const auto key = Lower(profile.name);
                if (g_attackFamilies.contains(key)) {
                    logger::warn("Attack-family catalog duplicate profile '{}'; later definition overrides earlier", profile.name);
                }
                g_attackFamilies[key] = std::move(profile);
                ++loaded;
            }

            logger::info("Attack-family catalog [{}]: profiles={}", path.filename().string(), loaded);
        }

        std::optional<HandPolicy> ParseHand(std::string value)
        {
            std::ranges::transform(value, value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            if (value == "left") return HandPolicy::kLeft;
            if (value == "right") return HandPolicy::kRight;
            if (value == "both") return HandPolicy::kBoth;
            return std::nullopt;
        }

        std::optional<std::string> PluginFromRaceSpec(std::string_view spec)
        {
            const auto bar = spec.rfind('|');
            if (bar == std::string_view::npos || bar == 0 || bar + 1 >= spec.size()) {
                return std::nullopt;
            }
            return std::string(spec.substr(0, bar));
        }

        bool PluginLoaded(std::string_view plugin)
        {
            if (plugin.empty()) {
                return false;
            }
            if (auto* data = RE::TESDataHandler::GetSingleton()) {
                return data->LookupLoadedModByName(plugin) != nullptr ||
                       data->LookupLoadedLightModByName(plugin) != nullptr;
            }
            return false;
        }

        RE::TESRace* ResolveRace(std::string_view spec)
        {
            const auto bar = spec.rfind('|');
            if (bar == std::string_view::npos || bar == 0 || bar + 1 >= spec.size()) {
                return nullptr;
            }

            try {
                const std::string plugin(spec.substr(0, bar));
                const auto formID = static_cast<RE::FormID>(
                    std::stoul(std::string(spec.substr(bar + 1)), nullptr, 16));
                if (auto* data = RE::TESDataHandler::GetSingleton()) {
                    return data->LookupForm<RE::TESRace>(formID, plugin);
                }
            } catch (...) {
            }
            return nullptr;
        }

        void LoadFile(const std::filesystem::path& path, bool applyPlayableFlags)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                logger::warn("Race catalog could not be opened: {}", path.filename().string());
                return;
            }

            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            nlohmann::json root;
            try {
                root = nlohmann::json::parse(text, nullptr, true, true);
            } catch (const std::exception& e) {
                logger::error("Race catalog parse failed [{}]: {}", path.filename().string(), e.what());
                return;
            }

            const auto it = root.find("races");
            if (it == root.end() || !it->is_array()) {
                logger::warn("Race catalog [{}] has no object array named 'races'", path.filename().string());
                return;
            }

            std::size_t fileEntries = 0;
            std::size_t fileResolved = 0;
            std::size_t filePlayable = 0;
            std::size_t filePolicies = 0;
            std::size_t fileWeaponPolicies = 0;
            std::size_t fileCombatWorkarounds = 0;
            std::unordered_map<std::string, bool> pluginLoadedCache;
            std::unordered_set<std::string> unloadedPluginsReported;

            for (const auto& item : *it) {
                if (!item.is_object()) {
                    logger::warn("Race catalog [{}] skipped non-object entry", path.filename().string());
                    continue;
                }

                const auto raceIt = item.find("race");
                if (raceIt == item.end() || !raceIt->is_string()) {
                    logger::warn("Race catalog [{}] skipped entry without string 'race'", path.filename().string());
                    continue;
                }

                ++g_entryCount;
                ++fileEntries;
                const auto spec = raceIt->get<std::string>();
                const auto plugin = PluginFromRaceSpec(spec);
                if (plugin) {
                    const auto pluginKey = Lower(*plugin);
                    const auto [loadedIt, inserted] = pluginLoadedCache.try_emplace(pluginKey, false);
                    if (inserted) {
                        loadedIt->second = PluginLoaded(*plugin);
                    }
                    if (!loadedIt->second) {
                        if (unloadedPluginsReported.insert(pluginKey).second) {
                            logger::info(
                                "Race catalog [{}] skipping entries for unloaded plugin '{}'",
                                path.filename().string(), *plugin);
                        }
                        continue;
                    }
                }

                auto* race = ResolveRace(spec);
                if (!race) {
                    logger::warn("Race catalog unresolved [{}]: {}", path.filename().string(), spec);
                    continue;
                }

                ++g_resolvedCount;
                ++fileResolved;

                if (!g_seenRaces.insert(race).second) {
                    logger::warn(
                        "Race catalog duplicate [{}]: {}; later row overrides prior availability/hand/weapon/combat metadata",
                        path.filename().string(), spec);
                }

                // Each row is authoritative for this race. A later duplicate can
                // deliberately disable availability or remove a prior hand policy.
                g_enabledRaces.erase(race);
                g_spellHands.erase(race);
                g_weaponVisibility.erase(race);
                g_attackFamilyNames.erase(race);
                g_combatWorkaroundRaces.erase(race);

                const bool playable = item.value("playable", false);
                if (playable) {
                    // `playable` is also the UPC/UCC runtime availability switch.
                    // Keep that metadata even when RaceMenu flag application is
                    // disabled, so one catalog remains authoritative for features.
                    g_enabledRaces.insert(race);
                    if (applyPlayableFlags) {
                        race->data.flags.set(RE::RACE_DATA::Flag::kPlayable);
                        ++filePlayable;
                    } else {
                        logger::warn(
                            "Race catalog [{}] requested playable=true for {}, but RaceMenu crash protection is disabled; playable flag not applied",
                            path.filename().string(), spec);
                    }
                }

                const auto handIt = item.find("spellHand");
                if (handIt != item.end()) {
                    if (!handIt->is_string()) {
                        logger::warn("Race catalog [{}] invalid spellHand type for {}", path.filename().string(), spec);
                    } else if (const auto policy = ParseHand(handIt->get<std::string>())) {
                        g_spellHands[race] = *policy;
                        ++filePolicies;
                    } else {
                        logger::warn("Race catalog [{}] invalid spellHand for {}", path.filename().string(), spec);
                    }
                }

                WeaponVisibilityPolicy weaponPolicy{};
                if (const auto hideEquippedIt = item.find("hideEquippedWeapon"); hideEquippedIt != item.end()) {
                    if (hideEquippedIt->is_boolean()) {
                        weaponPolicy.hideEquippedWeapon = hideEquippedIt->get<bool>();
                    } else {
                        logger::warn("Race catalog [{}] invalid hideEquippedWeapon type for {}; using false", path.filename().string(), spec);
                    }
                }
                if (const auto hideSheathedIt = item.find("hideSheathedWeapon"); hideSheathedIt != item.end()) {
                    if (hideSheathedIt->is_boolean()) {
                        weaponPolicy.hideSheathedWeapon = hideSheathedIt->get<bool>();
                    } else {
                        logger::warn("Race catalog [{}] invalid hideSheathedWeapon type for {}; using false", path.filename().string(), spec);
                    }
                }
                g_weaponVisibility[race] = weaponPolicy;
                ++fileWeaponPolicies;

                if (const auto combatIt = item.find("useCombatWorkaround"); combatIt != item.end()) {
                    if (combatIt->is_boolean()) {
                        if (combatIt->get<bool>()) {
                            g_combatWorkaroundRaces.insert(race);
                            ++fileCombatWorkarounds;
                        }
                    } else {
                        logger::warn("Race catalog [{}] invalid useCombatWorkaround type for {}; using false", path.filename().string(), spec);
                    }
                }

                if (const auto familyIt = item.find("attackFamily"); familyIt != item.end()) {
                    if (!familyIt->is_string()) {
                        logger::warn("Race catalog [{}] invalid attackFamily type for {}; generic attack discovery will be used", path.filename().string(), spec);
                    } else {
                        auto familyName = familyIt->get<std::string>();
                        if (!familyName.empty()) {
                            g_attackFamilyNames[race] = familyName;
                            if (!g_attackFamilies.contains(Lower(familyName))) {
                                logger::warn(
                                    "Race catalog [{}] attackFamily '{}' for {} is not defined; generic attack discovery will be used",
                                    path.filename().string(), familyName, spec);
                            }
                        }
                    }
                }
            }

            logger::info(
                "Race catalog [{}]: entries={} resolved={} playableApplied={} spellPolicies={} weaponPolicies={} combatWorkarounds={}",
                path.filename().string(), fileEntries, fileResolved, filePlayable, filePolicies, fileWeaponPolicies, fileCombatWorkarounds);
        }
    }

    void Load(bool applyPlayableFlags)
    {
        g_spellHands.clear();
        g_weaponVisibility.clear();
        g_attackFamilyNames.clear();
        g_attackFamilies.clear();
        g_enabledRaces.clear();
        g_seenRaces.clear();
        g_combatWorkaroundRaces.clear();
        g_entryCount = 0;
        g_resolvedCount = 0;

        const std::filesystem::path familyDir{ std::string(kAttackFamilyConfigDir) };
        std::vector<std::filesystem::path> familyFiles;
        std::error_code familyEc;
        if (std::filesystem::is_directory(familyDir, familyEc)) {
            for (std::filesystem::directory_iterator it(familyDir, familyEc), end;
                 !familyEc && it != end; it.increment(familyEc)) {
                if (!it->is_regular_file()) {
                    continue;
                }

                const auto filename = Lower(it->path().filename().string());
                if (filename.ends_with(".attackfamilies.json")) {
                    familyFiles.push_back(it->path());
                }
            }

            if (familyEc) {
                logger::error("Attack-family folder scan failed: {}", familyEc.message());
                familyFiles.clear();
            }
        } else if (familyEc) {
            logger::warn("Attack-family folder check failed [{}]: {}", kAttackFamilyConfigDir, familyEc.message());
        } else {
            logger::info("Attack-family folder not present; named attack-family profiles will be unavailable: {}", kAttackFamilyConfigDir);
        }

        // Profile files are parsed exactly once during the DataLoaded catalog pass.
        // Gameplay attack input performs only in-memory lookups; save loads and
        // attack presses never re-read JSON from disk.
        std::ranges::sort(familyFiles);
        for (const auto& path : familyFiles) {
            LoadAttackFamilies(path);
        }

        const std::filesystem::path dir{ std::string(kRaceConfigDir) };
        std::error_code ec;
        if (!std::filesystem::is_directory(dir, ec)) {
            logger::warn("Race catalog folder not found: {}", kRaceConfigDir);
            return;
        }

        std::vector<std::filesystem::path> files;
        for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
            if (!it->is_regular_file()) {
                continue;
            }
            auto ext = Lower(it->path().extension().string());
            if (ext == ".json") {
                files.push_back(it->path());
            }
        }

        if (ec) {
            logger::error("Race catalog folder scan failed: {}", ec.message());
            return;
        }

        std::ranges::sort(files);
        for (const auto& path : files) {
            LoadFile(path, applyPlayableFlags);
        }

        // Duplicate tracking is load-time-only; release its buckets after the
        // deterministic catalog pass. Runtime lookups retain only resolved maps.
        g_seenRaces.clear();
        g_seenRaces.rehash(0);

        logger::info(
            "Race catalogs loaded: raceFiles={} attackFamilyFiles={} entries={} resolved={} enabled={} spellPolicies={} weaponPolicies={} combatWorkarounds={} attackFamilyAssignments={} attackFamilyProfiles={} playableApplication={}",
            files.size(), familyFiles.size(), g_entryCount, g_resolvedCount, g_enabledRaces.size(), g_spellHands.size(), g_weaponVisibility.size(), g_combatWorkaroundRaces.size(), g_attackFamilyNames.size(), g_attackFamilies.size(), applyPlayableFlags);
    }

    bool IsEnabled(const RE::TESRace* race)
    {
        return race && g_enabledRaces.contains(race);
    }

    bool UseCombatWorkaround(const RE::TESRace* race)
    {
        return race && g_combatWorkaroundRaces.contains(race);
    }

    const AttackFamilyProfile* GetAttackFamily(const RE::TESRace* race)
    {
        if (!race) {
            return nullptr;
        }
        const auto assignment = g_attackFamilyNames.find(race);
        if (assignment == g_attackFamilyNames.end()) {
            return nullptr;
        }
        const auto profile = g_attackFamilies.find(Lower(assignment->second));
        return profile != g_attackFamilies.end() ? std::addressof(profile->second) : nullptr;
    }

    std::optional<HandPolicy> GetSpellHand(const RE::TESRace* race)
    {
        if (!race) return std::nullopt;
        if (const auto it = g_spellHands.find(race); it != g_spellHands.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    WeaponVisibilityPolicy GetWeaponVisibility(const RE::TESRace* race)
    {
        if (!race) return {};
        if (const auto it = g_weaponVisibility.find(race); it != g_weaponVisibility.end()) {
            return it->second;
        }
        return {};
    }

    std::size_t EntryCount() { return g_entryCount; }
    std::size_t ResolvedCount() { return g_resolvedCount; }
    std::size_t EnabledCount() { return g_enabledRaces.size(); }
}
