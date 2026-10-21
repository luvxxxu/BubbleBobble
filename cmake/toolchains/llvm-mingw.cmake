# Unix 호스트에서 Windows 실행 파일을 만드는 LLVM-MinGW 교차 컴파일 설정이다.
# 실행 호스트와 출력 대상이 다르다는 사실을 CMake의 플랫폼 선택에 먼저 알린다.
set(CMAKE_SYSTEM_NAME Windows)
# 지원하는 두 대상만 허용한다. x86_64는 64비트, i686은 32비트 Windows이다.
# CACHE 값을 써 프리셋이나 -D 인수에서 대상 아키텍처를 선택할 수 있게 한다.
set(BB_WINDOWS_ARCH "x86_64" CACHE STRING "Windows target: x86_64 or i686")
set_property(CACHE BB_WINDOWS_ARCH PROPERTY STRINGS x86_64 i686)
if(NOT BB_WINDOWS_ARCH MATCHES "^(x86_64|i686)$")
    message(FATAL_ERROR "BB_WINDOWS_ARCH must be x86_64 or i686")
endif()
set(CMAKE_SYSTEM_PROCESSOR "${BB_WINDOWS_ARCH}")
# 도구 모음 루트는 환경 변수에서 기본값을 받고 CMake 캐시에 보관한다.
# 설치 위치를 가정하지 않아 사용자가 준비한 도구 모음을 명시적으로 선택할 수 있다.
set(BB_LLVM_MINGW_ROOT "$ENV{BB_LLVM_MINGW_ROOT}" CACHE PATH "Extracted LLVM-MinGW toolchain directory")
if(NOT BB_LLVM_MINGW_ROOT)
    message(FATAL_ERROR "Set BB_LLVM_MINGW_ROOT to an extracted LLVM-MinGW toolchain")
endif()
# 대상 이름이 붙은 C 컴파일러와 Windows 리소스 컴파일러를 함께 선택한다.
# sysroot에는 해당 Windows 대상의 표준 헤더와 링크 라이브러리가 들어 있다.
set(CMAKE_C_COMPILER "${BB_LLVM_MINGW_ROOT}/bin/${BB_WINDOWS_ARCH}-w64-mingw32-clang")
set(CMAKE_RC_COMPILER "${BB_LLVM_MINGW_ROOT}/bin/${BB_WINDOWS_ARCH}-w64-mingw32-windres")
set(CMAKE_FIND_ROOT_PATH "${BB_LLVM_MINGW_ROOT}/${BB_WINDOWS_ARCH}-w64-mingw32")
# 실행할 빌드 도구는 호스트에서 찾고 라이브러리·헤더·패키지는 대상 sysroot에서 찾는다.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
# 대상 선택을 컴파일러 기능 try_compile 하위 빌드에도 전달한다.
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES BB_WINDOWS_ARCH BB_LLVM_MINGW_ROOT)
