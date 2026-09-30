#include "bb_game.h"

#include <stdio.h>
#include <stdlib.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#define FRAME_TIME (1.0f / 60.0f)

static float difference(float a, float b)
{
    return a > b ? a - b : b - a;
}

static void advance(BBGame *game, BBInput input, int frames)
{
    int frame;
    for (frame = 0; frame < frames; ++frame)
        bb_game_update(game, input, FRAME_TIME);
}

static int active_bubbles(const BBGame *game)
{
    int count = 0;
    int i;
    for (i = 0; i < BB_MAX_BUBBLES; ++i)
        if (game->bubbles[i].active) ++count;
    return count;
}

static void test_start_and_movement(void)
{
    BBGame game;
    BBInput input = {0};
    float start_x;

    bb_game_init(&game);
    CHECK(game.state == BB_STATE_MENU);
    bb_game_start(&game);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(game.player.lives == 3);
    CHECK(bb_game_enemies_left(&game) == BB_ENEMY_COUNT);

    /* 적과 부딪치지 않도록 이동 동작만 확인한다. */
    game.player.invincible = 100.0f;
    start_x = game.player.x;
    input.move = 1.0f;
    advance(&game, input, 20);
    CHECK(game.player.x > start_x + 1.0f);
    CHECK(game.player.facing == 1);

    input.move = -1.0f;
    advance(&game, input, 20);
    CHECK(difference(game.player.x, start_x) < 0.2f);
    CHECK(game.player.facing == -1);

    advance(&game, input, 120);
    CHECK(game.player.x >= 0.0f);
}

static void test_jump_to_first_platform(void)
{
    BBGame game;
    BBInput input = {0};

    bb_game_start(&game);
    game.player.x = 4.0f;
    game.player.y = bb_stage_platforms[game.stage][0].y - BB_PLAYER_SIZE;
    game.player.vy = 0.0f;
    game.player.grounded = true;
    game.player.invincible = 100.0f;

    input.jump = true;
    bb_game_update(&game, input, FRAME_TIME);
    CHECK(game.player.vy < 0.0f);
    CHECK(!game.player.grounded);

    input.jump = false;
    advance(&game, input, 65);
    CHECK(game.player.grounded);
    CHECK(difference(game.player.y + BB_PLAYER_SIZE,
                     bb_stage_platforms[game.stage][1].y) < 0.05f);
}

static void test_enemies_move_without_player_input(void)
{
    BBGame game;
    BBInput input = {0};
    float start_x;

    bb_game_start(&game);
    start_x = game.enemies[0].x;
    advance(&game, input, 30);
    CHECK(difference(game.enemies[0].x, start_x) > 0.2f);
    CHECK(game.enemies[0].x >= bb_stage_platforms[game.stage][1].x);
    CHECK(game.enemies[0].x + BB_ENEMY_SIZE <=
          bb_stage_platforms[game.stage][1].x +
          bb_stage_platforms[game.stage][1].width);
    CHECK(game.state == BB_STATE_PLAY);
}

static void test_reach_every_platform(void)
{
    BBGame game;
    BBInput input = {0};
    int platform;

    bb_game_start(&game);
    game.player.x = 4.0f;
    game.player.invincible = 100.0f;

    /* 첫 발판에 오른 뒤 걷기와 점프만으로 나머지 발판에 도달한다. */
    input.jump = true;
    bb_game_update(&game, input, FRAME_TIME);
    input.jump = false;
    advance(&game, input, 64);
    CHECK(game.player.grounded);
    CHECK(difference(game.player.y + BB_PLAYER_SIZE,
                     bb_stage_platforms[game.stage][1].y) < 0.05f);

    for (platform = 2; platform < BB_PLATFORM_COUNT; ++platform) {
        input.move = 1.0f;
        advance(&game, input, 40);
        CHECK(game.player.grounded);

        input.jump = true;
        bb_game_update(&game, input, FRAME_TIME);
        input.jump = false;
        advance(&game, input, 39);
        input.move = 0.0f;
        advance(&game, input, 25);

        CHECK(game.player.grounded);
        CHECK(difference(game.player.y + BB_PLAYER_SIZE,
                         bb_stage_platforms[game.stage][platform].y) < 0.05f);
        CHECK(game.state == BB_STATE_PLAY);
    }
}

