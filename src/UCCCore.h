#pragma once

#include "PCH.h"

namespace UCCCore
{
    struct Config
    {
        bool enableHideUnsupportedEquippedWeapons{ true };
        bool enableHideSheathedWeapons{ true };
    };

    bool Initialize(const SKSE::LoadInterface* skse);
    void Configure(const Config& config);
    void HandleSKSEMessage(const SKSE::MessagingInterface::Message* message);
    void ProcessInputEvents(RE::InputEvent* const* events);
    void ProcessEquipEvent(const RE::TESEquipEvent* event);
    void ProcessMenuEvent(const RE::MenuOpenCloseEvent* event);
    void RefreshCurrentPlayer(std::string_view reason);
}
