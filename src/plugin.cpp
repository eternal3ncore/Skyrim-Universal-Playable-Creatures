#include "PCH.h"

namespace
{
    constexpr REL::Version kPluginVersion{ 0, 1, 6, 0 };
    constexpr auto kPluginName = "UniversalPlayableCreatures"sv;
    constexpr auto kConfigPath = "Data/SKSE/Plugins/UniversalPlayableCreatures.json"sv;
    constexpr auto kRaceConfigDir = "Data/SKSE/Plugins/UniversalPlayableCreatures"sv;
    constexpr char kRaceMenuName[] = "RaceSex Menu";
    constexpr char kCamName[] = "Camera3rd [Cam3]";

    enum class HandPolicy
    {
        kLeft,
        kRight,
        kBoth
    };

    struct Config
    {
        bool enableRaceMenuCrashFix{ true };
        bool enableCameraNode{ true };
        bool enableSpellHandRestriction{ true };
        bool enableHideSheathedWeapons{ true };

        float cameraNodeHeightZ{ 121.0f };

        bool enableCreatureRacesForSpellHandRestriction{ true };
        bool enablePlayableHumanoidRacesForSpellHandRestriction{ false };
        HandPolicy skyrimCreatureSpellHand{ HandPolicy::kLeft };
        HandPolicy convertedTES4CreatureSpellHand{ HandPolicy::kRight };

    };

    Config g_config;

    std::optional<bool> ReadJsonBool(std::string_view text, std::string_view key)
    {
        const auto quotedKey = std::format("\"{}\"", key);
        auto pos = text.find(quotedKey);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        pos = text.find(':', pos + quotedKey.size());
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        ++pos;
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        if (text.substr(pos, 4) == "true") {
            return true;
        }
        if (text.substr(pos, 5) == "false") {
            return false;
        }
        return std::nullopt;
    }

    std::optional<std::string> ReadJsonString(std::string_view text, std::string_view key)
    {
        const auto quotedKey = std::format("\"{}\"", key);
        auto pos = text.find(quotedKey);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        pos = text.find(':', pos + quotedKey.size());
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        pos = text.find('"', pos + 1);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        const auto end = text.find('"', pos + 1);
        if (end == std::string_view::npos) {
            return std::nullopt;
        }
        return std::string(text.substr(pos + 1, end - pos - 1));
    }

