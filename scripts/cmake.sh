#!/bin/sh
# 시스템 또는 작업공간에서 사용할 수 있는 CMake를 찾아 전달받은 인수로 실행한다.
# 실행 실패와 미정의 변수 사용을 감지한다. 선택 인수에는 아래에서 기본값을 제공한다.
set -eu

# 스크립트 위치를 기준으로 로컬 도구 경로를 계산하므로 호출자의 현재 폴더에 의존하지 않는다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

# 명시적으로 지정한 CMake가 최우선이다. 잘못된 경로라면 다른 버전을 조용히 선택하지 않는다.
if [ -n "${CMAKE_BIN:-}" ]; then
    [ -x "$CMAKE_BIN" ] || {
        printf '%s\n' "CMAKE_BIN이 실행 가능한 CMake 파일을 가리키지 않습니다: $CMAKE_BIN" >&2
        exit 127
    }
    # exec로 현재 셸을 CMake로 바꾸고 "$@"로 인수 경계와 공백을 그대로 전달한다.
    exec "$CMAKE_BIN" "$@"
fi

# 명시적 설정이 없으면 시스템 PATH에서 찾는다. 찾지 못한 진단은 마지막에 한 번만 출력한다.
if cmake_bin=$(command -v cmake 2>/dev/null); then
    exec "$cmake_bin" "$@"
fi

# 이 작업공간에서 검증에 사용한 로컬 CMake 보관 위치도 지원한다.
workspace_cmake="$repo_dir/../../work/tools/cmake-3.31.6-macos-universal/CMake.app/Contents/bin/cmake"
if [ -x "$workspace_cmake" ]; then
    exec "$workspace_cmake" "$@"
fi

# 세 경로 모두 사용할 수 없으면 명령을 찾지 못했다는 관례적인 종료 코드 127을 반환한다.
printf '%s\n' 'CMake 3.24 이상을 찾지 못했습니다. CMake를 설치해 PATH에 추가하거나 CMAKE_BIN에 실행 파일 경로를 설정하세요.' >&2
exit 127
