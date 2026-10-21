/* 그래픽이나 게임 규칙과 분리된 운영체제별 파일·경로·점수 저장 기능이다.
 * 이 파일 밖에서는 경로를 UTF-8로 다루고, Windows API 호출 직전에만 UTF-16으로 바꾼다.
 * Linux의 readlink 등 POSIX.1-2008 선언을 쓰기 위해 시스템 헤더보다 먼저 매크로를 둔다. */
#if !defined(_WIN32) && !defined(__APPLE__)
#define _POSIX_C_SOURCE 200809L
#endif
#include "bb_platform.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* 각 운영체제에서 필요한 헤더만 읽는다. Windows의 불필요한 API 선언을 줄이고,
 * macOS는 실행 파일 경로 조회 API, Linux는 /proc 링크 조회 API를 사용한다. */
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

/* 디렉터리와 파일명을 하나의 경로로 연결한다. capacity는 종료 문자까지 포함한
 * 출력 버퍼 크기다. snprintf가 요구한 길이를 돌려주므로 잘린 경로는 실패로 알린다.
 * 호출자는 반환값을 확인한 다음에만 out을 실제 파일 작업에 사용해야 한다. */
bool bb_path_join(char *out, size_t capacity, const char *dir, const char *name)
{
    int result = snprintf(out, capacity, "%s/%s", dir, name);
    return result >= 0 && (size_t)result < capacity;
}

#if defined(_WIN32)
/* Windows의 넓은 문자 API에 전달할 UTF-16 경로를 만든다. -1은 입력의 종료
 * 문자까지 변환한다는 뜻이며, 잘못된 UTF-8이나 너무 긴 결과는 거부한다. */
static bool to_wide(const char *path, wchar_t out[BB_PATH_CAP])
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, BB_PATH_CAP) > 0;
}

/* Windows가 돌려준 UTF-16 문자열을 게임 내부의 UTF-8로 변환한다. API의 버퍼
 * 크기 인수는 int이므로 size_t 값을 줄여 넣기 전에 표현 가능한 범위를 검사한다. */
static bool from_wide(const wchar_t *path, char *out, size_t capacity)
{
    if (capacity > INT_MAX) return false;
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, path, -1,
                              out, (int)capacity, NULL, NULL) > 0;
}
#endif

/* 파일 열기의 공통 진입점이다. 빈 경로/모드는 명백한 인수 오류로 구분하고,
 * 실제 파일의 부재나 권한 문제는 fopen 계열 함수가 설정한 errno로 전달한다.
 * 성공한 FILE 포인터의 fclose 책임은 이 함수를 호출한 쪽에 있다. */
FILE *bb_platform_fopen(const char *path, const char *mode)
{
    if (!path || !path[0] || !mode || !mode[0]) { errno = EINVAL; return NULL; }
#if defined(_WIN32)
    /* 경로뿐 아니라 열기 모드도 넓은 문자열이어야 _wfopen에 전달할 수 있다. */
    wchar_t wide_path[BB_PATH_CAP], wide_mode[BB_PATH_CAP];
    if (!to_wide(path, wide_path) || !to_wide(mode, wide_mode)) { errno = EINVAL; return NULL; }
    return _wfopen(wide_path, wide_mode);
#else
    return fopen(path, mode);
#endif
}

/* 생성에 실패한 임시 파일 등을 지운다. Windows에서도 UTF-8 파일명을 그대로
 * 받으며, 파일이 없거나 삭제 권한이 없을 때는 false를 반환한다. */
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

/* Windows에서 따로 복사한 인수만 해제한다. 다른 운영체제에서 빌려온 argv는
 * C 런타임이 소유하므로 해제하지 않는다. 마지막에는 구조체를 비워 재해제를 막는다. */
void bb_platform_free_arguments(BBArguments *arguments)
{
    int i;

    if (arguments->owned && arguments->values) {
        /* 변환 도중 실패해 비어 있는 칸은 NULL이며 free(NULL)은 안전하다. */
        for (i = 0; i < arguments->count; ++i) free(arguments->values[i]);
        free(arguments->values);
    }
    memset(arguments, 0, sizeof *arguments);
}

