# Bubble Bobble C11 / Raylib

[JulianRijken/BubbleBobble](https://github.com/JulianRijken/BubbleBobble)의 커밋 `6a59ee99e621f4061cab50802155b9c28c51c4f9`를 바탕으로 만든 C11 포트입니다. Raylib 5.5로 화면, 키보드·게임패드 입력과 소리를 처리합니다. 현재는 1인 또는 2인이 함께 몬스터를 거품에 가두고 터뜨려 다섯 라운드를 진행하는 게임입니다. [참고 저장소 main](https://github.com/luvxxxu/BubbleBobble/tree/5ea63987d393d9f3c811a5b129f453d08e599bd0)의 게임 규칙을 현재의 단순한 C 함수와 배열 구조로 복원했습니다.

## 플레이

- 메뉴에서 위/아래로 1인, 2인 또는 순위표를 고르고 Enter로 선택합니다. 숫자 1/2로 인원을 바로 선택할 수도 있습니다.
- 1P는 초록 캐릭터, 2P는 파랑 캐릭터입니다. 같은 키보드 또는 게임패드 두 개로 함께 플레이할 수 있습니다.
- 서로 다른 다섯 라운드에 각각 공중 발판 다섯 개, 측면 벽과 바닥 순환 통로가 있습니다.
- 각 플레이어에게 별도의 마나, 점수, 생명 5개, 발사 대기 시간과 이동 상태가 있습니다.

| 동작 | 1P | 2P |
|---|---|---|
| 이동 | A / D | 왼쪽 / 오른쪽 방향키 |
| 점프 | W / Space | 위쪽 방향키 |
| 거품 발사 | E / Z | **Right Shift** |
| 게임패드 | 첫 번째 패드 | 두 번째 패드 |

게임패드는 왼쪽 스틱 또는 D-pad로 이동하고, 동쪽 버튼 또는 D-pad 위로 점프하며, 남쪽 버튼으로 거품을 발사합니다. 1인 모드에서는 방향키와 X, Slash, Right Ctrl도 1P를 조작합니다. 2인 모드의 Slash와 Right Ctrl은 발사에 사용하지 않습니다. 두 캐릭터는 서로를 밀지 않으며, 누구나 떠 있는 거품에 올라타거나 빠르게 부딪쳐 터뜨릴 수 있습니다.

- 인트로에서 Enter로 바로 시작합니다.
- P 또는 게임패드 Start로 일시 정지하거나 재개합니다. 창 포커스를 잃어도 게임 시간이 멈춥니다.
- Escape로 메뉴에 돌아갑니다. 메뉴에서 다시 누르면 종료합니다.
- M은 음소거, F11은 전체 화면 전환입니다.

## 몬스터와 라운드

총 다섯 라운드에 몬스터가 1·2·3·4·5마리씩 등장합니다.

- **ZenChan:** 플레이어를 향해 걷고 발판으로 점프합니다. 거품에서 탈출하면 더 빨라집니다.
- **Maita:** 플레이어를 따라 움직이며 바위를 던집니다. 바위도 피해야 합니다.
- **Monsta:** 주기적으로 도약하고 공중에서도 전진합니다.

거품에 닿은 몬스터는 포획됩니다. 시간이 지나 거품이 저절로 터지면 몬스터가 탈출하므로, 플레이어가 빠르게 부딪쳐 터뜨려 처치해야 합니다. 쓰러진 몬스터는 튀어 오른 뒤 음식으로 바뀝니다. 음식은 먼저 먹은 플레이어 한 명에게만 점수를 줍니다. 20종 음식의 드롭 순서를 섞어 한 묶음 안에서 중복되지 않게 하며, 순서는 다음 라운드에도 이어집니다.

몬스터나 바위에 맞으면 생명이 하나 줄어듭니다. 1.5초 뒤 자기 시작 위치에서 부활하고 3초 동안 무적입니다. 생명이 0이면 탈락하며, 2인 모드에서는 남은 플레이어가 계속할 수 있습니다. 모두 탈락하면 게임 오버입니다. 포획 중인 적도 남은 적으로 세며, 모두 처치하면 7초 동안 음식을 더 모은 뒤 다음 라운드로 넘어갑니다. 마지막 라운드까지 마치면 승리합니다.

## 순위표

승리 또는 게임 오버 뒤 참여한 플레이어별로 이니셜 세 글자를 입력합니다. 위/아래는 글자 선택, 왼쪽/오른쪽은 위치 선택, Enter는 다음 글자 또는 저장입니다. A–Z를 직접 입력할 수도 있습니다. 점수가 0이거나 먼저 탈락했더라도 각 플레이어의 기록을 따로 입력할 수 있습니다. Escape는 아직 확정하지 않은 입력을 취소하고 메뉴로 돌아갑니다.

상위 10개 기록은 macOS의 `~/Library/Application Support/BubbleBobble-C11/Scores.txt`, Windows의 `%LOCALAPPDATA%\BubbleBobble-C11\Scores.txt`에 저장합니다. 메뉴의 순위표에서 다시 확인할 수 있으며 저장 실패 시 화면에 표시하고 재시도할 수 있습니다. `--score-file PATH`로 다른 저장 파일을 지정할 수 있습니다. 스모크 테스트는 이 옵션을 지정해도 순위표를 저장하지 않습니다.

## 플레이어별 마나

마나는 새 게임에서 두 플레이어 모두 0으로 시작합니다. 플레이 중 각자 다음 주기를 자동으로 반복합니다.

1. 15초 동안 0%에서 100%까지 충전합니다.
2. 가득 차면 5초 동안 좌우 이동 속도가 두 배가 됩니다. `X2`와 남은 시간이 표시됩니다.
3. 마나가 0이 되면 원래 속도로 돌아가고 다시 충전합니다.

점프 높이와 낙하 속도는 바뀌지 않습니다. 기본 좌우 속도는 지상 6타일/초, 공중 제어 시 3타일/초입니다. 메뉴·인트로·결과 화면·사망 대기·탈락·일시 정지·창 포커스 상실 중에는 마나가 진행되지 않습니다. 부활이나 다음 라운드에서는 남은 마나를 유지하며, 새 게임에서만 초기화합니다. 1인 모드에서는 2P의 입력과 마나를 갱신하지 않습니다. 두 플레이어가 동시에 시작하므로 보통 게이지는 같은 비율로 보이지만, 저장 값과 계산은 각각 독립적입니다.

## 빌드 및 실행

네이티브 C 프로젝트이므로 Bun이나 JavaScript 패키지는 필요하지 않습니다. 첫 데스크톱 구성에서 SHA-256으로 고정한 Raylib 5.5 소스를 받습니다.

### Windows x64 / Visual Studio 2026

Visual Studio 2026 또는 Build Tools의 **Desktop development with C++** 워크로드, Windows SDK, CMake 4.2 이상이 필요합니다. 워크로드 이름과 관계없이 게임 소스는 C11로 컴파일합니다. 저장소 루트에서 PowerShell로 실행하세요.

```powershell
./scripts/build-windows.ps1 -Architecture x64
./build/package/windows-x64/bin/BubbleBobble.exe
```

Visual Studio에서는 저장소 폴더를 열고 `windows-x64` 또는 `windows-debug-x64` CMake 프리셋을 선택합니다. 실행 파일 옆 `assets` 폴더도 함께 유지해야 합니다. x86 프리셋도 남아 있습니다.

### macOS ARM / Apple Silicon

Apple Command Line Tools와 CMake 3.24 이상이 필요합니다.

```sh
./scripts/build-macos.sh
./build/package/macos-arm64/bin/BubbleBobble
```

Finder에서 실행할 로컬 앱은 다음 명령으로 만듭니다.

```sh
./scripts/package-macos.sh
open build/package/macos-arm64/BubbleBobble.app
```

빌드 스크립트는 컴파일, 회귀 테스트와 설치된 에셋 검증을 실행합니다. 자세한 설정은 [빌드 안내](docs/BUILD.md), 앱 구성은 [macOS 앱 패키징](docs/MACOS_APP.md), 실제 확인 범위는 [포트 상태](docs/PORT_STATUS.md)에 있습니다.

## 코드를 읽는 순서

게임 로직은 고정 크기 배열, 간단한 구조체, 일반 함수와 `if`·`for` 중심으로 작성했습니다. 함수 포인터를 통한 충돌 교체, 이벤트 비트마스크, 별도 충돌 호출 순서는 사용하지 않습니다.

1. `src/bb_game.h`: `players[2]`, `bubbles[64]`, 위치·속도·마나 등 저장할 값을 정의합니다.
2. `src/bb_levels.c`: 다섯 라운드의 발판 좌표를 배열에 두고 반복문으로 벽과 발판을 배치합니다.
3. `src/bb_game.c`: `bb_game_start`에서 초기화하고 `bb_game_update`가 플레이어 → 적·거품·바위·아이템 → 접촉 판정 → 라운드 전환 순서로 처리합니다. `update_mana`는 전달받은 플레이어 한 명의 마나만 계산합니다.
4. `src/main.c`: 각 플레이어의 입력을 배열에 모아 갱신하고 화면을 그립니다. 렌더링 속도와 관계없이 1/120초 간격으로 게임을 갱신합니다.
5. `src/bb_render.c`: 캐릭터·몬스터·아이템 스프라이트, 각자의 마나·점수·생명, 메뉴·순위표와 소리를 표시합니다.

`src/bb_platform.c`는 Windows UTF-8 경로 등 운영체제 차이와 순위표 파일의 읽기·저장을 처리합니다. 입력 범위 검사, 에셋 로딩 실패 처리와 자원 해제는 유지합니다. 게임 로직에서 동적 메모리 할당은 하지 않습니다. macOS의 Raylib 창 백엔드는 Cocoa를 위해 Objective-C를 사용하지만 게임 소스와 공개 인터페이스는 C입니다.

## 검증 명령

```sh
./scripts/build-headless.sh
cmake --preset sanitizers
cmake --build --preset sanitizers
ctest --preset sanitizers

./build/macos-arm64/bin/BubbleBobble --validate-assets
./build/macos-arm64/bin/BubbleBobble --smoke-test 1200 --players 2 --mute --screenshot build/two-player.png
```

`--players 1` 또는 `--players 2`는 메뉴의 초기 선택과 스모크 실행 인원을 지정합니다. 스모크 실행은 각 플레이어에게 다른 이동·점프·발사 입력을 주고 요청한 프레임 수 뒤 종료합니다. 실행 결과에 인원과 각 플레이어의 위치·마나를 출력합니다. 조기 종료나 스크린샷 저장 실패는 실패 코드로 반환합니다.

`--validate-assets`는 화면이나 오디오 장치 없이 캐릭터·몬스터·아이템 이미지와 소리 파일을 검증합니다. `--assets DIR`로 에셋 위치를 지정할 수 있으며 Windows에서도 경로와 명령줄 인수는 UTF-8로 처리합니다.

## 출처 및 라이선스

업스트림의 [GPL-3.0-or-later 라이선스](LICENSE.md)를 유지하는 수정 파생물입니다. 원본 에셋은 보존하며 Raylib과 번들 의존성의 고지는 [licenses](licenses/)에 있습니다. 업스트림 전체 기능과의 동일성을 주장하지 않습니다.
