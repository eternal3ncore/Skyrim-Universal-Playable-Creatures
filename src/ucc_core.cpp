
#include "RE/Skyrim.h"
#include "SKSE/SKSE.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <xmmintrin.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <nlohmann/json.hpp>

namespace
{
    struct PluginConfig
    {
        bool enableHideUnsupportedEquippedWeapons = true;
        bool enableHideSheathedWeapons = true;
    };

    PluginConfig g_config{};

    std::filesystem::path GetConfigPath()
    {
        const auto modulePath = REL::Module::get().filePath();
        auto runtime = std::filesystem::path(modulePath.data()).parent_path();
        return runtime / "Data" / "SKSE" / "Plugins" / "UniversalPlayableCreatures.json";
    }

    void LoadConfig()
    {
        const auto path = GetConfigPath();
        std::ifstream in(path);
        if (!in) {
            spdlog::info(
                "Config not found at '{}'; using defaults: HideUnsupportedEquipped={} HideSheathed={}",
                path.string(),
                g_config.enableHideUnsupportedEquippedWeapons,
                g_config.enableHideSheathedWeapons);
            return;
        }

        try {
            nlohmann::json json;
            in >> json;

            g_config.enableHideUnsupportedEquippedWeapons =
                json.value("EnableHideUnsupportedEquippedWeapons", g_config.enableHideUnsupportedEquippedWeapons);
            g_config.enableHideSheathedWeapons =
                json.value("EnableHideSheathedWeapons", g_config.enableHideSheathedWeapons);

            spdlog::info(
                "Config loaded: HideUnsupportedEquipped={} HideSheathed={}",
                g_config.enableHideUnsupportedEquippedWeapons,
                g_config.enableHideSheathedWeapons);
        } catch (const std::exception& e) {
            spdlog::error(
                "Failed to parse UniversalPlayableCreatures.json: {}; using defaults",
                e.what());
        }
    }

    bool g_forward = false;
    bool g_back = false;
    bool g_strafeLeft = false;
    bool g_strafeRight = false;

    using Clock = std::chrono::steady_clock;

    enum class SpellVisualMode
    {
        kNone,
        kAttackFallback
    };

    SpellVisualMode g_leftSpellVisualMode = SpellVisualMode::kNone;
    SpellVisualMode g_rightSpellVisualMode = SpellVisualMode::kNone;

    enum class CreatureInputPolicy : std::uint8_t
    {
        kNormalPlayer = 0,
        kUniversalCreature,
        // Werewolf/Vampire Lord/Werebear are deliberately excluded from every
        // Universal Creature Controls feature except crafting interception.
        kCraftingOnly
    };

    CreatureInputPolicy g_inputPolicy =
        CreatureInputPolicy::kNormalPlayer;

    std::size_t g_rightCursor = 0;

    RE::FormID g_lastRaceFormID = 0;
    bool g_qualifiedCreatureRaceActive = false;
    bool g_creatureControlsActive = false;

    bool g_workbenchActivationPending = false;
    RE::TESFurniture::WorkBenchData::BenchType g_pendingBenchType =
        RE::TESFurniture::WorkBenchData::BenchType::kNone;
    RE::ObjectRefHandle g_pendingWorkbenchRef{};
    RE::UI::Create_t* g_originalCraftingMenuCreate = nullptr;
    bool g_craftingFactoryHookInstalled = false;
    bool g_weaponVisualActionSinkRegistered = false;
    bool g_weaponVisualNiNodeSinkRegistered = false;

    Clock::time_point g_shoutPressTime{};


    struct PendingFallbackCast
    {
        RE::SpellItem* spell = nullptr;
        bool leftHand = false;
        float effectiveness = 1.0F;
        bool active = false;
    };

    PendingFallbackCast g_leftPendingFallbackCast{};
    PendingFallbackCast g_rightPendingFallbackCast{};

    struct SpellPressState
    {
        RE::SpellItem* spell = nullptr;
        Clock::time_point pressedAt{};
        bool active = false;
    };

    SpellPressState g_leftSpellPress{};
    SpellPressState g_rightSpellPress{};

    struct PendingFallbackShout
    {
        RE::TESShout* shout = nullptr;
        RE::SpellItem* spell = nullptr;
        float recoveryTime = 0.0F;
        std::size_t variation = 0;
        bool active = false;
    };

    PendingFallbackShout g_pendingFallbackShout{};

    bool g_playerNoCollisionHeld = false;
    bool g_pluginBlockHeld = false;

    // Explicit emergency control chord: Ready Weapon + Sprint.  This is not a
    // background recovery mechanism; it runs only while the user deliberately
    // presses the two controls together.
    bool g_forceSheatheReadyWeaponHeld = false;
    bool g_forceSheatheSprintHeld = false;
    bool g_forceSheatheChordLatched = false;

    bool g_leftSelfConcentrationHeld = false;
    bool g_rightSelfConcentrationHeld = false;


    struct AttackNameTraits
    {
        bool oneHand = false;
        bool twoHand = false;
        bool unarmed = false;
        bool bow = false;
        bool staff = false;
        bool forward = false;
        bool back = false;
        bool left = false;
        bool right = false;
        bool tes4Event = false;
        bool swim = false;
        bool tes4NamedPower = false;
        bool nameLooksBash = false;
        bool tes4BlockAttack = false;
        bool tes4CounterAttack = false;

        bool HasExplicitWeaponFamily() const
        {
            return oneHand || twoHand || unarmed || bow || staff;
        }

        bool HasExplicitTES4Context() const
        {
            return tes4Event && (HasExplicitWeaponFamily() || swim);
        }
    };

    struct AttackChoice
    {
        std::string event;
        RE::NiPointer<RE::BGSAttackData> data;
        AttackNameTraits traits{};
    };

    std::vector<AttackChoice> g_cachedNormalAttacks;
    std::vector<AttackChoice> g_cachedPowerAttacks;
    std::vector<AttackChoice> g_cachedSpecialAttacks;
    bool g_hasUsableAttackData = false;
    bool g_attackDataArmedByUCC = false;

    struct HiddenUnsupportedWeaponClone
    {
        RE::NiPointer<RE::NiAVObject> clone;
        bool wasCulled = false;
    };

    std::vector<HiddenUnsupportedWeaponClone> g_hiddenUnsupportedWeaponClones;

    void ForcePlayerCollisionEnabled();
    void ClearActiveAttackData(RE::PlayerCharacter* player);
    bool CastExactSpellImmediate(
        RE::PlayerCharacter* player,
        RE::SpellItem* spell,
        bool leftHand,
        float effectiveness,
        std::string_view reason);
    void QueueFallbackCastForHitFrame(
        RE::SpellItem* spell,
        bool leftHand,
        float effectiveness);
    PendingFallbackCast& PendingCastForHand(bool leftHand);
    void RestoreUnsupportedWeaponVisual(std::string_view reason);
    void SyncUnsupportedEquippedWeaponVisual(
        RE::PlayerCharacter* player,
        std::string_view reason);
    void SyncConfiguredWeaponVisual(
        RE::PlayerCharacter* player,
        std::string_view reason);