    std::optional<float> ReadJsonFloat(std::string_view text, std::string_view key)
    {
        const auto quotedKey = std::format("\"{}\"", key);
        auto pos = text.find(quotedKey);
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        pos = text.find(':', pos + quotedKey.size());
        if (pos == std::string_view::npos) {
            return std::nullopt;
        }
        ++pos;
        while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
            ++pos;
        }
        const auto end = text.find_first_not_of("+-0123456789.eE", pos);
        try {
            return std::stof(std::string(text.substr(pos, end == std::string_view::npos ? end : end - pos)));
        } catch (...) {
            return std::nullopt;
        }
    }

    std::vector<std::string> ReadJsonStringArray(std::string_view text, std::string_view key)
    {
        std::vector<std::string> values;
        const auto quotedKey = std::format("\"{}\"", key);
        const auto keyPos = text.find(quotedKey);
        if (keyPos == std::string_view::npos) {
            return values;
        }

        const auto colon = text.find(':', keyPos + quotedKey.size());
        if (colon == std::string_view::npos) {
            return values;
        }

        const auto open = text.find('[', colon + 1);
        if (open == std::string_view::npos) {
            return values;
        }

        // Parse the array structurally instead of looking for the first ']'.
        // Race catalog files intentionally allow JSONC-style // and /* */ comments,
        // and those comments may contain text such as [EditorID]. A naive find(']')
        // therefore truncated Oblivion/Morroblivion after their first entry.
        std::size_t arrayDepth = 1;
        bool inLineComment = false;
        bool inBlockComment = false;
        bool inString = false;
        bool escaped = false;
        std::string current;

        for (std::size_t i = open + 1; i < text.size() && arrayDepth > 0; ++i) {
            const char c = text[i];
            const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

            if (inLineComment) {
                if (c == '\n' || c == '\r') {
                    inLineComment = false;
                }
                continue;
            }

            if (inBlockComment) {
                if (c == '*' && next == '/') {
                    inBlockComment = false;
                    ++i;
                }
                continue;
            }

            if (inString) {
                if (escaped) {
                    switch (c) {
                    case '"': current.push_back('"'); break;
                    case '\\': current.push_back('\\'); break;
                    case '/': current.push_back('/'); break;
                    case 'b': current.push_back('\b'); break;
                    case 'f': current.push_back('\f'); break;
                    case 'n': current.push_back('\n'); break;
                    case 'r': current.push_back('\r'); break;
                    case 't': current.push_back('\t'); break;
                    default:
                        // Race entries are plugin|FormID strings and do not require
                        // unicode escapes; preserve unknown escapes literally.
                        current.push_back(c);
                        break;
                    }
                    escaped = false;
                    continue;
                }

                if (c == '\\') {
                    escaped = true;
                    continue;
                }
                if (c == '"') {
                    inString = false;
                    if (arrayDepth == 1) {
                        values.push_back(current);
                    }
                    current.clear();
                    continue;
                }
                current.push_back(c);
                continue;
            }

            if (c == '/' && next == '/') {
                inLineComment = true;
                ++i;
                continue;
            }
            if (c == '/' && next == '*') {
                inBlockComment = true;
                ++i;
                continue;
            }
            if (c == '"') {
                inString = true;
                escaped = false;
                current.clear();
                continue;
            }
            if (c == '[') {
                ++arrayDepth;
                continue;
            }
            if (c == ']') {
                --arrayDepth;
                continue;
            }
        }

        return values;
    }

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

    bool IsConvertedTES4Race(const RE::TESRace* race)
    {
        if (!race) {
            return false;
        }
        const char* editorID = race->GetFormEditorID();
        return editorID && std::string_view(editorID).starts_with("TES4");
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
            return;
        }

        const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        if (const auto v = ReadJsonBool(text, "EnableCreatureRaceMenuCrashFix")) g_config.enableRaceMenuCrashFix = *v;
        if (const auto v = ReadJsonBool(text, "EnableThirdPersonCameraNode")) g_config.enableCameraNode = *v;
        if (const auto v = ReadJsonBool(text, "EnableCreatureSpellHandRestriction")) g_config.enableSpellHandRestriction = *v;
        if (const auto v = ReadJsonBool(text, "EnableHideSheathedWeapons")) g_config.enableHideSheathedWeapons = *v;
        if (const auto v = ReadJsonFloat(text, "CameraNodeHeightZ")) g_config.cameraNodeHeightZ = *v;
        if (const auto v = ReadJsonBool(text, "EnablePlayableHumanoidRacesForSpellHandRestriction")) g_config.enablePlayableHumanoidRacesForSpellHandRestriction = *v;
        if (const auto v = ReadJsonBool(text, "EnableCreatureRacesForSpellHandRestriction")) g_config.enableCreatureRacesForSpellHandRestriction = *v;
        if (const auto v = ReadJsonString(text, "SkyrimCreatureSpellHand")) {
            if (const auto policy = ParseHandPolicy(*v)) g_config.skyrimCreatureSpellHand = *policy;
        }
        if (const auto v = ReadJsonString(text, "ConvertedTES4CreatureSpellHand")) {
            if (const auto policy = ParseHandPolicy(*v)) g_config.convertedTES4CreatureSpellHand = *policy;
        }

        logger::info("Config loaded: RaceMenuCrashFix={} CameraNode={} SpellHandRestriction={} HideSheathedWeapons={}",
            g_config.enableRaceMenuCrashFix,
            g_config.enableCameraNode,
            g_config.enableSpellHandRestriction,
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

        std::size_t ApplyPlayableRaceFile(const std::filesystem::path& path, RE::TESDataHandler* data)
        {
            std::ifstream file(path, std::ios::binary);
            if (!file) {
                logger::warn("Playable-race config could not be opened: {}", path.filename().string());
                return 0;
            }

            const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            const auto entries = ReadJsonStringArray(text, "playableRaces");
            logger::info("Playable-race config {}: {} entr{} parsed", path.filename().string(), entries.size(), entries.size() == 1 ? "y" : "ies");
            std::size_t applied = 0;

            for (const auto& entry : entries) {
                const auto bar = entry.rfind('|');
                if (bar == std::string::npos) {
                    logger::warn("{}: entry missing plugin|formid separator: {}", path.filename().string(), entry);
                    continue;
                }
                try {
                    const auto plugin = entry.substr(0, bar);
                    const auto form = static_cast<RE::FormID>(std::stoul(entry.substr(bar + 1), nullptr, 16));
                    if (auto* race = data->LookupForm<RE::TESRace>(form, plugin)) {
                        race->data.flags.set(RE::RACE_DATA::Flag::kPlayable);
                        logger::info("Playable race enabled [{}]: {}", path.filename().string(), entry);
                        ++applied;
                    } else {
                        logger::warn("Playable race unresolved [{}]: {}", path.filename().string(), entry);
                    }
                } catch (...) {
                    logger::warn("Playable race invalid [{}]: {}", path.filename().string(), entry);
                }
            }

            logger::info("Playable-race config {}: {} applied", path.filename().string(), applied);
            return applied;
        }

        void ApplyPlayableRaceConfig()
        {
            if (!g_config.enableRaceMenuCrashFix) {
                return;
            }

            auto* data = RE::TESDataHandler::GetSingleton();
            if (!data) {
                logger::warn("Playable-race configs skipped: TESDataHandler unavailable");
                return;
            }

            const std::filesystem::path dir{ std::string(kRaceConfigDir) };
            std::error_code ec;
            if (!std::filesystem::is_directory(dir, ec)) {
                logger::warn("Playable-race config folder not found: {}", kRaceConfigDir);
                return;
            }

            std::vector<std::filesystem::path> files;
            for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
                if (!it->is_regular_file()) {
                    continue;
                }
                auto ext = it->path().extension().string();
                std::ranges::transform(ext, ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                if (ext == ".json") {
                    files.push_back(it->path());
                }
            }
            if (ec) {
                logger::warn("Playable-race config folder scan failed: {}", ec.message());
                return;
            }

            std::ranges::sort(files);
            std::size_t totalApplied = 0;
            for (const auto& path : files) {
                totalApplied += ApplyPlayableRaceFile(path, data);
            }
            logger::info("Playable-race configs loaded: {} file(s), {} race(s) applied", files.size(), totalApplied);
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
                if (!g_config.enableCreatureRacesForSpellHandRestriction) {
                    return { false, HandPolicy::kBoth };
                }
                return { true, IsConvertedTES4Race(race) ? g_config.convertedTES4CreatureSpellHand : g_config.skyrimCreatureSpellHand };
            }

            if (playable && g_config.enablePlayableHumanoidRacesForSpellHandRestriction) {
                return { true, HandPolicy::kBoth };
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

    namespace WeaponHide
    {
        constexpr std::array<RE::BIPED_OBJECT, 8> kWeaponBipedSlots{
            RE::BIPED_OBJECT::kHandToHandMelee,
            RE::BIPED_OBJECT::kOneHandSword,
            RE::BIPED_OBJECT::kOneHandDagger,
            RE::BIPED_OBJECT::kOneHandAxe,
            RE::BIPED_OBJECT::kOneHandMace,
            RE::BIPED_OBJECT::kTwoHandMelee,
            RE::BIPED_OBJECT::kBow,
            RE::BIPED_OBJECT::kStaff
        };

        constexpr RE::BIPED_OBJECT kCrossbowSlot = RE::BIPED_OBJECT::kCrossbow;
        bool g_hidden = false;

        std::size_t SetNativeWeaponDisplayHidden(RE::PlayerCharacter* player, bool hide)
        {
            if (!g_config.enableHideSheathedWeapons || !player) {
                return 0;
            }
            const auto& biped = player->GetBiped(false);
            if (!biped) {
                logger::debug("Sheathed weapon hide: third-person BipedAnim unavailable");
                return 0;
            }

            std::size_t changed = 0;
            auto apply = [&](RE::BIPED_OBJECT slot) {
                const auto index = static_cast<std::size_t>(slot);
                if (index >= static_cast<std::size_t>(RE::BIPED_OBJECT::kTotal)) {
                    return;
                }
                auto& part = biped->objects[index];
                if (part.partClone) {
                    part.partClone->SetAppCulled(hide);
                    ++changed;
                }
            };

            for (const auto slot : kWeaponBipedSlots) {
                apply(slot);
            }
            apply(kCrossbowSlot);
            g_hidden = hide && changed > 0;
            if (changed > 0) {
                logger::info("Sheathed weapon display {}: {} active clone(s)", hide ? "hidden" : "restored", changed);
            }
            return changed;
        }

        bool IsCreaturePlayer(RE::Actor* actor, RE::PlayerCharacter*& outPlayer)
        {
            outPlayer = nullptr;
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!g_config.enableHideSheathedWeapons || !actor || !player || actor != player) {
                return false;
            }
            if (!IsCreatureRaceByFaceGen(player->GetRace())) {
                if (g_hidden) {
                    SetNativeWeaponDisplayHidden(player, false);
                }
                g_hidden = false;
                return false;
            }
            outPlayer = player;
            return true;
        }

        void SyncFromEngineState(std::string_view reason)
        {
            if (!g_config.enableHideSheathedWeapons) {
                return;
            }
            auto* player = RE::PlayerCharacter::GetSingleton();
            if (!player) {
                return;
            }
            if (!IsCreatureRaceByFaceGen(player->GetRace())) {
                if (g_hidden) {
                    SetNativeWeaponDisplayHidden(player, false);
                }
                g_hidden = false;
                return;
            }

            const auto weaponState = player->GetWeaponState();
            const bool shouldHide = weaponState == RE::WEAPON_STATE::kSheathed;
            const auto changed = SetNativeWeaponDisplayHidden(player, shouldHide);
            logger::info("Weapon state sync [{}]: state={} hide={} clones={}", reason, static_cast<std::uint32_t>(weaponState), shouldHide, changed);
        }

        void OnAction(const SKSE::ActionEvent* event)
        {
            if (!event) {
                return;
            }
            RE::PlayerCharacter* player = nullptr;
            if (!IsCreaturePlayer(event->actor, player)) {
                return;
            }
            switch (event->type.get()) {
            case SKSE::ActionEvent::Type::kBeginDraw:
            case SKSE::ActionEvent::Type::kEndDraw:
                SetNativeWeaponDisplayHidden(player, false);
                break;
            case SKSE::ActionEvent::Type::kEndSheathe:
                SetNativeWeaponDisplayHidden(player, true);
                break;
            default:
                break;
            }
        }
    }

    class EventRouter final :
        public RE::BSTEventSink<RE::MenuOpenCloseEvent>,
        public RE::BSTEventSink<RE::TESSwitchRaceCompleteEvent>,
        public RE::BSTEventSink<RE::TESObjectLoadedEvent>,
        public RE::BSTEventSink<RE::InputEvent*>,
        public RE::BSTEventSink<SKSE::ActionEvent>
    {
    public:
        static EventRouter* GetSingleton()
        {
            static EventRouter singleton;
            return std::addressof(singleton);
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
        {
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
                WeaponHide::SyncFromEngineState("race-switch");
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(const RE::TESObjectLoadedEvent* event, RE::BSTEventSource<RE::TESObjectLoadedEvent>*) override
        {
            if (event && event->loaded && event->formID == 0x14) {
                CameraNode::InjectIntoCurrent3D("player-object-loaded");
                WeaponHide::SyncFromEngineState("player-object-loaded");
            }
            return RE::BSEventNotifyControl::kContinue;
        }

        RE::BSEventNotifyControl ProcessEvent(RE::InputEvent* const* events, RE::BSTEventSource<RE::InputEvent*>*) override
        {
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

        RE::BSEventNotifyControl ProcessEvent(const SKSE::ActionEvent* event, RE::BSTEventSource<SKSE::ActionEvent>*) override
        {
            WeaponHide::OnAction(event);
            return RE::BSEventNotifyControl::kContinue;
        }
    };

    void RegisterSinks()
    {
        auto* router = EventRouter::GetSingleton();

        if (auto* ui = RE::UI::GetSingleton()) {
            ui->AddEventSink<RE::MenuOpenCloseEvent>(router);
            logger::info("Menu event routing registered");
        } else {
            logger::error("UI unavailable; menu routing not registered");
        }

        if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
            holder->AddEventSink<RE::TESSwitchRaceCompleteEvent>(router);
            holder->AddEventSink<RE::TESObjectLoadedEvent>(router);
            logger::info("Race/ObjectLoaded event routing registered");
        } else {
            logger::error("ScriptEventSourceHolder unavailable; race/object routing not registered");
        }

        if (g_config.enableCameraNode) {
            if (auto* input = RE::BSInputDeviceManager::GetSingleton()) {
                input->AddEventSink<RE::InputEvent*>(router);
                logger::info("POV input event routing registered");
            } else {
                logger::warn("Input device manager unavailable; POV binding event not registered");
            }
        }

        if (g_config.enableHideSheathedWeapons) {
            if (auto* source = SKSE::GetActionEventSource()) {
                source->AddEventSink(router);
                logger::info("Weapon ActionEvent routing registered");
            } else {
                logger::warn("ActionEvent source unavailable");
            }
        }
    }

    void MessageHandler(SKSE::MessagingInterface::Message* message)
    {
        if (!message) {
            return;
        }
        switch (message->type) {
        case SKSE::MessagingInterface::kDataLoaded:
            LoadConfig();
            RaceMenuFix::ApplyPlayableRaceConfig();
            RegisterSinks();
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
            WeaponHide::SyncFromEngineState("PostLoadGame");
            break;
        case SKSE::MessagingInterface::kNewGame:
            SpellHandRestriction::RefreshPlayerState("NewGame");
            CameraNode::InjectIntoCurrent3D("NewGame");
            WeaponHide::SyncFromEngineState("NewGame");
            break;
        case SKSE::MessagingInterface::kPreLoadGame:
            WeaponHide::g_hidden = false;
            break;
        default:
            break;
        }
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
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    logger::info("SKSE messaging registered; no save serialization is used");
    return true;
}
