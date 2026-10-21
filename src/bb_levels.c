/* 다섯 라운드의 원본 타일 맵을 만드는 파일이다.
 * 여기서는 정수 좌표로 초기 배치만 정의하고, 플레이 중 좌우 이동은
 * bb_game.c의 platform_offset과 bb_render.c의 그리기 좌표에서 처리한다. */
#include "bb_levels.h"

#include <stddef.h>
#include <string.h>

/* 발판 한 개를 왼쪽 끝 열 x, 윗면 행 y, 연속 타일 수 width로 표현한다.
 * 구조체 하나와 for문으로 한 줄을 만들 수 있어 타일 좌표를 일일이 나열하지 않는다. */
typedef struct BBPlatformSpan { int x, y, width; } BBPlatformSpan;

/* 발판 높이 차이는 기본 점프로 오를 수 있는 네 칸이다.
 * 연속 발판의 수평 구간이 겹쳐 가속 없이도 다음 발판에 착지할 수 있다.
 * 바깥 배열은 라운드, 안쪽 배열은 낮은 발판부터 높은 발판 순서다.
 * 모든 공중 발판이 함께 좌우 한 칸씩 움직여도 서로의 간격은 유지되고,
 * 가장 왼쪽/오른쪽 발판도 양옆 벽 안쪽(x=2..30)에 머문다. */
static const BBPlatformSpan layouts[BB_LEVEL_COUNT][BB_LEVEL_PLATFORM_COUNT] = {
    {{3, 22, 10}, {9, 18, 10}, {17, 14, 10}, {10, 10, 10}, {4, 6, 10}},
    {{19, 22, 10}, {13, 18, 10}, {5, 14, 10}, {12, 10, 10}, {18, 6, 10}},
    {{10, 22, 12}, {4, 18, 10}, {11, 14, 10}, {18, 10, 10}, {10, 6, 12}},
    {{4, 22, 10}, {11, 18, 10}, {18, 14, 10}, {11, 10, 10}, {4, 6, 10}},
    {{18, 22, 10}, {11, 18, 10}, {4, 14, 10}, {11, 10, 10}, {18, 6, 10}}
};

/* 호출자가 준비한 연속 버퍼에 모든 라운드를 채운다. 메모리를 새로 할당하지
 * 않으며 버퍼 크기는 BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트다.
 * 0=빈 공간, 1=충돌 없는 장식, 2=아래에서 통과할 수 있는 발판, 3=단단한 벽이다. */
void bb_levels_build(uint8_t *maps)
{
    int level;
    int x;
    int y;
    int i;
    uint8_t *map;
    const BBPlatformSpan *span;

    if (maps == NULL) return;
    /* 이전 데이터가 남지 않게 전체를 빈 공간으로 지운 다음 필요한 타일만 쓴다. */
    memset(maps, 0, BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH);
    for (level = 0; level < BB_LEVEL_COUNT; ++level) {
        /* 1차원 포인터를 현재 라운드의 첫 칸으로 옮긴다.
         * 해당 라운드 안의 (x,y)는 y * 가로크기 + x로 접근한다. */
        map = maps + level * BB_MAP_HEIGHT * BB_MAP_WIDTH;
        /* 맨 위 두 줄은 진입 공간으로 비우고, 좌우에 두 타일 두께의 벽을 만든다. */
        for (y = 2; y < BB_MAP_HEIGHT; ++y) {
            map[y * BB_MAP_WIDTH] = 3;
            map[y * BB_MAP_WIDTH + 1] = 3;
            map[y * BB_MAP_WIDTH + 30] = 3;
            map[y * BB_MAP_WIDTH + 31] = 3;
        }
        /* 바닥 중앙 네 칸을 비워 낙하 후 상단으로 순환하는 경로를 만든다. */
        for (x = 2; x < BB_MAP_WIDTH - 2; ++x) {
            if (x >= 14 && x < 18) continue;
            /* y=26은 실제 착지면, y=27은 그 아래 그림이다.
             * 이 두 줄은 이동하지 않아 시작 위치와 순환 구멍이 고정된다. */
            map[26 * BB_MAP_WIDTH + x] = 2;
            map[27 * BB_MAP_WIDTH + x] = 1;
        }
        for (i = 0; i < BB_LEVEL_PLATFORM_COUNT; ++i) {
            span = &layouts[level][i];
            /* 끝 열은 포함하지 않는 범위로 순회하여 정확히 width개를 채운다.
             * 원본 배열은 계속 보존하고 게임의 소수 좌표 이동량만 덧붙여 사용한다. */
            for (x = span->x; x < span->x + span->width; ++x)
                map[span->y * BB_MAP_WIDTH + x] = 2;
        }
    }
}
