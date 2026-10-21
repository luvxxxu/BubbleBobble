#!/bin/sh
# 명령 실패나 정의하지 않은 변수 사용 시 중단해 실패한 구성을 계속 사용하지 않는다.
set -eu
# 그래픽 의존성 없이 게임, 레벨, 플랫폼 테스트와 C 소스 검사를 실행한다.
# 스크립트 자신의 위치에서 저장소 루트를 계산한다. CDPATH 출력이 경로에 섞이지 않게 비운다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
# 먼저 빌드 파일을 생성한다. headless 프리셋은 raylib 다운로드와 그래픽 창을 요구하지 않는다.
cmake --preset headless
# 구성한 타깃을 컴파일한 뒤 테스트 프로그램이 실제 동작하는지 실행한다.
cmake --build --preset headless
ctest --preset headless
