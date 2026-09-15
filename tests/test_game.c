#include "bb_game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    ++checks; \
    if(!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while(0)

static void tick(BBGame *game, float move, bool jump, bool fire)
{
    const BBInput inputs[2] = { { .move = move, .jump = jump, .fire = fire }, {0} };
    bb_game_update(game, inputs, BB_FIXED_DT);
    bb_game_check_bump(game);
}

static void wait_ticks(BBGame *game, int count)
{
    for(int i = 0; i < count; ++i)
        tick(game, 0.0f, false, false);
}

static void fixture(BBGame *game)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{{0}}};
    for(int level = 0; level < BB_LEVEL_COUNT; ++level)
        for(int x = 0; x < BB_MAP_WIDTH; ++x)
            maps[level][26][x] = 3;
    bb_game_init(game, &maps[0][0][0], UINT32_C(1234567));
    bb_game_start(game, BB_MODE_SOLO);
    bb_game_skip_intro(game);
    memset(game->enemies, 0, sizeof game->enemies);
    /* 포획 상태의 감시 적으로 무관한 자동 스테이지 전환을 막는다. */
    game->enemies[BB_MAX_ENEMIES - 1].active = true;
    game->enemies[BB_MAX_ENEMIES - 1].state = BB_ENEMY_CAPTURED;
    wait_ticks(game, 30);
    CHECK(game->players[0].body.grounded);
}

static int active_bubbles(const BBGame *game)
{
    int count = 0;
    for(int i = 0; i < BB_MAX_BUBBLES; ++i)
        count += game->bubbles[i].active ? 1 : 0;
    return count;
}

static int active_pickups(const BBGame *game)
{
    int count = 0;
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        count += game->pickups[i].active ? 1 : 0;
    return count;
}

static void test_platforms_and_momentum(void)
{
    BBGame game;
    fixture(&game);
    for(int x = 2; x <= 10; ++x)
        game.maps[0][22][x] = 2;
    CHECK(fabsf(game.players[0].body.y - 25.025f) < 0.001f);
    tick(&game, 0.0f, true, false);
    CHECK((game.events & BB_EVENT_JUMP) != 0);
    CHECK(game.players[0].body.vy < -17.0f);
    bool rose_above = false;
    for(int i = 0; i < 180; ++i)
    {
        tick(&game, 0.0f, false, false);
        rose_above = rose_above || game.players[0].body.y < 21.025f;
    }
    CHECK(rose_above);
    CHECK(game.players[0].body.grounded);
    CHECK(fabsf(game.players[0].body.y - 21.025f) < 0.001f);

    fixture(&game);
    tick(&game, 1.0f, true, false);
    CHECK(fabsf(game.players[0].body.vx - 8.0f) < 0.001f);
    tick(&game, 0.0f, false, false);
    CHECK(fabsf(game.players[0].body.vx - 8.0f) < 0.001f);
    tick(&game, -1.0f, false, false);
    CHECK(fabsf(game.players[0].body.vx + 4.0f) < 0.001f);
}

static void test_walls_wrap_and_fire(void)
{
    BBGame game;
    fixture(&game);
    for(int y = 22; y <= 25; ++y)
        game.maps[0][y][12] = 3;
    game.players[0].body.x = 10.0f;
    for(int i = 0; i < 90; ++i)
        tick(&game, 1.0f, false, false);
    CHECK(fabsf(game.players[0].body.x - 11.1f) < 0.001f);
    tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);
    CHECK((game.events & BB_EVENT_FIRE) != 0);
    CHECK(game.bubbles[0].body.x + 0.75f <= 12.001f);
    CHECK(game.bubbles[0].body.y + 0.75f < 26.0f);
    /* 벽 가까이 발사해도 버블이 단단한 바닥 내부에 생기면 안 된다. */
    for(int i = 0; i < 20; ++i)
        tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);

    fixture(&game);
    memset(game.maps, 0, sizeof game.maps);
    game.players[0].body.y = 28.99f;
    game.players[0].body.grounded = false;
    game.players[0].falling = true;
    wait_ticks(&game, 3);
    CHECK(game.players[0].body.y < 1.0f);
    CHECK(game.players[0].body.y > -1.1f);
}

static void test_capture_and_release(void)
{
    BBGame game;
    fixture(&game);
    game.enemies[0] = (BBEnemy){ .body = { .x = 6.8f, .y = 25.05f, .grounded = true },
        .type = BB_ENEMY_ZENCHAN, .active = true, .controlled = true, .state = BB_ENEMY_WALKING, .facing = 1 };
    tick(&game, 0.0f, false, true);
    CHECK(game.bubbles[0].captured_enemy == 0);
    CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);
    CHECK(bb_game_enemies_left(&game) == 2);
    game.bubbles[0].age = 7.999f;
    tick(&game, 0.0f, false, false);
    CHECK(game.bubbles[0].popping);
    CHECK(game.bubbles[0].releasing);
    wait_ticks(&game, 32);
    CHECK(!game.bubbles[0].active);
    CHECK(game.enemies[0].state != BB_ENEMY_CAPTURED);
    CHECK(game.enemies[0].state != BB_ENEMY_DEAD);
    CHECK(game.enemies[0].angry);
}

