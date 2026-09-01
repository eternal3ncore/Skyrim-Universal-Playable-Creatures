$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
Set-Location $ProjectRoot

$VcpkgRoot = "C:\vcpkg"
$Toolchain = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
if (-not (Test-Path $Toolchain)) {
    throw "vcpkg toolchain not found at $Toolchain"
}

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE="$Toolchain" -DVCPKG_TARGET_TRIPLET=x64-windows
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

cmake --build build --config Release
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$Dll = Get-ChildItem -Path build -Recurse -Filter UniversalPlayableCreatures.dll |
    Sort-Object LastWriteTime -Descending |
    Select-Object -First 1
if (-not $Dll) {
    throw "Build succeeded but UniversalPlayableCreatures.dll was not found"
}

$RuntimeDir = Join-Path $ProjectRoot "runtime\Data\SKSE\Plugins"
New-Item -ItemType Directory -Force -Path $RuntimeDir | Out-Null
Copy-Item $Dll.FullName -Destination (Join-Path $RuntimeDir "UniversalPlayableCreatures.dll") -Force

Write-Host "Built: $($Dll.FullName)"
Write-Host "Staged: $(Join-Path $RuntimeDir 'UniversalPlayableCreatures.dll')"
