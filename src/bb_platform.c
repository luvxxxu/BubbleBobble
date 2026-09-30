#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bb_platform.h"

#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

bool bb_path_join(char *out, size_t capacity, const char *dir, const char *name)
{
    int result = snprintf(out, capacity, "%s/%s", dir, name);
    return result >= 0 && (size_t)result < capacity;
}

#if defined(_WIN32)
static bool to_wide(const char *path, wchar_t out[BB_PATH_CAP])
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, BB_PATH_CAP) > 0;
}

static bool from_wide(const wchar_t *path, char *out, size_t capacity)
{
    if (capacity > INT_MAX) return false;
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                              out, (int)capacity, NULL, NULL) > 0;
}
#endif

FILE *bb_platform_fopen(const char *path, const char *mode)
{
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP], wide_mode[BB_PATH_CAP];
    if (!to_wide(path, wide_path) || !to_wide(mode, wide_mode)) return NULL;
    return _wfopen(wide_path, wide_mode);
#else
    return fopen(path, mode);
#endif
}

void bb_platform_free_arguments(BBArguments *arguments)
{
    int i;

    if (arguments->owned && arguments->values) {
        for (i = 0; i < arguments->count; ++i) free(arguments->values[i]);
        free(arguments->values);
    }
    memset(arguments, 0, sizeof *arguments);
}

bool bb_platform_arguments(int argc, char **argv, BBArguments *out)
{
#if defined(_WIN32)
    int count;
    wchar_t **wide;
    bool ok;
    int i;
    int size;
#endif

    /* Windows의 좁은 argv 인코딩에 의존하지 않고 원본 유니코드 명령줄을 UTF-8로 변환한다. */
    out->count = argc;
    out->values = argv;
    out->owned = false;
#if defined(_WIN32)
    count = 0;
    wide = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!wide) return false;
    out->values = calloc((size_t)count + 1, sizeof *out->values);
    out->count = count;
    out->owned = true;
    if (!out->values) { LocalFree(wide); return false; }
    ok = true;
    for (i = 0; i < count; ++i) {
        size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, NULL, 0, NULL, NULL);
        if (size <= 0 || size > BB_PATH_CAP) { ok = false; break; }
        out->values[i] = malloc((size_t)size);
        if (!out->values[i] || !from_wide(wide[i], out->values[i], (size_t)size)) { ok = false; break; }
    }
    LocalFree(wide);
    if (!ok) { bb_platform_free_arguments(out); return false; }
#endif
    return true;
}

bool bb_platform_asset_dir(char *out, size_t capacity)
{
    /* 작업 디렉터리와 무관하게 실행 파일 옆의 assets 디렉터리를 찾는다. */
    char executable[BB_PATH_CAP];
    char *separator;
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP];
    DWORD n;
    char *backslash;

    n = GetModuleFileNameW(NULL, wide_path, BB_PATH_CAP);
    if (n == 0 || n >= BB_PATH_CAP || !from_wide(wide_path, executable, sizeof executable)) return false;
#elif defined(__APPLE__)
    uint32_t n;

    n = (uint32_t)sizeof executable;
    if (_NSGetExecutablePath(executable, &n) != 0) return false;
#else
    ssize_t n;

    n = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (n < 0 || (size_t)n >= sizeof executable - 1) return false;
    executable[n] = '\0';
#endif
    separator = strrchr(executable, '/');
#if defined(_WIN32)
    backslash = strrchr(executable, '\\');
    if (backslash && (!separator || backslash > separator)) separator = backslash;
#endif
    if (!separator) return false;
    *separator = '\0';
    return bb_path_join(out, capacity, executable, "assets");
}
