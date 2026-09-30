param(
    [ValidateSet("x86", "x64")]
    [string]$Architecture = "x64"
)
$ErrorActionPreference = "Stop"
$repoDir = Split-Path -Parent $PSScriptRoot
# 프리셋과 상대 설치 경로를 저장소 기준으로 실행하고 호출자의 위치를 복원한다.
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
