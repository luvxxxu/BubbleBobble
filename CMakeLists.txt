# 이 프로젝트는 CMake 3.24 이상을 요구하며, 3.31까지의 정책 동작을 기준으로 구성한다.
# 더 최신 Visual Studio 생성기의 최소 버전은 Windows 실행 스크립트에서도 검사한다.
cmake_minimum_required(VERSION 3.24...3.31)

# Windows 패키지는 Visual C++ 재배포 DLL 없이 실행할 수 있도록 기본적으로 정적 CRT를 쓴다.
# CMP0091은 첫 project()/enable_language() 전에 이 변수를 읽으므로 이 위치를 유지한다.
option(BB_MSVC_STATIC_RUNTIME "Statically link the MSVC runtime in Windows builds" ON)
if(BB_MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# LANGUAGES C를 명시해 C++ 컴파일러 없이도 게임 핵심부와 테스트를 구성한다.
project(BubbleBobble VERSION 0.1.0 LANGUAGES C)

# 프로젝트 기본값은 확장 문법 없는 C11이다. 실제 자체 타깃에도 아래 함수에서
# 같은 표준을 다시 적용해 의존성의 설정이 프로젝트 규칙을 바꾸지 않도록 한다.
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
# 지원하는 생성기에서 compile_commands.json을 만들어 편집기가 실제 컴파일 옵션을 알게 한다.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
# 실행 파일과 테스트를 빌드 폴더의 bin에 모은다. 다중 구성 생성기는 Debug/Release를 덧붙인다.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

# headless 프리셋은 데스크톱 게임을 끄고 그래픽 의존성 없는 테스트만 구성한다.
# 경고를 오류로 승격하는 옵션과 런타임 메모리 검사는 각각 독립적으로 선택할 수 있다.
option(BB_BUILD_GAME "Build the raylib desktop game" ON)
option(BB_WARNINGS_AS_ERRORS "Treat warnings in project C code as errors" ON)
option(BB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
# BUILD_TESTING 옵션과 add_test/ctest 연동을 준비한다.
include(CTest)

# 모든 자체 C 실행 파일·라이브러리에 공통 컴파일 정책을 적용한다.
# PRIVATE 옵션은 해당 타깃에만 적용되어 의존하거나 의존받는 외부 코드로 전파되지 않는다.
function(bb_configure_c_target target)
    # 자체 C 타깃에만 엄격한 경고를 적용해 외부 raylib의 빌드 옵션을 건드리지 않는다.
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(MSVC)
        # MSVC는 높은 경고 수준, UTF-8 소스 인코딩, C11 모드를 명시한다.
        # 표준 C 파일 API 사용에 대한 CRT 권고 경고만 별도로 끈다.
        target_compile_options(${target} PRIVATE /W4 /utf-8 /std:c11)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(BB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        # GCC와 Clang은 일반 경고·추가 경고·표준 위반 진단을 함께 활성화한다.
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(BB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        # winpthreads 같은 번들 도구 모음 지원을 포함한다. 시스템 UCRT는 동적으로 유지한다.
        target_link_options(${target} PRIVATE -static)
    endif()
    # ASan은 메모리 범위/수명 오류를, UBSan은 정의되지 않은 연산을 실행 중 점검한다.
    # 프레임 포인터를 남겨 오류 보고서의 호출 경로를 추적하기 쉽게 한다.
    if(BB_ENABLE_SANITIZERS)
        if(MSVC OR WIN32)
            message(FATAL_ERROR "BB_ENABLE_SANITIZERS currently requires Clang or GCC on a Unix host")
        endif()
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
endfunction()

# 핵심 게임 상태는 그래픽 백엔드와 분리해 헤드리스 테스트에서도 사용한다.
add_library(bb_core STATIC src/bb_game.c src/bb_levels.c)
target_include_directories(bb_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
bb_configure_c_target(bb_core)
# Unix 계열/MinGW의 수학 함수는 libm에서 링크한다. MSVC는 별도 libm이 필요 없다.
if(NOT MSVC)
    target_link_libraries(bb_core PUBLIC m)
endif()

# 테스트를 세 실행 파일로 분리해 규칙, 맵 구성, 파일 저장의 실패 위치를 구분한다.
if(BUILD_TESTING)
    add_executable(bb_game_tests tests/test_game.c)
    target_link_libraries(bb_game_tests PRIVATE bb_core)
    bb_configure_c_target(bb_game_tests)
    add_executable(bb_level_tests tests/test_levels.c)
    target_link_libraries(bb_level_tests PRIVATE bb_core)
    bb_configure_c_target(bb_level_tests)
    # 플랫폼 테스트에는 그래픽 라이브러리가 필요 없으므로 플랫폼 소스만 직접 포함한다.
    add_executable(bb_platform_tests tests/test_platform.c src/bb_platform.c)
    target_include_directories(bb_platform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    bb_configure_c_target(bb_platform_tests)
    # Windows의 유니코드 명령줄 분해 함수 CommandLineToArgvW는 shell32에 있다.
    if(WIN32)
        target_link_libraries(bb_platform_tests PRIVATE shell32)
    endif()
    # Linux 시스템 헤더에서 POSIX 함수 선언이 노출되도록 기능 수준을 지정한다.
    if(UNIX AND NOT APPLE)
        target_compile_definitions(bb_platform_tests PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    # 테스트는 Release 구성에서도 assertion을 활성화한 채 유지해야 한다.
    foreach(test_target bb_game_tests bb_level_tests bb_platform_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    # CTest 이름은 플랫폼별 프리셋에서 동일하다. 플랫폼 테스트의 임시 파일은
    # 소스 폴더가 아닌 현재 빌드 폴더에 두어 여러 빌드가 서로 덮어쓰지 않게 한다.
    add_test(NAME gameplay COMMAND bb_game_tests)
    add_test(NAME level_layouts COMMAND bb_level_tests)
    add_test(NAME platform COMMAND bb_platform_tests "${CMAKE_CURRENT_BINARY_DIR}/platform-test-file.txt")
    # 실행 테스트와 별도로 소스 확장자를 감사해 C++ 파일의 유입을 감지한다.
    add_test(NAME pure_c_sources COMMAND "${CMAKE_COMMAND}"
        "-DBB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditPureC.cmake")
endif()

if(BB_BUILD_GAME)
    # 다운로드한 raylib은 고정 버전과 해시로 제한하고 정적 데스크톱 빌드로 구성한다.
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Disable raylib examples" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "Disable raylib games" FORCE)
    set(PLATFORM Desktop CACHE STRING "raylib backend" FORCE)
    set(USE_EXTERNAL_GLFW OFF CACHE STRING "Use pinned GLFW shipped with raylib" FORCE)
    # Linux CI/개발 환경에서 그래픽 백엔드를 X11로 고정해 Wayland 의존성을 요구하지 않는다.
    if(UNIX AND NOT APPLE)
        set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "Use X11 on Linux" FORCE)
        set(GLFW_BUILD_X11 ON CACHE BOOL "Use X11 on Linux" FORCE)
    endif()
    # raylib 5.5는 C인데 한정하지 않은 루트 project()가 불필요하게 CXX를 활성화한다.
    # 구성 전에 정확하고 여러 번 적용해도 같은 결과인 CMake 전용 패치를 적용한다.
    FetchContent_Declare(raylib
        URL https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/5.5
        URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        PATCH_COMMAND "${CMAKE_COMMAND}" "-DRAYLIB_SOURCE_DIR=<SOURCE_DIR>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/PureCRaylib.cmake")
    # 선언한 고정 소스를 확보하고 하위 프로젝트로 구성한다. 필요한 raylib 타깃은
    # 링크 의존성으로 빌드하되 외부 프로젝트의 나머지 기본 타깃은 전체 빌드에서 제외한다.
    FetchContent_MakeAvailable(raylib)
    set_property(DIRECTORY "${raylib_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)

    # 실제 게임은 창·입력·렌더링·파일 입출력을 담당하며, 규칙은 bb_core를 공유한다.
    add_executable(BubbleBobble src/main.c src/bb_render.c src/bb_platform.c src/bb_raylib_types.c)
    target_link_libraries(BubbleBobble PRIVATE bb_core raylib)
    bb_configure_c_target(BubbleBobble)
    if(UNIX AND NOT APPLE)
        target_compile_definitions(BubbleBobble PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    # Visual Studio 디버거가 실행 파일 폴더에서 시작하도록 설정한다.
    set_target_properties(BubbleBobble PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:BubbleBobble>")
    # Visual Studio에서 기본 실행 프로젝트를 게임으로 정하고 Windows API를 연결한다.
    if(WIN32)
        set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT BubbleBobble)
        target_link_libraries(BubbleBobble PRIVATE shell32)
    endif()
    # 소스를 다시 컴파일하지 않아도 ALL은 변경된 에셋을 복사한다.
    add_custom_target(bb_assets ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/Assets" "$<TARGET_FILE_DIR:BubbleBobble>/assets"
        COMMENT "Copying original game assets" VERBATIM)
    add_dependencies(BubbleBobble bb_assets)
    if(BUILD_TESTING)
        # 실제 raylib 경로로 이미지와 오디오를 장치 없이 디코딩한다.
        add_test(NAME desktop_assets COMMAND BubbleBobble --validate-assets)
    endif()
    # 설치/배포 결과에서도 실행 파일 옆에 assets를 두어 개발 빌드와 경로 규칙을 맞춘다.
    install(TARGETS BubbleBobble RUNTIME DESTINATION bin)
    install(DIRECTORY Assets/ DESTINATION bin/assets)
endif()

# 모든 의존성을 추가한 마지막 시점에 활성화된 언어를 검사한다.
# 자체 project 선언만 검사하면 의존성이 뒤늦게 C++를 켠 경우를 놓칠 수 있다.
get_property(bb_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
# 의존성 구성 과정에서도 C++가 암묵적으로 활성화되지 않았는지 확인한다.
if("CXX" IN_LIST bb_languages OR "OBJCXX" IN_LIST bb_languages)
    message(FATAL_ERROR "The pure C build must not enable C++ or Objective-C++")
endif()
