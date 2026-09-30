param(
    [ValidateSet("x86", "x64")]
    [string]$Architecture = "x64"
)
$ErrorActionPreference = "Stop"
$repoDir = Split-Path -Parent $PSScriptRoot
# 프리셋과 상대 설치 경로를 저장소 기준으로 실행하고 호출자의 위치를 복원한다.
Push-Location $repoDir
try {
    $cmakeVersion = (& cmake -E capabilities | ConvertFrom-Json).version
    if ($LASTEXITCODE -ne 0) { throw "Cannot query CMake version" }
    if ($cmakeVersion.major -lt 4 -or ($cmakeVersion.major -eq 4 -and $cmakeVersion.minor -lt 2)) {
        throw "Visual Studio 2026 requires CMake 4.2 or newer; found $($cmakeVersion.string)"
    }
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
