param(
    [ValidateSet("x86", "x64")]
    [string]$Architecture = "x64"
)
$ErrorActionPreference = "Stop"
$repoDir = Split-Path -Parent $PSScriptRoot
Push-Location $repoDir
try {
    $preset = "windows-$Architecture"
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
    & cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed" }
    & ctest --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
    & cmake --install "build/$preset" --config Release --prefix "build/package/$preset"
    if ($LASTEXITCODE -ne 0) { throw "Packaging failed" }
} finally {
    Pop-Location
}
