$ErrorActionPreference = 'Stop'

$path = Join-Path (Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)) 'src\plugin.cpp'
if (-not (Test-Path $path)) { throw "Merged UPC source not found: $path" }
$text = Get-Content -LiteralPath $path -Raw

function Remove-Regex {
    param(
        [Parameter(Mandatory=$true)][string]$Text,
        [Parameter(Mandatory=$true)][string]$Pattern,
        [Parameter(Mandatory=$true)][string]$Label
    )
    $m = [regex]::Match($Text, $Pattern)
    if (-not $m.Success) { throw "Cleanup failed: expected block not found: $Label" }
    return [regex]::Replace($Text, $Pattern, '', 1)
}

# Old standalone race-catalog support is superseded by RaceCatalog v2.
$text = $text.Replace('    constexpr auto kRaceConfigDir = "Data/SKSE/Plugins/UniversalPlayableCreatures"sv;' + "`r`n", '')
$text = $text.Replace('    constexpr auto kRaceConfigDir = "Data/SKSE/Plugins/UniversalPlayableCreatures"sv;' + "`n", '')
$text = Remove-Regex $text '(?s)\n    std::vector<std::string> ReadJsonStringArray\(.*?(?=\n    std::optional<HandPolicy> ParseHandPolicy)' 'legacy JSON string-array parser'
$text = Remove-Regex $text '(?s)\n    bool IsConvertedTES4Race\(.*?(?=\n    void SetupLog\(\))' 'legacy TES4 origin classifier'
$text = Remove-Regex $text '(?s)\n        std::size_t ApplyPlayableRaceFile\(.*?(?=\n    \}\n\n    namespace CameraNode)' 'legacy playableRaces loader'

# UCC is the sole owner of weapon visibility in the merged DLL. Remove UPC's
# older clone-culling state and its separate ActionEvent lifecycle completely.
$text = Remove-Regex $text '(?s)\n        // The merged UCC core exclusively owns weapon visibility\..*?\n        bool enableHideSheathedWeapons\{ false \};\n' 'dormant UPC weapon-hide config member'
$text = Remove-Regex $text '(?s)\n    namespace WeaponHide\n    \{.*?(?=\n    class EventRouter final)' 'legacy UPC WeaponHide namespace'
$text = [regex]::Replace($text, 'public RE::BSTEventSink<RE::InputEvent\*>,\r?\n\s*public RE::BSTEventSink<SKSE::ActionEvent>', 'public RE::BSTEventSink<RE::InputEvent*>', 1)
$text = Remove-Regex $text '(?s)\n        RE::BSEventNotifyControl ProcessEvent\(const SKSE::ActionEvent\* event, RE::BSTEventSource<SKSE::ActionEvent>\*\) override\n        \{.*?\n        \}\n(?=    \};)' 'legacy UPC ActionEvent handler'
$text = [regex]::Replace($text, '(?m)^\s*WeaponHide::SyncFromEngineState\([^\n]*\);\r?\n', '')
$text = [regex]::Replace($text, '(?m)^\s*WeaponHide::g_hidden\s*=\s*false;\r?\n', '')
$text = Remove-Regex $text '(?s)\n        if \(g_config\.enableHideSheathedWeapons\) \{\n            if \(auto\* source = SKSE::GetActionEventSource\(\)\) \{.*?\n        \}\n(?=    \})' 'legacy UPC ActionEvent sink registration'

$forbidden = @(
    'namespace WeaponHide',
    'enableHideSheathedWeapons',
    'ReadJsonStringArray',
    'ApplyPlayableRaceFile',
    'ApplyPlayableRaceConfig',
    'IsConvertedTES4Race',
    'BSTEventSink<SKSE::ActionEvent>',
    'WeaponHide::'
)
foreach ($token in $forbidden) {
    if ($text.Contains($token)) { throw "Cleanup incomplete; obsolete token remains: $token" }
}

Set-Content -LiteralPath $path -Value $text -Encoding utf8NoBOM
Write-Host 'Removed redundant standalone UPC race-parser and weapon-hide implementations.'
