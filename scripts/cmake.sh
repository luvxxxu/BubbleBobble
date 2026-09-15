#!/bin/sh
# 시스템 또는 작업공간에서 사용할 수 있는 CMake를 찾아 전달받은 인수로 실행한다.
set -eu

repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

if [ -n "${CMAKE_BIN:-}" ]; then
    [ -x "$CMAKE_BIN" ] || {
        printf '%s\n' "CMAKE_BIN이 실행 가능한 CMake 파일을 가리키지 않습니다: $CMAKE_BIN" >&2
        exit 127
    }
    exec "$CMAKE_BIN" "$@"
fi

if cmake_bin=$(command -v cmake 2>/dev/null); then
    exec "$cmake_bin" "$@"
fi

# 이 작업공간에서 검증에 사용한 로컬 CMake 보관 위치도 지원한다.
workspace_cmake="$repo_dir/../../work/tools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake"
if [ -x "$workspace_cmake" ]; then
    exec "$workspace_cmake" "$@"
fi

printf '%s\n' 'CMake 3.24 이상을 찾지 못했습니다. CMake를 설치해 PATH에 추가하거나 CMAKE_BIN에 실행 파일 경로를 설정하세요.' >&2
exit 127
