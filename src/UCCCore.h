#pragma once

#include "PCH.h"

namespace UCCCore
{
    bool Initialize(const SKSE::LoadInterface* skse);
    void HandleSKSEMessage(const SKSE::MessagingInterface::Message* message);
    void ProcessInputEvents(RE::InputEvent* const* events);
    void ProcessEquipEvent(const RE::TESEquipEvent* event);
    void ProcessMenuEvent(const RE::MenuOpenCloseEvent* event);
    void RefreshCurrentPlayer(std::string_view reason);
}
