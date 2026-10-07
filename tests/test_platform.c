#include "bb_platform.h"

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <direct.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static void remove_test_file(const char *path)
{
    assert(bb_platform_remove(path));
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

static size_t load_count(const char *path, BbScore scores[BB_SCORE_COUNT])
{
    size_t count = 99;
    assert(bb_scores_load(path, scores, &count));
    return count;
}

static void write_text(const char *path, const char *text)
{
    FILE *file = bb_platform_fopen(path, "wb");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void score_order_and_round_trip(const char *path)
{
    BbScore scores[BB_SCORE_COUNT] = {0};
    BbScore read_back[BB_SCORE_COUNT] = {0};
    BbScore record = {0, 1, "AAA"};
    size_t count = 0, i;
    for (i = 0; i < 15; ++i) {
        record.score = (unsigned)i * 100;
        bb_scores_insert(scores, &count, record);
    }
    assert(count == BB_SCORE_COUNT);
    assert(scores[0].score == 1400 && scores[9].score == 500);
    record.score = 1400;
    record.round = 5;
    memcpy(record.name, "BBB", 4);
    bb_scores_insert(scores, &count, record);
    assert(scores[0].round == 5 && scores[1].round == 1);
    memcpy(record.name, "CCC", 4);
    bb_scores_insert(scores, &count, record);
    assert(strcmp(scores[0].name, "BBB") == 0 && strcmp(scores[1].name, "CCC") == 0);
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
    assert(bb_scores_save(path, NULL, 0));
    assert(load_count(path, read_back) == 0);
    remove_test_file(path);
    assert(load_count(path, read_back) == 0);
}

static void malformed_scores(const char *path)
{
    BbScore scores[BB_SCORE_COUNT] = {0};
    FILE *file;
    size_t count;
    int i;
    write_text(path, "-1 1 BAD\n999999999999999999999999 1 BAD\n100 999 BAD\n"
               "12x 1 BAD\n12 1 A\n12 1 LONG\n12 1 aAA\n12 0 BAD\n12 6 BAD\n"
               "12 999999999999999999999999 BAD\n12 1 AAA\rBAD\n12 1 AAA extra\n"
               "200 2 OKA\r\n100 1 OKB\n200 5 OKC");
    assert(load_count(path, scores) == 3);
    assert(scores[0].score == 200 && scores[0].round == 5);
    assert(strcmp(scores[1].name, "OKA") == 0 && strcmp(scores[2].name, "OKB") == 0);
    file = bb_platform_fopen(path, "wb");
    assert(file);
    assert(fputs("999 5 BAD", file) >= 0);
    assert(fputc(0, file) != EOF);
    assert(fputs("\n", file) >= 0);
    for (i = 0; i < 300; ++i) assert(fputc('X', file) != EOF);
    assert(fputs("\n900 5 AAA\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(load_count(path, scores) == 1 && scores[0].score == 900);
    file = bb_platform_fopen(path, "wb");
    assert(file);
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
    for (i = 0; i < 65537; ++i) assert(fputc('X', file) != EOF);
    assert(fclose(file) == 0);
    count = 99;
    assert(!bb_scores_load(path, scores, &count) && count == 0);
    remove_test_file(path);
}

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
    write_text(temporary, "KEEP THIS FILE\n");
    record.score = 1000;
    assert(bb_scores_save(path, &record, 1));
    file = bb_platform_fopen(temporary, "rb");
    assert(file && fgets(contents, sizeof contents, file));
    assert(strcmp(contents, "KEEP THIS FILE\n") == 0);
    assert(fclose(file) == 0);
    remove_test_file(temporary);
    assert(bb_path_join(child, sizeof child, path, "no-parent"));
    assert(!bb_scores_save(child, &record, 1));
    assert(load_count(path, scores) == 1 && scores[0].score == 1000);
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

    bb_platform_free_arguments(&arguments);
    assert(arguments.count == 0 && arguments.values == NULL);
    puts("Platform tests passed: UTF-8 paths, arguments, top 10 scores, rounds 1..5, malformed input, safe save failures.");
    return 0;
}
