cmake_minimum_required(VERSION 3.24...3.31)

# Windows 패키지는 Visual C++ 재배포 DLL 없이 실행할 수 있도록 기본적으로 정적 CRT를 쓴다.
# CMP0091은 첫 project()/enable_language() 전에 이 변수를 읽으므로 이 위치를 유지한다.
option(BB_MSVC_STATIC_RUNTIME "Statically link the MSVC runtime in Windows builds" ON)
if(BB_MSVC_STATIC_RUNTIME)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

project(BubbleBobble VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")

option(BB_BUILD_GAME "Build the raylib desktop game" ON)
option(BB_WARNINGS_AS_ERRORS "Treat warnings in project C code as errors" ON)
option(BB_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
include(CTest)

function(bb_configure_c_target target)
    set_target_properties(${target} PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        # /std:c11은 VS2019 16.8 이후에만 있다. VS2010에서는 인식하지 못한다.
        if(MSVC_VERSION GREATER 1600)
            target_compile_options(${target} PRIVATE /std:c11)
        endif()
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
        if(BB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
        if(BB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
    if(MINGW)
        # winpthreads 같은 번들 도구 모음 지원을 포함한다. 시스템 UCRT는 동적으로 유지한다.
        target_link_options(${target} PRIVATE -static)
    endif()
    if(BB_ENABLE_SANITIZERS)
        if(MSVC OR WIN32)
            message(FATAL_ERROR "BB_ENABLE_SANITIZERS currently requires Clang or GCC on a Unix host")
        endif()
        target_compile_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
        target_link_options(${target} PRIVATE -fno-omit-frame-pointer -fsanitize=address,undefined)
    endif()
endfunction()

add_library(bb_core STATIC src/bb_game.c)
target_include_directories(bb_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
bb_configure_c_target(bb_core)
if(NOT MSVC)
    target_link_libraries(bb_core PUBLIC m)
endif()

if(BUILD_TESTING)
    add_executable(bb_game_tests tests/test_game.c)
    target_link_libraries(bb_game_tests PRIVATE bb_core)
    bb_configure_c_target(bb_game_tests)
    add_executable(bb_platform_tests tests/test_platform.c src/bb_platform.c)
    target_include_directories(bb_platform_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
    bb_configure_c_target(bb_platform_tests)
    if(WIN32)
        target_link_libraries(bb_platform_tests PRIVATE shell32)
    endif()
    if(UNIX AND NOT APPLE)
        target_compile_definitions(bb_platform_tests PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    # 테스트는 Release 구성에서도 assertion을 활성화한 채 유지해야 한다.
    foreach(test_target bb_game_tests bb_platform_tests)
        if(MSVC)
            target_compile_options(${test_target} PRIVATE /UNDEBUG)
        else()
            target_compile_options(${test_target} PRIVATE -UNDEBUG)
        endif()
    endforeach()
    add_test(NAME gameplay COMMAND bb_game_tests)
    add_test(NAME platform COMMAND bb_platform_tests "${CMAKE_CURRENT_BINARY_DIR}/platform-test-scores.txt")
    add_test(NAME pure_c_sources COMMAND "${CMAKE_COMMAND}"
        "-DBB_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/AuditPureC.cmake")
endif()

if(BB_BUILD_GAME)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
    set(BUILD_EXAMPLES OFF CACHE BOOL "Disable raylib examples" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "Disable raylib games" FORCE)
    set(PLATFORM Desktop CACHE STRING "raylib backend" FORCE)
    set(USE_EXTERNAL_GLFW OFF CACHE STRING "Use pinned GLFW shipped with raylib" FORCE)
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
    FetchContent_MakeAvailable(raylib)
    set_property(DIRECTORY "${raylib_SOURCE_DIR}" PROPERTY EXCLUDE_FROM_ALL TRUE)

    add_executable(BubbleBobble src/main.c src/bb_render.c src/bb_platform.c src/bb_collision_raylib.c src/bb_raylib_types.c)
    target_link_libraries(BubbleBobble PRIVATE bb_core raylib)
    bb_configure_c_target(BubbleBobble)
    if(UNIX AND NOT APPLE)
        target_compile_definitions(BubbleBobble PRIVATE _POSIX_C_SOURCE=200809L)
    endif()
    set_target_properties(BubbleBobble PROPERTIES
        VS_DEBUGGER_WORKING_DIRECTORY "$<TARGET_FILE_DIR:BubbleBobble>")
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
    install(TARGETS BubbleBobble RUNTIME DESTINATION bin)
    install(DIRECTORY Assets/ DESTINATION bin/assets)
endif()

get_property(bb_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
if("CXX" IN_LIST bb_languages OR "OBJCXX" IN_LIST bb_languages)
    message(FATAL_ERROR "The pure C build must not enable C++ or Objective-C++")
endif()
