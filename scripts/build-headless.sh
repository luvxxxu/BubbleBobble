#!/bin/sh
set -eu
# 그래픽 의존성 없이 게임, 레벨, 플랫폼 테스트와 C 소스 검사를 실행한다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
