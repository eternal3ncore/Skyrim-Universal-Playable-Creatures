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
exit $LASTEXITCODE
