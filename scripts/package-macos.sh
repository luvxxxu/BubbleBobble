#!/bin/sh
# 실행 파일 상대 에셋을 포함한 로컬 Apple Silicon 애플리케이션 번들을 빌드한다.
# 실패한 빌드나 잘못된 경로를 사용해 패키징을 계속하지 않도록 즉시 중단한다.
set -eu

# 모든 명시적 실패는 같은 접두어를 붙여 표준 오류로 알리고 성공 코드로 끝나지 않게 한다.
fail() { printf '%s\n' "package-macos: $*" >&2; exit 1; }

# Apple 도구를 요구하는 작업이므로 운영체제와 선택 인수 개수를 먼저 검사한다.
[ "$(uname -s)" = Darwin ] || fail '이 스크립트에는 macOS와 Apple 명령줄 개발 도구가 필요합니다.'
[ "$#" -le 1 ] || fail '사용법: scripts/package-macos.sh [output/BubbleBobble.app]'
# 저장소는 스크립트 위치에서 찾고, 출력 상대 경로는 호출자의 현재 폴더를 기준으로 해석한다.
repo_dir=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# CMAKE_BIN으로 지정한 도구를 우선하고, CTest도 같은 설치 폴더의 버전을 고른다.
cmake_bin=${CMAKE_BIN:-cmake}
command -v "$cmake_bin" >/dev/null 2>&1 || fail 'CMake를 찾을 수 없습니다. CMake 3.24 이상을 설치하거나 CMAKE_BIN에 실행 파일을 설정하세요.'
ctest_bin="$(dirname -- "$(command -v "$cmake_bin")")/ctest"

# 출력 인수를 생략하면 저장소의 패키지 폴더에 번들을 만든다.
# 작업 폴더를 바꾸기 전에 절대 경로로 고정해 이후 cd가 출력 위치를 바꾸지 않게 한다.
bundle_path=${1:-"$repo_dir/build/package/macos-arm64/BubbleBobble.app"}
case "$bundle_path" in
    /*) ;;
    *) bundle_path="$PWD/$bundle_path" ;;
esac
# Finder가 애플리케이션으로 해석하는 .app 접미사를 요구한다.
case "$bundle_path" in
    *.app) ;;
    *) fail '출력 경로는 .app으로 끝나야 합니다.' ;;
esac
# 기존 경로는 이 스크립트가 만든 번들임을 식별자로 확인한 뒤에만 교체한다.
if [ -e "$bundle_path" ]; then
    existing_id=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$bundle_path/Contents/Info.plist" 2>/dev/null || true)
    [ "$existing_id" = local.bubblebobble.c11 ] || fail '이 애플리케이션 번들이 아닌 기존 디렉터리는 교체하지 않습니다.'
fi

# 새 번들을 만들기 전에 현재 소스를 구성·컴파일하고 모든 macOS CTest를 통과시킨다.
cd "$repo_dir"
"$cmake_bin" --preset macos-arm64
"$cmake_bin" --build --preset macos-arm64
"$ctest_bin" --preset macos-arm64

# 출력 부모가 없으면 준비하되 기존 번들 내용은 최종 교체 직전까지 그대로 둔다.
bundle_parent=$(dirname -- "$bundle_path")
mkdir -p "$bundle_parent"
# 완성 전 결과물이 대상 경로에 노출되지 않도록 같은 부모 디렉터리에 준비한다.
stage_dir=$(mktemp -d "$bundle_parent/.bubblebobble-package.XXXXXX")
# 정상 종료와 중단 신호에서도 이번 실행이 만든 임시 작업 폴더를 정리한다.
trap 'rm -rf "$stage_dir"' EXIT HUP INT TERM
# CMake의 설치 규칙으로 실행 파일·에셋을 모은 뒤 실행 가능 여부와 arm64 포함 여부를 확인한다.
"$cmake_bin" --install build/macos-arm64 --prefix "$stage_dir/install"
executable="$stage_dir/install/bin/BubbleBobble"
[ -x "$executable" ] || fail "설치한 실행 파일이 없습니다: $executable"
/usr/bin/lipo -verify_arch arm64 "$executable" || fail '설치한 실행 파일에 Apple Silicon 코드가 없습니다.'
[ -f "$stage_dir/install/bin/assets/BubbleCharacter.png" ] || fail '설치한 게임 에셋이 없습니다.'

# macOS 번들의 표준 Contents 구조를 만든다. Resources에는 배포 라이선스를 담는다.
staged_bundle="$stage_dir/BubbleBobble.app"
mkdir -p "$staged_bundle/Contents/MacOS" "$staged_bundle/Contents/Resources/licenses"
# 게임은 실행 파일 옆의 assets를 찾으므로 앱 번들 안에서도 이 배치를 유지한다.
/usr/bin/ditto "$executable" "$staged_bundle/Contents/MacOS/BubbleBobble"
/usr/bin/ditto "$stage_dir/install/bin/assets" "$staged_bundle/Contents/MacOS/assets"
# 최종 번들과 같은 상대 경로에서 실행해 파일 누락과 잘못된 에셋 형식을 검사한다.
"$staged_bundle/Contents/MacOS/BubbleBobble" --validate-assets
# 원본 게임과 사용한 raylib의 라이선스를 패키지에 함께 제공한다.
# 소스 캐시가 없을 때는 기존 동작대로 진단을 남기고 번들 생성은 계속한다.
cp LICENSE.md "$staged_bundle/Contents/Resources/licenses/UPSTREAM-LICENSE.md"
raylib_license="$repo_dir/build/macos-arm64/_deps/raylib-src/LICENSE"
if [ -f "$raylib_license" ]; then
    cp "$raylib_license" "$staged_bundle/Contents/Resources/licenses/RAYLIB-LICENSE.txt"
else
    printf '%s\n' 'package-macos: 예상한 소스 캐시에서 raylib 라이선스를 찾지 못했습니다.' >&2
fi

# Finder가 읽을 앱 이름·실행 파일명·고유 식별자·최소 OS·아키텍처 정보를 기록한다.
# 따옴표를 붙인 heredoc 구분자를 사용해 본문의 달러 기호 등이 셸에서 확장되지 않게 한다.
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
# PkgInfo에는 앱 유형을 기록하고, plist는 배치 전에 Apple 도구로 문법을 검사한다.
printf 'APPL????' > "$staged_bundle/Contents/PkgInfo"
/usr/bin/plutil -lint "$staged_bundle/Contents/Info.plist"

# 위에서 검사한 것처럼 식별자가 일치하는 번들만 교체할 수 있다.
if [ -e "$bundle_path" ]; then
    # 기존 정상 번들을 임시 폴더에 보관해 다음 이동이 실패했을 때 복구할 수 있게 한다.
    mv "$bundle_path" "$stage_dir/previous.app"
fi
if ! mv "$staged_bundle" "$bundle_path"; then
    # 새 번들 배치가 실패하면 앞서 옮겨 둔 기존 번들을 되돌린다.
    if [ -d "$stage_dir/previous.app" ]; then
        mv "$stage_dir/previous.app" "$bundle_path"
    fi
    fail '완성한 번들을 대상 위치로 옮기지 못했습니다.'
fi
# 이 스크립트는 코드 서명/공증을 수행하지 않으므로 결과의 배포 상태를 정확히 알린다.
printf '서명되지 않은 로컬 앱을 만들었습니다: %s\n' "$bundle_path"
