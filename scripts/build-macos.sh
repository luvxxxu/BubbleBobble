#!/bin/sh
set -eu
[ "$(uname -s)" = Darwin ] || { printf '%s\n' 'macOS에서 실행해야 합니다.' >&2; exit 1; }
# 어느 작업 디렉터리에서 실행해도 프리셋과 설치 경로는 저장소 기준으로 해석한다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake_bin=${CMAKE_BIN:-cmake}
cmake_path=$(command -v "$cmake_bin") || { printf '%s\n' 'CMake를 설치하거나 CMAKE_BIN에 실행 파일 경로를 설정하세요.' >&2; exit 1; }
ctest_bin="$(dirname -- "$cmake_path")/ctest"
cd "$repo_dir"
"$cmake_path" --preset macos-arm64
"$cmake_path" --build --preset macos-arm64
"$ctest_bin" --preset macos-arm64
"$cmake_path" --install build/macos-arm64 --prefix build/package/macos-arm64
./build/package/macos-arm64/bin/BubbleBobble --validate-assets
