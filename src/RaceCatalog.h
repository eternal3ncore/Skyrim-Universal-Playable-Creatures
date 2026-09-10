#pragma once

#include "PCH.h"

namespace UPC
{
    enum class HandPolicy : std::uint8_t
    {
        kLeft,
        kRight,
        kBoth
    };

    struct WeaponVisibilityPolicy
    {
        bool hideEquippedWeapon{ false };
        bool hideSheathedWeapon{ false };
    };

    struct AttackFamilyProfile
    {
        std::string name;
        std::vector<std::string> eventPrefixes;
        std::vector<std::string> oneHandMarkers;
        std::vector<std::string> twoHandMarkers;
        std::vector<std::string> unarmedMarkers;
        std::vector<std::string> bowMarkers;
        std::vector<std::string> staffMarkers;
        std::vector<std::string> swimmingPrefixes;
        std::vector<std::string> powerMarkers;
        std::vector<std::string> bashMarkers;
        std::vector<std::string> blockAttackMarkers;
        std::vector<std::string> counterAttackMarkers;
        std::vector<std::string> excludedAttackMarkers;
        bool redispatchMatchedEvents{ false };
    };

    namespace RaceCatalog
    {
        void Load(bool applyPlayableFlags);
        bool IsEnabled(const RE::TESRace* race);
        bool UseCombatWorkaround(const RE::TESRace* race);
        const AttackFamilyProfile* GetAttackFamily(const RE::TESRace* race);
        std::optional<HandPolicy> GetSpellHand(const RE::TESRace* race);
        WeaponVisibilityPolicy GetWeaponVisibility(const RE::TESRace* race);
        std::size_t EntryCount();
        std::size_t ResolvedCount();
        std::size_t EnabledCount();
    }
}
