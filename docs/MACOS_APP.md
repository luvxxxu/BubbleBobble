# 로컬 Apple Silicon 애플리케이션 번들

Apple 명령줄 개발 도구와 CMake 3.24 이상이 설치된 macOS에서 실행합니다.

```sh
./scripts/package-macos.sh
open build/package/macos-arm64/BubbleBobble.app
```

스크립트는 `macos-arm64` 프리셋을 구성·빌드하고 CTest를 통과한 결과를 임시 스테이징 디렉터리에 설치한 뒤 `build/package/macos-arm64/BubbleBobble.app`을 만듭니다. 실행 파일이 arm64 코드인지 확인하고, 번들 안의 실행 파일로 에셋을 검사한 다음 번들 메타데이터를 검증합니다. 첫 빌드는 고정된 raylib 소스 압축 파일을 내려받으며 이후 빌드는 로컬 캐시를 재사용합니다.

선택 인수로 다른 출력 경로를 지정할 수 있습니다. 상대 경로는 스크립트를 실행한 디렉터리를 기준으로 해석합니다. CMake가 `PATH` 밖에 있으면 `CMAKE_BIN`을 설정하세요.

```sh
CMAKE_BIN=/Applications/CMake.app/Contents/bin/cmake \
  ./scripts/package-macos.sh /path/to/BubbleBobble.app
```

`.app` 전체를 함께 옮길 수 있습니다. 에셋은 실행 파일 상대 경로인 `Contents/MacOS/assets`에 있으므로 Finder에서 실행해도 현재 작업 디렉터리에 의존하지 않습니다. 업스트림과 raylib 라이선스 파일은 `Contents/Resources/licenses`에 포함됩니다. 순위표는 앱 외부의 `~/Library/Application Support/BubbleBobble-C11/Scores.txt`에 저장하므로 앱을 다시 빌드해도 유지됩니다. 진행 중인 라운드는 저장하지 않습니다.

이 결과물은 Apple Silicon의 macOS 11 이상을 대상으로 하는 로컬 개발자 번들입니다. 스크립트는 Developer ID 배포 서명을 적용하거나 번들을 공증하지 않으며 Gatekeeper 승인을 주장하지 않습니다. 기존 출력은 식별자가 `local.bubblebobble.c11`인 번들과 일치할 때만 교체합니다.

패키징 중 게임플레이와 플랫폼 테스트도 실행합니다. 테스트만 다시 실행하려면 다음 명령을 사용합니다.

```sh
ctest --preset macos-arm64
```
