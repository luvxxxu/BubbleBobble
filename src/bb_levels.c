#include "bb_levels.h"

#include <stddef.h>
#include <string.h>

typedef struct BBPlatformSpan { int x, y, width; } BBPlatformSpan;

/* 발판 높이 차이는 기본 점프로 오를 수 있는 네 칸이다.
 * 연속 발판의 수평 구간이 겹쳐 가속 없이도 다음 발판에 착지할 수 있다. */
static const BBPlatformSpan layouts[BB_LEVEL_COUNT][BB_LEVEL_PLATFORM_COUNT] = {
    {{3, 22, 10}, {9, 18, 10}, {17, 14, 10}, {10, 10, 10}, {4, 6, 10}},
    {{19, 22, 10}, {13, 18, 10}, {5, 14, 10}, {12, 10, 10}, {18, 6, 10}},
    {{10, 22, 12}, {4, 18, 10}, {11, 14, 10}, {18, 10, 10}, {10, 6, 12}},
    {{4, 22, 10}, {11, 18, 10}, {18, 14, 10}, {11, 10, 10}, {4, 6, 10}},
    {{18, 22, 10}, {11, 18, 10}, {4, 14, 10}, {11, 10, 10}, {18, 6, 10}}
};

void bb_levels_build(uint8_t *maps)
{
    int level;
    int x;
    int y;
    int i;
    uint8_t *map;
    const BBPlatformSpan *span;

    if (maps == NULL) return;
    memset(maps, 0, BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH);
    for (level = 0; level < BB_LEVEL_COUNT; ++level) {
        map = maps + level * BB_MAP_HEIGHT * BB_MAP_WIDTH;
        for (y = 2; y < BB_MAP_HEIGHT; ++y) {
            map[y * BB_MAP_WIDTH] = 3;
            map[y * BB_MAP_WIDTH + 1] = 3;
            map[y * BB_MAP_WIDTH + 30] = 3;
            map[y * BB_MAP_WIDTH + 31] = 3;
        }
        /* 바닥 중앙 네 칸을 비워 낙하 후 상단으로 순환하는 경로를 만든다. */
        for (x = 2; x < BB_MAP_WIDTH - 2; ++x) {
            if (x >= 14 && x < 18) continue;
            map[26 * BB_MAP_WIDTH + x] = 2;
            map[27 * BB_MAP_WIDTH + x] = 1;
        }
        for (i = 0; i < BB_LEVEL_PLATFORM_COUNT; ++i) {
            span = &layouts[level][i];
            for (x = span->x; x < span->x + span->width; ++x)
                map[span->y * BB_MAP_WIDTH + x] = 2;
        }
    }
}
