#include "bb_levels.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_layouts_and_jumps(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {{0}};
    int left[BB_MAP_HEIGHT];
    int right[BB_MAP_HEIGHT];
    int count;
    int lower_y;
    int lower_left;
    int lower_right;
    int overlap_left;
    int overlap_right;
    int landing_ticks;
    float jump_x;

    bb_levels_build(&maps[0][0][0]);
    for (int level = 0; level < BB_LEVEL_COUNT; ++level) {
        count = 0;
        /* 각 y줄의 발판은 하나의 연속 구간이어야 도달성 계산이 유효하다. */
        for (int y = 0; y < BB_MAP_HEIGHT; ++y) {
            left[y] = right[y] = -1;
            for (int x = 0; x < BB_MAP_WIDTH; ++x) {
                assert(maps[level][y][x] <= 3);
                if (y < 26 && maps[level][y][x] == 2) {
                    if (left[y] == -1) { left[y] = x; ++count; }
                    else assert(right[y] == x - 1);
                    right[y] = x;
                }
            }
        }
        assert(count == 5);
        for (int other = 0; other < level; ++other)
            assert(memcmp(maps[level], maps[other], sizeof maps[level]) != 0);
        for (int x = 14; x < 18; ++x) {
            assert(maps[level][26][x] == 0);
            assert(maps[level][27][x] == 0);
        }

        /* Every next ledge is reachable at 1x speed with a normal jump,
         * including each round's first ledge from the floor. */
        lower_y = 26;
        lower_left = 2;
        lower_right = 29;
        for (int y = 25; y >= 0; --y) {
            if (left[y] < 0) continue;
            overlap_left = left[y] > lower_left ? left[y] : lower_left;
            overlap_right = right[y] < lower_right ? right[y] : lower_right;
            assert(overlap_right - overlap_left >= 1);
            jump_x = (float)overlap_left + 1.0f;
            /* 첫 점프의 출발점은 바닥 순환 구멍을 피한다. */
            if (lower_y == 26 && jump_x > 13.1f && jump_x < 18.9f)
                jump_x = 19.0f;
            bb_game_init(&game, &maps[0][0][0], 9);
            bb_game_start(&game, 2);
            bb_game_skip_intro(&game);
            game.level = level;
            memset(game.enemies, 0, sizeof game.enemies);
            game.enemies[0].active = true;
            game.enemies[0].state = BB_ENEMY_SPAWNING;
            game.enemies[0].spawn_delay = 1000000.0f;
            for (int p = 0; p < BB_MAX_PLAYERS; ++p) {
                game.players[p].body.x = jump_x;
                game.players[p].body.y = (float)lower_y - 0.975f;
                game.players[p].body.grounded = true;
                inputs[p].jump = true;
            }
            landing_ticks = 0;
            do {
                bb_game_update(&game, inputs, BB_FIXED_DT);
                inputs[0].jump = inputs[1].jump = false;
                ++landing_ticks;
            } while ((!game.players[0].body.grounded || !game.players[1].body.grounded) && landing_ticks < 240);
            assert(landing_ticks < 240);
            for (int p = 0; p < BB_MAX_PLAYERS; ++p)
                assert(fabsf(game.players[p].body.y - ((float)y - 0.975f)) < 0.001f);
            assert(game.state == BB_STATE_PLAY);
            lower_y = y;
            lower_left = left[y];
            lower_right = right[y];
        }
    }
}

int main(void)
{
    test_layouts_and_jumps();
    puts("Five distinct layouts and all 25 platform jumps passed for both players.");
    return 0;
}
