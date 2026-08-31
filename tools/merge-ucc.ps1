$ErrorActionPreference = 'Stop'

function Replace-Exact {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][string]$Old,
        [Parameter(Mandatory=$true)][string]$New,
        [Parameter(Mandatory=$true)][string]$Label
    )
    if (-not $Text.Contains($Old)) {
        throw "Merge transform failed: expected block not found: $Label"
    }
    return $Text.Replace($Old, $New)
}

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$upcPath = Join-Path $root 'src\plugin.cpp'
$uccSource = Join-Path $root '_ucc\src\plugin.cpp'
$uccOut = Join-Path $root 'src\ucc_core.cpp'

if (-not (Test-Path $upcPath)) { throw "UPC source not found: $upcPath" }
if (-not (Test-Path $uccSource)) { throw "UCC source not found: $uccSource" }

# ---------------------------------------------------------------------------
# UPC: convert v0.1.6 into the merged v0.2.0 host/entry point.
# ---------------------------------------------------------------------------
$upc = Get-Content -LiteralPath $upcPath -Raw

if ($upc.Contains('constexpr REL::Version kPluginVersion{ 0, 1, 6, 0 };')) {
    $upc = Replace-Exact $upc '#include "PCH.h"' @'
#include "PCH.h"
#include "RaceCatalog.h"

namespace UCCCore
{
    bool Initialize(const SKSE::LoadInterface* skse);
}
'@ 'UPC includes/forward declaration'

    $upc = Replace-Exact $upc 'constexpr REL::Version kPluginVersion{ 0, 1, 6, 0 };' 'constexpr REL::Version kPluginVersion{ 0, 2, 0, 0 };' 'UPC version'

    $oldEnum = @'
    enum class HandPolicy
    {
        kLeft,
        kRight,
        kBoth
    };
'@
    $upc = Replace-Exact $upc $oldEnum '    using HandPolicy = UPC::HandPolicy;' 'HandPolicy alias'

    $oldConfig = @'
        bool enableRaceMenuCrashFix{ true };
        bool enableCameraNode{ true };
        bool enableSpellHandRestriction{ true };
        bool enableHideSheathedWeapons{ true };

        float cameraNodeHeightZ{ 121.0f };

        bool enableCreatureRacesForSpellHandRestriction{ true };
        bool enablePlayableHumanoidRacesForSpellHandRestriction{ false };
        HandPolicy skyrimCreatureSpellHand{ HandPolicy::kLeft };
        HandPolicy convertedTES4CreatureSpellHand{ HandPolicy::kRight };
'@
    $newConfig = @'
        bool enableRaceMenuCrashFix{ true };
        bool enableCameraNode{ true };
        bool enableSpellHandRestriction{ true };

        // The merged UCC core exclusively owns weapon visibility.  This legacy
        // UPC field is deliberately hard-disabled so only one subsystem can cull
        // and restore BipedAnim weapon clones.
        bool enableHideSheathedWeapons{ false };

        float cameraNodeHeightZ{ 121.0f };
        HandPolicy playableHumanoidSpellHand{ HandPolicy::kBoth };
'@
    $upc = Replace-Exact $upc $oldConfig $newConfig 'Config structure'

    $oldLoad = @'
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
'@
    $newLoad = @'
        if (const auto v = ReadJsonBool(text, "EnableCreatureRaceMenuCrashFix")) g_config.enableRaceMenuCrashFix = *v;
        if (const auto v = ReadJsonBool(text, "EnableThirdPersonCameraNode")) g_config.enableCameraNode = *v;
        if (const auto v = ReadJsonBool(text, "EnableCreatureSpellHandRestriction")) g_config.enableSpellHandRestriction = *v;
        if (const auto v = ReadJsonFloat(text, "CameraNodeHeightZ")) g_config.cameraNodeHeightZ = *v;
        if (const auto v = ReadJsonString(text, "PlayableHumanoidSpellHand")) {
            if (const auto policy = ParseHandPolicy(*v)) {
                g_config.playableHumanoidSpellHand = *policy;
            } else {
                logger::warn("Invalid PlayableHumanoidSpellHand='{}'; using Both", *v);
            }
        }

        logger::info("Config loaded: RaceMenuCrashFix={} CameraNode={} SpellHandRestriction={} HumanoidSpellHand={}",
            g_config.enableRaceMenuCrashFix,
            g_config.enableCameraNode,
            g_config.enableSpellHandRestriction,
            PolicyName(g_config.playableHumanoidSpellHand));
'@
    $upc = Replace-Exact $upc $oldLoad $newLoad 'Config loading'

    $oldPolicy = @'
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
'@
    $newPolicy = @'
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
'@
    $upc = Replace-Exact $upc $oldPolicy $newPolicy 'Spell hand policy routing'

    $upc = Replace-Exact $upc '            RaceMenuFix::ApplyPlayableRaceConfig();' '            UPC::RaceCatalog::Load(g_config.enableRaceMenuCrashFix);' 'DataLoaded race catalog call'

    $oldLoader = @'
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageHandler)) {
        logger::critical("Failed to register SKSE messaging listener");
        return false;
    }

    logger::info("SKSE messaging registered; no save serialization is used");
    return true;
}
'@
    $newLoader = @'
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging || !messaging->RegisterListener(MessageHandler)) {
        logger::critical("Failed to register UPC SKSE messaging listener");
        return false;
    }

    if (!UCCCore::Initialize(skse)) {
        logger::critical("Universal Creature Controls core initialization failed");
        return false;
    }

    logger::info("Merged UPC/UCC messaging registered; no save serialization is used");
    return true;
}
'@
    $upc = Replace-Exact $upc $oldLoader $newLoader 'Merged loader'

    Set-Content -LiteralPath $upcPath -Value $upc -Encoding utf8NoBOM
} elseif (-not $upc.Contains('constexpr REL::Version kPluginVersion{ 0, 2, 0, 0 };')) {
    throw 'UPC source is neither expected v0.1.6 nor already-transformed v0.2.0'
}

