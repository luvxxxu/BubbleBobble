# game

한 명이 플레이하는 간단한 실시간 창 게임입니다. C11과 raylib으로 만들었으며 그림 파일과 소리는 사용하지 않습니다. 검은 배경의 창에 캐릭터, 적, 발판, 거품 공격을 기본 도형으로 그립니다. 각 스테이지의 적을 모두 물리치면 다음 스테이지로 넘어가며, 3스테이지를 마치면 게임을 클리어합니다.

## 게임 방법

게임 창에서 키를 누르면 즉시 움직입니다. 이동 키는 누르고 있는 동안 계속 적용됩니다.

| 키 | 동작 |
| --- | --- |
| A / 왼쪽 화살표 | 왼쪽 이동 |
| D / 오른쪽 화살표 | 오른쪽 이동 |
| W / Space / 위쪽 화살표 | 점프 |
| F / J | 바라보는 방향으로 거품 공격 |
| Enter | 게임 시작, 다음 스테이지 진행, 다시 시작 |
| Esc | 메뉴로 돌아가기 |

적과 부딪히면 생명을 하나 잃습니다. 생명은 세 개이며, 모두 잃으면 게임 오버입니다. 창을 닫으면 게임이 종료됩니다.

발판 배치는 스테이지마다 다릅니다.

- 1스테이지: 오른쪽으로 이동하며 발판을 올라갑니다.
- 2스테이지: 오른쪽 발판에 오른 뒤 왼쪽으로 올라갑니다.
- 3스테이지: 더 높고 좁은 발판을 따라 좌우로 이동합니다.

## 빌드와 실행

CMake 3.20 이상과 C/C++ 컴파일러가 필요합니다. 게임 소스는 C11이며, raylib 빌드 설정이 C++ 컴파일러도 확인합니다. 첫 빌드에는 인터넷 연결이 필요합니다. CMake가 버전이 고정된 raylib 5.5를 자동으로 내려받아 빌드합니다.

macOS (Xcode Command Line Tools 필요):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
open build/game.app
```

빌드 후 Finder에서 `build/game.app`을 더블 클릭해도 실행할 수 있습니다.

Windows (Visual Studio 2022의 C/C++ 데스크톱 개발 도구 필요):

```powershell
cmake -S . -B build
cmake --build build --config Release
.\build\Release\game.exe
```

Linux에서는 같은 방식으로 빌드한 뒤 `./build/game`을 실행합니다. raylib 빌드에 필요한 X11 개발 패키지가 설치되어 있어야 합니다.

게임 규칙 테스트는 창 없이 실행할 수 있습니다.

```sh
cmake -S . -B build/test -DBB_BUILD_GAME=OFF
cmake --build build/test
ctest --test-dir build/test --output-on-failure
```

이미 게임을 빌드했다면 다음 명령으로도 테스트할 수 있습니다.

```sh
ctest --test-dir build -C Release --output-on-failure
```

게임 상태와 규칙은 `src/bb_game.c`와 `src/bb_game.h`에, 창·키보드 입력·도형 그리기는 `src/main.c`에 있습니다. 게임 코드에서 이해해야 할 부분을 줄이기 위해 음악, 과일 보상, 멀티플레이, 4스테이지 이후는 넣지 않았습니다.

이 프로젝트는 [원본 저장소](https://github.com/JulianRijken/BubbleBobble)를 바탕으로 하며, [GPL-3.0-or-later 라이선스](LICENSE.md)를 따릅니다.
