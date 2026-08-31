#include "RaceCatalog.h"

#include <nlohmann/json.hpp>

namespace UPC::RaceCatalog
{
    namespace
    {
        constexpr auto kRaceConfigDir = "Data/SKSE/Plugins/UniversalPlayableCreatures"sv;

        std::unordered_map<const RE::TESRace*, HandPolicy> g_spellHands;
        std::size_t g_entryCount = 0;
        std::size_t g_resolvedCount = 0;

        std::optional<HandPolicy> ParseHand(std::string value)
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

        void LoadFile(const std::filesystem::path& path)
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
                auto* race = ResolveRace(spec);
                if (!race) {
                    logger::warn("Race catalog unresolved [{}]: {}", path.filename().string(), spec);
                    continue;
                }

                ++g_resolvedCount;
                ++fileResolved;

                const bool playable = item.value("playable", false);
                if (playable) {
                    race->data.flags.set(RE::RACE_DATA::Flag::kPlayable);
                    ++filePlayable;
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
            }

            logger::info(
                "Race catalog [{}]: entries={} resolved={} playable={} spellPolicies={}",
                path.filename().string(), fileEntries, fileResolved, filePlayable, filePolicies);
        }
    }

    void Load()
    {
        g_spellHands.clear();
        g_entryCount = 0;
        g_resolvedCount = 0;

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
            auto ext = it->path().extension().string();
            std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
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
            LoadFile(path);
        }

        logger::info(
            "Race catalogs loaded: files={} entries={} resolved={} spellPolicies={}",
            files.size(), g_entryCount, g_resolvedCount, g_spellHands.size());
    }

    std::optional<HandPolicy> GetSpellHand(const RE::TESRace* race)
    {
        if (!race) {
            return std::nullopt;
        }
        if (const auto it = g_spellHands.find(race); it != g_spellHands.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    std::size_t EntryCount()
    {
        return g_entryCount;
    }

    std::size_t ResolvedCount()
    {
        return g_resolvedCount;
    }
}
