# 인수는 지원하는 x86/x64 중 하나만 받는다. 생략하면 64비트 Windows 빌드를 만든다.
param(
    [ValidateSet("x86", "x64")]
    [string]$Architecture = "x64"
)
# PowerShell 내부 오류는 예외로 처리한다. 외부 프로그램 종료 코드는 아래에서
# 별도로 확인해야 하므로 각 cmake/ctest/게임 실행 직후 LASTEXITCODE를 검사한다.
$ErrorActionPreference = "Stop"
$repoDir = Split-Path -Parent $PSScriptRoot
# 프리셋과 상대 설치 경로를 저장소 기준으로 실행하고 호출자의 위치를 복원한다.
Push-Location $repoDir
try {
    # Visual Studio 2026 생성기는 CMake 4.2 이상에서 지원하므로 시작 전에 확인한다.
    $cmakeVersion = (& cmake -E capabilities | ConvertFrom-Json).version
    if ($LASTEXITCODE -ne 0) { throw "Cannot query CMake version" }
    if ($cmakeVersion.major -lt 4 -or ($cmakeVersion.major -eq 4 -and $cmakeVersion.minor -lt 2)) {
        throw "Visual Studio 2026 requires CMake 4.2 or newer; found $($cmakeVersion.string)"
    }
    # 선택한 아키텍처를 프리셋 이름에 연결해 구성/빌드/테스트 대상이 일치하게 한다.
    $preset = "windows-$Architecture"
    # 구성 실패 시 이전 빌드 산출물로 다음 단계가 실행되지 않도록 즉시 중단한다.
    & cmake --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "CMake configuration failed" }
    & cmake --build --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Compilation failed" }
    # 게임 규칙, 맵, 파일 처리, 에셋과 소스 언어 검사를 모두 실행한다.
    & ctest --preset $preset
    if ($LASTEXITCODE -ne 0) { throw "Tests failed" }
    # Visual Studio는 다중 구성이므로 Release를 명시하고 별도의 패키지 폴더에 설치한다.
    & cmake --install "build/$preset" --config Release --prefix "build/package/$preset"
    if ($LASTEXITCODE -ne 0) { throw "Packaging failed" }
    # 설치된 EXE를 실행해 배포 폴더의 assets를 실제 디코딩할 수 있는지 확인한다.
    & "./build/package/$preset/bin/BubbleBobble.exe" --validate-assets
    if ($LASTEXITCODE -ne 0) { throw "Packaged assets validation failed" }
} finally {
    # 성공 여부와 무관하게 호출자의 원래 작업 폴더로 돌아간다.
    Pop-Location
}
