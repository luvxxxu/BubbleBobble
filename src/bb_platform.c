#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bb_platform.h"

#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <direct.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <sys/stat.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#if !defined(_WIN32)
static bool copy_path(char *out, size_t capacity, const char *value)
{
    size_t length = strlen(value);
    if (length >= capacity) return false;
    memcpy(out, value, length + 1);
    return true;
}
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

bool bb_platform_remove(const char *path)
{
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP];
    return to_wide(path, wide_path) && _wremove(wide_path) == 0;
#else
    return remove(path) == 0;
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

static bool make_directory(const char *path)
{
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP];
    if (!to_wide(path, wide_path)) return false;
    return _wmkdir(wide_path) == 0 || errno == EEXIST;
#else
    return mkdir(path, 0700) == 0 || errno == EEXIST;
#endif
}

bool bb_platform_score_path(char *out, size_t capacity)
{
    char parent[BB_PATH_CAP], directory[BB_PATH_CAP];
#if defined(_WIN32)
    wchar_t wide_parent[BB_PATH_CAP];
    DWORD n;

    n = GetEnvironmentVariableW(L"LOCALAPPDATA", wide_parent, BB_PATH_CAP);
    if (n == 0 || n >= BB_PATH_CAP || !from_wide(wide_parent, parent, sizeof parent)) return false;
#else
    const char *home_dir;

    home_dir = getenv("HOME");
    if (!home_dir || !copy_path(parent, sizeof parent, home_dir)) return false;
#if defined(__APPLE__)
    if (!bb_path_join(directory, sizeof directory, parent, "Library") || !make_directory(directory)) return false;
    if (!bb_path_join(parent, sizeof parent, directory, "Application Support") || !make_directory(parent)) return false;
#endif
#endif
    if (!bb_path_join(directory, sizeof directory, parent, "BubbleBobble-C11") || !make_directory(directory)) return false;
    return bb_path_join(out, capacity, directory, "Scores.txt");
}

void bb_scores_insert(BbScore scores[BB_SCORE_COUNT], size_t *count, BbScore score)
{
    size_t length = *count < BB_SCORE_COUNT ? *count : BB_SCORE_COUNT;
    size_t position = 0;
    size_t i;

    while (position < length && (scores[position].score > score.score ||
           (scores[position].score == score.score && scores[position].round >= score.round))) ++position;
    if (position >= BB_SCORE_COUNT) return;
    if (length < BB_SCORE_COUNT) ++length;
    for (i = length - 1; i > position; --i) scores[i] = scores[i - 1];
    scores[position] = score;
    scores[position].name[3] = '\0';
    *count = length;
}

bool bb_scores_load(const char *path, BbScore scores[BB_SCORE_COUNT], size_t *out_count)
{
    FILE *file;
    size_t count;
    size_t consumed;
    char line[128];
    int lines;
    char *cursor;
    char *end;
    unsigned long score;
    unsigned long round;
    int next;
    size_t name_length;
    bool valid;
    size_t i;
    BbScore record;
    bool ok;

    *out_count = 0;
    file = bb_platform_fopen(path, "rb");
    count = 0;
    consumed = 0;
    if (!file) return errno == ENOENT;
    /* 손상되었거나 외부에서 수정된 파일도 읽기 범위를 제한한다. */
    for (lines = 0; lines < 512 && consumed < 65536 && fgets(line, sizeof line, file); ++lines) {
        cursor = line;
        consumed += strlen(line);
        if (!strchr(line, '\n') && !feof(file)) {
            while (consumed < 65536 && (next = fgetc(file)) != EOF) {
                ++consumed;
                if (next == '\n') break;
            }
            continue;
        }
        if (*cursor < '0' || *cursor > '9') continue;
        errno = 0;
        score = strtoul(cursor, &end, 10);
        if (errno || score > UINT_MAX || *end != ' ') continue;
        cursor = end + 1;
        if (*cursor < '0' || *cursor > '9') continue;
        round = strtoul(cursor, &end, 10);
        if (errno || round > 3 || *end != ' ') continue;
        cursor = end + 1;
        name_length = strcspn(cursor, "\r\n");
        if (name_length != 3) continue;
        valid = true;
        for (i = 0; i < 3; ++i)
            if (!((cursor[i] >= 'A' && cursor[i] <= 'Z') ||
                  (cursor[i] >= '0' && cursor[i] <= '9') || cursor[i] == '.')) valid = false;
        if (!valid) continue;
        record.score = (unsigned)score;
        record.round = (int)round;
        record.name[0] = cursor[0];
        record.name[1] = cursor[1];
        record.name[2] = cursor[2];
        record.name[3] = '\0';
        bb_scores_insert(scores, &count, record);
    }
    ok = consumed <= 65536 && fgetc(file) == EOF && !ferror(file);
    if (fclose(file) != 0) ok = false;
    if (ok) *out_count = count;
    return ok;
}

bool bb_scores_save(const char *path, const BbScore *scores, size_t count)
{
    char temporary[BB_PATH_CAP];
    int n;
    FILE *file;
    bool ok;
    size_t i;
#if defined(_WIN32)
    wchar_t wide_temp[BB_PATH_CAP], wide_path[BB_PATH_CAP];
#endif

    if (!path[0] || count > BB_SCORE_COUNT) return false;
    n = snprintf(temporary, sizeof temporary, "%s.tmp", path);
    if (n < 0 || (size_t)n >= sizeof temporary) return false;
    file = bb_platform_fopen(temporary, "wb");
    if (!file) return false;
    ok = true;
    for (i = 0; i < count; ++i)
        if (fprintf(file, "%u %d %.3s\n", scores[i].score, scores[i].round, scores[i].name) < 0) ok = false;
    if (fclose(file) != 0) ok = false;
#if defined(_WIN32)
    if (!to_wide(temporary, wide_temp) || !to_wide(path, wide_path)) return false;
    if (ok) ok = MoveFileExW(wide_temp, wide_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
    if (!ok) DeleteFileW(wide_temp);
#else
    if (ok) ok = rename(temporary, path) == 0;
    if (!ok) remove(temporary);
#endif
    return ok;
}
