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

    namespace RaceCatalog
    {
        void Load(bool applyPlayableFlags);
        bool IsEnabled(const RE::TESRace* race);
        std::optional<HandPolicy> GetSpellHand(const RE::TESRace* race);
        std::size_t EntryCount();
        std::size_t ResolvedCount();
        std::size_t EnabledCount();
    }
}
