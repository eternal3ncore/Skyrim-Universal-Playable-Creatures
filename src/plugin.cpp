#include "PCH.h"
#include "RaceCatalog.h"
#include "UCCCore.h"

#include <nlohmann/json.hpp>

namespace
{
    constexpr REL::Version kPluginVersion{ 0, 2, 12, 0 };
    constexpr auto kPluginName = "UniversalPlayableCreatures"sv;
    constexpr auto kConfigPath = "Data/SKSE/Plugins/UniversalPlayableCreatures.json"sv;
    constexpr char kRaceMenuName[] = "RaceSex Menu";
    constexpr char kCamName[] = "Camera3rd [Cam3]";

    using HandPolicy = UPC::HandPolicy;

    struct Config
    {
        bool enableRaceMenuCrashFix{ true };
        bool enableCameraNode{ true };
        bool enableSpellHandRestriction{ true };
        bool enableHideUnsupportedEquippedWeapons{ true };
        bool enableHideSheathedWeapons{ true };

        float cameraNodeHeightZ{ 121.0f };
        HandPolicy playableHumanoidSpellHand{ HandPolicy::kBoth };

    };

    Config g_config;

    std::optional<HandPolicy> ParseHandPolicy(std::string value)
    {
        std::ranges::transform(value, value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (value == "left") {
            return HandPolicy::kLeft;
        }
        if (value == "right") {
            return HandPolicy::kRight;
        }
        if (value == "both") {
            return HandPolicy::kBoth;
        }
        return std::nullopt;
    }

    const char* PolicyName(HandPolicy policy)
    {
        switch (policy) {
        case HandPolicy::kLeft:
            return "Left";
        case HandPolicy::kRight:
            return "Right";
        default:
            return "Both";
        }
    }

    const char* SafeName(const RE::TESForm* form)
    {
        if (!form) {
            return "<null>";
        }
        const auto* name = form->GetName();
        return (name && *name) ? name : "<unnamed>";
    }

    bool IsCreatureRaceByFaceGen(RE::TESRace* race)
    {
        return race && !race->data.flags.any(RE::RACE_DATA::Flag::kFaceGenHead);
    }

    void SetupLog()
    {
        auto path = logger::log_directory();
        if (!path) {
            SKSE::stl::report_and_fail("Failed to resolve SKSE log directory");
        }
        *path /= std::format("{}.log", kPluginName);
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path->string(), true);
        auto log = std::make_shared<spdlog::logger>("global log", std::move(sink));
        spdlog::set_default_logger(std::move(log));
        spdlog::set_level(spdlog::level::info);
        spdlog::flush_on(spdlog::level::info);
    }

    void LoadConfig()
    {
        g_config = {};
        std::ifstream file(std::filesystem::path(std::string(kConfigPath)), std::ios::binary);
        if (!file) {
            logger::warn("Config not found at '{}'; using defaults", kConfigPath);
            UCCCore::Configure({
                g_config.enableHideUnsupportedEquippedWeapons,
                g_config.enableHideSheathedWeapons });
            return;
        }

        try {
            const std::string text(
                (std::istreambuf_iterator<char>(file)),
                std::istreambuf_iterator<char>());
            const auto json = nlohmann::json::parse(text, nullptr, true, true);

            g_config.enableRaceMenuCrashFix = json.value(
                "EnableCreatureRaceMenuCrashFix", g_config.enableRaceMenuCrashFix);
            g_config.enableCameraNode = json.value(
                "EnableThirdPersonCameraNode", g_config.enableCameraNode);
            g_config.enableSpellHandRestriction = json.value(
                "EnableCreatureSpellHandRestriction", g_config.enableSpellHandRestriction);
            g_config.enableHideUnsupportedEquippedWeapons = json.value(
                "EnableHideUnsupportedEquippedWeapons", g_config.enableHideUnsupportedEquippedWeapons);
            g_config.enableHideSheathedWeapons = json.value(
                "EnableHideSheathedWeapons", g_config.enableHideSheathedWeapons);
            g_config.cameraNodeHeightZ = json.value(
                "CameraNodeHeightZ", g_config.cameraNodeHeightZ);

            if (const auto it = json.find("PlayableHumanoidSpellHand");
                it != json.end() && it->is_string()) {
                const auto value = it->get<std::string>();
                if (const auto policy = ParseHandPolicy(value)) {
                    g_config.playableHumanoidSpellHand = *policy;
                } else {
                    logger::warn(
                        "Invalid PlayableHumanoidSpellHand='{}'; using Both",
                        value);
                }
            }
        } catch (const std::exception& e) {
            logger::error("Config parse failed: {}; using defaults", e.what());
            g_config = {};
        }

        UCCCore::Configure({
            g_config.enableHideUnsupportedEquippedWeapons,
            g_config.enableHideSheathedWeapons });

        logger::info(
            "Config loaded: RaceMenuCrashFix={} CameraNode={} SpellHandRestriction={} HumanoidSpellHand={} HideUnsupportedEquipped={} HideSheathed={}",
            g_config.enableRaceMenuCrashFix,
            g_config.enableCameraNode,
            g_config.enableSpellHandRestriction,
            PolicyName(g_config.playableHumanoidSpellHand),
            g_config.enableHideUnsupportedEquippedWeapons,
            g_config.enableHideSheathedWeapons);
    }