static void test_pop_drop_and_score(void)
{
    BBGame game;
    fixture(&game);
    memset(game.enemies, 0, sizeof game.enemies);
    game.enemies[0] = (BBEnemy){ .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_CAPTURED };
    game.bubbles[0] = (BBBubble){ .body = { .x = 10.0f, .y = 20.0f },
        .active = true, .age = 2.0f, .captured_enemy = 0 };
    game.players[0].body = (BBBody){ .x = 10.0f, .y = 21.0f, .vy = -18.0f };
    game.players[0].falling = false;
    game.players[0].air_control = true;
    game.players[0].jump_start_y = 25.0f;
    tick(&game, 0.0f, false, false);
    CHECK(game.bubbles[0].popping);
    CHECK(!game.bubbles[0].releasing);
    wait_ticks(&game, 32);
    CHECK(game.enemies[0].state == BB_ENEMY_DEAD);
    CHECK(bb_game_enemies_left(&game) == 0);
    CHECK(game.state == BB_STATE_CLEAR);
    game.players[0].body.x = 2.0f;
    wait_ticks(&game, 500);
    CHECK(game.level == 0);
    CHECK(active_pickups(&game) == 1);
    /* 마지막 적의 아이템은 7초 클리어 타이머 전에 먹을 수 있어야 한다. */
    game.players[0].body = game.pickups[0].body;
    game.players[0].body.vy = 0.0f;
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].score == 100);
    CHECK(active_pickups(&game) == 0);
    CHECK((game.events & BB_EVENT_PICKUP) != 0);

    game.pickups[0] = (BBPickup){ .body = game.players[0].body, .type = BB_PICKUP_FRIES, .active = true };
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].score == 300);
}

static void collide_enemy_with_player(BBGame *game, int index)
{
    game->players[index].invulnerable = 0.0f;
    game->enemies[0] = (BBEnemy){ .body = game->players[index].body,
        .active = true, .controlled = true, .state = BB_ENEMY_WALKING, .facing = 1 };
    tick(game, 0.0f, false, false);
    game->enemies[0].active = false;
}

static void test_lives_respawn_and_coop(void)
{
    BBGame game;
    fixture(&game);
    collide_enemy_with_player(&game, 0);
    CHECK(game.players[0].lives == 2);
    CHECK(game.players[0].state == BB_PLAYER_DEAD);
    CHECK((game.events & BB_EVENT_DEATH) != 0);
    wait_ticks(&game, 190);
    CHECK(game.players[0].state == BB_PLAYER_NORMAL);
    CHECK(game.players[0].invulnerable > 2.8f);
    game.enemies[0] = (BBEnemy){ .body = game.players[0].body,
        .active = true, .controlled = true, .state = BB_ENEMY_WALKING };
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].lives == 2);
    game.enemies[0].active = false;
    collide_enemy_with_player(&game, 0);
    wait_ticks(&game, 190);
    collide_enemy_with_player(&game, 0);
    wait_ticks(&game, 190);
    CHECK(game.players[0].lives == 0);
    CHECK(game.players[0].state == BB_PLAYER_OUT);
    CHECK(game.state == BB_STATE_SCORE);
    CHECK(!game.won);

    bb_game_start(&game, BB_MODE_COOP);
    bb_game_skip_intro(&game);
    game.players[0].state = BB_PLAYER_OUT;
    tick(&game, 0.0f, false, false);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(game.players[1].active);
    game.players[1].state = BB_PLAYER_OUT;
    tick(&game, 0.0f, false, false);
    CHECK(game.state == BB_STATE_SCORE);
}

