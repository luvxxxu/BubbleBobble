cmake_minimum_required(VERSION 3.20)
# Keep the Windows executable self-contained when using Visual Studio.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
project(game VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

function(bb_enable_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

# The rules can be built and tested without a graphics environment.
include(CTest)
if(BUILD_TESTING)
    add_executable(bb_game_tests tests/test_game.c src/bb_game.c)
    target_include_directories(bb_game_tests PRIVATE src)
    bb_enable_warnings(bb_game_tests)
    if(UNIX AND NOT APPLE)
        target_link_libraries(bb_game_tests PRIVATE m)
    endif()
    if(MSVC)
        target_compile_options(bb_game_tests PRIVATE /UNDEBUG)
    else()
        target_compile_options(bb_game_tests PRIVATE -UNDEBUG)
    endif()
    add_test(NAME gameplay COMMAND bb_game_tests)
endif()

option(BB_BUILD_GAME "Build the windowed game" ON)
if(BB_BUILD_GAME)
    include(FetchContent)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES OFF CACHE BOOL "" FORCE)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(CUSTOMIZE_BUILD ON CACHE BOOL "" FORCE)
    set(SUPPORT_MODULE_RAUDIO OFF CACHE BOOL "" FORCE)
    set(USE_AUDIO OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        URL https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/5.5
        URL_HASH SHA256=aea98ecf5bc5c5e0b789a76de0083a21a70457050ea4cc2aec7566935f5e258e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(raylib)

    # WIN32 hides the terminal on Windows; MACOSX_BUNDLE makes a Finder app.
    add_executable(game WIN32 MACOSX_BUNDLE src/main.c src/bb_game.c)
    target_include_directories(game PRIVATE src)
    bb_enable_warnings(game)
    target_link_libraries(game PRIVATE raylib)
    if(UNIX AND NOT APPLE)
        target_link_libraries(game PRIVATE m)
    endif()
    set_target_properties(game PROPERTIES
        MACOSX_BUNDLE_BUNDLE_NAME "game"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.game"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
    if(MSVC)
        # The source uses main(), including for the Windows GUI executable.
        target_link_options(game PRIVATE /ENTRY:mainCRTStartup)
    endif()
endif()
