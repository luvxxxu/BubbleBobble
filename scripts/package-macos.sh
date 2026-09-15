#!/bin/sh
# 실행 파일 상대 에셋을 포함한 로컬 Apple Silicon 애플리케이션 번들을 빌드한다.
set -eu

fail() { printf '%s\n' "package-macos: $*" >&2; exit 1; }

[ "$(uname -s)" = Darwin ] || fail '이 스크립트에는 macOS와 Apple 명령줄 개발 도구가 필요합니다.'
[ "$#" -le 1 ] || fail '사용법: scripts/package-macos.sh [output/BubbleBobble.app]'
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cmake_bin=${CMAKE_BIN:-cmake}
command -v "$cmake_bin" >/dev/null 2>&1 || fail 'CMake를 찾을 수 없습니다. CMake 3.24 이상을 설치하거나 CMAKE_BIN에 실행 파일을 설정하세요.'

bundle_path=${1:-"$repo_dir/build/package/macos-arm64/BubbleBobble.app"}
case "$bundle_path" in
    /*) ;;
    *) bundle_path="$PWD/$bundle_path" ;;
esac
case "$bundle_path" in
    *.app) ;;
    *) fail '출력 경로는 .app으로 끝나야 합니다.' ;;
esac
if [ -e "$bundle_path" ]; then
    existing_id=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$bundle_path/Contents/Info.plist" 2>/dev/null || true)
    [ "$existing_id" = local.bubblebobble.c11 ] || fail '이 애플리케이션 번들이 아닌 기존 디렉터리는 교체하지 않습니다.'
fi

cd "$repo_dir"
"$cmake_bin" --preset macos-arm64
"$cmake_bin" --build --preset macos-arm64

bundle_parent=$(dirname -- "$bundle_path")
mkdir -p "$bundle_parent"
stage_dir=$(mktemp -d "$bundle_parent/.bubblebobble-package.XXXXXX")
trap 'rm -rf "$stage_dir"' EXIT HUP INT TERM
"$cmake_bin" --install build/macos-arm64 --prefix "$stage_dir/install"
executable="$stage_dir/install/bin/BubbleBobble"
[ -x "$executable" ] || fail "설치한 실행 파일이 없습니다: $executable"
/usr/bin/lipo -verify_arch arm64 "$executable" || fail '설치한 실행 파일에 Apple Silicon 코드가 없습니다.'
[ -f "$stage_dir/install/bin/assets/Levels.png" ] || fail '설치한 게임 에셋이 없습니다.'

staged_bundle="$stage_dir/BubbleBobble.app"
mkdir -p "$staged_bundle/Contents/MacOS" "$staged_bundle/Contents/Resources/licenses"
/usr/bin/ditto "$executable" "$staged_bundle/Contents/MacOS/BubbleBobble"
/usr/bin/ditto "$stage_dir/install/bin/assets" "$staged_bundle/Contents/MacOS/assets"
cp LICENSE.md "$staged_bundle/Contents/Resources/licenses/UPSTREAM-LICENSE.md"
raylib_license="$repo_dir/build/macos-arm64/_deps/raylib-src/LICENSE"
if [ -f "$raylib_license" ]; then
    cp "$raylib_license" "$staged_bundle/Contents/Resources/licenses/RAYLIB-LICENSE.txt"
else
    printf '%s\n' 'package-macos: 예상한 소스 캐시에서 raylib 라이선스를 찾지 못했습니다.' >&2
fi

cat > "$staged_bundle/Contents/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>Bubble Bobble C11</string>
  <key>CFBundleExecutable</key><string>BubbleBobble</string>
  <key>CFBundleIdentifier</key><string>local.bubblebobble.c11</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>BubbleBobble</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleVersion</key><string>1</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSArchitecturePriority</key><array><string>arm64</string></array>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
printf 'APPL????' > "$staged_bundle/Contents/PkgInfo"
/usr/bin/plutil -lint "$staged_bundle/Contents/Info.plist"

# 위에서 검사한 것처럼 식별자가 일치하는 번들만 교체할 수 있다.
if [ -e "$bundle_path" ]; then
    mv "$bundle_path" "$stage_dir/previous.app"
fi
if ! mv "$staged_bundle" "$bundle_path"; then
    if [ -d "$stage_dir/previous.app" ]; then
        mv "$stage_dir/previous.app" "$bundle_path"
    fi
    fail '완성한 번들을 대상 위치로 옮기지 못했습니다.'
fi
printf '서명되지 않은 로컬 앱을 만들었습니다: %s\n' "$bundle_path"
