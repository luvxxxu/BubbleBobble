#include "bb_platform.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
/* Windows의 디렉터리 생성/삭제는 UTF-16 API로 검사한다. 다른 운영체제는 POSIX API를 쓴다. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

/* 시험이 만든 파일을 플랫폼 래퍼로 삭제한다. 삭제 실패도 검증 실패로 처리해
 * 다음 실행에 오래된 파일이 남아 결과를 바꾸는 일을 막는다. */
static void remove_test_file(const char *path)
{
    assert(bb_platform_remove(path));
}

/* 같은 경로에 짧은 텍스트를 쓰고 다시 읽어 내용과 EOF를 확인한다.
 * 바이너리 모드로 열어 운영체제의 줄바꿈 변환을 배제하고 실제 경로 처리를 검사한다. */
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

/* 순위표를 정상적으로 읽을 수 있는 경우에 사용한다. 출력 개수를 99로 채워
 * 로더가 실제 결과를 써 주었는지 확인할 수 있게 한 뒤 반환한다. */
static size_t load_count(const char *path, BbScore scores[BB_SCORE_COUNT])
{
    size_t count = 99;
    assert(bb_scores_load(path, scores, &count));
    return count;
}

/* 잘못된 입력을 포함한 시험용 순위표를 그대로 기록한다.
 * 파일 생성/쓰기/닫기의 실패가 파서 검사 실패로 잘못 해석되지 않게 각각 확인한다. */
