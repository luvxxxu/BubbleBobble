#include "bb_platform.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

static void remove_test_file(const char *path)
{
#if defined(_WIN32)
    wchar_t wide[BB_PATH_CAP];
    assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1,
                               wide, BB_PATH_CAP) > 0);
    assert(_wremove(wide) == 0);
#else
    assert(remove(path) == 0);
#endif
}

static void check_file_round_trip(const char *path)
{
    FILE *file = bb_platform_fopen(path, "wb");
    assert(file != NULL);
    assert(fputs("bubble\n", file) >= 0);
    assert(fclose(file) == 0);

    file = bb_platform_fopen(path, "rb");
    assert(file != NULL);
    char contents[16] = {0};
    assert(fgets(contents, sizeof contents, file) != NULL);
    assert(strcmp(contents, "bubble\n") == 0);
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
    remove_test_file(path);
}

int main(int argc, char **argv)
{
    BBArguments arguments;
    assert(bb_platform_arguments(argc, argv, &arguments));
    assert(arguments.count == 2);
    const char *path = arguments.values[1];

    char small[4], joined[32], assets[BB_PATH_CAP];
    assert(!bb_path_join(small, sizeof small, "too", "long"));
    assert(bb_path_join(joined, sizeof joined, "base", "asset.png"));
    assert(strcmp(joined, "base/asset.png") == 0);
    assert(bb_platform_asset_dir(assets, sizeof assets));
    assert(strstr(assets, "assets") != NULL);
    assert(!bb_platform_asset_dir(small, sizeof small));

    check_file_round_trip(path);
    /* Windows의 파일 열기 래퍼도 UTF-8 이름을 처리해야 한다. */
    char unicode_path[BB_PATH_CAP];
    int n = snprintf(unicode_path, sizeof unicode_path,
                     "%s-\xed\x95\x9c\xea\xb8\x80.txt", path);
    assert(n > 0 && (size_t)n < sizeof unicode_path);
    check_file_round_trip(unicode_path);

    bb_platform_free_arguments(&arguments);
    assert(arguments.count == 0 && arguments.values == NULL);
    puts("Platform tests passed: asset path, bounded join, UTF-8 file I/O, arguments.");
    return 0;
}
