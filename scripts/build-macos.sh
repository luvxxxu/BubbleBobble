#!/bin/sh
set -eu
# 어느 작업 디렉터리에서 실행해도 프리셋과 설치 경로는 저장소 기준으로 해석한다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64
cmake --install build/macos-arm64 --prefix build/package/macos-arm64
