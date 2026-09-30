# Bubble Bobble C11 포트

[JulianRijken/BubbleBobble](https://github.com/JulianRijken/BubbleBobble)의 업스트림 커밋 `6a59ee99e621f4061cab50802155b9c28c51c4f9`를 바탕으로 만든 초기 플레이 가능 C 포트입니다.

실행 게임 소스는 C11입니다. 기존 C++20 게임과 Julgen 서브모듈은 그래픽 의존성이 없는 C 시뮬레이션 및 정적 raylib 5.5 데스크톱 프런트엔드로 대체했습니다. 원본 에셋 40개는 바이트 단위까지 변경하지 않았습니다. 업스트림 구현은 이 복제본의 Git 기록에서 확인할 수 있습니다.

이 포트는 상당한 초기 이식 결과물이며, 원작과 완전히 동일한 게임플레이 또는 완성된 프로덕션 릴리스라고 주장하지 않습니다. 검증 결과와 남은 작업은 [포트 상태](docs/PORT_STATUS.md)를 참고하세요.

## 구현한 기능

- `src/bb_levels.c`의 서로 다른 5라운드 맵. 라운드마다 공중 발판은 정확히 5개이며, 일반 점프로 모두 올라갈 수 있는 4타일 높이 간격을 사용
- 1인 플레이 전용. 메뉴에서 게임 시작 또는 순위표 선택
- 걷기, 점프, 일방 통과 발판, 세로 화면 순환, 거품 발사·포획·파열·탈출, ZenChan, Maita, Monsta의 서로 다른 행동, 바위, 적 드롭, 아이템, 점수, 생명, 부활, 스테이지 클리어, 게임 오버
- 시작 생명 5개, 기본 좌우 이동 속도 25% 감소, 자동 에너지 가속과 HUD
- 원본 시트의 적 3종과 음식 아이템 20종. 20종을 모두 한 번씩 뽑은 뒤 다시 섞는 드롭 순서를 라운드 사이에도 유지
- 원본 PNG 스프라이트, 8px NES 글꼴, WAV 효과음, 스트리밍 OGG 음악
- 픽셀 배율 렌더링, 창 크기 조절과 레터박싱, 일시 정지, 음소거, 전체 화면, 키보드와 게임패드 입력
- 현재 사용자의 애플리케이션 데이터 폴더에 저장하는 이니셜 및 상위 10개 순위표. 읽기 검증, 크기 제한 파싱, 임시 파일 교체를 적용
- Windows x86/x64와 macOS arm64용 C11 전용 CMake 프리셋, 그래픽 의존성 없는 회귀 테스트 및 sanitizer 빌드

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

생성된 `assets` 폴더는 실행 파일 옆에 유지해야 합니다. 에셋은 실행 파일 위치를 기준으로 찾으므로 다른 디렉터리에서 실행해도 됩니다. 자세한 빌드, 교차 컴파일, CI, 테스트는 [빌드 안내](docs/BUILD.md), macOS 앱 패키징은 [macOS 앱 패키징](docs/MACOS_APP.md)을 참고하세요.

## 조작법

| 동작 | 조작 |
|---|---|
| 이동 | A / D 또는 왼쪽 / 오른쪽 |
| 점프 | W / Space / X / 위쪽 |
| 발사 | E / Z / Slash / Right Ctrl |
| 게임패드 | 첫 번째 패드의 왼쪽 스틱/D-pad, 동쪽 버튼 점프, 남쪽 버튼 발사 |

- 메뉴: 위/아래 또는 W/S, Enter. 게임패드는 D-pad와 남쪽 버튼
- 일시 정지/재개: P 또는 게임패드 Start. 창 포커스를 잃으면 시뮬레이션을 멈춤
- 메뉴로 돌아가기: Escape. 메뉴에서 Escape를 한 번 더 누르면 종료
- M: 음소거/해제. F11: 전체 화면
- 이니셜: 위/아래로 글자 선택, 왼쪽/오른쪽으로 위치 선택, Enter로 다음 글자 및 저장. A–Z를 직접 입력할 수도 있음

## 라운드와 에너지

총 5라운드에 적이 각각 1·2·3·4·5마리 등장합니다. 1라운드는 ZenChan 한 마리, 2라운드는 바위를 던지는 Maita와 주기적으로 뛰는 Monsta가 등장하며, 3라운드부터 세 종류가 함께 등장합니다. 마지막 라운드를 끝내면 승리합니다. 시작 생명은 5개이고, 다섯 번 피격되면 탈락합니다.

좌우 이동은 지상 6타일/초, 공중 제어 시 3타일/초입니다. 점프 높이와 낙하 속도는 기존과 같습니다. 에너지는 새 게임에서 0으로 시작하며 다음 과정을 자동으로 반복합니다.

1. 가속하지 않는 동안 15초에 걸쳐 0%에서 100%로 충전합니다.
2. 100%가 되면 즉시 5초간 좌우 이동만 2배가 됩니다. HUD는 `X2`와 남은 시간을 표시하고 게이지는 100%에서 0%로 줄어듭니다.
3. 5초가 끝나면 공중에 남아 있는 수평 관성까지 1배로 돌아가고, 다시 15초 충전을 시작합니다.

이동하지 않아도 살아 있는 플레이 시간에 따라 충전되며, 조작 가능한 라운드 클리어 대기 중에도 진행됩니다. 메뉴·인트로·점수 화면·일시 정지·창 포커스 상실·사망 후 부활 대기 중에는 해당 시간이 진행되지 않습니다. 라운드 전환과 부활은 남아 있는 에너지를 유지하고, 새 게임은 초기화합니다.

아이템 20종은 수박, 감자튀김, 체리, 딸기, 복숭아, 오렌지, 포도, 바나나, 파인애플, 레몬, 사과, 배, 무, 옥수수, 당근, 가지, 아이스크림, 케이크, 도넛, 햄버거입니다. 기존 `Assets/Items.png`의 서로 다른 셀을 사용하며, 원본 아트 파일은 유지합니다. 새 게임마다 20종을 섞고 중복 없이 드롭하며, 5라운드의 적 15마리를 모두 처치하면 그중 15종이 등장합니다. 먹지 않고 지나치면 수집이 보장되지는 않습니다. 원본 3개 맵 이미지 `Assets/Levels.png`는 참고 자료로 보존하고 실제 플레이는 5개 발판 배치를 사용합니다.

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

`--validate-assets`에는 화면이나 오디오 장치가 필요하지 않습니다. 스모크 모드는 결정적인 1인 플레이 입력 시퀀스를 실행하고 요청한 렌더링 프레임 수 뒤 종료합니다. 조기 종료나 스크린샷 실패는 실패 코드로 반환됩니다. 스모크 모드는 점수를 기록하지 않습니다. 선택 사항인 `--assets DIR`와 `--score-file PATH`는 파일 위치를 바꿉니다. 경로와 명령줄 인수는 Windows를 포함해 UTF-8입니다.

기본 점수 파일은 macOS의 `~/Library/Application Support/BubbleBobble-C11/Scores.txt`와 Windows의 `%LOCALAPPDATA%/BubbleBobble-C11/Scores.txt`에 저장합니다. 업스트림 `Scores/Scores.txt`는 참고용으로 보존하며 자동으로 덮어쓰거나 가져오지 않습니다. 새 파일도 `score round initials` 행 형식을 사용하며 상위 10개를 저장합니다.

## 소스 구성

| 파일 | 역할 |
|---|---|
| `src/bb_levels.c`, `src/bb_levels.h` | 5라운드의 공중 발판·벽·바닥 배치 |
| `src/bb_game.c`, `src/bb_game.h` | 고정 시간 간격 게임 상태, 크기 제한 엔티티 풀, 충돌과 게임플레이. 플랫폼·그래픽 의존성 없음 |
| `src/bb_render.c`, `src/bb_render.h` | 에셋 검증, 원본 스프라이트 렌더링, HUD·메뉴·오디오 |
| `src/main.c` | 애플리케이션 수명 주기, 입력 에지 큐, 120Hz 단계 처리, 메뉴와 점수 입력 흐름 |
| `src/bb_platform.c`, `src/bb_platform.h` | 실행 파일·사용자 경로, Windows UTF-8 변환, 이식 가능한 파일 I/O와 점수 저장 |
| `tests/` | 게임플레이와 플랫폼 회귀 테스트 |
| `cmake/`, `scripts/`, `.github/workflows/` | 네이티브·교차 빌드, 패키징, 검증 |

애플리케이션에는 C++, STL, 클래스 체계, 템플릿 라이브러리, 예외 처리, C++ 런타임 링크가 없습니다. GLFW의 macOS 백엔드는 Cocoa 호출을 위해 Objective-C `.m` 파일을 사용하지만 Objective-C++나 C++는 사용하지 않습니다.

## 출처 및 라이선스

이 프로젝트는 업스트림의 [GPL-3.0-or-later 라이선스](LICENSE.md)를 유지하는 수정 파생물입니다. 원본 작성자와 에셋 출처는 업스트림 기록을 따르며 이 포트는 해당 에셋의 새 소유권을 주장하지 않습니다. Raylib과 번들 의존성의 고지는 [licenses](licenses/)에 남아 있습니다. raylib에 적용한 유일한 로컬 소스 패치는 루트 CMake 언어 선언을 C로 바꾸는 것이며, 의존성 구현은 변경하지 않았습니다.
