# C 포트 빌드

애플리케이션과 게임플레이 코어에는 C11 컴파일러가 필요합니다. C++ 컴파일러, C++ 게임 소스, STL, C++ 런타임은 필요하지 않습니다. CMake는 해시로 고정한 raylib 5.5 소스 압축 파일을 빌드 트리에 내려받습니다. 번들 GLFW, 이미지·글꼴 디코더, miniaudio가 나머지 의존성을 제공합니다. GLFW는 macOS에서 네이티브 Objective-C Cocoa 인터페이스를 사용합니다. 게임과 플랫폼 소스는 C입니다.

필수 조건은 CMake 3.24 이상과 플랫폼 컴파일러입니다. 첫 데스크톱 구성에는 GitHub에 HTTPS로 접근할 수 있어야 합니다. 이후 빌드는 내려받은 의존성을 재사용합니다. `BB_BUILD_GAME=OFF`는 내려받은 의존성, 윈도 서버, 오디오 장치 없이 실행할 수 있습니다.

## macOS arm64

Apple Command Line Tools(`xcode-select --install`)와 CMake를 설치한 뒤 실행합니다.

```sh
./scripts/build-macos.sh
./build/package/macos-arm64/bin/BubbleBobble
```

`macos-arm64` 프리셋은 macOS 11.0 이상을 대상으로 하고 arm64 코드를 명시적으로 생성합니다. 테스트 실행에는 Apple Silicon 장비가 필요합니다. `native`는 호스트 아키텍처용으로 빌드합니다. 스크립트는 실행 파일 옆에 `assets` 폴더를 둔 재배치 가능한 결과물을 설치합니다. 에셋 검색은 현재 작업 디렉터리가 아니라 실행 파일 위치를 따르므로 해당 폴더를 실행 파일과 함께 유지해야 합니다.

## Windows x86 및 x86_64 / AMD64

C 컴파일러와 Windows SDK를 제공하는 **Desktop development with C++** 워크로드를 포함해 Visual Studio 2022 또는 Build Tools를 설치하고, CMake 3.24 이상을 설치합니다. 이 워크로드 이름과 달리 C++ 소스는 컴파일하지 않습니다. 저장소에서 PowerShell을 열어 실행합니다.

```powershell
./scripts/build-windows.ps1 -Architecture x86
./scripts/build-windows.ps1 -Architecture x64
```

기본 설치 결과물은 서명된 설치 관리자가 아니라 명령줄 실행 파일(Windows의 콘솔 `.exe`)입니다. `scripts/package-macos.sh`는 별도로 서명되지 않은 macOS `.app`을 만듭니다. 자세한 내용은 [macOS 앱 패키징](MACOS_APP.md)을 참고하세요.

패키지는 `build/package/windows-x86/bin` 및 `build/package/windows-x64/bin`에 생성됩니다. 각 폴더에는 `BubbleBobble.exe`와 `assets`가 들어 있습니다. 프리셋은 `Win32`와 `x64`를 명시적으로 선택합니다. 애플리케이션은 플랫폼 C 런타임과 시스템 그래픽·오디오 라이브러리를 사용하며 raylib는 정적으로 링크합니다.

## macOS 또는 Linux에서 Windows 교차 컴파일

빌드 호스트에 맞는 [LLVM-MinGW 도구 모음](https://github.com/mstorsjo/llvm-mingw/releases)을 압축 해제해 사용합니다. 도구 모음 디렉터리는 이 저장소 밖에 둡니다. 제공하는 파일은 두 대상 아키텍처를 모두 지원합니다.

```sh
export BB_LLVM_MINGW_ROOT=/absolute/path/to/extracted/llvm-mingw
cmake --preset windows-cross-x86
cmake --build --preset windows-cross-x86
cmake --install build/windows-cross-x86 --prefix build/package/windows-cross-x86
cmake --preset windows-cross-x64
cmake --build --preset windows-cross-x64
cmake --install build/windows-cross-x64 --prefix build/package/windows-cross-x64
```

교차 빌드는 게임플레이·플랫폼 테스트 실행 파일과 게임을 모두 컴파일하지만 빌드 호스트에서 Windows 바이너리를 실행하지는 않습니다. Windows에서 테스트(`bin/bb_game_tests.exe`, `bin/bb_platform_tests.exe platform-test-scores.txt`)를 실행하거나 Windows CI 작업을 사용하세요. 교차 링크 성공만으로는 Windows 그래픽, 오디오, 입력 동작을 검증할 수 없습니다.

## 그래픽 의존성 없는 게임플레이 테스트

```sh
./scripts/build-headless.sh
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers
```

sanitizer 프리셋에는 macOS/Linux의 Clang 또는 GCC가 필요합니다. 의존성 없는 MSVC 테스트에는 `cmake --preset windows-x64 -DBB_BUILD_GAME=OFF` 뒤 해당 빌드·테스트 프리셋을 사용하세요. Release 구성에서도 assertion을 유지합니다. 애플리케이션·코어·테스트 C 코드는 기본적으로 경고를 오류로 처리하며, 의존성 경고는 오류로 승격하지 않습니다.

## 빌드 범위와 재현성

- `bb_core`는 그래픽에 의존하지 않는 C11 정적 라이브러리이며 `bb_game_tests`가 CTest로 검사합니다. `bb_platform_tests`는 빌드 디렉터리 안의 전용 임시 점수 파일을 사용해 점수 저장과 경로 처리를 검사합니다.
- `BubbleBobble`은 `main.c`, `bb_render.c`, `bb_platform.c`, `bb_core`로 구성됩니다. 플랫폼 통합은 `bb_platform.h` 뒤에 둡니다.
- `cmake/PureCRaylib.cmake`는 내려받은 raylib 루트의 한정되지 않은 `project(raylib)`를 `project(raylib LANGUAGES C)`로 바꿉니다. 이는 CMake가 불필요하게 C++ 컴파일러를 탐색하지 않게 할 뿐 의존성 구현은 바꾸지 않습니다. 패치는 변경 전 기대한 소스인지 검증합니다.
- 의존성이 C++ 또는 Objective-C++를 활성화하면 구성이 실패합니다. CTest도 `src/`와 `tests/`에서 C++ 소스 확장자를 거부합니다.
- Raylib 5.5 압축 파일 SHA-256: `aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e`
- `.github/workflows/build.yml`은 Windows x86/x64 및 macOS arm64에서 게임플레이 테스트를 빌드·실행하고, GCC/Clang sanitizer로 이식 가능한 코어를 검사합니다. 이 작업은 저장소를 GitHub에 푸시한 뒤 실행하며 워크플로를 추가했다고 원격 실행이 완료된 것은 아닙니다.
- 사용하지 않는 Julgen/Emscripten 빌드 스크립트는 제거했습니다. 웹 빌드는 요청한 대상 범위 밖입니다.

커밋 `2e05bd722b7444b40dd6b02985f666eeb6b091ce`의 기존 Julgen 의존성은 SDL2/SDL_image/SDL_ttf와 함께 C++20, GLM, fmt, Box2D, SoLoud, Dear ImGui를 사용했습니다. 현재 C 빌드는 어느 것도 사용하지 않습니다. 선택 사항인 Steamworks와 Visual Leak Detector 통합도 제거했습니다.