    namespace RaceMenuFix
    {
        std::atomic_bool g_latentCreatureRaceMenu{ false };
        constexpr uintptr_t kPatchA = 0x432550;
        constexpr uintptr_t kPatchB = 0x432D50;
        using FnA = void (*)(void*, std::uint8_t);
        using FnB = void (*)(void*, std::uint32_t, std::uint32_t);
        FnA g_originalA = nullptr;
        FnB g_originalB = nullptr;

        bool IsRaceMenuOpen()
        {
            auto* ui = RE::UI::GetSingleton();
            return ui && ui->IsMenuOpen(kRaceMenuName);
        }

        bool ShouldProtect()
        {
            if (!g_config.enableRaceMenuCrashFix || !IsRaceMenuOpen()) {
                g_latentCreatureRaceMenu = false;
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (player && IsCreatureRaceByFaceGen(player->GetRace())) {
                g_latentCreatureRaceMenu = true;
            }
            return g_latentCreatureRaceMenu;
        }

        void HookA(void* object, std::uint8_t flag)
        {
            if (!object && ShouldProtect()) {
                return;
            }
            g_originalA(object, flag);
        }

        void HookB(void* object, std::uint32_t a, std::uint32_t b)
        {
            if (!object && ShouldProtect()) {
                return;
            }
            g_originalB(object, a, b);
        }

        void WriteAbsJump(std::uint8_t* dst, const void* target)
        {
            dst[0] = 0xff;
            dst[1] = 0x25;
            dst[2] = dst[3] = dst[4] = dst[5] = 0;
            const auto addr = reinterpret_cast<std::uint64_t>(target);
            std::memcpy(dst + 6, std::addressof(addr), sizeof(addr));
        }

        void* Detour(uintptr_t target, const void* hook, size_t length)
        {
            auto* gateway = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, length + 14, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
            if (!gateway) {
                return nullptr;
            }
            std::memcpy(gateway, reinterpret_cast<void*>(target), length);
            WriteAbsJump(gateway + length, reinterpret_cast<void*>(target + length));

            DWORD oldProtect = 0;
            if (!VirtualProtect(reinterpret_cast<void*>(target), length, PAGE_EXECUTE_READWRITE, std::addressof(oldProtect))) {
                return nullptr;
            }
            WriteAbsJump(reinterpret_cast<std::uint8_t*>(target), hook);
            for (size_t i = 14; i < length; ++i) {
                reinterpret_cast<std::uint8_t*>(target)[i] = 0x90;
            }
            DWORD ignored = 0;
            VirtualProtect(reinterpret_cast<void*>(target), length, oldProtect, std::addressof(ignored));
            FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(target), length);
            return gateway;
        }