static void write_text(const char *path, const char *text)
{
    FILE *file = bb_platform_fopen(path, "wb");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

/* 상위 10개 제한, 점수와 라운드의 정렬, 동점의 안정적인 순서, 저장/복원을 확인한다.
 * 잘못된 기록은 거절하고 기존 파일은 유지해야 하며 빈 순위표나 없는 파일도 처리한다. */
static void score_order_and_round_trip(const char *path)
{
    BbScore scores[BB_SCORE_COUNT] = {0};
    BbScore read_back[BB_SCORE_COUNT] = {0};
    BbScore record = {0, 1, "AAA"};
    size_t count = 0, i;
    /* 정원보다 많은 기록을 넣어 낮은 점수 다섯 개가 제거되는지 확인한다. */
    for (i = 0; i < 15; ++i) {
        record.score = (unsigned)i * 100;
        bb_scores_insert(scores, &count, record);
    }
    assert(count == BB_SCORE_COUNT);
    assert(scores[0].score == 1400 && scores[9].score == 500);
    /* 점수가 같으면 더 높은 라운드가 앞서고, 점수와 라운드까지 같으면 기존 순서를 유지한다. */
    record.score = 1400;
    record.round = 5;
    memcpy(record.name, "BBB", 4);
    bb_scores_insert(scores, &count, record);
    assert(scores[0].round == 5 && scores[1].round == 1);
    memcpy(record.name, "CCC", 4);
    bb_scores_insert(scores, &count, record);
    assert(strcmp(scores[0].name, "BBB") == 0 && strcmp(scores[1].name, "CCC") == 0);
    /* 허용된 점수의 최댓값도 잘못된 overflow로 거절하지 않고 왕복 저장할 수 있어야 한다. */
    record.score = UINT_MAX;
    memcpy(record.name, "A1.", 4);
    bb_scores_insert(scores, &count, record);
    assert(scores[0].score == UINT_MAX);
    assert(bb_scores_save(path, scores, count));
    assert(load_count(path, read_back) == count);
    for (i = 0; i < count; ++i) {
        assert(read_back[i].score == scores[i].score);
        assert(read_back[i].round == scores[i].round);
        assert(strcmp(read_back[i].name, scores[i].name) == 0);
    }
    /* 라운드 1..5, 이름 형식, 배열 크기, NULL 인수의 검증을 각각 확인한다. */
    record.round = 6;
    bb_scores_insert(scores, &count, record);
    assert(count == BB_SCORE_COUNT && scores[0].round == 5);
    assert(!bb_scores_save(path, &record, 1));
    record.round = 0;
    assert(!bb_scores_save(path, &record, 1));
    record.round = 1;
    memcpy(record.name, "aAA", 4);
    assert(!bb_scores_save(path, &record, 1));
    assert(!bb_scores_save(path, scores, BB_SCORE_COUNT + 1));
    assert(!bb_scores_save(path, NULL, 1));
    /* 거부한 저장은 기존 순위표를 바꾸지 않는다. */
    assert(load_count(path, read_back) == BB_SCORE_COUNT && read_back[0].score == UINT_MAX);
    /* 기록이 0개면 NULL 배열은 정상 입력이며 파일 삭제 후에도 빈 순위표로 읽는다. */
    assert(bb_scores_save(path, NULL, 0));
    assert(load_count(path, read_back) == 0);
    remove_test_file(path);
    assert(load_count(path, read_back) == 0);
}

/* 숫자 넘침, 잘못된 이름/라운드, 여분 문자, NUL, 너무 긴 줄을 섞어
 * 유효한 기록만 읽는지 검사한다. 전체 줄 수와 파일 크기 한계는 실패와 빈 결과로 반환해야 한다. */
static void malformed_scores(const char *path)
{
    BbScore scores[BB_SCORE_COUNT] = {0};
    FILE *file;
    size_t count;
    int i;
    /* 앞부분은 모두 불량 행이다. 끝의 정상 세 행만 정렬되어 남아야 한다.
     * CRLF와 마지막 줄바꿈 없는 정상 행도 함께 검사한다. */
    write_text(path, "-1 1 BAD\n999999999999999999999999 1 BAD\n100 999 BAD\n"
               "12x 1 BAD\n12 1 A\n12 1 LONG\n12 1 aAA\n12 0 BAD\n12 6 BAD\n"
               "12 999999999999999999999999 BAD\n12 1 AAA\rBAD\n12 1 AAA extra\n"
               "200 2 OKA\r\n100 1 OKB\n200 5 OKC");
    assert(load_count(path, scores) == 3);
    assert(scores[0].score == 200 && scores[0].round == 5);
    assert(strcmp(scores[1].name, "OKA") == 0 && strcmp(scores[2].name, "OKB") == 0);
    file = bb_platform_fopen(path, "wb");
    assert(file);
    /* NUL로 끊긴 것처럼 보이는 행과 과도하게 긴 행은 전체를 버린 뒤 다음 행을 읽어야 한다. */
    assert(fputs("999 5 BAD", file) >= 0);
    assert(fputc(0, file) != EOF);
    assert(fputs("\n", file) >= 0);
    for (i = 0; i < 300; ++i) assert(fputc('X', file) != EOF);
    assert(fputs("\n900 5 AAA\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(load_count(path, scores) == 1 && scores[0].score == 900);
    file = bb_platform_fopen(path, "wb");
    assert(file);
    /* 허용 줄 수의 경계까지는 성공하고 한 줄을 더 추가하면 전체 읽기가 실패해야 한다. */
    for (i = 0; i < 512; ++i) assert(fputc('\n', file) != EOF);
    assert(fclose(file) == 0);
    assert(load_count(path, scores) == 0);
    file = bb_platform_fopen(path, "ab");
    assert(file);
    assert(fputs("900 5 AAA\n", file) >= 0);
    assert(fclose(file) == 0);
    count = 99;
    assert(!bb_scores_load(path, scores, &count) && count == 0);
    file = bb_platform_fopen(path, "wb");
    assert(file);
    /* 파일 크기 64KiB 제한을 한 바이트 초과하여 크기 검사 경계도 확인한다. */
    for (i = 0; i < 65537; ++i) assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
    count = 99;
    assert(!bb_scores_load(path, scores, &count) && count == 0);
    remove_test_file(path);
}

/* 임시 파일 이름 충돌과 최종 파일 교체 실패를 의도적으로 만든다.
 * 기존 데이터/다른 파일을 보존하고 실패 후 자신이 만든 임시 파일만 정리하는지 확인한다. */
static void failed_saves_preserve_data(const char *path)
{
    BbScore record = {900, 5, "AAA"};
    BbScore scores[BB_SCORE_COUNT] = {0};
    char temporary[BB_PATH_CAP], directory[BB_PATH_CAP], child[BB_PATH_CAP];
    FILE *file;
    char contents[32];
    int n;
    assert(bb_scores_save(path, &record, 1));
    n = snprintf(temporary, sizeof temporary, "%s.tmp.0", path);
    assert(n > 0 && (size_t)n < sizeof temporary);
    /* 저장 함수가 고를 첫 임시 이름을 선점한다. 저장에 성공해도 이 파일은 그대로 남아야 한다. */
    write_text(temporary, "KEEP THIS FILE\n");
    record.score = 1000;
    assert(bb_scores_save(path, &record, 1));
    file = bb_platform_fopen(temporary, "rb");
    assert(file && fgets(contents, sizeof contents, file));
    assert(strcmp(contents, "KEEP THIS FILE\n") == 0);
    assert(fclose(file) == 0);
    remove_test_file(temporary);
    /* 일반 파일 아래를 하위 경로처럼 사용하면 저장 실패이며 원래 순위표는 유지되어야 한다. */
    assert(bb_path_join(child, sizeof child, path, "no-parent"));
    assert(!bb_scores_save(child, &record, 1));
    assert(load_count(path, scores) == 1 && scores[0].score == 1000);
    /* 목적지를 실제 디렉터리로 만들어 최종 파일 이름 변경 실패 경로를 실행한다. */
    n = snprintf(directory, sizeof directory, "%s-dir", path);
    assert(n > 0 && (size_t)n < sizeof directory);
#if defined(_WIN32)
    {
        wchar_t wide[BB_PATH_CAP];
        assert(MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, directory, -1, wide, BB_PATH_CAP) > 0);
        assert(_wmkdir(wide) == 0);
        assert(!bb_scores_save(directory, &record, 1));
        assert(_wrmdir(wide) == 0);
    }
#else
    assert(mkdir(directory, 0700) == 0);
    assert(!bb_scores_save(directory, &record, 1));
    assert(rmdir(directory) == 0);
#endif
    /* 이름 변경 실패 뒤에는 직접 만든 임시 파일도 남기지 않는다. */
    n = snprintf(temporary, sizeof temporary, "%s.tmp.0", directory);
    assert(n > 0 && (size_t)n < sizeof temporary);
    file = bb_platform_fopen(temporary, "rb");
    assert(file == NULL);
    remove_test_file(path);
}

/* CTest가 전달한 시험 경로에서 경로 합치기, 파일 입출력, 점수 저장과
 * 잘못된 인수를 검사한다. 한글 경로도 별도로 사용하여 Windows UTF-8 변환을 검증한다. */
int main(int argc, char **argv)
{
    BBArguments arguments;
    assert(bb_platform_arguments(argc, argv, &arguments));
    assert(arguments.count == 2);
    const char *path = arguments.values[1];

    /* 너무 작은 버퍼는 실패해야 하며, 정상 경로에는 구분자가 한 번 들어가야 한다. */
    char small[4], joined[32], assets[BB_PATH_CAP];
    assert(!bb_path_join(small, sizeof small, "too", "long"));
    assert(bb_path_join(joined, sizeof joined, "base", "asset.png"));
    assert(strcmp(joined, "base/asset.png") == 0);
    assert(bb_platform_asset_dir(assets, sizeof assets));
    assert(strstr(assets, "assets") != NULL);
    assert(!bb_platform_asset_dir(small, sizeof small));

    check_file_round_trip(path);
    score_order_and_round_trip(path);
    malformed_scores(path);
    failed_saves_preserve_data(path);
    /* Windows의 파일 열기 래퍼도 UTF-8 이름을 처리해야 한다. */
    char unicode_path[BB_PATH_CAP];
    int n = snprintf(unicode_path, sizeof unicode_path,
                     "%s-\xed\x95\x9c\xea\xb8\x80.txt", path);
    assert(n > 0 && (size_t)n < sizeof unicode_path);
    check_file_round_trip(unicode_path);
    score_order_and_round_trip(unicode_path);

    /* 디렉터리/빈 이름/NULL은 유효한 파일 인수가 아니다. 실패 결과 개수도 0으로 초기화한다. */
    BbScore scores[BB_SCORE_COUNT] = {0};
    size_t failed_count = 99;
    assert(!bb_scores_load(".", scores, &failed_count) && failed_count == 0);
    assert(!bb_scores_save("", scores, 0));
    assert(!bb_platform_score_path(NULL, 0));
    assert(bb_platform_fopen(NULL, "rb") == NULL && errno == EINVAL);
#if defined(_WIN32)
    /* 잘못된 UTF-8을 없는 파일(ENOENT)로 오인하면 저장을 허용할 수 있다. */
    errno = ENOENT;
    assert(!bb_scores_load("\xff", scores, &failed_count) && failed_count == 0);
#endif

    /* Windows에서는 변환된 인수가 할당될 수 있다. 해제 뒤 구조체도 빈 상태가 되어야 한다. */
    bb_platform_free_arguments(&arguments);
    assert(arguments.count == 0 && arguments.values == NULL);
    puts("Platform tests passed: UTF-8 paths, arguments, top 10 scores, rounds 1..5, malformed input, safe save failures.");
    return 0;
}