static void test_bubble_hits_enemy(void)
{
    BBGame game;
    BBInput input = {0};

    bb_game_start(&game);
    game.player.x = 2.0f;
    game.player.y = bb_stage_platforms[game.stage][0].y - BB_PLAYER_SIZE;
    game.player.facing = 1;
    game.enemies[0].x = 5.0f;
    game.enemies[0].y = game.player.y;
    game.enemies[0].platform = 0;
    game.enemies[0].direction = -1;

    input.fire = true;
    bb_game_update(&game, input, FRAME_TIME);
    input.fire = false;
    advance(&game, input, 30);

    CHECK(!game.enemies[0].alive);
    CHECK(bb_game_enemies_left(&game) == BB_ENEMY_COUNT - 1);
    CHECK(game.state == BB_STATE_PLAY);
}

static void test_damage_and_game_over(void)
{
    BBGame game;
    BBInput input = {0};
    int expected_lives;

    bb_game_start(&game);
    for (expected_lives = 2; expected_lives >= 0; --expected_lives) {
        game.player.invincible = 0.0f;
        game.enemies[0].x = game.player.x + 0.1f;
        game.enemies[0].y = game.player.y;
        game.enemies[0].platform = 0;
        game.enemies[0].direction = 1;
        bb_game_update(&game, input, FRAME_TIME);
        CHECK(game.player.lives == expected_lives);
        CHECK(difference(game.player.x, 1.5f) < 0.05f);
        CHECK(difference(game.player.y,
                         bb_stage_platforms[game.stage][0].y - BB_PLAYER_SIZE) < 0.05f);
        if (expected_lives > 0) {
            CHECK(game.state == BB_STATE_PLAY);
            CHECK(game.player.invincible > 0.0f);
        }
    }

    CHECK(game.state == BB_STATE_LOST);
    input.move = 1.0f;
    input.jump = true;
    input.fire = true;
    advance(&game, input, 60);
    CHECK(game.state == BB_STATE_LOST);
    CHECK(game.player.lives == 0);
    CHECK(difference(game.player.x, 1.5f) < 0.05f);
}

static void clear_current_stage(BBGame *game)
{
    BBInput input = {0};
    int i;

    /* 마지막 적을 버블로 처치해 스테이지 종료 흐름을 확인한다. */
    for (i = 0; i < BB_ENEMY_COUNT - 1; ++i)
        game->enemies[i].alive = false;
    game->player.x = 2.0f;
    game->player.y = bb_stage_platforms[game->stage][0].y - BB_PLAYER_SIZE;
    game->player.vy = 0.0f;
    game->player.grounded = true;
    game->player.facing = 1;
    game->player.fire_cooldown = 0.0f;
    game->enemies[BB_ENEMY_COUNT - 1].x = 5.0f;
    game->enemies[BB_ENEMY_COUNT - 1].y = game->player.y;
    game->enemies[BB_ENEMY_COUNT - 1].platform = 0;
    game->enemies[BB_ENEMY_COUNT - 1].direction = -1;

    input.fire = true;
    bb_game_update(game, input, FRAME_TIME);
    input.fire = false;
    advance(game, input, 30);
    CHECK(bb_game_enemies_left(game) == 0);
}

static void walk(BBGame *game, int direction, int frames)
{
    BBInput input = {0};

    input.move = (float)direction;
    advance(game, input, frames);
    CHECK(game->player.grounded);
}

static void jump_to_platform(BBGame *game, int direction, int moving_frames,
                             int platform)
{
    BBInput input = {0};

    input.move = (float)direction;
    input.jump = true;
    bb_game_update(game, input, FRAME_TIME);
    CHECK(game->player.vy < 0.0f);
    input.jump = false;
    if (moving_frames > 1) advance(game, input, moving_frames - 1);
    input.move = 0.0f;
    advance(game, input, 80 - moving_frames);
    CHECK(game->player.grounded);
    CHECK(difference(game->player.y + BB_PLAYER_SIZE,
                     bb_stage_platforms[game->stage][platform].y) < 0.05f);
}