/* 프로그램 인수를 UTF-8 문자열 배열로 준비한다. Windows에서는 시스템 원본
 * 명령줄을 다시 분석해 한글 경로를 보존하고, 다른 환경에서는 argv를 빌려 쓴다.
 * 반환 구조체의 owned 값이 나중에 메모리를 해제할지를 결정한다. */
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
    /* 끝의 NULL 한 칸을 포함해 0으로 초기화한다. 부분 실패 시에도 어떤 문자열이
     * 할당되었는지 별도 배열 없이 안전하게 정리할 수 있다. */
    out->values = calloc((size_t)count + 1, sizeof *out->values);
    out->count = count;
    out->owned = true;
    if (!out->values) { LocalFree(wide); return false; }
    ok = true;
    for (i = 0; i < count; ++i) {
        /* 출력 버퍼 없이 먼저 필요한 바이트 수를 묻는다. 종료 문자를 포함한
         * 실제 크기만 할당하고, 지나치게 긴 인수는 게임의 경로 한도에 맞춰 거부한다. */
        size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide[i], -1, NULL, 0, NULL, NULL);
        if (size <= 0 || size > BB_PATH_CAP) { ok = false; break; }
        out->values[i] = malloc((size_t)size);
        if (!out->values[i] || !from_wide(wide[i], out->values[i], (size_t)size)) { ok = false; break; }
    }
    /* CommandLineToArgvW의 메모리는 malloc 계열이 아니므로 LocalFree로 돌려준다.
     * UTF-8 복사본을 만들다 실패했다면 이미 만든 복사본도 함께 해제한다. */
    LocalFree(wide);
    if (!ok) { bb_platform_free_arguments(out); return false; }
#endif
    return true;
}

/* 실행 파일 위치에서 assets 경로를 만든다. 터미널·IDE·Finder처럼 시작한
 * 작업 폴더가 달라도 설치된 실행 파일 옆의 동일한 에셋을 찾을 수 있다.
 * 경로 조회 또는 버퍼 크기 검증이 실패하면 불완전한 경로로 계속하지 않는다. */
bool bb_platform_asset_dir(char *out, size_t capacity)
{
    /* 작업 디렉터리와 무관하게 실행 파일 옆의 assets 디렉터리를 찾는다. */
    char executable[BB_PATH_CAP];
    char *separator;
#if defined(_WIN32)
    wchar_t wide_path[BB_PATH_CAP];
    DWORD n;
    char *backslash;

    /* n이 버퍼 크기 이상이면 경로가 잘린 것이므로 UTF-8로 변환하지 않는다. */
    n = GetModuleFileNameW(NULL, wide_path, BB_PATH_CAP);
    if (n == 0 || n >= BB_PATH_CAP || !from_wide(wide_path, executable, sizeof executable)) return false;
#elif defined(__APPLE__)
    uint32_t n;

    /* macOS API는 필요한 버퍼 크기를 n에 돌려준다. 고정 버퍼에 담지 못하면 실패한다. */
    n = (uint32_t)sizeof executable;
    if (_NSGetExecutablePath(executable, &n) != 0) return false;
#else
    ssize_t n;

    /* readlink는 문자열 종료 문자를 붙이지 않는다. 한 바이트를 남겨 두고 직접 닫는다. */
    n = readlink("/proc/self/exe", executable, sizeof executable - 1);
    if (n < 0 || (size_t)n >= sizeof executable - 1) return false;
    executable[n] = '\0';
#endif
    /* 마지막 구분자 뒤의 실행 파일명만 제거한다. Windows는 두 종류의 구분자 중
     * 더 뒤에 있는 것을 선택해 슬래시와 역슬래시가 섞인 경로도 처리한다. */
    separator = strrchr(executable, '/');
#if defined(_WIN32)
    backslash = strrchr(executable, '\\');
    if (backslash && (!separator || backslash > separator)) separator = backslash;
#endif
    if (!separator) return false;
    *separator = '\0';
    return bb_path_join(out, capacity, executable, "assets");
}

/* 한 단계의 디렉터리를 만들며, 이미 존재한다면 실제 디렉터리인지 확인한다.
 * 이름이 같은 일반 파일을 성공으로 처리하면 이후 점수 저장 오류를 숨기게 된다. */
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
    /* Unix에서는 새 디렉터리를 소유자만 읽고 쓰고 탐색할 수 있도록 만든다. */
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
#endif
}

/* 운영체제의 사용자별 데이터 위치에 게임 전용 폴더를 만들고 Scores.txt 경로를
 * 돌려준다. 쓰기 금지일 수 있는 실행 파일 폴더에 사용자 기록을 저장하지 않는다.
 * 실패 시 호출자가 빈 경로로 구분할 수 있도록 출력 첫 글자를 먼저 지운다. */
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
    /* Windows는 LOCALAPPDATA 원본 유니코드 값을 읽어 사용자 이름의 문자를 보존한다. */
    n = GetEnvironmentVariableW(L"LOCALAPPDATA", wide_parent, BB_PATH_CAP);
    if (n == 0 || n >= BB_PATH_CAP || !from_wide(wide_parent, parent, sizeof parent)) return false;
