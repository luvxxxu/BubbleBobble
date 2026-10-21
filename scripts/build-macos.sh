#!/bin/sh
# 하나라도 실패하면 이후 설치/실행을 중단하고, 오타로 비어 있는 변수를 사용하는 것도 막는다.
set -eu
# Apple 전용 프리셋이므로 다른 운영체제에서 실행한 경우 즉시 이유를 알린다.
[ "$(uname -s)" = Darwin ] || { printf '%s\n' 'macOS에서 실행해야 합니다.' >&2; exit 1; }
# 어느 작업 디렉터리에서 실행해도 프리셋과 설치 경로는 저장소 기준으로 해석한다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# 사용자가 CMAKE_BIN을 지정했다면 그 CMake를, 아니면 PATH의 cmake를 사용한다.
cmake_bin=${CMAKE_BIN:-cmake}
cmake_path=$(command -v "$cmake_bin") || { printf '%s\n' 'CMake를 설치하거나 CMAKE_BIN에 실행 파일 경로를 설정하세요.' >&2; exit 1; }
# CTest도 같은 설치 폴더에서 선택해 서로 다른 버전의 CMake 도구가 섞이지 않게 한다.
ctest_bin="$(dirname -- "$cmake_path")/ctest"
cd "$repo_dir"
# 구성 → 빌드 → 테스트를 통과해야 설치 단계를 진행한다.
"$cmake_path" --preset macos-arm64
"$cmake_path" --build --preset macos-arm64
"$ctest_bin" --preset macos-arm64
# 실행 파일과 에셋을 배포용 폴더로 복사한 뒤 설치 경로에서 다시 에셋을 디코딩한다.
# 이 검사는 이미지/오디오 파일의 존재와 형식을 확인하며 사용자 조작 테스트를 대신하지 않는다.
"$cmake_path" --install build/macos-arm64 --prefix build/package/macos-arm64
./build/package/macos-arm64/bin/BubbleBobble --validate-assets