        bool ApplyHooks()
        {
            if (!g_config.enableRaceMenuCrashFix) {
                logger::info("RaceMenu crash fix disabled by config");
                return true;
            }
            auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(L"SkyrimSE.exe"));
            constexpr std::array<std::uint8_t, 19> sigA{ 0x88, 0x54, 0x24, 0x10, 0x4c, 0x8b, 0xdc, 0x56, 0x41, 0x54, 0x41, 0x56, 0x48, 0x81, 0xec, 0xe0, 0, 0, 0 };
            constexpr std::array<std::uint8_t, 14> sigB{ 0x48, 0x89, 0x5c, 0x24, 0x18, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x83, 0xec, 0x40 };
            if (!base || std::memcmp(reinterpret_cast<void*>(base + kPatchA), sigA.data(), sigA.size()) || std::memcmp(reinterpret_cast<void*>(base + kPatchB), sigB.data(), sigB.size())) {
                logger::error("RaceMenu crash fix signature check failed; hooks not installed");
                return false;
            }
            g_originalA = reinterpret_cast<FnA>(Detour(base + kPatchA, reinterpret_cast<void*>(&HookA), sigA.size()));
            g_originalB = reinterpret_cast<FnB>(Detour(base + kPatchB, reinterpret_cast<void*>(&HookB), sigB.size()));
            logger::info("RaceMenu crash safeguards {}", (g_originalA && g_originalB) ? "enabled" : "failed");
            return g_originalA && g_originalB;
        }

    }

    namespace CameraNode
    {
        RE::NiAVObject* FindCameraNode(RE::NiAVObject* root)
        {
            return root ? root->GetObjectByName(kCamName) : nullptr;
        }

        bool InjectIntoCurrent3D(std::string_view reason)
        {
            if (!g_config.enableCameraNode) {
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player || !IsCreatureRaceByFaceGen(player->GetRace())) {
                return false;
            }
            auto* root = player->Get3D();
            if (!root) {
                logger::debug("Camera node inject [{}]: player 3D unavailable", reason);
                return false;
            }
            if (FindCameraNode(root)) {
                return true;
            }
            auto* parent = root->AsNode();
            if (!parent) {
                logger::warn("Camera node inject [{}]: root is not NiNode", reason);
                return false;
            }
            auto node = RE::NiNode::Create(0);
            if (!node) {
                logger::warn("Camera node inject [{}]: NiNode::Create failed", reason);
                return false;
            }
            node->name = kCamName;
            node->local.translate = RE::NiPoint3{ 0.0f, 0.0f, g_config.cameraNodeHeightZ };
            parent->AttachChild(node, true);
            const bool ok = FindCameraNode(root) != nullptr;
            logger::info("Camera node {} [{}]: {} at local Z={}", ok ? "injected" : "verification failed", reason, kCamName, g_config.cameraNodeHeightZ);
            return ok;
        }

        bool BindThirdPersonCameraObj(std::string_view reason)
        {
            if (!g_config.enableCameraNode) {
                return false;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            auto* camera = RE::PlayerCamera::GetSingleton();
            if (!player || !camera || !IsCreatureRaceByFaceGen(player->GetRace())) {
                return false;
            }
            auto thirdPersonState = camera->cameraStates[RE::CameraState::kThirdPerson];
            auto* thirdPerson = reinterpret_cast<RE::ThirdPersonState*>(thirdPersonState.get());
            if (!thirdPerson || camera->currentState != thirdPersonState) {
                return false;
            }
            auto* camNode = FindCameraNode(player->Get3D());
            if (!camNode) {
                InjectIntoCurrent3D("third-person-bind");
                camNode = FindCameraNode(player->Get3D());
            }
            if (!camNode) {
                return false;
            }
            if (thirdPerson->thirdPersonCameraObj) {
                return thirdPerson->thirdPersonCameraObj == camNode;
            }
            thirdPerson->thirdPersonCameraObj = camNode;
            logger::info("Third-person cameraObj bound [{}] to {}", reason, kCamName);
            return thirdPerson->thirdPersonCameraObj == camNode;
        }

        void QueueBindAfterPOVInput()
        {
            if (!g_config.enableCameraNode) {
                return;
            }
            if (auto* task = SKSE::GetTaskInterface()) {
                task->AddTask([]() { BindThirdPersonCameraObj("toggle-pov"); });
            }
        }

        void OnRaceMenuEvent(bool opening)
        {
            if (!g_config.enableCameraNode) {
                return;
            }
            if (opening) {
                InjectIntoCurrent3D("racemenu-open");
            } else {
                InjectIntoCurrent3D("racemenu-close");
                BindThirdPersonCameraObj("racemenu-close");
            }
        }

        void OnRaceSwitch()
        {
            if (!g_config.enableCameraNode) {
                return;
            }
            InjectIntoCurrent3D("race-switch");
            BindThirdPersonCameraObj("race-switch");
        }
    }

    namespace SpellHandRestriction
    {
        struct OriginalSpellState
        {
            RE::BGSEquipSlot* equipSlot{ nullptr };
        };

        HandPolicy g_activePolicy = HandPolicy::kBoth;
        bool g_active = false;
        std::unordered_map<RE::SpellItem*, OriginalSpellState> g_restrictedSpells;

        std::pair<bool, HandPolicy> GetPolicyForRace(RE::TESRace* race)
        {
            if (!race || !g_config.enableSpellHandRestriction) {
                return { false, HandPolicy::kBoth };
            }

            const bool faceGen = race->data.flags.any(RE::RACE_DATA::Flag::kFaceGenHead);
            const bool playable = race->data.flags.any(RE::RACE_DATA::Flag::kPlayable);

            if (!faceGen) {
                // Creature handedness is explicit per race.  Missing rows are
                // intentionally unrestricted rather than guessed from origin.
                if (const auto policy = UPC::RaceCatalog::GetSpellHand(race)) {
                    return { *policy != HandPolicy::kBoth, *policy };
                }
                return { false, HandPolicy::kBoth };
            }

            // Humanoids do not need catalog rows.  Both is the default/no-op.
            if (playable && g_config.playableHumanoidSpellHand != HandPolicy::kBoth) {
                return { true, g_config.playableHumanoidSpellHand };
            }

            return { false, HandPolicy::kBoth };
        }

        RE::BGSEquipSlot* GetEquipSlot(HandPolicy policy)
        {
            constexpr RE::FormID kRightHandEquip = 0x00013F42;
            constexpr RE::FormID kLeftHandEquip = 0x00013F43;
            constexpr RE::FormID kEitherHandEquip = 0x00013F44;

            switch (policy) {
            case HandPolicy::kLeft:
                return RE::TESForm::LookupByID<RE::BGSEquipSlot>(kLeftHandEquip);
            case HandPolicy::kRight:
                return RE::TESForm::LookupByID<RE::BGSEquipSlot>(kRightHandEquip);
            default:
                return RE::TESForm::LookupByID<RE::BGSEquipSlot>(kEitherHandEquip);
            }
        }

        void RestoreAllSpellSlots()
        {
            if (g_restrictedSpells.empty()) {
                return;
            }
            std::size_t restored = 0;
            for (auto& [spell, original] : g_restrictedSpells) {
                if (spell) {
                    spell->SetEquipSlot(original.equipSlot);
                    ++restored;
                }
            }
            g_restrictedSpells.clear();
            logger::info("Spell hand restriction restored original equip slot on {} spell(s)", restored);
        }

        void CollectKnownOrdinarySpells(RE::PlayerCharacter* player, std::vector<RE::SpellItem*>& out)
        {
            auto* dataHandler = RE::TESDataHandler::GetSingleton();
            if (!player || !dataHandler) {
                return;
            }
            const auto& spells = dataHandler->GetFormArray<RE::SpellItem>();
            out.reserve(spells.size());
            for (auto* spell : spells) {
                if (spell && spell->data.spellType == RE::MagicSystem::SpellType::kSpell && player->HasSpell(spell)) {
                    out.push_back(spell);
                }
            }
        }

        void ApplySpellOrientation(RE::PlayerCharacter* player)
        {
            if (!player || !g_active || g_activePolicy == HandPolicy::kBoth) {
                RestoreAllSpellSlots();
                return;
            }
            auto* targetSlot = GetEquipSlot(g_activePolicy);
            if (!targetSlot) {
                logger::error("Cannot apply spell hand restriction: {} slot unavailable", PolicyName(g_activePolicy));
                return;
            }

            RestoreAllSpellSlots();

            std::vector<RE::SpellItem*> learnedSpells;
            CollectKnownOrdinarySpells(player, learnedSpells);

            std::unordered_set<RE::SpellItem*> unique;
            std::size_t restricted = 0;
            for (auto* spell : learnedSpells) {
                if (!spell || !unique.insert(spell).second) {
                    continue;
                }
                g_restrictedSpells.emplace(spell, OriginalSpellState{ spell->GetEquipSlot() });
                spell->SetEquipSlot(targetSlot);
                ++restricted;
            }

            logger::info("Applied {}-hand spell orientation to {} known ordinary spell(s)", PolicyName(g_activePolicy), restricted);
        }

        void EnforceCurrentSelection(RE::PlayerCharacter* player)
        {
            if (!player || !g_active || g_activePolicy == HandPolicy::kBoth) {
                return;
            }

            auto& runtime = player->GetActorRuntimeData();
            auto* leftMagic = runtime.selectedSpells[RE::Actor::SlotTypes::kLeftHand];
            auto* rightMagic = runtime.selectedSpells[RE::Actor::SlotTypes::kRightHand];
            auto* left = leftMagic ? leftMagic->As<RE::SpellItem>() : nullptr;
            auto* right = rightMagic ? rightMagic->As<RE::SpellItem>() : nullptr;

            RE::SpellItem* wrong = nullptr;
            RE::SpellItem* alreadyAllowed = nullptr;
            if (g_activePolicy == HandPolicy::kLeft) {
                wrong = right;
                alreadyAllowed = left;
            } else {
                wrong = left;
                alreadyAllowed = right;
            }

            if (!wrong || !g_restrictedSpells.contains(wrong)) {
                return;
            }

            logger::info("Removing wrong-hand spell {:08X} '{}' policy={}", wrong->GetFormID(), SafeName(wrong), PolicyName(g_activePolicy));
            player->DeselectSpell(wrong);

            if (!alreadyAllowed || alreadyAllowed == wrong) {
                if (auto* equipManager = RE::ActorEquipManager::GetSingleton()) {
                    if (auto* targetSlot = GetEquipSlot(g_activePolicy)) {
                        equipManager->EquipSpell(player, wrong, targetSlot);
                        logger::info("Moved {:08X} '{}' to permitted {} hand", wrong->GetFormID(), SafeName(wrong), PolicyName(g_activePolicy));
                    }
                }
            }
        }

        void RefreshPlayerState(std::string_view reason)
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }

            auto* race = player->GetRace();
            const auto [active, policy] = GetPolicyForRace(race);
            const bool changed = active != g_active || policy != g_activePolicy;
            g_active = active;
            g_activePolicy = policy;

            logger::info("Spell hand refresh {}: race={:08X} editorID='{}' active={} policy={}",
                reason,
                race ? race->GetFormID() : 0,
                (race && race->GetFormEditorID()) ? race->GetFormEditorID() : "",
                g_active,
                PolicyName(g_activePolicy));

            if (changed) {
                ApplySpellOrientation(player);
                EnforceCurrentSelection(player);
            } else if (!g_active) {
                RestoreAllSpellSlots();
            }
        }

    }

    class EventRouter final :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>,
        public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
        public RE::BSTEventSink<RE::TESEquipEvent>,
        public RE::BSTEventSink<RE::InputEvent*>
    {
    public:
        static EventRouter* GetSingleton()
        {
            static EventRouter singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
            UCCCore::ProcessMenuEvent(event);
            if (!event) {
                return RE::BSEventNotifyControl::kContinue;
            }
            if (event->menuName == kRaceMenuName) {
                RaceMenuFix::g_latentCreatureRaceMenu = false;
                CameraNode::OnRaceMenuEvent(event->opening);
            } else if (event->menuName == RE::MagicMenu::MENU_NAME && g_config.enableSpellHandRestriction) {
                if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                    if (event->opening) {
                        SpellHandRestriction::ApplySpellOrientation(player);
                    } else {
                        SpellHandRestriction::EnforceCurrentSelection(player);
                    }
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESSwitchRaceCompleteEvent* event, RE::BSTEventSource<RE::TESSwitchRaceCompleteEvent>*) override
        {
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (event && player && event->subject.get() == player) {
                CameraNode::OnRaceSwitch();
                SpellHandRestriction::RefreshPlayerState("RaceSwitchComplete");
                UCCCore::RefreshCurrentPlayer("RaceSwitchComplete");
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
        {
            if (event && event->loaded && event->formID == 0x14) {
                CameraNode::InjectIntoCurrent3D("player-object-loaded");
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESEquipEvent* event, RE::BSTEventSource<RE::TESEquipEvent>*) override
        {
            UCCCore::ProcessEquipEvent(event);
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
        {
            // One BSInputDeviceManager sink owns all merged UPC/UCC input. UCC
            // processes creature controls first; camera POV handling is an
            // independent lightweight pass over the same event chain.
            UCCCore::ProcessInputEvents(events);

            if (!g_config.enableCameraNode || !events || !*events) {
                return RE::BSEventNotifyControl::kContinue;
            }
            auto* userEvents = RE::UserEvents::GetSingleton();
            if (!userEvents) {
                return RE::BSEventNotifyControl::kContinue;
            }
            for (auto* event = *events; event; event = event->next) {
                auto* button = event->AsButtonEvent();
                if (button && button->QUserEvent() == userEvents->togglePOV && button->IsDown()) {
                    CameraNode::QueueBindAfterPOVInput();
                    break;
                }
            }
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    bool g_inputSinkRegistered = false;
    bool g_dataSinksRegistered = false;

    void RegisterInputSink()
    {
        if (g_inputSinkRegistered) {
            return;
        }
        if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
            input->AddEventSink<RE::InputEvent*>(EventRouter::GetSingleton());
            g_inputSinkRegistered = true;
            logger::info("Shared UPC/UCC input event routing registered");
        } else {
            logger::error("Input device manager unavailable; UPC/UCC controls not registered");
        }
    }

    void RegisterDataSinks()
    {
        if (g_dataSinksRegistered) {
            return;
        }

        auto* router = EventRouter::GetSingleton();
        bool complete = true;

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(router);
            logger::info("Menu event routing registered");
        } else {
            logger::error("UI unavailable; menu routing not registered");
            complete = false;
        }

        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESSwitchRaceCompleteEvent>(router);
            holder->AddEventSink<RE::TESObjectLoadedEvent>(router);
            holder->AddEventSink<RE::TESEquipEvent>(router);
            logger::info("Race/ObjectLoaded/Equip event routing registered");
        } else {
            logger::error("ScriptEventSourceHolder unavailable; race/object/equip routing not registered");
            complete = false;
        }

        g_dataSinksRegistered = complete;
    }

    void MessageHandler(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }

        switch (message->type) {
        case SKSE::MessagingInterface::kInputLoaded:
            RegisterInputSink();
            break;

        case SKSE::MessagingInterface::kDataLoaded:
            LoadConfig();
            UPC::RaceCatalog::Load(g_config.enableRaceMenuCrashFix);
            RegisterDataSinks();
            RaceMenuFix::ApplyHooks();
            SpellHandRestriction::RefreshPlayerState("DataLoaded");
            CameraNode::InjectIntoCurrent3D("DataLoaded");
            break;

        case SKSE::MessagingInterface::kPostLoadGame:
            SpellHandRestriction::RefreshPlayerState("PostLoadGame");
            if (auto* player = RE::PlayerCharacter::GetSingleton()) {
                SpellHandRestriction::ApplySpellOrientation(player);
                SpellHandRestriction::EnforceCurrentSelection(player);
            }
            CameraNode::InjectIntoCurrent3D("PostLoadGame");
            break;

        case SKSE::MessagingInterface::kNewGame:
            SpellHandRestriction::RefreshPlayerState("NewGame");
            CameraNode::InjectIntoCurrent3D("NewGame");
            break;

        default:
            break;
        }

        // UCC is an internal module, not a second SKSE plugin. Forward the same
        // lifecycle message through the sole UPC listener after host-owned state
        // (notably config/catalog loading) is ready.
        UCCCore::HandleSKSEMessage(message);
    }

}

SKSEPluginInfo(
    .Version = kPluginVersion,
    .Name = "UniversalPlayableCreatures",
    .Author = "eternal3ncore / ChatGPT",
    .StructCompatibility = SKSE::StructCompatibility::Independent,
    .RuntimeCompatibility = SKSE::VersionIndependence::AddressLibrary)

SKSEPluginLoad(const SKSE::LoadInterface* skse)
{
    SKSE::Init(skse);
    SetupLog();
    logger::info("{} v{} loading; runtime {}", kPluginName, kPluginVersion.string(), REL::Module::get().version().string());

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageHandler)) {
        logger::critical("Failed to register UPC SKSE messaging listener");
        return false;
    }

    if (!UCCCore::Initialize(skse)) {
        logger::critical("Universal Creature Controls core initialization failed");
        return false;
    }

    logger::info("Unified UPC/UCC lifecycle registered; one SKSE listener, one input route, no save serialization");
    return true;
}