#else
    /* Unix 계열의 HOME은 읽기만 한다. 환경 변수 메모리를 수정하지 않고 복사한다. */
    home_dir = getenv("HOME");
    if (!home_dir || !home_dir[0]) return false;
    length = strlen(home_dir);
    if (length >= sizeof parent) return false;
    memcpy(parent, home_dir, length + 1);
#if defined(__APPLE__)
    /* macOS 저장 관례에 따라 Library/Application Support를 한 단계씩 준비한다. */
    if (!bb_path_join(directory, sizeof directory, parent, "Library") || !make_directory(directory)) return false;
    if (!bb_path_join(parent, sizeof parent, directory, "Application Support") || !make_directory(parent)) return false;
#endif
#endif
    if (!bb_path_join(directory, sizeof directory, parent, "BubbleBobble-C11") || !make_directory(directory)) return false;
    return bb_path_join(out, capacity, directory, "Scores.txt");
}

/* 메모리나 파일에서 받은 기록을 공통 규칙으로 검증한다. 라운드는 게임에서
 * 사용하는 범위여야 하며, 이름은 정확히 세 글자와 종료 문자 한 글자다. */
static bool valid_score(BbScore score)
{
    int i;
    if (score.round < 1 || score.round > BB_SCORE_MAX_ROUND || score.name[3] != '\0') return false;
    /* UI에서 선택할 수 있는 대문자·숫자·마침표만 허용해 파일 형식도 단순하게 유지한다. */
    for (i = 0; i < 3; ++i)
        if (!((score.name[i] >= 'A' && score.name[i] <= 'Z') ||
              (score.name[i] >= '0' && score.name[i] <= '9') || score.name[i] == '.')) return false;
    return true;
}

/* 고정 길이 순위표에 기록 하나를 끼워 넣는다. 점수가 높을수록 앞에 놓고,
 * 같은 점수라면 더 높은 라운드가 앞선다. 둘 다 같으면 기존 기록의 순서를 유지한다.
 * 새 기록이 표의 최대 개수 밖으로 밀리면 저장하지 않아 배열 범위를 넘지 않는다. */
void bb_scores_insert(BbScore scores[BB_SCORE_COUNT], size_t *count, BbScore score)
{
    size_t length, position, i;
    if (!scores || !count || !valid_score(score)) return;
    /* 호출자가 준 개수가 커도 실제 배열 용량까지만 접근한다. */
    length = *count < BB_SCORE_COUNT ? *count : BB_SCORE_COUNT;
    *count = length;
    position = 0;
    /* 현재 기록보다 앞설 기존 항목을 건너뛰어 삽입 위치를 찾는다. */
    while (position < length && (scores[position].score > score.score ||
           (scores[position].score == score.score && scores[position].round >= score.round))) ++position;
    if (position >= BB_SCORE_COUNT) return;
    if (length < BB_SCORE_COUNT) ++length;
    /* 뒤쪽부터 한 칸씩 옮겨 아직 복사하지 않은 기록을 덮어쓰지 않는다.
     * 표가 이미 가득 찼다면 마지막 기록이 자연스럽게 빠진다. */
    for (i = length - 1; i > position; --i) scores[i] = scores[i - 1];
    scores[position] = score;
    *count = length;
}

/* 한 줄의 "점수 라운드 이름" 형식을 파싱한다. strtoul은 부호와 공백도 받을 수
 * 있으므로 첫 글자를 직접 확인해 이 파일 형식에서 허용한 십진수만 읽는다.
 * 숫자의 넘침, 구분자, 이름 길이까지 확인한 기록만 호출자에게 유효하다고 알린다. */
