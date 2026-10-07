#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bb_platform.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
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
    if (!path || !path[0] || !mode || !mode[0]) { errno = EINVAL; return NULL; }
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP], wide_mode[BB_PATH_CAP];
    if (!to_wide(path, wide_path) || !to_wide(mode, wide_mode)) { errno = EINVAL; return NULL; }
    return _wfopen(wide_path, wide_mode);
#else
    return fopen(path, mode);
#endif
}

bool bb_platform_remove(const char *path)
{
    if (!path || !path[0]) return false;
#if defined(_WIN32)
    {
        wchar_t wide_path[BB_PATH_CAP];
        return to_wide(path, wide_path) && _wremove(wide_path) == 0;
    }
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

static bool make_directory(const char *path)
{
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP];
    DWORD attributes;
    if (!to_wide(path, wide_path)) return false;
    if (_wmkdir(wide_path) == 0) return true;
    if (errno != EEXIST) return false;
    attributes = GetFileAttributesW(wide_path);
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat info;
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

bool bb_platform_score_path(char *out, size_t capacity)
{
    char parent[BB_PATH_CAP], directory[BB_PATH_CAP];
#if defined(_WIN32)
    wchar_t wide_parent[BB_PATH_CAP];
    DWORD n;
#else
    const char *home_dir;
    size_t length;
#endif
    if (!out || capacity == 0) return false;
    out[0] = '\0';
    /* 작업 폴더나 앱 번들이 아닌 사용자별 데이터 폴더에 저장한다. */
#if defined(_WIN32)
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", wide_parent, BB_PATH_CAP);
    if (n == 0 || n >= BB_PATH_CAP || !from_wide(wide_parent, parent, sizeof parent)) return false;
#else
    home_dir = getenv("HOME");
    if (!home_dir || !home_dir[0]) return false;
    length = strlen(home_dir);
    if (length >= sizeof parent) return false;
    memcpy(parent, home_dir, length + 1);
#if defined(__APPLE__)
    if (!bb_path_join(directory, sizeof directory, parent, "Library") || !make_directory(directory)) return false;
    if (!bb_path_join(parent, sizeof parent, directory, "Application Support") || !make_directory(parent)) return false;
#endif
#endif
    if (!bb_path_join(directory, sizeof directory, parent, "BubbleBobble-C11") || !make_directory(directory)) return false;
    return bb_path_join(out, capacity, directory, "Scores.txt");
}

static bool valid_score(BbScore score)
{
    int i;
    if (score.round < 1 || score.round > BB_SCORE_MAX_ROUND || score.name[3] != '\0') return false;
    for (i = 0; i < 3; ++i)
        if (!((score.name[i] >= 'A' && score.name[i] <= 'Z') ||
              (score.name[i] >= '0' && score.name[i] <= '9') || score.name[i] == '.')) return false;
    return true;
}

void bb_scores_insert(BbScore scores[BB_SCORE_COUNT], size_t *count, BbScore score)
{
    size_t length, position, i;
    if (!scores || !count || !valid_score(score)) return;
    length = *count < BB_SCORE_COUNT ? *count : BB_SCORE_COUNT;
    *count = length;
    position = 0;
    while (position < length && (scores[position].score > score.score ||
           (scores[position].score == score.score && scores[position].round >= score.round))) ++position;
    if (position >= BB_SCORE_COUNT) return;
    if (length < BB_SCORE_COUNT) ++length;
    for (i = length - 1; i > position; --i) scores[i] = scores[i - 1];
    scores[position] = score;
    *count = length;
}

static bool parse_score(const char *line, BbScore *record)
{
    char *end;
    const char *cursor = line;
    unsigned long score, round;
    if (*cursor < '0' || *cursor > '9') return false;
    errno = 0;
    score = strtoul(cursor, &end, 10);
    if (errno || score > UINT_MAX || *end != ' ') return false;
    cursor = end + 1;
    if (*cursor < '0' || *cursor > '9') return false;
    errno = 0;
    round = strtoul(cursor, &end, 10);
    if (errno || round < 1 || round > BB_SCORE_MAX_ROUND || *end != ' ') return false;
    cursor = end + 1;
    if (strlen(cursor) != 3) return false;
    record->score = (unsigned)score;
    record->round = (int)round;
    memcpy(record->name, cursor, sizeof record->name);
    return valid_score(*record);
}

bool bb_scores_load(const char *path, BbScore scores[BB_SCORE_COUNT], size_t *out_count)
{
    FILE *file;
    BbScore loaded[BB_SCORE_COUNT] = {0};
    BbScore record;
    char line[128];
    size_t count = 0, length = 0, consumed = 0;
    int next, lines = 0;
    bool bad_line = false, ok = true;
    if (!out_count) return false;
    *out_count = 0;
    if (!path || !path[0] || !scores) return false;
    file = bb_platform_fopen(path, "rb");
    if (!file) return errno == ENOENT;
    /* 한 글자씩 읽어 긴 행과 NUL 문자를 확실히 거부한다.
     * 최대 512행, 64 KiB만 읽어 손상된 파일에도 실행 시간이 제한된다. */
    while (ok) {
        next = fgetc(file);
        if (next != EOF && ++consumed > 65536) { ok = false; break; }
        if (next == '\n' || next == EOF) {
            if (next == '\n' || length > 0 || bad_line) {
                if (++lines > 512) { ok = false; break; }
                if (length > 0 && line[length - 1] == '\r') --length;
                line[length] = '\0';
                if (!bad_line && parse_score(line, &record)) bb_scores_insert(loaded, &count, record);
            }
            if (next == EOF) break;
            length = 0;
            bad_line = false;
        } else if (next == 0 || length >= sizeof line - 1) {
            bad_line = true;
        } else {
            line[length++] = (char)next;
        }
    }
    if (ferror(file)) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok) {
        memcpy(scores, loaded, sizeof loaded);
        *out_count = count;
    }
    return ok;
}

bool bb_scores_save(const char *path, const BbScore *scores, size_t count)
{
    char temporary[BB_PATH_CAP];
    int n, attempt;
    FILE *file = NULL;
    bool ok = true;
    size_t i;
#if defined(_WIN32)
    wchar_t wide_temp[BB_PATH_CAP], wide_path[BB_PATH_CAP];
#endif
    if (!path || !path[0] || count > BB_SCORE_COUNT || (count > 0 && !scores)) return false;
    for (i = 0; i < count; ++i)
        if (!valid_score(scores[i])) return false;
    /* x 모드는 이미 있는 파일을 덮어쓰지 않는다. 다른 실행 중인 게임이나
     * 이전 실행의 임시 파일을 만나면 다음 번호를 사용한다. */
    for (attempt = 0; attempt < 32; ++attempt) {
        n = snprintf(temporary, sizeof temporary, "%s.tmp.%d", path, attempt);
        if (n < 0 || (size_t)n >= sizeof temporary) return false;
        file = bb_platform_fopen(temporary, "wbx");
        if (file) break;
        if (errno != EEXIST) return false;
    }
    if (!file) return false;
    for (i = 0; i < count; ++i)
        if (fprintf(file, "%u %d %.3s\n", scores[i].score, scores[i].round, scores[i].name) < 0) ok = false;
    if (fclose(file) != 0) ok = false;
#if defined(_WIN32)
    if (ok) ok = to_wide(temporary, wide_temp) && to_wide(path, wide_path);
    if (ok) ok = MoveFileExW(wide_temp, wide_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    if (ok) ok = rename(temporary, path) == 0;
#endif
    if (!ok) bb_platform_remove(temporary);
    return ok;
}