    void SetupLog()
    {
        auto dir = SKSE::log::log_directory();
        if (!dir) {
            return;
        }

        auto path = *dir / "UniversalCreatureControls.log";
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
        auto log = std::make_shared<spdlog::logger>("UniversalCreatureControls", std::move(sink));
        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::trace);
        spdlog::flush_on(spdlog::level::trace);
    }

    std::string Lower(std::string_view value)
    {
        std::string out(value);
        std::transform(out.begin(), out.end(), out.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool IsCraftingOnlySpecialRace(RE::TESRace* race)
    {
        if (!race) {
            return false;
        }

        // Werewolf, Vampire Lord, and Dragonborn Werebear already have complete
        // dedicated vanilla/vanilla-derived player behavior systems. Universal Creature Controls must not touch their
        // combat, spells, shouts, movement, jump, collision, animation graph,
        // or caster state. The sole exception is crafting-station interception.
        const auto editorID = Lower(race->GetFormEditorID() ?
            race->GetFormEditorID() : "");

        return editorID == "werewolfbeastrace" ||
               editorID == "dlc1vampirebeastrace" ||
               editorID == "vampirelordrace" ||
               editorID == "dlc2werebearbeastrace";
    }

    bool IsCreatureRaceByFaceGen(RE::TESRace* race)
    {
        if (!race) {
            return false;
        }

        // Same classifier chosen for CreatureRaceMenuFix:
        // a normal humanoid/player race advertises kFaceGenHead; creature
        // races do not. Attack Data is deliberately NOT part of qualification.
        if (race->data.flags.any(RE::RACE_DATA::Flag::kFaceGenHead)) {
            return false;
        }

        return true;
    }



    void StopAllCreatureOnlyStates()
    {
        ForcePlayerCollisionEnabled();

        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            // UCC may now own either the unsupported-drawn hide or the optional
            // sheathed hide, so leaving UniversalCreature policy always releases
            // the cull state UCC itself owns.
            RestoreUnsupportedWeaponVisual("creature-mode-exit");
        }

        g_leftSelfConcentrationHeld = false;
        g_rightSelfConcentrationHeld = false;

        g_workbenchActivationPending = false;
        g_pendingBenchType =
            RE::TESFurniture::WorkBenchData::BenchType::kNone;
        g_pendingWorkbenchRef = {};
        g_leftPendingFallbackCast = {};
        g_rightPendingFallbackCast = {};
        g_leftSpellPress = {};
        g_rightSpellPress = {};
        g_pendingFallbackShout = {};
        if (auto* player = RE::PlayerCharacter::GetSingleton()) {
            // Creature-mode teardown must not leave a UCC-owned attack payload in
            // HighProcessData after a race/policy transition.
            ClearActiveAttackData(player);
            player->NotifyAnimationGraph(RE::BSFixedString("Spell_Interrupt"));

            for (const auto source : {
                     RE::MagicSystem::CastingSource::kLeftHand,
                     RE::MagicSystem::CastingSource::kRightHand }) {
                if (auto* caster = player->GetMagicCaster(source)) {
                    caster->InterruptCastImpl(false);
                }
            }
        }

        g_inputPolicy = CreatureInputPolicy::kNormalPlayer;

        g_forward = false;
        g_back = false;
        g_strafeLeft = false;
        g_strafeRight = false;
        g_forceSheatheReadyWeaponHeld = false;
        g_forceSheatheSprintHeld = false;
        g_forceSheatheChordLatched = false;

        g_leftSpellVisualMode = SpellVisualMode::kNone;
        g_rightSpellVisualMode = SpellVisualMode::kNone;
        g_cachedNormalAttacks.clear();
        g_cachedPowerAttacks.clear();
        g_cachedSpecialAttacks.clear();
        g_hasUsableAttackData = false;

        if (g_pluginBlockHeld) {
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                player->NotifyAnimationGraph(RE::BSFixedString("blockStop"));
            }
            g_pluginBlockHeld = false;
        }
    }

    bool UpdateCreatureControlsState(RE::PlayerCharacter* player)
    {
        const bool wasActive = g_creatureControlsActive;

        g_creatureControlsActive =
            player &&
            player->GetRace() &&
            IsCreatureRaceByFaceGen(player->GetRace());

        if (wasActive && !g_creatureControlsActive) {
            StopAllCreatureOnlyStates();
            spdlog::info("Creature controls master gate OFF");
        } else if (!wasActive && g_creatureControlsActive) {
            spdlog::info("Creature controls master gate ON");
        }

        return g_creatureControlsActive;
    }

    CreatureInputPolicy SelectInputPolicyForRace(RE::TESRace* race)
    {
        if (!race || !IsCreatureRaceByFaceGen(race)) {
            return CreatureInputPolicy::kNormalPlayer;
        }

        if (IsCraftingOnlySpecialRace(race)) {
            return CreatureInputPolicy::kCraftingOnly;
        }

        return CreatureInputPolicy::kUniversalCreature;
    }

    const char* InputPolicyName(CreatureInputPolicy policy)
    {
        switch (policy) {
        case CreatureInputPolicy::kUniversalCreature:
            return "UniversalCreature";
        case CreatureInputPolicy::kCraftingOnly:
            return "CraftingOnly";
        default:
            return "NormalPlayer";
        }
    }

    void RefreshAttackCapabilityProfile(RE::TESRace* race);


    bool RefreshCreatureRaceState(RE::PlayerCharacter* player)
    {
        if (!player) {
            g_lastRaceFormID = 0;
            g_qualifiedCreatureRaceActive = false;
            return false;
        }

        auto* race = player->GetRace();
        const RE::FormID currentRaceID = race ? race->GetFormID() : 0;

        // Fast path: qualification, attack discovery, and event-name capability
        // classification are race-change work, never per-input/per-frame work.
        if (currentRaceID == g_lastRaceFormID) {
            return g_qualifiedCreatureRaceActive;
        }

        const auto previousPolicy = g_inputPolicy;

        // Restore collision only when LEAVING our UniversalCreature policy.
        // Merely loading/entering Werewolf, Vampire Lord, or Werebear must not run one of
        // our collision helpers against that vanilla-special actor.
        if (previousPolicy == CreatureInputPolicy::kUniversalCreature) {
            ForcePlayerCollisionEnabled();
        }

        g_lastRaceFormID = currentRaceID;
        g_qualifiedCreatureRaceActive = IsCreatureRaceByFaceGen(race);
        UpdateCreatureControlsState(player);

        g_inputPolicy = g_qualifiedCreatureRaceActive ?
            SelectInputPolicyForRace(race) :
            CreatureInputPolicy::kNormalPlayer;

        spdlog::info(
            "Creature input policy: {}",
            InputPolicyName(g_inputPolicy));

        if (g_inputPolicy == CreatureInputPolicy::kUniversalCreature) {
            spdlog::info(
                "Universal creature race activated: {:08X} editorID='{}'",
                currentRaceID,
                race && race->GetFormEditorID() ? race->GetFormEditorID() : "");
        } else if (g_inputPolicy == CreatureInputPolicy::kCraftingOnly) {
            spdlog::info(
                "Crafting-only vanilla special creature activated: {:08X} editorID='{}'",
                currentRaceID,
                race && race->GetFormEditorID() ? race->GetFormEditorID() : "");
        } else {
            spdlog::info(
                "Universal creature controls inactive/excluded; current race {:08X} editorID='{}'",
                currentRaceID,
                race && race->GetFormEditorID() ? race->GetFormEditorID() : "");
        }

        g_rightCursor = 0;
        ClearActiveAttackData(player);

        // A race change can change both attack capability and the configured
        // weapon-visibility policy. Release any UCC-owned cull from the previous
        // race, rebuild capability, then reconcile the current drawn/sheathed state.
        RestoreUnsupportedWeaponVisual("race-change");

        RefreshAttackCapabilityProfile(
            g_inputPolicy == CreatureInputPolicy::kUniversalCreature ? race : nullptr);

        SyncConfiguredWeaponVisual(player, "race-change");

        return g_qualifiedCreatureRaceActive;
    }


    AttackNameTraits ClassifyAttackNameTraits(std::string_view lower)
    {
        AttackNameTraits traits;
        traits.oneHand =
            lower.find("onehand") != std::string::npos ||
            lower.find("1hand") != std::string::npos;
        traits.twoHand =
            lower.find("twohand") != std::string::npos ||
            lower.find("2hand") != std::string::npos;
        traits.unarmed =
            lower.find("handtohand") != std::string::npos ||
            lower.find("unarmed") != std::string::npos;
        traits.bow =
            lower.find("attackbow") != std::string::npos ||
            lower.find("bowattack") != std::string::npos ||
            lower.find("crossbow") != std::string::npos;
        traits.staff =
            lower.find("staffattack") != std::string::npos ||
            lower.find("attackstaff") != std::string::npos;
        traits.forward = lower.find("forward") != std::string::npos;
        traits.back =
            lower.find("backward") != std::string::npos ||
            lower.find("back") != std::string::npos;
        traits.left = lower.find("left") != std::string::npos;
        traits.right = lower.find("right") != std::string::npos;
        traits.tes4Event = lower.starts_with("attackstart_tes4_");
        traits.swim =
            traits.tes4Event &&
            lower.starts_with("attackstart_tes4_swim");
        traits.tes4NamedPower =
            traits.tes4Event &&
            lower.find("power") != std::string::npos;
        traits.nameLooksBash = lower.find("bash") != std::string::npos;
        traits.tes4BlockAttack =
            traits.tes4Event &&
            lower.find("blockattack") != std::string::npos;
        traits.tes4CounterAttack =
            traits.tes4Event &&
            lower.find("counterattack") != std::string::npos;
        return traits;
    }

    std::vector<AttackChoice> GetCreatureAttacks(RE::TESRace* race)
    {
        std::vector<AttackChoice> result;
        if (!race || !race->attackDataMap) {
            return result;
        }

        // BGSAttackDataMap is already race-local attack metadata. We therefore
        // inspect only the ACTIVE player's race and never scan global forms or
        // maintain a table of Skyrim creature animation names.
        for (auto& [key, data] : race->attackDataMap->attackDataMap) {
            if (!data) {
                continue;
            }

            std::string evt(data->event.c_str());
            if (evt.empty()) {
                continue;
            }

            const auto lower = Lower(evt);
            if (lower.find("equip") != std::string::npos ||
                lower.find("unequip") != std::string::npos) {
                continue;
            }

            AttackChoice choice;
            choice.event = std::move(evt);
            choice.data = data;
            choice.traits = ClassifyAttackNameTraits(lower);
            result.push_back(std::move(choice));
        }

        std::sort(result.begin(), result.end(),
            [](const AttackChoice& a, const AttackChoice& b) {
                return a.event < b.event;
            });

        return result;
    }

    bool IsPowerAttackData(const AttackChoice& choice)
    {
        if (!choice.data) {
            return false;
        }

        // Native Skyrim attack records expose the authoritative ATKD power flag.
        if (choice.data->data.flags.any(
                RE::AttackData::AttackFlag::kPowerAttack)) {
            return true;
        }

        // Converted TES4 ATKE/ATKD records do not consistently preserve Skyrim's
        // kPowerAttack bit even though the converter gives their authored power
        // clips explicit attackStart_TES4_*power* event names. Treat that explicit
        // converted naming as a compatibility fallback so those records never
        // enter the normal attack rotation and remain available to Sneak.
        // Metadata remains primary for native/custom Skyrim creature records.
        return choice.traits.tes4NamedPower;
    }

    bool IsBashAttackData(const AttackChoice& choice)
    {
        if (!choice.data) {
            return false;
        }

        // ATKD provides a dedicated bash flag. Bash records belong to the
        // block/bash lifecycle and must never be rotated as ordinary primary
        // attacks. Doing so can enter a bash state without the matching
        // lifecycle completion and make subsequent attack events reject.
        if (choice.data->data.flags.any(
                RE::AttackData::AttackFlag::kBashAttack)) {
            return true;
        }

        // Defensive compatibility fallback for malformed/custom attack data
        // that exposes a bash event without the ATKD bash flag. Metadata is
        // authoritative; the name is only a final safety net.
        return choice.traits.nameLooksBash;
    }

    bool IsTES4SpecialAttackName(const AttackChoice& choice)
    {
        // Oblivion-authored block/counter attacks are ordinary ATKD entries in
        // some converted races (for example Golden Saint) and do not reliably
        // carry Skyrim's kBashAttack flag. They belong to the block/counter
        // lifecycle, not the player's primary attack rotation. Restrict the
        // compatibility fallback to the converter's explicit `blockattack`
        // event family so unrelated names containing "block" are unaffected.
        return choice.traits.tes4BlockAttack ||
               choice.traits.tes4CounterAttack;
    }

    void RefreshAttackCapabilityProfile(RE::TESRace* race)
    {
        g_cachedNormalAttacks.clear();
        g_cachedPowerAttacks.clear();
        g_cachedSpecialAttacks.clear();
        g_hasUsableAttackData = false;

        if (!race) {
            return;
        }

        const auto all = GetCreatureAttacks(race);
        for (const auto& choice : all) {
            // Classification order matters: a bash record is a special action
            // even if another flag is also present. Never let it enter either
            // player attack rotation.
            if (IsBashAttackData(choice) || IsTES4SpecialAttackName(choice)) {
                g_cachedSpecialAttacks.push_back(choice);
            } else if (IsPowerAttackData(choice)) {
                g_cachedPowerAttacks.push_back(choice);
            } else {
                g_cachedNormalAttacks.push_back(choice);
            }
        }

        g_hasUsableAttackData = !g_cachedNormalAttacks.empty() ||
                                !g_cachedPowerAttacks.empty();

        spdlog::info(
            "Creature attack capability: race={:08X} normal={} power={} special={} usable={}",
            race->GetFormID(),
            g_cachedNormalAttacks.size(),
            g_cachedPowerAttacks.size(),
            g_cachedSpecialAttacks.size(),
            g_hasUsableAttackData);

        if (!g_hasUsableAttackData) {
            spdlog::warn(
                "Creature race {:08X} has no usable BGSAttackData events; attack input will be ignored safely",
                race->GetFormID());
        }
    }


    RE::TESForm* GetHandObject(RE::PlayerCharacter* player, bool leftHand)
    {
        return player ? player->GetEquippedObject(leftHand) : nullptr;
    }

    RE::TESObjectWEAP* GetWeapon(RE::PlayerCharacter* player, bool leftHand)
    {
        auto* form = GetHandObject(player, leftHand);
        return form ? form->As<RE::TESObjectWEAP>() : nullptr;
    }

    // Skyrim can expose two-handed/ranged equipment through either equipped-object
    // slot depending on the weapon/controller state. Resolve the active weapon
    // once from both hands rather than assuming the RH slot is authoritative.
    RE::TESObjectWEAP* GetEquippedWeapon(RE::PlayerCharacter* player)
    {
        if (!player) {
            return nullptr;
        }
        if (auto* weapon = GetWeapon(player, false)) {
            return weapon;
        }
        return GetWeapon(player, true);
    }

    RE::SpellItem* GetSpell(RE::PlayerCharacter* player, bool leftHand)
    {
        auto* form = GetHandObject(player, leftHand);
        return form ? form->As<RE::SpellItem>() : nullptr;
    }

    enum class NormalWeaponFamily : std::uint8_t
    {
        kUnarmed = 0,
        kOneHand,
        kTwoHand,
        kBow,
        kStaff,
        kOther
    };

    NormalWeaponFamily GetNormalWeaponFamily(
        RE::PlayerCharacter* player)
    {
        auto* weapon = GetEquippedWeapon(player);
        if (!weapon) {
            return NormalWeaponFamily::kUnarmed;
        }

        switch (weapon->GetWeaponType()) {
        case RE::WEAPON_TYPE::kTwoHandSword:
        case RE::WEAPON_TYPE::kTwoHandAxe:
            return NormalWeaponFamily::kTwoHand;

        case RE::WEAPON_TYPE::kOneHandSword:
        case RE::WEAPON_TYPE::kOneHandDagger:
        case RE::WEAPON_TYPE::kOneHandAxe:
        case RE::WEAPON_TYPE::kOneHandMace:
            return NormalWeaponFamily::kOneHand;

        case RE::WEAPON_TYPE::kBow:
        case RE::WEAPON_TYPE::kCrossbow:
            return NormalWeaponFamily::kBow;

        case RE::WEAPON_TYPE::kStaff:
            return NormalWeaponFamily::kStaff;

        default:
            return NormalWeaponFamily::kOther;
        }
    }

    bool AttackFamilyMatches(
        const AttackChoice& choice,
        NormalWeaponFamily family)
    {
        switch (family) {
        case NormalWeaponFamily::kOneHand:
            return choice.traits.oneHand;
        case NormalWeaponFamily::kTwoHand:
            return choice.traits.twoHand;
        case NormalWeaponFamily::kBow:
            return choice.traits.bow;
        case NormalWeaponFamily::kStaff:
            return choice.traits.staff;
        case NormalWeaponFamily::kOther:
            return !choice.traits.HasExplicitWeaponFamily();
        case NormalWeaponFamily::kUnarmed:
        default:
            return choice.traits.unarmed;
        }
    }

    bool IsTES4SwimmingContext(RE::PlayerCharacter* player)
    {
        if (!player) {
            return false;
        }

        // The converted TES4 behavior graphs themselves use the integer graph
        // variable `isSwimming` to select their swim locomotion/attack branch.
        // Actor::IsSwimming() is not authoritative for these converted races and
        // produced persistent false positives on land (confirmed with Grummite).
        // Reading the graph variable keeps UCC synchronized with the same state
        // the behavior graph is already using, without polling or duplicate state.
        std::int32_t isSwimming = 0;
        return player->GetGraphVariableInt("isSwimming", isSwimming) && isSwimming == 1;
    }

    std::vector<AttackChoice> FilterAttackContext(
        RE::PlayerCharacter* player,
        const std::vector<AttackChoice>& source)
    {
        if (source.empty()) {
            return {};
        }

        const bool swimming = IsTES4SwimmingContext(player);
        std::vector<AttackChoice> out;
        out.reserve(source.size());

        // Environment is a strict orthogonal context. The graph's own
        // `isSwimming` variable decides whether swim or land records are valid;
        // weapon-family matching happens afterward. Never cross-fall back between
        // swim and land animations merely to force an attack.
        for (const auto& choice : source) {
            if (choice.traits.swim == swimming) {
                out.push_back(choice);
            }
        }
        return out;
    }

    std::vector<AttackChoice> FilterNormalAttackFamily(
        const std::vector<AttackChoice>& normal,
        NormalWeaponFamily family)
    {
        std::vector<AttackChoice> out;
        for (const auto& choice : normal) {
            if (AttackFamilyMatches(choice, family)) {
                out.push_back(choice);
            }
        }
        return out;
    }

    std::vector<AttackChoice> BuildNormalAttackPool(RE::PlayerCharacter* player)
    {
        const auto normal =
            FilterAttackContext(player, g_cachedNormalAttacks);
        if (normal.empty()) {
            return {};
        }

        const auto equippedFamily =
            GetNormalWeaponFamily(player);
        // Prefer an explicitly matching weapon family when the attack event
        // names provide one. Native Skyrim creature ATKE names are often
        // family-neutral, so neutral events are accepted before any cross-family
        // fallback instead of being filtered out by TES4 naming assumptions.
        auto preferred = FilterNormalAttackFamily(normal, equippedFamily);
        if (!preferred.empty()) {
            return preferred;
        }

        std::vector<AttackChoice> neutral;
        for (const auto& choice : normal) {
            // "Neutral" means the event carries no explicit weapon-family
            // contract. Ranged/staff attacks are NOT neutral just because they
            // lack onehand/twohand text; allowing that was what made Goblins,
            // Grummites, and Skeletons fire their bow animation with melee
            // weapons equipped.
            if (!choice.traits.HasExplicitWeaponFamily()) {
                neutral.push_back(choice);
            }
        }
        if (!neutral.empty()) {
            spdlog::info(
                "Normal attack: using family-neutral BGSAttackData events");
            return neutral;
        }

        // Converted creatures sometimes expose only the opposite weapon-family
        // animation. Keep the historical 1H<->2H compatibility fallback.
        if (equippedFamily == NormalWeaponFamily::kOneHand) {
            auto alternate =
                FilterNormalAttackFamily(normal, NormalWeaponFamily::kTwoHand);
            if (!alternate.empty()) {
                spdlog::info(
                    "Normal attack: no 1H family; falling back to 2H animations");
                return alternate;
            }
        } else if (equippedFamily == NormalWeaponFamily::kTwoHand) {
            auto alternate =
                FilterNormalAttackFamily(normal, NormalWeaponFamily::kOneHand);
            if (!alternate.empty()) {
                spdlog::info(
                    "Normal attack: no 2H family; falling back to 1H animations");
                return alternate;
            }
        }

        // Some creature races have only authored H2H normal attacks even though
        // Skyrim permits a melee weapon to remain equipped. After exhausting the
        // exact 1H/2H family, family-neutral records, and the historical 1H<->2H
        // compatibility fallback, allow those H2H records as the final melee-only
        // compatibility path. Keep bow/staff/other families isolated so this does
        // not reintroduce the earlier cross-family ranged/special animation leaks.
        if (equippedFamily == NormalWeaponFamily::kOneHand ||
            equippedFamily == NormalWeaponFamily::kTwoHand) {
            auto unarmedFallback =
                FilterNormalAttackFamily(normal, NormalWeaponFamily::kUnarmed);
            if (!unarmedFallback.empty()) {
                spdlog::info(
                    "Normal attack: no compatible armed melee family; falling back to H2H animations");
                return unarmedFallback;
            }
        }

        // Preserve metadata fallback for native/custom attacks whose names do
        // not express a weapon family. Do NOT let an explicitly contextual TES4
        // event (bow/staff/H2H/1H/2H/swim) leak through after its context failed.
        // That permissive fallback is what previously selected ranged/swim/special
        // animations in the wrong situation.
        std::vector<AttackChoice> metadataFallback;
        for (const auto& choice : normal) {
            if (!choice.traits.HasExplicitTES4Context()) {
                metadataFallback.push_back(choice);
            }
        }
        if (!metadataFallback.empty()) {
            spdlog::info(
                "Normal attack: using family-neutral/native BGSAttackData fallback");
            return metadataFallback;
        }

        spdlog::info(
            "Normal attack: explicit TES4 attack families are incompatible with current context");
        return {};
    }


    bool UsesBottomH2HFallback(RE::PlayerCharacter* player)
    {
        if (!player ||
            g_inputPolicy != CreatureInputPolicy::kUniversalCreature) {
            return false;
        }

        const auto normal = FilterAttackContext(player, g_cachedNormalAttacks);
        if (normal.empty()) {
            return false;
        }

        const auto equippedFamily = GetNormalWeaponFamily(player);
        if (equippedFamily != NormalWeaponFamily::kOneHand &&
            equippedFamily != NormalWeaponFamily::kTwoHand) {
            return false;
        }

        // Mirror BuildNormalAttackPool's priority exactly. The weapon is hidden
        // only when the normal attack selector must reach its final armed-melee
        // -> H2H compatibility path; an exact, neutral, or 1H<->2H attack keeps
        // the equipped weapon visible.
        if (!FilterNormalAttackFamily(normal, equippedFamily).empty()) {
            return false;
        }

        for (const auto& choice : normal) {
            if (!choice.traits.HasExplicitWeaponFamily()) {
                return false;
            }
        }

        const auto alternateFamily =
            equippedFamily == NormalWeaponFamily::kOneHand ?
                NormalWeaponFamily::kTwoHand : NormalWeaponFamily::kOneHand;
        if (!FilterNormalAttackFamily(normal, alternateFamily).empty()) {
            return false;
        }

        return !FilterNormalAttackFamily(
                    normal,
                    NormalWeaponFamily::kUnarmed).empty();
    }

    void RestoreUnsupportedWeaponVisual(std::string_view reason)
    {
        if (g_hiddenUnsupportedWeaponClones.empty()) {
            return;
        }

        std::size_t restored = 0;
        for (auto& entry : g_hiddenUnsupportedWeaponClones) {
            if (entry.clone) {
                entry.clone->SetAppCulled(entry.wasCulled);
                ++restored;
            }
        }
        g_hiddenUnsupportedWeaponClones.clear();

        spdlog::info(
            "Unsupported equipped melee weapon visual restored [{}]: clones={}",
            reason,
            restored);
    }

    std::size_t HideWeaponCloneInBiped(
        const RE::BSTSmartPointer<RE::BipedAnim>& bipedPtr,
        RE::TESObjectWEAP* weapon)
    {
        auto* biped = bipedPtr.get();
        if (!biped || !weapon) {
            return 0;
        }

        std::size_t hidden = 0;
        for (auto& object : biped->objects) {
            if (object.item != weapon || !object.partClone) {
                continue;
            }

            auto* clone = object.partClone.get();
            const auto alreadyOwned = std::find_if(
                g_hiddenUnsupportedWeaponClones.begin(),
                g_hiddenUnsupportedWeaponClones.end(),
                [clone](const HiddenUnsupportedWeaponClone& entry) {
                    return entry.clone.get() == clone;
                });
            if (alreadyOwned != g_hiddenUnsupportedWeaponClones.end()) {
                continue;
            }

            HiddenUnsupportedWeaponClone entry;
            entry.clone = object.partClone;
            entry.wasCulled = clone->GetAppCulled();
            g_hiddenUnsupportedWeaponClones.push_back(std::move(entry));
            clone->SetAppCulled(true);
            ++hidden;
        }
        return hidden;
    }

    void SyncUnsupportedEquippedWeaponVisual(
        RE::PlayerCharacter* player,
        std::string_view reason)
    {
        if (!player || !g_config.enableHideUnsupportedEquippedWeapons ||
            !UsesBottomH2HFallback(player)) {
            return;
        }

        // If this creature has no usable 1H/2H attack family and must use the
        // final H2H compatibility fallback, the weapon is visually unsupported
        // in BOTH drawn and sheathed states. Keep the item mechanically equipped
        // but hide whichever BipedAnim clone Skyrim currently owns.
        auto* weapon = GetEquippedWeapon(player);
        if (!weapon) {
            return;
        }

        std::size_t hidden = 0;
        hidden += HideWeaponCloneInBiped(player->GetBiped(false), weapon);
        hidden += HideWeaponCloneInBiped(player->GetBiped(true), weapon);

        if (hidden > 0) {
            spdlog::info(
                "Unsupported equipped melee weapon hidden [{}]: form={:08X} family={} state={} clones={}",
                reason,
                weapon->GetFormID(),
                GetNormalWeaponFamily(player) == NormalWeaponFamily::kOneHand ? "1H" : "2H",
                player->IsWeaponDrawn() ? "drawn" : "sheathed",
                hidden);
        }
    }

    void SyncConfiguredWeaponVisual(
        RE::PlayerCharacter* player,
        std::string_view reason)
    {
        if (!player) {
            return;
        }

        // A 3D rebuild can replace BipedAnim clones. Drop any ownership of the
        // previous clone first, then make one authoritative decision against the
        // player's CURRENT 3D. This also keeps draw/sheath transitions simple.
        RestoreUnsupportedWeaponVisual(reason);

        // Weapon visibility is a creature-player visual utility. Preserve the
        // long-standing UCC exception for Werewolf/Vampire Lord/Werebear by
        // requiring the full UniversalCreature policy.
        if (g_inputPolicy != CreatureInputPolicy::kUniversalCreature) {
            return;
        }

        // Highest priority: a 1H/2H weapon whose creature has to use the final
        // H2H fallback has no supported weapon animation. If configured, hide it
        // in BOTH drawn and sheathed states.
        if (g_config.enableHideUnsupportedEquippedWeapons &&
            UsesBottomH2HFallback(player)) {
            SyncUnsupportedEquippedWeaponVisual(player, reason);
            return;
        }

        // Supported drawn weapons remain visible.
        if (player->IsWeaponDrawn()) {
            return;
        }

        // Independent cosmetic option for creatures whose skeleton/behavior has
        // no useful sheathe placement. Some creatures do have valid sheathe nodes,
        // so this remains user-configurable.
        if (!g_config.enableHideSheathedWeapons) {
            return;
        }

        auto* weapon = GetEquippedWeapon(player);
        if (!weapon) {
            return;
        }

        std::size_t hidden = 0;
        hidden += HideWeaponCloneInBiped(player->GetBiped(false), weapon);
        hidden += HideWeaponCloneInBiped(player->GetBiped(true), weapon);
        if (hidden > 0) {
            spdlog::info(
                "Sheathed creature weapon hidden [{}]: form={:08X} clones={}",
                reason,
                weapon->GetFormID(),
                hidden);
        }
    }

    std::optional<AttackChoice> ChooseNormalAttack(
        RE::PlayerCharacter* player)
    {
        auto pool = BuildNormalAttackPool(player);
        if (pool.empty()) {
            return std::nullopt;
        }

        // Creature primary attack is hand-agnostic: rotate through the complete
        // current race normal-attack pool. "left"/"right" in event names are
        // animation variants, not player-hand assignments.
        auto choice = pool[g_rightCursor % pool.size()];
        ++g_rightCursor;
        return choice;
    }


    enum class PowerDirection : std::uint8_t
    {
        kStanding = 0,
        kForward,
        kBack,
        kLeft,
        kRight
    };

    PowerDirection GetRequestedPowerDirection()
    {
        // Skyrim/Oblivion directional power attacks are cardinal. If the player
        // is holding a diagonal, prefer forward/back over lateral input.
        if (g_forward && !g_back) {
            return PowerDirection::kForward;
        }
        if (g_back && !g_forward) {
            return PowerDirection::kBack;
        }
        if (g_strafeLeft && !g_strafeRight) {
            return PowerDirection::kLeft;
        }
        if (g_strafeRight && !g_strafeLeft) {
            return PowerDirection::kRight;
        }
        return PowerDirection::kStanding;
    }

    const char* PowerDirectionName(PowerDirection direction)
    {
        switch (direction) {
        case PowerDirection::kForward:
            return "forward";
        case PowerDirection::kBack:
            return "back";
        case PowerDirection::kLeft:
            return "left";
        case PowerDirection::kRight:
            return "right";
        default:
            return "standing";
        }
    }

    bool AttackMatchesPowerDirection(
        const AttackChoice& choice,
        PowerDirection direction)
    {
        switch (direction) {
        case PowerDirection::kForward:
            return choice.traits.forward;
        case PowerDirection::kBack:
            return choice.traits.back;
        case PowerDirection::kLeft:
            return choice.traits.left;
        case PowerDirection::kRight:
            return choice.traits.right;
        case PowerDirection::kStanding:
        default:
            return !choice.traits.forward &&
                   !choice.traits.back &&
                   !choice.traits.left &&
                   !choice.traits.right;
        }
    }

    enum class PowerWeaponFamily : std::uint8_t
    {
        kUnspecified = 0,
        kHandToHand,
        kOneHand,
        kTwoHand
    };

    PowerWeaponFamily GetPowerWeaponFamily(
        RE::PlayerCharacter* player)
    {
        auto* weapon = GetEquippedWeapon(player);
        if (!weapon) {
            return PowerWeaponFamily::kHandToHand;
        }

        switch (weapon->GetWeaponType()) {
        case RE::WEAPON_TYPE::kTwoHandSword:
        case RE::WEAPON_TYPE::kTwoHandAxe:
            return PowerWeaponFamily::kTwoHand;

        case RE::WEAPON_TYPE::kOneHandSword:
        case RE::WEAPON_TYPE::kOneHandDagger:
        case RE::WEAPON_TYPE::kOneHandAxe:
        case RE::WEAPON_TYPE::kOneHandMace:
            return PowerWeaponFamily::kOneHand;

        default:
            return PowerWeaponFamily::kUnspecified;
        }
    }

    bool AttackMatchesPowerWeaponFamily(
        const AttackChoice& choice,
        PowerWeaponFamily family)
    {
        switch (family) {
        case PowerWeaponFamily::kTwoHand:
            return choice.traits.twoHand;
        case PowerWeaponFamily::kOneHand:
            return choice.traits.oneHand;
        case PowerWeaponFamily::kHandToHand:
            return choice.traits.unarmed ||
                   (!choice.traits.twoHand && !choice.traits.oneHand);
        case PowerWeaponFamily::kUnspecified:
        default:
            return !choice.traits.twoHand &&
                   !choice.traits.oneHand &&
                   !choice.traits.unarmed;
        }
    }

    std::optional<AttackChoice> ChoosePowerAttack(
        RE::PlayerCharacter* player)
    {
        const auto pool =
            FilterAttackContext(player, g_cachedPowerAttacks);
        if (pool.empty()) {
            return std::nullopt;
        }

        const auto direction = GetRequestedPowerDirection();
        const auto family = GetPowerWeaponFamily(player);

        std::vector<AttackChoice> weaponMatches;
        for (const auto& choice : pool) {
            if (AttackMatchesPowerWeaponFamily(choice, family)) {
                weaponMatches.push_back(choice);
            }
        }

        // Native/custom power records can be family-neutral. Keep those as a
        // metadata fallback, but never cross an explicit TES4 weapon/context
        // boundary merely because no preferred family matched.
        if (weaponMatches.empty()) {
            for (const auto& choice : pool) {
                if (!choice.traits.HasExplicitTES4Context()) {
                    weaponMatches.push_back(choice);
                }
            }
            if (!weaponMatches.empty()) {
                spdlog::info(
                    "Power attack: using family-neutral/native BGSAttackData fallback");
            } else {
                spdlog::info(
                    "Power attack: explicit TES4 attack families are incompatible with current context");
                return std::nullopt;
            }
        }

        std::vector<AttackChoice> directionMatches;
        for (const auto& choice : weaponMatches) {
            if (AttackMatchesPowerDirection(choice, direction)) {
                directionMatches.push_back(choice);
            }
        }

        // Direction is a best-effort preference because BGSAttackData has a
        // power flag but no universal cardinal-direction field. If native ATKE
        // naming does not encode direction, still execute a confirmed power
        // attack rather than doing nothing.
        auto& candidates =
            directionMatches.empty() ? weaponMatches : directionMatches;

        if (directionMatches.empty()) {
            spdlog::info(
                "Power attack: no {} naming match; using confirmed power metadata fallback",
                PowerDirectionName(direction));
        }

        auto choice = candidates[g_rightCursor % candidates.size()];
        ++g_rightCursor;
        return choice;
    }


    std::optional<AttackChoice> ChooseHostileSpellPowerVisual(
        RE::PlayerCharacter* player)
    {
        const auto pool =
            FilterAttackContext(player, g_cachedPowerAttacks);
        if (pool.empty()) {
            return std::nullopt;
        }

        // Hostile spell visuals are not weapon attacks. Prefer the creature's
        // natural/unarmed power attack so an equipped sword/axe does not decide
        // the spellcasting visual.
        std::vector<AttackChoice> natural;
        for (const auto& choice : pool) {
            if (AttackMatchesPowerWeaponFamily(
                    choice,
                    PowerWeaponFamily::kHandToHand)) {
                natural.push_back(choice);
            }
        }

        if (natural.empty()) {
            return std::nullopt;
        }

        // Prefer a genuinely non-directional power attack (e.g. Minotaur
        // headbutt) so movement keys do not alter the spellcasting visual.
        std::vector<AttackChoice> standing;
        for (const auto& choice : natural) {
            if (AttackMatchesPowerDirection(
                    choice,
                    PowerDirection::kStanding)) {
                standing.push_back(choice);
            }
        }

        if (!standing.empty()) {
            auto choice =
                standing[g_rightCursor % standing.size()];
            ++g_rightCursor;
            return choice;
        }

        // If this creature has natural power attacks but all are directional,
        // use the current cardinal direction rather than choosing randomly.
        const auto direction = GetRequestedPowerDirection();
        std::vector<AttackChoice> directional;
        for (const auto& choice : natural) {
            if (AttackMatchesPowerDirection(
                    choice,
                    direction)) {
                directional.push_back(choice);
            }
        }

        if (!directional.empty()) {
            auto choice =
                directional[g_rightCursor % directional.size()];
            ++g_rightCursor;
            return choice;
        }

        return std::nullopt;
    }

    bool ArmAttackData(RE::PlayerCharacter* player, const AttackChoice& choice)
    {
        if (!player || !choice.data) {
            return false;
        }

        auto& runtime = player->GetActorRuntimeData();
        if (!runtime.currentProcess || !runtime.currentProcess->high) {
            spdlog::error("Player high process unavailable for '{}'", choice.event);
            return false;
        }

        runtime.currentProcess->high->attackData = choice.data;
        g_attackDataArmedByUCC = true;
        return true;
    }

    bool SendGraphEvent(RE::PlayerCharacter* player, std::string_view eventName)
    {
        if (!player) {
            return false;
        }

        return player->NotifyAnimationGraph(RE::BSFixedString(eventName.data()));
    }

    bool SendAttackGraphEvent(RE::PlayerCharacter* player, std::string_view eventName)
    {
        if (!player) {
            return false;
        }

        // The current tes4skyrim behavior generator restores the vanilla-style
        // combat posture by wrapping TES4 attacks in an outer stance/family
        // selector and a nested per-family attack state machine.  On entry, the
        // event that activates the outer attack state can be consumed before the
        // newly-active nested machine sees it; its start state is then visible
        // (for the Minotaur this is the back-power clip).
        //
        // Re-deliver the SAME authored TES4 attack event immediately after the
        // outer state has accepted it.  No timer, polling, synthetic animation
        // name, or behavior-file patch is involved.  The first delivery activates
        // the converter's attack-family context; the second reaches the now-active
        // nested machine and selects the requested authored attack.
        //
        // Native Skyrim creature graphs retain the single-delivery path.
        const bool first = SendGraphEvent(player, eventName);
        if (!first || !eventName.starts_with("attackStart_TES4_")) {
            return first;
        }

        player->NotifyAnimationGraph(RE::BSFixedString(eventName.data()));

        // The outer graph accepting the first delivery proves the event belongs
        // to this behavior.  A nested machine may report either true or false on
        // the immediate repeat depending on Havok transition semantics, so do not
        // turn a successful first delivery into an input failure solely because
        // of the compatibility redispatch.
        return first;
    }

    bool DoMeleeAttack(RE::PlayerCharacter* player)
    {
        // The normal attack pool already advances g_rightCursor. Clear stale
        // attackData before arming the next entry so the behavior graph cannot
        // reuse the previous selection and hide the round-robin rotation.
        ClearActiveAttackData(player);

        if (!g_hasUsableAttackData || g_cachedNormalAttacks.empty()) {
            spdlog::warn(
                "Normal attack ignored: active creature race has no confirmed normal BGSAttackData");
            return false;
        }

        auto choice = ChooseNormalAttack(player);
        if (!choice) {
            spdlog::error("No compatible creature normal attack available");
            return false;
        }

        // If this attack reached the bottom armed-melee -> H2H fallback, hide
        // the weapon clone before the authored unarmed animation begins. This is
        // a visual-only correction; the weapon remains equipped in gameplay.
        SyncUnsupportedEquippedWeaponVisual(player, "normal-attack");

        if (!ArmAttackData(player, *choice)) {
            return false;
        }

        player->SetGraphVariableBool("IsAttackReady", true);
        player->SetGraphVariableBool("bEquipOK", true);

        if (!SendAttackGraphEvent(player, choice->event)) {
            ClearActiveAttackData(player);
            spdlog::warn(
                "Normal attack metadata exists but active graph rejected '{}'",
                choice->event);
            return false;
        }

        return true;
    }

    void DoPowerAttack(RE::PlayerCharacter* player)
    {
        // Every explicit physical attack owns a fresh attack-data lifetime.
        // Never allow a previous normal/power attack to bleed into this action.
        ClearActiveAttackData(player);

        if (!g_hasUsableAttackData || g_cachedPowerAttacks.empty()) {
            spdlog::info(
                "Sneak power attack ignored: current creature has no confirmed power attack metadata/event");
            return;
        }

        auto choice = ChoosePowerAttack(player);
        if (!choice) {
            spdlog::info(
                "Sneak power attack: no compatible confirmed power attack");
            return;
        }

        if (!ArmAttackData(player, *choice)) {
            return;
        }

        player->SetGraphVariableBool("IsAttackReady", true);
        player->SetGraphVariableBool("bEquipOK", true);

        if (!SendAttackGraphEvent(player, choice->event)) {
            ClearActiveAttackData(player);
            spdlog::warn(
                "Power attack metadata exists but active graph rejected '{}'",
                choice->event);
            return;
        }
    }

    void HandleCreatureBlock(
        RE::PlayerCharacter* player,
        const RE::ButtonEvent* button)
    {
        if (!player || !button) {
            return;
        }

        if (button->IsDown()) {
            // Block/reaction states must never inherit a physical attack payload.
            ClearActiveAttackData(player);
            if (g_pluginBlockHeld) {
                return;
            }

            // No block-event scan or guessed event table: the standard creature
            // graph event is attempted once. NotifyAnimationGraph returns false
            // when the active graph does not expose it.
            if (SendGraphEvent(player, "blockStart")) {
                g_pluginBlockHeld = true;
                spdlog::info("Creature block START accepted");
            } else {
                spdlog::info(
                    "Creature block unavailable: graph rejected blockStart");
            }
        } else if (button->IsUp() && g_pluginBlockHeld) {
            SendGraphEvent(player, "blockStop");
            g_pluginBlockHeld = false;
            spdlog::info("Creature block STOP");
        }
    }

    void ClearActiveAttackData(RE::PlayerCharacter* player)
    {
        if (!player) {
            return;
        }

        auto& runtime = player->GetActorRuntimeData();
        if (runtime.currentProcess && runtime.currentProcess->high) {
            runtime.currentProcess->high->attackData = nullptr;
        }
        g_attackDataArmedByUCC = false;
    }

    bool ForceCreatureSheathe(RE::PlayerCharacter* player)
    {
        if (!player || g_inputPolicy != CreatureInputPolicy::kUniversalCreature) {
            return false;
        }

        const auto before = player->GetWeaponState();

        // The emergency chord means "stop UCC's current action and put the
        // weapon away now."  Clear only action-scoped state owned by UCC so a
        // stale attack/spell payload cannot survive the forced transition.
        ClearActiveAttackData(player);
        g_leftPendingFallbackCast = {};
        g_rightPendingFallbackCast = {};
        g_pendingFallbackShout = {};
        g_leftSpellPress = {};
        g_rightSpellPress = {};

        if (g_pluginBlockHeld) {
            SendGraphEvent(player, "blockStop");
            g_pluginBlockHeld = false;
        }

        // Ask Skyrim's normal actor weapon machinery to sheathe first.
        player->DrawWeaponMagicHands(false);
        const auto afterNativeRequest = player->GetWeaponState();
        const bool nativeReachedSheathed = afterNativeRequest == RE::WEAPON_STATE::kSheathed;

        bool recoveryAttempted = false;
        bool unequipSucceeded = false;
        bool reequipped = false;

        // Never write ActorState::weaponState directly.  That produced an
        // internally inconsistent actor/graph state and crashed in testing.
        // If Skyrim's ordinary sheathe request cannot leave the wedged state,
        // rebuild the weapon equip state through ActorEquipManager instead.
        if (!nativeReachedSheathed) {
            auto* weapon = GetEquippedWeapon(player);
            auto* equipManager = RE::ActorEquipManager::GetSingleton();
            if (weapon && equipManager) {
                recoveryAttempted = true;

                // Apply immediately and silently.  Passing no explicit slot lets
                // Skyrim resolve the weapon's normal equip slot, avoiding hand-
                // specific assumptions for converted creature graphs.
                unequipSucceeded = equipManager->UnequipObject(
                    player,
                    weapon,
                    nullptr,
                    1,
                    nullptr,
                    false,
                    true,
                    false,
                    true,
                    nullptr);

                if (unequipSucceeded) {
                    equipManager->EquipObject(
                        player,
                        weapon,
                        nullptr,
                        1,
                        nullptr,
                        false,
                        true,
                        false,
                        true);
                    reequipped = GetEquippedWeapon(player) == weapon;

                    // A fresh equip should be born sheathed. Ask the native
                    // machinery once more, but never force the ActorState bits.
                    player->DrawWeaponMagicHands(false);
                }
            }
        }

        // Re-apply UCC's configured visual policy immediately. Unsupported
        // H2H-fallback weapons remain hidden regardless of state; otherwise the
        // ordinary HideSheathed JSON rule decides the visual result.
        SyncConfiguredWeaponVisual(player, "forced-sheathe-chord");

        const auto finalState = player->GetWeaponState();
        spdlog::info(
            "Forced sheathe chord: state {} -> nativeRequest={} -> final={} nativeReachedSheathed={} recoveryAttempted={} unequipSucceeded={} reequipped={} IsWeaponDrawn={}",
            static_cast<std::uint32_t>(before),
            static_cast<std::uint32_t>(afterNativeRequest),
            static_cast<std::uint32_t>(finalState),
            nativeReachedSheathed,
            recoveryAttempted,
            unequipSucceeded,
            reequipped,
            player->IsWeaponDrawn());

        return finalState == RE::WEAPON_STATE::kSheathed;
    }

    bool DoVisualFallback(RE::PlayerCharacter* player, std::string_view reason)
    {
        auto pool = BuildNormalAttackPool(player);
        if (pool.empty()) {
            spdlog::warn("{} fallback: no creature normal attack available", reason);
            return false;
        }

        // Critical: clear any stale melee attack context before playing an
        // attack clip as a spell/shout VISUAL. Otherwise a previous real melee
        // BGSAttackData can survive and the visual HitFrame can damage a target.
        ClearActiveAttackData(player);

        // Try the discovered normal attack events until the active behavior graph
        // actually accepts one. BGSAttackData proves the race owns the attack;
        // NotifyAnimationGraph proves the CURRENT graph can enter it.
        for (const auto& choice : pool) {
            spdlog::info("{} visual fallback candidate: '{}'", reason, choice.event);
            if (SendAttackGraphEvent(player, choice.event)) {
                return true;
            }
        }

        spdlog::warn(
            "{} fallback: race has normal attack metadata but active graph rejected every event",
            reason);
        return false;
    }

    bool DoHostileSpellPowerVisual(
        RE::PlayerCharacter* player,
        std::string_view reason)
    {
        auto choice = ChooseHostileSpellPowerVisual(player);
        if (!choice) {
            spdlog::info(
                "{}: no compatible natural power-attack visual; using normal fallback",
                reason);
            return DoVisualFallback(player, reason);
        }

        // Animation only. Never arm BGSAttackData for a spell visual or the
        // power attack's HitFrame could physically damage the target in addition
        // to the spell.
        ClearActiveAttackData(player);

        spdlog::info(
            "{} hostile spell power visual: '{}'",
            reason,
            choice->event);

        return SendAttackGraphEvent(player, choice->event);
    }

    SpellVisualMode StartSpellVisual(
        RE::PlayerCharacter* player,
        bool leftHand,
        RE::SpellItem* spell)
    {
        if (!player || !spell) {
            return SpellVisualMode::kNone;
        }

        const bool selfDelivery =
            spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf;
        const bool concentration =
            spell->GetCastingType() ==
            RE::MagicSystem::CastingType::kConcentration;
        const bool hostile = spell->hostileCount > 0;

        // Universal fallback visual path for creature spellcasting. Native and
        // converted creature races use the same backend; behavior-graph-native
        // casting is not assumed to be player-controllable.

        // Aimed fire-and-forget spells always receive a synchronized creature
        // attack visual. Do not suppress the visual based on SpellItem::hostileCount:
        // converted/ported spells can report hostileCount == 0 even when offensive
        // (observed with Firebolt), and genuinely friendly aimed spells are allowed
        // to accept the known close-range physical-hit tradeoff for consistency.

        if (selfDelivery && concentration) {
            spdlog::info(
                "{} sustained self spell: event-driven MagicCaster lifecycle",
                leftHand ? "Left" : "Right");
            return SpellVisualMode::kNone;
        }

        bool accepted = false;
        if (hostile) {
            accepted = DoHostileSpellPowerVisual(
                player,
                leftHand ? "Left hostile spell" : "Right hostile spell");
        } else {
            accepted = DoVisualFallback(
                player,
                selfDelivery ?
                    (leftHand ? "Left self spell" : "Right self spell") :
                    (leftHand ? "Left aimed spell" : "Right aimed spell"));
        }

        return accepted ? SpellVisualMode::kAttackFallback : SpellVisualMode::kNone;
    }


    bool CheckSpellCast(
        RE::MagicCaster* caster,
        RE::SpellItem* spell,
        bool leftHand,
        float& effectiveness)
    {
        if (!caster || !spell) {
            return false;
        }

        effectiveness = 1.0F;
        RE::MagicSystem::CannotCastReason reason =
            RE::MagicSystem::CannotCastReason::kOK;

        if (!caster->CheckCast(spell, false, &effectiveness, &reason, false)) {
            spdlog::warn("{} spell CheckCast failed, reason={}",
                leftHand ? "Left" : "Right",
                static_cast<std::int32_t>(reason));
            return false;
        }

        return true;
    }

    SpellPressState& SpellPressForHand(bool leftHand)
    {
        return leftHand ? g_leftSpellPress : g_rightSpellPress;
    }

    void BeginFireForgetSpellPress(RE::SpellItem* spell, bool leftHand)
    {
        // A new explicit spell press supersedes any older fallback waiting for
        // a HitFrame in this hand. This prevents a late HitFrame from a prior
        // animation from firing the newly selected spell.
        PendingCastForHand(leftHand) = {};

        auto& press = SpellPressForHand(leftHand);
        press.spell = spell;
        press.pressedAt = Clock::now();
        press.active = spell != nullptr;

        if (spell) {
            spdlog::info(
                "{} spell charge input START {:08X} nativeCharge={:.3f}s",
                leftHand ? "Left" : "Right",
                spell->GetFormID(),
                std::max(0.0F, spell->GetChargeTime()));
        }
    }

    void CancelFireForgetSpellPress(bool leftHand, std::string_view reason)
    {
        auto& press = SpellPressForHand(leftHand);
        if (press.active && press.spell) {
            spdlog::info(
                "{} spell charge input CANCEL {:08X}: {}",
                leftHand ? "Left" : "Right",
                press.spell->GetFormID(),
                reason);
        }
        press = {};
    }

    void ReleaseFireForgetSpellPress(
        RE::PlayerCharacter* player,
        bool leftHand)
    {
        auto& press = SpellPressForHand(leftHand);
        if (!press.active || !press.spell || !player) {
            press = {};
            return;
        }

        auto* spell = press.spell;
        const auto pressedAt = press.pressedAt;
        press = {};

        // If equipment changed while held, do not cast a stale spell payload.
        if (GetSpell(player, leftHand) != spell) {
            spdlog::info(
                "{} spell charge input CANCEL {:08X}: hand equipment changed",
                leftHand ? "Left" : "Right",
                spell->GetFormID());
            return;
        }

        const float heldSeconds =
            pressedAt.time_since_epoch().count() == 0 ?
            0.0F :
            std::chrono::duration_cast<std::chrono::duration<float>>(
                Clock::now() - pressedAt).count();
        const float requiredCharge = std::max(0.0F, spell->GetChargeTime());

        // Use the spell's own native charge duration. This is measured only on
        // DOWN/UP input events; no timer, polling loop, or worker thread runs.
        if (heldSeconds + 0.0001F < requiredCharge) {
            spdlog::info(
                "{} spell charge input CANCEL {:08X}: held={:.3f}s required={:.3f}s",
                leftHand ? "Left" : "Right",
                spell->GetFormID(),
                heldSeconds,
                requiredCharge);
            return;
        }

        const auto source = leftHand ?
            RE::MagicSystem::CastingSource::kLeftHand :
            RE::MagicSystem::CastingSource::kRightHand;
        auto* caster = player->GetMagicCaster(source);
        float effectiveness = 1.0F;
        if (!caster ||
            !CheckSpellCast(caster, spell, leftHand, effectiveness)) {
            return;
        }

        spdlog::info(
            "{} spell charge input RELEASE {:08X}: held={:.3f}s required={:.3f}s",
            leftHand ? "Left" : "Right",
            spell->GetFormID(),
            heldSeconds,
            requiredCharge);

        auto& visualMode = leftHand ?
            g_leftSpellVisualMode : g_rightSpellVisualMode;
        visualMode = StartSpellVisual(player, leftHand, spell);

        if (visualMode == SpellVisualMode::kAttackFallback) {
            QueueFallbackCastForHitFrame(
                spell, leftHand, effectiveness);
        } else {
            CastExactSpellImmediate(
                player,
                spell,
                leftHand,
                effectiveness,
                "charged animationless fallback");
        }
    }

    void StartSelfConcentrationSpell(RE::PlayerCharacter* player, bool leftHand)
    {
        if (!g_creatureControlsActive || !player) {
            return;
        }

        auto* spell = GetSpell(player, leftHand);
        if (!spell ||
            spell->GetCastingType() != RE::MagicSystem::CastingType::kConcentration ||
            spell->GetDelivery() != RE::MagicSystem::Delivery::kSelf) {
            return;
        }

        auto& held = leftHand ?
            g_leftSelfConcentrationHeld : g_rightSelfConcentrationHeld;
        if (held) {
            return;
        }

        const auto source = leftHand ?
            RE::MagicSystem::CastingSource::kLeftHand :
            RE::MagicSystem::CastingSource::kRightHand;
        auto* caster = player->GetMagicCaster(source);
        if (!caster) {
            spdlog::warn(
                "{} self-concentration START failed: MagicCaster unavailable",
                leftHand ? "Left" : "Right");
            return;
        }

        float effectiveness = 1.0F;
        if (!CheckSpellCast(caster, spell, leftHand, effectiveness)) {
            return;
        }

        // Event-driven concentration: configure/start the caster once on button
        // DOWN, then let Skyrim's MagicCaster update itself normally. No worker
        // thread, no 100 ms pulse, and no periodic task scheduling.
        if (caster->currentSpell != spell) {
            caster->SetCurrentSpellImpl(spell);
        }

        const bool chargeStarted = caster->StartChargeImpl();
        if (!chargeStarted) {
            spdlog::warn(
                "{} self-concentration StartChargeImpl returned false; attempting StartCastImpl",
                leftHand ? "Left" : "Right");
        }

        caster->StartCastImpl();
        held = true;

        spdlog::info(
            "{} self-concentration event START {:08X}",
            leftHand ? "Left" : "Right",
            spell->GetFormID());
    }

    void FinishConcentrationSpell(RE::PlayerCharacter* player, bool leftHand)
    {
        auto& held = leftHand ?
            g_leftSelfConcentrationHeld : g_rightSelfConcentrationHeld;
        if (!held) {
            return;
        }

        held = false;

        if (!player) {
            return;
        }

        const auto source = leftHand ?
            RE::MagicSystem::CastingSource::kLeftHand :
            RE::MagicSystem::CastingSource::kRightHand;
        if (auto* caster = player->GetMagicCaster(source)) {
            caster->FinishCastImpl();
            spdlog::info(
                "{} self-concentration event STOP",
                leftHand ? "Left" : "Right");
        }
    }

    bool CastExactSpellImmediate(
        RE::PlayerCharacter* player,
        RE::SpellItem* spell,
        bool leftHand,
        float effectiveness,
        std::string_view reason)
    {
        if (!g_creatureControlsActive || !player || !spell) {
            return false;
        }

        const auto source = leftHand ?
            RE::MagicSystem::CastingSource::kLeftHand :
            RE::MagicSystem::CastingSource::kRightHand;

        auto* caster = player->GetMagicCaster(source);
        if (!caster) {
            spdlog::error("{} spell: MagicCaster unavailable",
                leftHand ? "Left" : "Right");
            return false;
        }

        RE::TESObjectREFR* target = nullptr;
        if (spell->GetDelivery() == RE::MagicSystem::Delivery::kSelf) {
            target = player;
        }

        caster->CastSpellImmediate(
            spell,
            false,
            target,
            effectiveness,
            false,
            0.0F,
            player);

        spdlog::info(
            "{} immediate spell cast ({}) {:08X}, delivery={}, castingType={}",
            leftHand ? "Left" : "Right",
            reason,
            spell->GetFormID(),
            static_cast<std::int32_t>(spell->GetDelivery()),
            static_cast<std::int32_t>(spell->GetCastingType()));

        return true;
    }

    bool IsConcentrationSpell(RE::SpellItem* spell)
    {
        return spell &&
            spell->GetCastingType() == RE::MagicSystem::CastingType::kConcentration;
    }

    const char* BenchTypeName(RE::TESFurniture::WorkBenchData::BenchType type)
    {
        using BenchType = RE::TESFurniture::WorkBenchData::BenchType;

        switch (type) {
        case BenchType::kCreateObject:
            return "CreateObject/Forge/Tanning";
        case BenchType::kSmithingWeapon:
            return "SmithingWeapon/Grindstone";
        case BenchType::kEnchanting:
            return "Enchanting";
        case BenchType::kEnchantingExperiment:
            return "EnchantingExperiment";
        case BenchType::kAlchemy:
            return "Alchemy";
        case BenchType::kAlchemyExperiment:
            return "AlchemyExperiment";
        case BenchType::kSmithingArmor:
            return "SmithingArmor/Workbench";
        default:
            return "None";
        }
    }

    void SetPendingWorkbenchAsOccupiedFurniture()
    {
        if (!g_creatureControlsActive || !g_workbenchActivationPending) {
            return;
        }

        auto ref = g_pendingWorkbenchRef.get();
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!ref || !player) {
            return;
        }

        auto* process = player->GetActorRuntimeData().currentProcess;
        if (!process || !process->middleHigh) {
            spdlog::warn(
                "Crafting factory prep: MiddleHighProcessData unavailable");
            return;
        }

        process->middleHigh->occupiedFurniture = ref->GetHandle();

        spdlog::info(
            "Crafting factory prep: occupiedFurniture set to {:08X}",
            ref->GetFormID());
    }

    void InjectWorkbenchContextIntoCraftingMenu(RE::CraftingMenu* menu)
    {
        if (!menu ||
            !g_creatureControlsActive ||
            !g_workbenchActivationPending) {
            return;
        }

        auto ref = g_pendingWorkbenchRef.get();
        if (!ref) {
            spdlog::warn(
                "Crafting factory injection: pending workstation expired");
            return;
        }

        auto* base = ref->GetBaseObject();
        auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
        if (!furniture) {
            spdlog::warn(
                "Crafting factory injection: pending ref is not TESFurniture");
            return;
        }

        auto* subMenu = menu->GetCraftingSubMenu();
        if (!subMenu) {
            spdlog::warn(
                "Crafting factory returned CraftingMenu with null subMenu; benchType={}",
                static_cast<std::uint32_t>(g_pendingBenchType));
            return;
        }

        subMenu->furniture = furniture;

        if (auto* smithing =
                skyrim_cast<RE::CraftingSubMenus::SmithingMenu*>(subMenu)) {
            smithing->furnitureRef = ref;
            spdlog::info(
                "Crafting factory injection complete: Smithing furnitureRef={:08X}",
                ref->GetFormID());
        } else {
            spdlog::info(
                "Crafting factory injection complete: furniture={:08X} benchType={}",
                ref->GetFormID(),
                static_cast<std::uint32_t>(g_pendingBenchType));
        }
    }

    RE::IMenu* CraftingMenuCreateHook()
    {
        if (!g_originalCraftingMenuCreate) {
            spdlog::error(
                "Crafting factory hook: original creator is null");
            return nullptr;
        }

        // This is the critical pre-construction injection. Skyrim's crafting
        // factory can query AIProcess::GetOccupiedFurniture while choosing and
        // initializing the CraftingSubMenu. Give it the actual captured
        // workstation BEFORE calling the vanilla creator.
        SetPendingWorkbenchAsOccupiedFurniture();

        RE::IMenu* created = g_originalCraftingMenuCreate();
        if (!created) {
            spdlog::warn(
                "Crafting factory hook: vanilla creator returned null");
            return nullptr;
        }

        auto* crafting =
            skyrim_cast<RE::CraftingMenu*>(created);
        if (!crafting) {
            spdlog::warn(
                "Crafting factory hook: creator returned non-CraftingMenu");
            return created;
        }

        spdlog::info(
            "Crafting factory hook: vanilla CraftingMenu created");

        // Reinforce the workstation fields immediately after vanilla creation.
        // The important difference from v2.18 is that occupiedFurniture was
        // already valid before the creator/submenu initialization ran.
        InjectWorkbenchContextIntoCraftingMenu(crafting);
        return created;
    }

    bool InstallCraftingMenuFactoryHook()
    {
        if (g_craftingFactoryHookInstalled) {
            return true;
        }

        auto* ui = RE::UI::GetSingleton();
        if (!ui) {
            return false;
        }

        const RE::BSFixedString name(RE::CraftingMenu::MENU_NAME);
        auto it = ui->menuMap.find(name);
        if (it == ui->menuMap.end()) {
            spdlog::error(
                "Crafting factory hook: Crafting Menu entry not found");
            return false;
        }

        auto& entry = it->second;
        if (!entry.create) {
            spdlog::error(
                "Crafting factory hook: Crafting Menu creator is null");
            return false;
        }

        g_originalCraftingMenuCreate = entry.create;
        entry.create = &CraftingMenuCreateHook;
        g_craftingFactoryHookInstalled = true;

        spdlog::info("Crafting Menu factory hook installed");
        return true;
    }

    bool CaptureWorkbenchForCraftingMenu(RE::PlayerCharacter* player)
    {
        if (!g_creatureControlsActive || !player) {
            return false;
        }

        g_workbenchActivationPending = false;
        g_pendingBenchType =
            RE::TESFurniture::WorkBenchData::BenchType::kNone;
        g_pendingWorkbenchRef = {};

        auto* pick = RE::CrosshairPickData::GetSingleton();
        if (!pick) {
            return false;
        }

        auto target = pick->target.get();
        if (!target) {
            return false;
        }

        auto* base = target->GetBaseObject();
        auto* furniture = base ? base->As<RE::TESFurniture>() : nullptr;
        if (!furniture) {
            return false;
        }

        const auto benchType =
            static_cast<RE::TESFurniture::WorkBenchData::BenchType>(
                furniture->workBenchData.benchType.get());

        spdlog::info(
            "Furniture Activate target={:08X} benchType={} ({})",
            target->GetFormID(),
            static_cast<std::uint32_t>(benchType),
            BenchTypeName(benchType));

        if (benchType ==
            RE::TESFurniture::WorkBenchData::BenchType::kNone) {
            return false;
        }

        g_workbenchActivationPending = true;
        g_pendingBenchType = benchType;
        g_pendingWorkbenchRef = target->GetHandle();

        if (!InstallCraftingMenuFactoryHook()) {
            spdlog::error(
                "Crafting activation: factory hook unavailable");
            return true;
        }

        // Set this before even queuing kShow so any pre-factory work performed by
        // UI processing sees the same workstation context.
        SetPendingWorkbenchAsOccupiedFurniture();

        if (auto* queue = RE::UIMessageQueue::GetSingleton()) {
            queue->AddMessage(
                RE::BSFixedString(RE::CraftingMenu::MENU_NAME),
                RE::UI_MESSAGE_TYPE::kShow,
                nullptr);

            spdlog::info(
                "Crafting Menu show queued with preloaded workstation {:08X}",
                target->GetFormID());
        }

        return true;
    }

    class CraftingMenuSink final :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>
    {
    public:
        static CraftingMenuSink* GetSingleton()
        {
            static CraftingMenuSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::MenuOpenCloseEvent* event,
            RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            if (!event || !g_creatureControlsActive) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (event->opening &&
                event->menuName == RE::CraftingMenu::MENU_NAME) {
                spdlog::info(
                    "Crafting Menu open event received");

                if (auto* ui = RE::UI::GetSingleton()) {
                    auto menu = ui->GetMenu<RE::CraftingMenu>();
                    if (menu) {
                        InjectWorkbenchContextIntoCraftingMenu(
                            menu.get());
                    }
                }
            } else if (!event->opening &&
                       event->menuName == RE::CraftingMenu::MENU_NAME) {
                g_workbenchActivationPending = false;
                g_pendingBenchType =
                    RE::TESFurniture::WorkBenchData::BenchType::kNone;
                g_pendingWorkbenchRef = {};
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };


    PendingFallbackCast& PendingCastForHand(bool leftHand)
    {
        return leftHand ?
            g_leftPendingFallbackCast :
            g_rightPendingFallbackCast;
    }

    void FirePendingFallbackCast(bool leftHand, std::string_view reason)
    {
        auto& pending = PendingCastForHand(leftHand);
        if (!pending.active || !pending.spell) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!g_creatureControlsActive || !player) {
            pending = {};
            return;
        }

        auto* spell = pending.spell;
        const float effectiveness = pending.effectiveness;
        pending = {};

        CastExactSpellImmediate(
            player,
            spell,
            leftHand,
            effectiveness,
            reason);
    }

    void QueueFallbackCastForHitFrame(
        RE::SpellItem* spell,
        bool leftHand,
        float effectiveness)
    {
        if (!g_creatureControlsActive || !spell) {
            return;
        }

        auto& pending = PendingCastForHand(leftHand);
        pending.spell = spell;
        pending.leftHand = leftHand;
        pending.effectiveness = effectiveness;
        pending.active = true;

        spdlog::info(
            "{} spell queued for exact animation HitFrame: {:08X}",
            leftHand ? "Left" : "Right",
            spell->GetFormID());

        // Deliberately no timer fallback. If the selected creature animation
        // never emits HitFrame, the spell remains unfired and the next explicit
        // input/race transition can replace/clear it. This keeps casting purely
        // event-driven and makes missing HitFrame support visible in the log.
    }

    void FirePendingFallbackShout(std::string_view reason)
    {
        if (!g_pendingFallbackShout.active ||
            !g_pendingFallbackShout.spell ||
            !g_pendingFallbackShout.shout) {
            return;
        }

        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!g_creatureControlsActive || !player) {
            g_pendingFallbackShout = {};
            return;
        }

        auto* process = player->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) {
            spdlog::warn("Shout HitFrame: HighProcessData unavailable");
            g_pendingFallbackShout = {};
            return;
        }

        const auto pending = g_pendingFallbackShout;
        g_pendingFallbackShout = {};

        auto* caster = player->GetMagicCaster(
            RE::MagicSystem::CastingSource::kInstant);
        if (!caster) {
            spdlog::error("Shout HitFrame: instant MagicCaster unavailable");
            return;
        }

        caster->CastSpellImmediate(
            pending.spell,
            false,
            nullptr,
            1.0F,
            false,
            0.0F,
            player);

        process->high->currentShout = pending.shout;
        process->high->voiceRecoveryTime = pending.recoveryTime;

        spdlog::info(
            "Shout cast at {}: spell={:08X} variation={} recovery={}",
            reason,
            pending.spell->GetFormID(),
            pending.variation,
            pending.recoveryTime);
    }

    class CreatureAnimationEventSink final :
        public RE::BSTEventSink<RE::BSAnimationGraphEvent>
    {
    public:
        static CreatureAnimationEventSink* GetSingleton()
        {
            static CreatureAnimationEventSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const RE::BSAnimationGraphEvent* event,
            RE::BSTEventSource<RE::BSAnimationGraphEvent>*) override
        {
            if (!event || !g_creatureControlsActive) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || event->holder != player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // A sink may have been attached while a previous UniversalCreature
            // race was active. If the player later becomes Werewolf/Vampire Lord/Werebear,
            // make the previously registered sink completely inert.
            if (g_inputPolicy != CreatureInputPolicy::kUniversalCreature) {
                return RE::BSEventNotifyControl::kContinue;
            }

            const auto tag = Lower(event->tag.c_str());

            // Physical BGSAttackData is a one-attack payload, not persistent actor
            // state. Clear UCC-owned attack context as soon as the graph reports an
            // attack terminal or enters a non-attack reaction. This prevents a later
            // stagger/recoil/idle HitFrame from inheriting damage from an old attack.
            const bool actionTerminatedOrInterrupted =
                tag == "attackstop" ||
                tag == "attackend" ||
                tag == "returntodefault" ||
                tag == "staggerstart" ||
                tag == "staggerstop" ||
                tag == "recoilstart" ||
                tag == "recoilstop";

            if (g_attackDataArmedByUCC && actionTerminatedOrInterrupted) {
                ClearActiveAttackData(player);
            }

            // Pending spell/shout payloads are action-scoped too. If their visual
            // attack terminates or is interrupted before its exact HitFrame, discard
            // them rather than allowing an unrelated later HitFrame to fire them.
            if (actionTerminatedOrInterrupted &&
                (g_leftPendingFallbackCast.active ||
                 g_rightPendingFallbackCast.active ||
                 g_pendingFallbackShout.active)) {
                g_leftPendingFallbackCast = {};
                g_rightPendingFallbackCast = {};
                g_pendingFallbackShout = {};
            }

            if (!g_leftPendingFallbackCast.active &&
                !g_rightPendingFallbackCast.active &&
                !g_pendingFallbackShout.active) {
                return RE::BSEventNotifyControl::kContinue;
            }

            if (tag == "hitframe") {
                // Spell and shout fallbacks use only the exact later HitFrame.
                // preHitFrame is intentionally ignored.
                FirePendingFallbackCast(false, "exact attack HitFrame");
                FirePendingFallbackCast(true, "exact attack HitFrame");
                FirePendingFallbackShout("exact attack HitFrame");

            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void EnsureCreatureAnimationEventSink(RE::PlayerCharacter* player)
    {
        if (!g_creatureControlsActive ||
            g_inputPolicy != CreatureInputPolicy::kUniversalCreature ||
            !player) {
            return;
        }

        const bool added = player->AddAnimationGraphEventSink(
            CreatureAnimationEventSink::GetSingleton());

        if (added) {
            spdlog::info("Creature animation event sink attached");
        }
    }

    RE::Setting* GetPlayerCollisionSetting()
    {
        auto* collection = RE::INISettingCollection::GetSingleton();
        if (!collection) {
            return nullptr;
        }

        return collection->GetSetting("bDisablePlayerCollision:HAVOK");
    }

    bool SetPlayerNoCollision(bool disabled)
    {
        if (disabled && !g_creatureControlsActive) {
            return false;
        }

        auto* setting = GetPlayerCollisionSetting();
        if (!setting) {
            spdlog::error(
                "Creature noclip: bDisablePlayerCollision:HAVOK not found");
            return false;
        }

        setting->data.b = disabled;
        g_playerNoCollisionHeld = disabled;

        spdlog::info(
            "Creature noclip {} (bDisablePlayerCollision:HAVOK={})",
            disabled ? "ON" : "OFF",
            disabled);

        return true;
    }

    void ForcePlayerCollisionEnabled()
    {
        if (!g_playerNoCollisionHeld) {
            return;
        }

        SetPlayerNoCollision(false);
    }

    float GetShoutHoldThreshold(std::string_view settingName, float fallback)
    {
        auto* settings = RE::GameSettingCollection::GetSingleton();
        if (!settings) {
            return fallback;
        }

        if (auto* setting = settings->GetSetting(settingName.data())) {
            return setting->data.f;
        }

        return fallback;
    }

    void CastCurrentShoutOnRelease(
        RE::PlayerCharacter* player,
        float heldSeconds)
    {
        if (!g_creatureControlsActive || !player) {
            return;
        }

        auto* process =
            player->GetActorRuntimeData().currentProcess;
        if (!process || !process->high) {
            spdlog::warn("Shout: HighProcessData unavailable");
            return;
        }

        auto* shout = process->high->currentShout;
        if (!shout) {
            spdlog::warn("Shout: no current TESShout");
            return;
        }

        const float voiceRecovery =
            process->high->voiceRecoveryTime;
        if (voiceRecovery > 0.0F) {
            spdlog::info(
                "Shout blocked by voice recovery {}s",
                voiceRecovery);
            return;
        }

        // Use Skyrim's own shout hold settings, evaluated once on button UP.
        // No polling or background timer runs while the key is held.
        const float shoutTime1 = GetShoutHoldThreshold("fShoutTime1", 0.45F);
        const float shoutTime2 = GetShoutHoldThreshold("fShoutTime2", 1.0F);

        std::size_t variation = 0;
        if (heldSeconds >= shoutTime2) {
            variation = 2;
        } else if (heldSeconds >= shoutTime1) {
            variation = 1;
        }

        while (variation > 0 &&
               !shout->variations[variation].spell) {
            --variation;
        }

        auto* spell = shout->variations[variation].spell;
        if (!spell) {
            spdlog::warn("Shout has no usable variation spell");
            return;
        }

        const float recoveryTime =
            shout->variations[variation].recoveryTime;

        // Shouts now use the same visual fallback family as spells and fire
        // only on that animation's exact HitFrame. This keeps the shout effect
        // synchronized to the visible creature spellcast instead of launching
        // immediately on button release.
        auto visualMode = StartSpellVisual(player, false, spell);
        if (visualMode != SpellVisualMode::kAttackFallback &&
            DoVisualFallback(player, "Shout spellcast")) {
            visualMode = SpellVisualMode::kAttackFallback;
        }

        if (visualMode == SpellVisualMode::kAttackFallback) {
            g_pendingFallbackShout.shout = shout;
            g_pendingFallbackShout.spell = spell;
            g_pendingFallbackShout.recoveryTime = recoveryTime;
            g_pendingFallbackShout.variation = variation;
            g_pendingFallbackShout.active = true;

            spdlog::info(
                "Shout queued for spellcast HitFrame: spell={:08X} held={:.3f}s variation={} recovery={}",
                spell->GetFormID(),
                heldSeconds,
                variation,
                recoveryTime);
        } else {
            spdlog::warn(
                "Shout not queued: current creature has no fallback animation event accepted by its graph");
        }
    }


    class InputSink final : public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static InputSink* GetSingleton()
        {
            static InputSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            RE::InputEvent* const* events,
            RE::BSTEventSource<RE::InputEvent*>*) override
        {
            if (!events || !*events) {
                return RE::BSEventNotifyControl::kContinue;
            }

            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            for (auto* event = *events; event; event = event->next) {
                if (event->eventType != RE::INPUT_EVENT_TYPE::kButton) {
                    continue;
                }

                auto* button = event->AsButtonEvent();
                if (!button) {
                    continue;
                }

                const std::string_view user(button->userEvent.c_str());

                // Keep the master creature-controls gate current.
                RefreshCreatureRaceState(player);

                // FIRST feature gate: when false, this DLL leaves all input
                // events completely alone.
                if (!g_creatureControlsActive) {
                    continue;
                }

                EnsureCreatureAnimationEventSink(player);

                // Workbench/crafting interception applies to every creature,
                // including Werewolf, Vampire Lord, and Werebear. It is handled before the
                // vanilla-special combat/jump passthrough below.
                if (user == "Activate" && button->IsDown()) {
                    if (CaptureWorkbenchForCraftingMenu(player)) {
                        continue;
                    }
                }

                // CraftingOnly is literal: Werewolf/Vampire Lord/Werebear are excluded
                // from every Universal Creature Controls feature below this point.
                // No attack discovery/dispatch, graph events, spell/shout handling,
                // Sneak remap, Jump/noclip, collision helper, or caster manipulation.
                if (g_inputPolicy == CreatureInputPolicy::kCraftingOnly) {
                    continue;
                }

                // From this point onward, every non-special creature race uses
                // the same Universal Creature Controls path, whether native
                // Skyrim or TES4-converted.
                if (user == "Forward") {
                    g_forward = button->IsPressed();
                    continue;
                }
                if (user == "Back") {
                    g_back = button->IsPressed();
                    continue;
                }
                if (user == "Strafe Left") {
                    g_strafeLeft = button->IsPressed();
                    continue;
                }
                if (user == "Strafe Right") {
                    g_strafeRight = button->IsPressed();
                    continue;
                }

                // Emergency forced-sheathe chord: Ready Weapon + Sprint.
                // Either press order works.  The latch makes one chord equal one
                // forced transition even if either key emits additional held
                // ButtonEvents.  Ordinary Ready Weapon and Sprint remain vanilla
                // when the chord is not active.
                if (user == "Ready Weapon" || user == "Sprint") {
                    if (user == "Ready Weapon") {
                        g_forceSheatheReadyWeaponHeld = button->IsPressed();
                    } else {
                        g_forceSheatheSprintHeld = button->IsPressed();
                    }

                    const bool chordHeld =
                        g_forceSheatheReadyWeaponHeld &&
                        g_forceSheatheSprintHeld;

                    if (chordHeld && !g_forceSheatheChordLatched) {
                        g_forceSheatheChordLatched = true;
                        ForceCreatureSheathe(player);
                    } else if (!chordHeld) {
                        g_forceSheatheChordLatched = false;
                    }

                    // When Ready Weapon is the key that completes the chord,
                    // neutralize this semantic event so Skyrim cannot immediately
                    // toggle the freshly forced sheathed state back to drawn.
                    // Sprint itself is left untouched.
                    if (chordHeld && user == "Ready Weapon") {
                        button->userEvent = RE::BSFixedString();
                    }
                    continue;
                }

                // Universal creature traversal fallback:
                // hold Jump = disable PLAYER collision; release = restore it.
                // Werewolf/Vampire Lord/Werebear were already passed through above and
                // therefore keep their native jump animations.
                if (user == "Jump") {
                    if (button->IsDown()) {
                        SetPlayerNoCollision(true);
                    } else if (button->IsUp()) {
                        SetPlayerNoCollision(false);
                    }
                    continue;
                }

                // Universal alternate offense:
                // Sneak triggers a BGSAttackData-confirmed power attack.
                if (user == "Sneak") {
                    if (button->IsDown()) {
                        DoPowerAttack(player);
                    }
                    continue;
                }

                if (user == "Shout") {
                    if (button->IsDown()) {
                        g_shoutPressTime = Clock::now();
                        continue;
                    }

                    if (button->IsUp()) {
                        const float heldSeconds =
                            g_shoutPressTime.time_since_epoch().count() == 0 ?
                            0.0F :
                            std::chrono::duration_cast<std::chrono::duration<float>>(
                                Clock::now() - g_shoutPressTime).count();

                        g_shoutPressTime = {};
                        CastCurrentShoutOnRelease(player, heldSeconds);
                        continue;
                    }
                }

                const bool rightInput = user == "Right Attack/Block";
                const bool leftInput = user == "Left Attack/Block";
                if (!rightInput && !leftInput) {
                    continue;
                }

                const bool leftHand = leftInput;

                // Always terminate an event-driven concentration or pending
                // spell press on button UP even if the player changed hand
                // equipment while the input was held.
                if (button->IsUp() &&
                    (leftHand ? g_leftSelfConcentrationHeld :
                                g_rightSelfConcentrationHeld)) {
                    FinishConcentrationSpell(player, leftHand);
                    SpellPressForHand(leftHand) = {};
                    if (leftHand) {
                        g_leftSpellVisualMode = SpellVisualMode::kNone;
                    } else {
                        g_rightSpellVisualMode = SpellVisualMode::kNone;
                    }
                    continue;
                }

                if (button->IsUp() &&
                    SpellPressForHand(leftHand).active &&
                    !GetSpell(player, leftHand)) {
                    CancelFireForgetSpellPress(
                        leftHand, "hand no longer contains a spell");
                    continue;
                }

                if (auto* spell = GetSpell(player, leftHand)) {
                    // Universal creature spell policy: never depend on a
                    // creature behavior graph's native player spellcasting path.
                    // Native Skyrim and TES4-converted creatures use the same
                    // fallback backend.
                    const bool concentration =
                        IsConcentrationSpell(spell);
                    const bool selfDelivery =
                        spell->GetDelivery() ==
                        RE::MagicSystem::Delivery::kSelf;

                    if (concentration && !selfDelivery) {
                        if (button->IsDown()) {
                            spdlog::info(
                                "{} aimed concentration spell ignored: {:08X}",
                                leftHand ? "Left" : "Right",
                                spell->GetFormID());
                        }
                        continue;
                    }

                    auto& visualMode =
                        leftHand ?
                            g_leftSpellVisualMode :
                            g_rightSpellVisualMode;

                    if (button->IsDown()) {
                        if (concentration && selfDelivery) {
                            StartSelfConcentrationSpell(
                                player,
                                leftHand);
                            visualMode = SpellVisualMode::kNone;
                        } else {
                            // Fire-and-forget/fixed-duration spell charge is
                            // represented only by input DOWN/UP timestamps. The
                            // fallback animation begins after a valid charged
                            // release, then exact HitFrame fires the effect.
                            BeginFireForgetSpellPress(spell, leftHand);
                            visualMode = SpellVisualMode::kNone;
                        }
                    } else if (button->IsUp()) {
                        if (concentration && selfDelivery) {
                            FinishConcentrationSpell(
                                player,
                                leftHand);
                        } else {
                            ReleaseFireForgetSpellPress(player, leftHand);
                        }
                        visualMode = SpellVisualMode::kNone;
                    }

                    continue;
                }

                // Universal creature combat policy:
                // RIGHT hand = discovered normal attacks; LEFT hand = block.
                //
                // Spell handling above still takes precedence when a spell is
                // actually equipped in that hand.
                if (leftHand) {
                    // Universal block supplement: attempt the standard creature
                    // blockStart/blockStop events and rely on graph acceptance
                    // rather than skeleton/race-name whitelists.
                    HandleCreatureBlock(player, button);
                    continue;
                }

                // Normal creature attack dispatch: mirror the proven Sneak power-attack
                // mechanism using confirmed NORMAL BGSAttackData. UCC supplies only the
                // attack selection/start bridge missing from converted player creatures;
                // the authored behavior graph owns HitFrame, recovery, attackstop, and
                // the rest of the attack lifecycle. No polling, timers, delayed retries,
                // input replay, or native AttackBlockHandler probing is involved.
                if (button->IsDown()) {
                    DoMeleeAttack(player);
                    continue;
                }
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };


    class WeaponVisualActionSink final :
        public RE::BSTEventSink<SKSE::ActionEvent>
    {
    public:
        static WeaponVisualActionSink* GetSingleton()
        {
            static WeaponVisualActionSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::ActionEvent* event,
            RE::BSTEventSource<SKSE::ActionEvent>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!event || !player || event->actor != player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // EndDraw is the exact event at which Skyrim has finished moving the
            // newly equipped weapon into its active BipedAnim clone. Re-evaluate
            // once here; no timer or frame polling is required.
            if (event->type == SKSE::ActionEvent::Type::kEndDraw) {
                RefreshCreatureRaceState(player);
                SyncConfiguredWeaponVisual(player, "end-draw");
            } else if (event->type == SKSE::ActionEvent::Type::kEndSheathe) {
                RefreshCreatureRaceState(player);
                SyncConfiguredWeaponVisual(player, "end-sheathe");
            }

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    class WeaponVisualNiNodeSink final :
        public RE::BSTEventSink<SKSE::NiNodeUpdateEvent>
    {
    public:
        static WeaponVisualNiNodeSink* GetSingleton()
        {
            static WeaponVisualNiNodeSink singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(
            const SKSE::NiNodeUpdateEvent* event,
            RE::BSTEventSource<SKSE::NiNodeUpdateEvent>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!event || !player || event->reference != player) {
                return RE::BSEventNotifyControl::kContinue;
            }

            // PostLoadGame can occur before the player's sheathed BipedAnim clone
            // has been rebuilt. SKSE sends NiNodeUpdateEvent after a player 3D
            // reconstruction, so this is the event-driven resync point for the
            // CURRENT clone. No delayed retry, timer, polling, or serialization.
            SyncConfiguredWeaponVisual(player, "player-3d-update");

            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void InitializeCreatureControlsFromCurrentPlayer(std::string_view reason)
    {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) {
            spdlog::warn(
                "Creature controls initialization skipped ({}): player unavailable",
                reason);
            return;
        }

        // Force a fresh race evaluation and attack-capability rebuild on every
        // DataLoaded/PostLoadGame/NewGame initialization, even when the player
        // was already this creature race when the save was made. No race-change
        // event is required after loading.
        spdlog::info("Creature state refresh: {}", reason);
        g_lastRaceFormID = 0;
        RefreshCreatureRaceState(player);

        // This is a no-op for NormalPlayer and CraftingOnly. In particular,
        // Werewolf/Vampire Lord/Werebear never receive our animation-event sink.
        EnsureCreatureAnimationEventSink(player);
        SyncConfiguredWeaponVisual(player, reason);

        spdlog::info(
            "Creature controls initialized from current player ({}) active={}",
            reason,
            g_creatureControlsActive);
    }

    void MessageHandler(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        if (message->type == SKSE::MessagingInterface::kInputLoaded) {
            auto* manager = RE::BSInputDeviceManager::GetSingleton();
            if (manager) {
                manager->AddEventSink(InputSink::GetSingleton());
                spdlog::info("Universal creature input sink registered");
            } else {
                spdlog::error("BSInputDeviceManager unavailable at kInputLoaded");
            }
        }

        if (message->type == SKSE::MessagingInterface::kDataLoaded) {
            if (!g_weaponVisualActionSinkRegistered) {
                if (auto* source = SKSE::GetActionEventSource()) {
                    source->AddEventSink(WeaponVisualActionSink::GetSingleton());
                    g_weaponVisualActionSinkRegistered = true;
                    spdlog::info("Weapon visual ActionEvent sink registered");
                } else {
                    spdlog::warn("SKSE ActionEvent source unavailable; weapon visual will still sync on race/load/3D update");
                }
            }

            if (!g_weaponVisualNiNodeSinkRegistered) {
                if (auto* source = SKSE::GetNiNodeUpdateEventSource()) {
                    source->AddEventSink(WeaponVisualNiNodeSink::GetSingleton());
                    g_weaponVisualNiNodeSinkRegistered = true;
                    spdlog::info("Weapon visual NiNodeUpdateEvent sink registered");
                } else {
                    spdlog::warn("SKSE NiNodeUpdateEvent source unavailable; reload-time weapon visual resync may be incomplete");
                }
            }

            if (auto* ui = RE::UI::GetSingleton()) {
                ui->AddEventSink<RE::MenuOpenCloseEvent>(
                    CraftingMenuSink::GetSingleton());
                spdlog::info("Crafting Menu context sink registered");

            }

            InitializeCreatureControlsFromCurrentPlayer("DataLoaded");
        } else if (message->type == SKSE::MessagingInterface::kPostLoadGame) {
            InitializeCreatureControlsFromCurrentPlayer("PostLoadGame");
        } else if (message->type == SKSE::MessagingInterface::kNewGame) {
            InitializeCreatureControlsFromCurrentPlayer("NewGame");
        }
    }
}

namespace UCCCore
{
    bool Initialize(const SKSE::LoadInterface* skse)
    {
        (void)skse;
        LoadConfig();

    spdlog::info("UniversalCreatureControls v2.45.46 loading; runtime {}",
        REL::Module::get().version().string());

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        spdlog::critical("SKSE messaging interface unavailable");
        return false;
    }

    messaging->RegisterListener(MessageHandler);
    return true;
    }
}