static bool parse_score(const char *line, BbScore *record)
{
    char *end;
    const char *cursor = line;
    unsigned long score, round;
    if (*cursor < '0' || *cursor > '9') return false;
    /* 이전 함수의 errno를 지운 뒤 변환해야 이번 숫자에서 생긴 범위 오류를 구분한다. */
    errno = 0;
    score = strtoul(cursor, &end, 10);
    if (errno || score > UINT_MAX || *end != ' ') return false;
    /* 점수 다음에는 정확히 공백 하나와 라운드 숫자가 이어져야 한다. */
    cursor = end + 1;
    if (*cursor < '0' || *cursor > '9') return false;
    errno = 0;
    round = strtoul(cursor, &end, 10);
    if (errno || round < 1 || round > BB_SCORE_MAX_ROUND || *end != ' ') return false;
    cursor = end + 1;
    /* 남은 문자열 전체가 세 글자여야 하므로 뒤의 불필요한 토큰도 함께 거부한다. */
    if (strlen(cursor) != 3) return false;
    record->score = (unsigned)score;
    record->round = (int)round;
    memcpy(record->name, cursor, sizeof record->name);
    return valid_score(*record);
}

/* 점수 파일 전체를 임시 배열로 읽고 성공했을 때만 호출자의 순위표에 반영한다.
 * 최초 실행으로 파일이 없는 경우는 정상적인 빈 순위표이며, 그 밖의 열기 실패는
 * 오류다. 잘못된 개별 행은 건너뛰되 전체 읽기 제한과 I/O 오류는 실패로 처리한다. */
bool bb_scores_load(const char *path, BbScore scores[BB_SCORE_COUNT], size_t *out_count)
{
    FILE *file;
    /* 읽는 도중 오류가 나도 기존 배열을 부분적으로 덮어쓰지 않기 위한 작업 공간이다. */
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
        /* 줄바꿈 또는 파일 끝에서 한 줄을 완성한다. 마지막 줄에 개행이 없어도
         * 읽어야 하며, 파일 끝 자체를 빈 기록 하나로 세지는 않는다. */
        if (next == '\n' || next == EOF) {
            if (next == '\n' || length > 0 || bad_line) {
                if (++lines > 512) { ok = false; break; }
                /* Windows의 CRLF에서 CR을 제거한 뒤, 플랫폼에 관계없이 같은 형식으로 읽는다. */
                if (length > 0 && line[length - 1] == '\r') --length;
                line[length] = '\0';
                if (!bad_line && parse_score(line, &record)) bb_scores_insert(loaded, &count, record);
            }
            if (next == EOF) break;
            length = 0;
            bad_line = false;
        /* 중간 NUL이나 버퍼보다 긴 행은 끝까지 소비하되 그 행 전체를 버린다. */
        } else if (next == 0 || length >= sizeof line - 1) {
            bad_line = true;
        } else {
            line[length++] = (char)next;
        }
    }
    /* EOF와 읽기 오류를 구분하고 파일 닫기 실패도 호출자에게 전달한다. */
    if (ferror(file)) ok = false;
    if (fclose(file) != 0) ok = false;
    if (ok) {
        memcpy(scores, loaded, sizeof loaded);
        *out_count = count;
    }
    return ok;
}

/* 모든 기록을 검사한 뒤 같은 디렉터리의 임시 파일에 완성하고 원본을 교체한다.
 * 쓰기 도중 실패해 기존 순위표가 빈 파일이나 반쪽 파일로 바뀌는 것을 피한다.
 * 성공은 기록 쓰기·닫기·교체가 모두 성공했다는 뜻이다. */
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
    /* 잘못된 기록은 파일 생성 전에 거부해 기존 저장 파일을 건드리지 않는다. */
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
    /* 후보 32개가 모두 사용 중이면 제한 없이 반복하지 않고 실패로 반환한다. */
    if (!file) return false;
    for (i = 0; i < count; ++i)
        if (fprintf(file, "%u %d %.3s\n", scores[i].score, scores[i].round, scores[i].name) < 0) ok = false;
    /* 버퍼에 남은 출력은 fclose 때 기록되므로 닫기 결과까지 확인한 후 교체한다. */
    if (fclose(file) != 0) ok = false;
#if defined(_WIN32)
    /* Windows는 기존 파일 교체를 허용하고, 디스크 쓰기 완료를 기다리는 옵션을 쓴다. */
    if (ok) ok = to_wide(temporary, wide_temp) && to_wide(path, wide_path);
    if (ok) ok = MoveFileExW(wide_temp, wide_path, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    /* 임시 파일을 원본과 같은 디렉터리에 두어 같은 파일시스템 안에서 이름을 바꾼다. */
    if (ok) ok = rename(temporary, path) == 0;
#endif
    /* 실패한 임시 파일만 지운다. 교체 전 실패했다면 기존 점수 파일은 그대로 남는다. */
    if (!ok) bb_platform_remove(temporary);
    return ok;
}
