#!/bin/sh
set -eu
# 그래픽 의존성 없이 게임, 플랫폼, 오디오 디코더 테스트를 빌드하고 실행한다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
cmake --preset headless
cmake --build --preset headless
ctest --preset headless
