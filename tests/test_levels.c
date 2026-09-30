#include "bb_levels.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void test_layout_and_jumps(void)
{
    uint8_t map[BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {{0}};
    int left[BB_MAP_HEIGHT];
    int right[BB_MAP_HEIGHT];
    int count = 0;
    int lower_y = 26;
    int lower_left = 2;
    int lower_right = 29;

    bb_levels_build(&map[0][0]);
    for (int y = 0; y < BB_MAP_HEIGHT; ++y) {
        left[y] = right[y] = -1;
        for (int x = 0; x < BB_MAP_WIDTH; ++x) {
            assert(map[y][x] <= 3);
            if (y < 26 && map[y][x] == 2) {
                if (left[y] == -1) { left[y] = x; ++count; }
                else assert(right[y] == x - 1);
                right[y] = x;
            }
        }
    }
    assert(count == BB_LEVEL_PLATFORM_COUNT);
    for (int x = 14; x < 18; ++x) {
        assert(map[26][x] == 0);
        assert(map[27][x] == 0);
    }

    /* 지상에서 시작해 다섯 발판을 기본 점프로 차례로 오를 수 있어야 한다. */
    bb_game_init(&game, &map[0][0], 9);
    bb_game_start(&game);
    bb_game_skip_intro(&game);
    assert(game.state == BB_STATE_PLAY);
    for (int y = 25; y >= 0; --y) {
        if (left[y] < 0) continue;
        int overlap_left = left[y] > lower_left ? left[y] : lower_left;
        int overlap_right = right[y] < lower_right ? right[y] : lower_right;
        assert(overlap_right - overlap_left >= 1);
        float jump_x = (float)overlap_left + 1.0f;
        if (lower_y == 26 && jump_x > 13.1f && jump_x < 18.9f)
            jump_x = 19.0f;
        game.players[0].body.x = jump_x;
        game.players[0].body.y = (float)lower_y - 0.975f;
        game.players[0].body.vx = 0.0f;
        game.players[0].body.vy = 0.0f;
        game.players[0].body.grounded = true;
        inputs[0].jump = true;
        int landing_ticks = 0;
        do {
            bb_game_update(&game, inputs, BB_FIXED_DT);
            bb_game_check_bump(&game);
            inputs[0].jump = false;
            ++landing_ticks;
        } while (!game.players[0].body.grounded && landing_ticks < 240);
        assert(landing_ticks < 240);
        assert(fabsf(game.players[0].body.y - ((float)y - 0.975f)) < 0.001f);
        assert(game.state == BB_STATE_PLAY);
        lower_y = y;
        lower_left = left[y];
        lower_right = right[y];
    }
}

int main(void)
{
    test_layout_and_jumps();
    puts("One layout and all five platform jumps passed.");
    return 0;
}
