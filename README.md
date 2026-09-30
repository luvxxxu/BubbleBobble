# Bubble Bobble C11 포트 — 단일 레벨 테스트 브랜치

[JulianRijken/BubbleBobble](https://github.com/JulianRijken/BubbleBobble)의 업스트림 커밋 `6a59ee99e621f4061cab50802155b9c28c51c4f9`를 바탕으로 만든 C11 포트입니다. 이 브랜치는 플레이어의 이동, 점프, 거품과 에너지 동작을 시험하기 위한 단일 레벨 구성입니다. 적, 아이템, 점수, 생명, 레벨 전환 및 종료 판정은 없습니다. 메뉴에서 시작한 뒤 같은 레벨을 계속 플레이하며 Escape로 메뉴에 돌아갑니다.

기존 C++20 게임과 Julgen 서브모듈은 그래픽 의존성이 없는 C 시뮬레이션 및 정적 raylib 5.5 데스크톱 프런트엔드로 대체했습니다. 원본 아트 파일은 참고 자료로 보존합니다. 이 구성이 업스트림과 같은 게임플레이 또는 완성된 프로덕션 릴리스라는 뜻은 아닙니다. 검증 범위는 [포트 상태](docs/PORT_STATUS.md)를 참고하세요.

## 구현한 기능

- 공중 발판 다섯 개, 측면 벽과 바닥 순환 구멍을 가진 한 개의 플레이 레벨
- 1인 플레이, 걷기, 점프, 일방 통과 발판, 세로 화면 순환
- 거품 발사, 부유, 충돌 및 파열. 적 포획이나 보상 생성은 없음
- 15초 충전 뒤 5초간 좌우 이동 속도가 두 배가 되는 에너지와 HUD
- 픽셀 배율 렌더링, 창 크기 조절, 일시 정지, 음소거, 전체 화면, 키보드와 게임패드 입력
- Windows x86/x64와 macOS arm64용 C11 CMake 프리셋, 그래픽 의존성 없는 회귀 테스트 및 sanitizer 빌드

## 빌드 및 실행

CMake 3.24 이상과 C 컴파일러가 필요합니다. 첫 데스크톱 구성 시 SHA-256으로 고정한 raylib 압축 파일을 내려받습니다. 이 네이티브 C 프로젝트에는 Bun이나 JavaScript 패키지가 필요하지 않습니다.

Apple Silicon에서는 Apple Command Line Tools와 CMake를 설치한 뒤 실행하세요.

```sh
./scripts/build-macos.sh
./build/package/macos-arm64/bin/BubbleBobble
```

클릭하여 실행할 수 있는 서명되지 않은 로컬 macOS 앱을 만들려면 다음을 실행합니다.

```sh
./scripts/package-macos.sh
open build/package/macos-arm64/BubbleBobble.app
```

Windows에서는 C 컴파일러와 Windows SDK를 포함한 Visual Studio 2022 또는 Build Tools를 설치한 뒤 PowerShell에서 실행합니다.

```powershell
./scripts/build-windows.ps1 -Architecture x86
./scripts/build-windows.ps1 -Architecture x64
```

생성된 `assets` 폴더는 실행 파일 옆에 유지해야 합니다. 에셋은 실행 파일 위치를 기준으로 찾습니다. 자세한 빌드와 CI 정보는 [빌드 안내](docs/BUILD.md), macOS 앱 패키징은 [macOS 앱 패키징](docs/MACOS_APP.md)을 참고하세요.

## 조작법

| 동작 | 조작 |
|---|---|
| 이동 | A / D 또는 왼쪽 / 오른쪽 |
| 점프 | W / Space / X / 위쪽 |
| 거품 발사 | E / Z / Slash / Right Ctrl |
| 게임패드 | 첫 번째 패드의 왼쪽 스틱/D-pad, 동쪽 버튼 점프, 남쪽 버튼 발사 |

- 메뉴에서 Enter 또는 게임패드 남쪽 버튼으로 시작합니다.
- 인트로에서 Enter로 바로 시작할 수 있습니다.
- P 또는 게임패드 Start로 일시 정지하거나 재개합니다. 창 포커스를 잃으면 시뮬레이션을 멈춥니다.
- Escape로 메뉴에 돌아갑니다. 메뉴에서 다시 누르면 종료합니다.
- M으로 음소거를 전환하고 F11로 전체 화면을 전환합니다.

## 레벨과 에너지

레벨은 하나입니다. 레벨 안에서 이동과 점프, 거품 동작을 계속 시험할 수 있습니다. 적 처치에 따른 클리어와 다음 레벨은 발생하지 않습니다.

좌우 이동은 지상 6타일/초, 공중 제어 시 3타일/초입니다. 에너지는 새 게임에서 0으로 시작하며 다음 과정을 자동으로 반복합니다.

1. 15초에 걸쳐 0%에서 100%로 충전합니다.
2. 100%가 되면 5초 동안 좌우 이동 속도가 두 배가 됩니다. HUD는 `X2`와 남은 시간을 표시합니다.
3. 가속이 끝나면 속도가 원래대로 돌아가고 다시 충전을 시작합니다.

조작하지 않아도 플레이 중에는 에너지가 진행됩니다. 메뉴·인트로·일시 정지·창 포커스 상실 중에는 진행되지 않으며 새 게임을 시작하면 초기화합니다. `Assets/Levels.png`와 적·아이템 아트는 참고 자료로 보존하지만 이 브랜치의 게임 규칙에는 사용하지 않습니다.

## 검증 명령

```sh
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers

./build/macos-arm64/bin/BubbleBobble --validate-assets
./build/macos-arm64/bin/BubbleBobble --smoke-test 360 --mute --screenshot gameplay.png
```

`--validate-assets`에는 화면이나 오디오 장치가 필요하지 않습니다. 스모크 모드는 결정적인 1인 플레이 입력을 실행하고 요청한 렌더링 프레임 수 뒤 종료합니다. 조기 종료나 스크린샷 실패는 실패 코드로 반환됩니다. 선택 사항인 `--assets DIR`로 에셋 위치를 바꿀 수 있습니다. 경로와 명령줄 인수는 Windows를 포함해 UTF-8입니다.

## 소스 구성

| 파일 | 역할 |
|---|---|
| `src/bb_levels.c`, `src/bb_levels.h` | 단일 레벨의 발판·벽·바닥 배치 |
| `src/bb_game.c`, `src/bb_game.h` | 고정 시간 간격 게임 상태, 충돌, 플레이어와 거품 동작 |
| `src/bb_render.c`, `src/bb_render.h` | 에셋 검증, 스프라이트 렌더링, HUD·메뉴·오디오 |
| `src/main.c` | 애플리케이션 수명 주기, 입력 에지 큐와 120Hz 단계 처리 |
| `src/bb_platform.c`, `src/bb_platform.h` | 실행 파일 경로, Windows UTF-8 변환과 파일 열기 |
| `tests/` | 이동·거품·에너지·발판·플랫폼 회귀 테스트 |
| `cmake/`, `scripts/`, `.github/workflows/` | 네이티브·교차 빌드, 패키징, 검증 |

애플리케이션에는 C++, STL, 클래스 체계, 템플릿 라이브러리, 예외 처리 또는 C++ 런타임 링크가 없습니다. GLFW의 macOS 백엔드는 Cocoa 호출을 위해 Objective-C `.m` 파일을 사용합니다.

## 출처 및 라이선스

이 프로젝트는 업스트림의 [GPL-3.0-or-later 라이선스](LICENSE.md)를 유지하는 수정 파생물입니다. 원본 작성자와 에셋 출처는 업스트림 기록을 따릅니다. Raylib과 번들 의존성의 고지는 [licenses](licenses/)에 남아 있습니다.