# ---------------------------------------------------------------------------
# UCC: vendor the proven core as an internal translation unit.  It keeps its
# event-driven implementation but no longer owns SKSE exports or a separate log.
# Both cores read the UPC base JSON, so there is one configuration namespace.
# ---------------------------------------------------------------------------
$ucc = Get-Content -LiteralPath $uccSource -Raw
$ucc = Replace-Exact $ucc 'UniversalCreatureControls.json' 'UniversalPlayableCreatures.json' 'UCC config path'

# Anchor at EOF so nested braces inside SKSEPluginLoad cannot terminate the match.
$loaderPattern = '(?s)SKSEPluginLoad\(const SKSE::LoadInterface\* skse\)\s*\{\s*SKSE::Init\(skse\);\s*SetupLog\(\);\s*LoadConfig\(\);(?<body>.*?)\n\}\s*$'
$m = [regex]::Match($ucc, $loaderPattern)
if (-not $m.Success) {
    throw 'Merge transform failed: UCC SKSEPluginLoad block not found'
}

$body = $m.Groups['body'].Value
$replacement = @"
namespace UCCCore
{
    bool Initialize(const SKSE::LoadInterface* skse)
    {
        (void)skse;
        LoadConfig();$body
    }
}
"@
$ucc = [regex]::Replace($ucc, $loaderPattern, [System.Text.RegularExpressions.MatchEvaluator]{ param($match) $replacement }, 1)

if ($ucc.Contains('SKSEPluginLoad(')) {
    throw 'Merged UCC core still contains an SKSEPluginLoad export'
}

Set-Content -LiteralPath $uccOut -Value $ucc -Encoding utf8NoBOM

Write-Host 'Merged source generation complete.'