static void test_levels_and_versus(void)
{
    BBGame game;
    bb_game_init(&game, NULL, 99);
    CHECK(game.state == BB_STATE_MENU);
    bb_game_start(&game, BB_MODE_SOLO);
    CHECK(game.state == BB_STATE_INTRO);
    wait_ticks(&game, 841);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(bb_game_enemies_left(&game) == 3);
    CHECK(game.enemies[1].type == BB_ENEMY_MAITA);
    CHECK(game.enemies[1].spawn_delay == 1.0f);
    CHECK(game.enemies[2].spawn_y == 8.0f);
    game.players[0].invulnerable = 1000.0f;
    for(int level = 0; level < 3; ++level)
    {
        CHECK(game.level == level);
        memset(game.enemies, 0, sizeof game.enemies);
        tick(&game, 0.0f, false, false);
        CHECK(game.state == BB_STATE_CLEAR);
        wait_ticks(&game, 841);
        if(level == 0)
        {
            CHECK(bb_game_enemies_left(&game) == 4);
            CHECK(game.enemies[1].spawn_y == 4.0f);
        }
        if(level == 1)
        {
            CHECK(game.enemies[0].spawn_delay == 2.0f);
            CHECK(game.enemies[0].spawn_y == 10.0f);
            CHECK(game.enemies[2].body.x == 20.0f);
        }
    }
    CHECK(game.state == BB_STATE_SCORE);
    CHECK(game.won);
    bb_game_menu(&game);
    CHECK(game.state == BB_STATE_MENU);

    fixture(&game);
    bb_game_start(&game, BB_MODE_VERSUS);
    bb_game_skip_intro(&game);
    CHECK(!game.players[1].active);
    CHECK(bb_game_enemies_left(&game) == 1);
    CHECK(game.enemies[0].controlled);
    game.enemies[0].type = BB_ENEMY_ZENCHAN;
    game.enemies[0].body = (BBBody){ .x = 16.0f, .y = 25.05f, .grounded = true };
    BBInput inputs[2] = {{0}, {.move = 1.0f, .fire = true}};
    bb_game_update(&game, inputs, BB_FIXED_DT);
    bb_game_check_bump(&game);
    CHECK(game.enemies[0].angry);
    CHECK(fabsf(game.enemies[0].body.vx - 14.4f) < 0.001f);
    game.enemies[0].type = BB_ENEMY_MAITA;
    game.enemies[0].throw_timer = 4.0f;
    bb_game_update(&game, inputs, BB_FIXED_DT);
    bb_game_check_bump(&game);
    CHECK(game.boulders[0].active);
    CHECK(game.boulders[0].body.vx > 13.0f);
    wait_ticks(&game, 365);
    CHECK(!game.boulders[0].active);
}

static void test_finite_input_and_determinism(void)
{
    BBGame a, b;
    fixture(&a);
    fixture(&b);
    uint64_t previous_tick = a.ticks;
    bb_game_update(&a, NULL, NAN);
    bb_game_check_bump(&a);
    bb_game_update(&a, NULL, INFINITY);
    bb_game_check_bump(&a);
    bb_game_update(&a, NULL, -1.0f);
    bb_game_check_bump(&a);
    bb_game_update(&a, NULL, 1.0f);
    bb_game_check_bump(&a);
    CHECK(a.ticks == previous_tick);
    uint32_t random = 67891;
    for(int step = 0; step < 60000; ++step)
    {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        BBInput inputs[2] = {
            { .move = (float)((int)(random % 3) - 1), .jump = (random & 15) == 0, .fire = (random & 7) == 0 },
            { .move = (float)((int)((random >> 8) % 3) - 1), .jump = (random & 31) == 0, .fire = (random & 63) == 0 }
        };
        if(step % 100 == 0)
            inputs[0].move = NAN;
        if(a.state == BB_STATE_SCORE)
        {
            BBMode mode = (BBMode)((step / 100) % 3);
            bb_game_start(&a, mode);
            bb_game_start(&b, mode);
            bb_game_skip_intro(&a);
            bb_game_skip_intro(&b);
        }
        bb_game_update(&a, inputs, BB_FIXED_DT);
        bb_game_check_bump(&a);
        bb_game_update(&b, inputs, BB_FIXED_DT);
        bb_game_check_bump(&b);
        CHECK(a.rng == b.rng);
        CHECK(a.state == b.state);
        CHECK(a.players[0].body.x == b.players[0].body.x);
        CHECK(a.players[0].body.y == b.players[0].body.y);
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            CHECK(isfinite(a.players[p].body.x) && isfinite(a.players[p].body.y));
            CHECK(a.players[p].body.x >= 0.0f && a.players[p].body.x <= 32.0f);
            CHECK(a.players[p].body.y > -20.0f && a.players[p].body.y < 31.0f);
            CHECK(a.players[p].lives >= 0 && a.players[p].lives <= 3);
        }
        for(int i = 0; i < BB_MAX_BUBBLES; ++i)
            if(a.bubbles[i].active)
                CHECK(isfinite(a.bubbles[i].body.x) && isfinite(a.bubbles[i].body.y));
    }
}

int main(void)
{
    test_platforms_and_momentum();
    test_walls_wrap_and_fire();
    test_capture_and_release();
    test_pop_drop_and_score();
    test_lives_respawn_and_coop();
    test_levels_and_versus();
    test_finite_input_and_determinism();
    printf("Core regression tests passed (%d checks, 60000 deterministic stress ticks).\n", checks);
    return EXIT_SUCCESS;
}
