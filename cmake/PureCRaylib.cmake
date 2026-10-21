# FetchContent가 압축을 푼 raylib의 CMake 선언에만 적용하는 제한된 패치다.
# 게임 소스나 의존성 구현을 바꾸지 않고, 불필요한 C++ 언어 활성화만 막는다.
if(NOT DEFINED RAYLIB_SOURCE_DIR)
    message(FATAL_ERROR "RAYLIB_SOURCE_DIR is required")
endif()
# 변경 대상은 의존성 루트의 단일 CMakeLists.txt이며 전체 내용을 먼저 읽는다.
set(raylib_cmake "${RAYLIB_SOURCE_DIR}/CMakeLists.txt")
file(READ "${raylib_cmake}" contents)
# FetchContent가 패치를 다시 실행해도 같은 결과가 되도록 이미 적용된 경우 종료한다.
if(contents MATCHES "project\\(raylib LANGUAGES C\\)")
    return()
endif()
# 현재 고정 버전에서 기대한 원문이 있는지 위치를 조사한다.
string(FIND "${contents}" "project(raylib)" match)
# 고정 버전의 예상 선언이 바뀌면 다른 위치를 임의로 바꾸지 않도록 중단한다.
if(match EQUAL -1)
    message(FATAL_ERROR "Pinned raylib root project() differs; review before updating this patch")
endif()
# 기대한 선언만 치환한 뒤 같은 파일에 저장한다. 실패 조건을 앞에서 검사했으므로
# 다른 버전의 CMake 내용을 추측해 수정하지 않는다.
string(REPLACE "project(raylib)" "project(raylib LANGUAGES C)" contents "${contents}")
file(WRITE "${raylib_cmake}" "${contents}")
