#!/bin/sh
set -eu
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repo_dir"
cmake --preset macos-arm64
cmake --build --preset macos-arm64
ctest --preset macos-arm64
cmake --install build/macos-arm64 --prefix build/package/macos-arm64
