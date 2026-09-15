#include "bb_platform.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static size_t load_count(const char *path, BbScore scores[BB_SCORE_COUNT])
{
    size_t count = 0;
    assert(bb_scores_load(path, scores, &count));
    return count;
}

int main(int argc, char **argv)
{
    BBArguments arguments;
    assert(bb_platform_arguments(argc, argv, &arguments));
    assert(arguments.count == 2);
    const char *path = arguments.values[1];
    BbScore scores[BB_SCORE_COUNT] = {0};
    size_t count = 0;
    for (unsigned i = 0; i < 15; ++i)
        bb_scores_insert(scores, &count, (BbScore){i * 100, 1, "AAA"});
    assert(count == BB_SCORE_COUNT);
    assert(scores[0].score == 1400 && scores[9].score == 500);
    bb_scores_insert(scores, &count, (BbScore){1400, 3, "BBB"});
    assert(scores[0].round == 3 && strcmp(scores[0].name, "BBB") == 0);
    assert(bb_scores_save(path, scores, count));
    BbScore read_back[BB_SCORE_COUNT] = {0};
    assert(load_count(path, read_back) == count);
    for (size_t i = 0; i < count; ++i) {
        assert(read_back[i].score == scores[i].score);
        assert(read_back[i].round == scores[i].round);
        assert(strcmp(read_back[i].name, scores[i].name) == 0);
    }
    /* 음수, 오버플로, 잘린 행, 잘못된 행은 거부하되 같은 파일 뒤쪽의
       정상 항목은 잃지 않아야 한다. */
    FILE *file = bb_platform_fopen(path, "wb");
    assert(file);
    assert(fputs("-1 1 BAD\n999999999999999999999999 1 BAD\n100 999 BAD\n"
                 "12x 1 BAD\n12 1 A\n12 1 LONG\n12 1 aAA\n"
                 "200 2 OKA\n100 1 OKB\n200 3 OKC\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(load_count(path, read_back) == 3);
    assert(read_back[0].score == 200 && read_back[0].round == 3);
    assert(strcmp(read_back[2].name, "OKB") == 0);
    file = bb_platform_fopen(path, "wb");
    assert(file);
    for (int i = 0; i < 300; ++i) assert(fputc('X', file) != EOF);
    assert(fputs("\n900 3 AAA\n", file) >= 0);
    assert(fclose(file) == 0);
    assert(load_count(path, read_back) == 1 && read_back[0].score == 900);
    assert(!bb_scores_save(path, read_back, BB_SCORE_COUNT + 1));
    char small[4], joined[32], assets[BB_PATH_CAP];
    assert(!bb_path_join(small, sizeof small, "too", "long"));
    assert(bb_path_join(joined, sizeof joined, "base", "asset.png"));
    assert(strcmp(joined, "base/asset.png") == 0);
    assert(bb_platform_asset_dir(assets, sizeof assets));
    assert(strstr(assets, "assets"));
    assert(!bb_platform_asset_dir(small, sizeof small));
    assert(bb_platform_remove(path));
    char unicode_path[BB_PATH_CAP];
    int n = snprintf(unicode_path, sizeof unicode_path, "%s-\xec\xa0\x90\xec\x88\x98.txt", path);
    assert(n > 0 && (size_t)n < sizeof unicode_path);
    assert(bb_scores_save(unicode_path, read_back, 1));
    assert(load_count(unicode_path, scores) == 1 && scores[0].score == 900);
    assert(bb_platform_remove(unicode_path));
    assert(load_count(unicode_path, scores) == 0);
    size_t failed_count = 99;
    assert(!bb_scores_load(".", scores, &failed_count) && failed_count == 0);
    assert(!bb_scores_save("", read_back, 1));
    bb_platform_free_arguments(&arguments);
    puts("Platform tests passed: score sorting, bounds, malformed input, atomic save, paths.");
    return 0;
}