static void test_stage_specific_platforms(void)
{
    BBGame game;

    CHECK(bb_stage_platforms[0][1].x < bb_stage_platforms[0][2].x);
    CHECK(bb_stage_platforms[1][1].x > bb_stage_platforms[1][2].x);
    CHECK(bb_stage_platforms[1][2].x > bb_stage_platforms[1][3].x);
    CHECK(bb_stage_platforms[2][1].x < bb_stage_platforms[2][2].x);
    CHECK(bb_stage_platforms[2][2].x > bb_stage_platforms[2][3].x);
    CHECK(bb_stage_platforms[2][2].y < bb_stage_platforms[0][2].y);

    bb_game_start(&game);
    clear_current_stage(&game);
    bb_game_next_stage(&game);
    CHECK(game.stage == 1);
    game.player.invincible = 100.0f;

    /* 두 번째 스테이지: 바닥 오른쪽에서 시작해 왼쪽으로 올라간다. */
    walk(&game, 1, 160);                   /* x=17.5 */
    jump_to_platform(&game, 0, 0, 1);
    walk(&game, -1, 10);                   /* x=16.5 */
    jump_to_platform(&game, -1, 40, 2);   /* x=12.5 */
    walk(&game, -1, 20);                   /* x=10.5 */
    jump_to_platform(&game, -1, 40, 3);   /* x=6.5 */
    CHECK(game.state == BB_STATE_PLAY);

    clear_current_stage(&game);
    bb_game_next_stage(&game);
    CHECK(game.stage == 2);
    game.player.invincible = 100.0f;

    /* 세 번째 스테이지: 오른쪽, 다시 왼쪽으로 방향을 바꾼다. */
    walk(&game, 1, 45);                    /* x=6.0 */
    jump_to_platform(&game, 0, 0, 1);
    walk(&game, 1, 30);                    /* x=9.0 */
    jump_to_platform(&game, 1, 40, 2);    /* x=13.0 */
    jump_to_platform(&game, -1, 40, 3);   /* x=9.0 */
    CHECK(game.state == BB_STATE_PLAY);
}

static void test_three_stage_progression(void)
{
    BBGame game;
    BBInput input = {0};
    float cleared_x;
    int stage;

    CHECK(BB_STAGE_COUNT == 3);
    CHECK(bb_stage_platforms[0][1].x != bb_stage_platforms[1][1].x);
    CHECK(bb_stage_platforms[1][1].x != bb_stage_platforms[2][1].x);
    bb_game_start(&game);
    CHECK(game.stage == 0);

    /* 실제 피격으로 생명 하나를 잃고, 다음 스테이지에서도 유지되는지 확인한다. */
    game.enemies[0].x = game.player.x + 0.1f;
    game.enemies[0].y = game.player.y;
    game.enemies[0].platform = 0;
    game.player.invincible = 0.0f;
    bb_game_update(&game, input, FRAME_TIME);
    CHECK(game.player.lives == 2);

    for (stage = 0; stage < BB_STAGE_COUNT; ++stage) {
        CHECK(game.stage == stage);
        CHECK(game.state == BB_STATE_PLAY);
        CHECK(game.player.lives == 2);
        clear_current_stage(&game);
        CHECK(game.state == (stage == BB_STAGE_COUNT - 1 ?
                             BB_STATE_WON : BB_STATE_CLEAR));

        cleared_x = game.player.x;
        input.move = 1.0f;
        advance(&game, input, 10);
        CHECK(game.player.x == cleared_x);
        CHECK(game.stage == stage);
        input.move = 0.0f;

        if (stage < BB_STAGE_COUNT - 1) {
            bb_game_next_stage(&game);
            CHECK(game.stage == stage + 1);
            CHECK(game.state == BB_STATE_PLAY);
            CHECK(game.player.lives == 2);
            CHECK(bb_game_enemies_left(&game) == BB_ENEMY_COUNT);
            CHECK(difference(game.player.y + BB_PLAYER_SIZE,
                             bb_stage_platforms[game.stage][0].y) < 0.05f);
        }
    }

    bb_game_next_stage(&game);
    CHECK(game.stage == BB_STAGE_COUNT - 1);
    CHECK(game.state == BB_STATE_WON);
    bb_game_start(&game);
    CHECK(game.stage == 0);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(game.player.lives == 3);
    CHECK(bb_game_enemies_left(&game) == BB_ENEMY_COUNT);
}

static void test_no_update_outside_play_or_invalid_time(void)
{
    BBGame game;
    BBInput input = {0};
    float x;

    input.move = 1.0f;
    input.jump = true;
    input.fire = true;
    bb_game_init(&game);
    x = game.player.x;
    advance(&game, input, 10);
    CHECK(game.state == BB_STATE_MENU);
    CHECK(game.player.x == x);
    CHECK(active_bubbles(&game) == 0);

    bb_game_start(&game);
    x = game.player.x;
    bb_game_update(&game, input, 0.0f);
    bb_game_update(&game, input, -FRAME_TIME);
    CHECK(game.player.x == x);
    CHECK(game.player.grounded);
    CHECK(active_bubbles(&game) == 0);
}

int main(void)
{
    test_start_and_movement();
    test_jump_to_first_platform();
    test_enemies_move_without_player_input();
    test_reach_every_platform();
    test_bubble_hits_enemy();
    test_damage_and_game_over();
    test_stage_specific_platforms();
    test_three_stage_progression();
    test_no_update_outside_play_or_invalid_time();
    puts("Game tests passed.");
    return 0;
}
