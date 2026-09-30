#include "bb_game.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

static void tick(BBGame *game, float move, bool jump, bool fire)
{
    const BBInput inputs[BB_MAX_PLAYERS] = {{ .move = move, .jump = jump, .fire = fire }};
    bb_game_update(game, inputs, BB_FIXED_DT);
    bb_game_check_bump(game);
}

static void wait_ticks(BBGame *game, int count)
{
    for (int i = 0; i < count; ++i)
        tick(game, 0.0f, false, false);
}

static void fixture(BBGame *game)
{
    uint8_t map[BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{0}};
    for (int x = 0; x < BB_MAP_WIDTH; ++x)
        map[26][x] = 3;
    bb_game_init(game, &map[0][0], UINT32_C(1234567));
    bb_game_start(game);
    bb_game_skip_intro(game);
    wait_ticks(game, 30);
    CHECK(game->state == BB_STATE_PLAY);
    CHECK(game->players[0].body.grounded);
}

static int active_bubbles(const BBGame *game)
{
    int count = 0;
    for (int i = 0; i < BB_MAX_BUBBLES; ++i)
        count += game->bubbles[i].active ? 1 : 0;
    return count;
}

static void test_single_level_stays_playable(void)
{
    BBGame game;
    bb_game_init(&game, NULL, 99);
    CHECK(game.state == BB_STATE_MENU);
    CHECK(BB_MAX_PLAYERS == 1);
    bb_game_start(&game);
    CHECK(game.state == BB_STATE_INTRO);
    /* 7초 인트로가 자동으로 끝나도 플레이 상태를 유지한다. */
    wait_ticks(&game, 841);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(game.players[0].active);
    wait_ticks(&game, 1200);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(game.maps[0][0] == 0);
    bb_game_menu(&game);
    CHECK(game.state == BB_STATE_MENU);
    bb_game_start(&game);
    CHECK(game.state == BB_STATE_INTRO);
    bb_game_skip_intro(&game);
    CHECK(game.state == BB_STATE_PLAY);
}

static void test_platforms_and_momentum(void)
{
    BBGame game;
    fixture(&game);
    /* 아래에서 일방 통과 발판을 지나 올라간 뒤 위에서 착지한다. */
    for (int x = 2; x <= 10; ++x)
        game.maps[22][x] = 2;
    CHECK(fabsf(game.players[0].body.y - 25.025f) < 0.001f);
    tick(&game, 0.0f, true, false);
    CHECK((game.events & BB_EVENT_JUMP) != 0);
    CHECK(game.players[0].body.vy < -17.0f);
    bool rose_above = false;
    for (int i = 0; i < 180; ++i) {
        tick(&game, 0.0f, false, false);
        rose_above = rose_above || game.players[0].body.y < 21.025f;
    }
    CHECK(rose_above);
    CHECK(game.players[0].body.grounded);
    CHECK(fabsf(game.players[0].body.y - 21.025f) < 0.001f);

    fixture(&game);
    tick(&game, 1.0f, true, false);
    CHECK(fabsf(game.players[0].body.vx - BB_PLAYER_GROUND_SPEED) < 0.001f);
    tick(&game, 0.0f, false, false);
    CHECK(fabsf(game.players[0].body.vx - BB_PLAYER_GROUND_SPEED) < 0.001f);
    tick(&game, -1.0f, false, false);
    CHECK(fabsf(game.players[0].body.vx + BB_PLAYER_AIR_SPEED) < 0.001f);
}

static void test_walls_wrap_and_fire(void)
{
    BBGame game;
    fixture(&game);
    for (int y = 22; y <= 25; ++y)
        game.maps[y][12] = 3;
    game.players[0].body.x = 10.0f;
    for (int i = 0; i < 90; ++i)
        tick(&game, 1.0f, false, false);
    CHECK(fabsf(game.players[0].body.x - 11.1f) < 0.001f);
    tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);
    CHECK((game.events & BB_EVENT_FIRE) != 0);
    CHECK(game.bubbles[0].body.x + 0.75f <= 12.001f);
    CHECK(game.bubbles[0].body.y + 0.75f < 26.0f);
    for (int i = 0; i < 20; ++i)
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

static void test_bubble_lifetime_and_player_pop(void)
{
    BBGame game;
    fixture(&game);
    tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);
    game.bubbles[0].age = 7.999f;
    tick(&game, 0.0f, false, false);
    CHECK(game.bubbles[0].popping);
    CHECK((game.events & BB_EVENT_POP) != 0);
    wait_ticks(&game, 32);
    CHECK(active_bubbles(&game) == 0);
    CHECK(game.state == BB_STATE_PLAY);

    fixture(&game);
    game.bubbles[0] = (BBBubble){ .body = { .x = 10.0f, .y = 20.0f },
                                   .active = true, .age = 2.0f };
    game.players[0].body = (BBBody){ .x = 10.0f, .y = 21.0f, .vy = -18.0f };
    game.players[0].falling = false;
    game.players[0].air_control = true;
    game.players[0].jump_start_y = 25.0f;
    tick(&game, 0.0f, false, false);
    CHECK(game.bubbles[0].popping);
    CHECK((game.events & BB_EVENT_POP) != 0);
    wait_ticks(&game, 32);
    CHECK(active_bubbles(&game) == 0);
}

static void set_energy_phase(BBPlayer *player, bool boosting, double elapsed)
{
    player->boosting = boosting;
    player->energy_elapsed = elapsed;
    player->energy = boosting ? 1.0f - (float)(elapsed / BB_ENERGY_BOOST_SECONDS)
                              : (float)(elapsed / BB_ENERGY_CHARGE_SECONDS);
}

static void update_with_dt(BBGame *game, float dt)
{
    bb_game_update(game, NULL, dt);
    bb_game_check_bump(game);
}

static void test_energy_cycle_and_boundaries(void)
{
    const int rates[] = {30, 60, 120};
    CHECK(BB_ENERGY_CHARGE_SECONDS == 15.0f);
    CHECK(BB_ENERGY_BOOST_SECONDS == 5.0f);
    for (unsigned rate_index = 0; rate_index < sizeof rates / sizeof rates[0]; ++rate_index) {
        BBGame game;
        int rate = rates[rate_index];
        float dt = 1.0f / (float)rate;
        fixture(&game);
        set_energy_phase(&game.players[0], false, 0.0);
        for (int cycle = 0; cycle < 3; ++cycle) {
            for (int tick_index = 0; tick_index < 15 * rate - 1; ++tick_index)
                update_with_dt(&game, dt);
            CHECK(!game.players[0].boosting);
            CHECK(game.players[0].energy > 0.99f && game.players[0].energy < 1.0f);
            update_with_dt(&game, dt);
            CHECK(game.players[0].boosting);
            CHECK(fabsf(game.players[0].energy - 1.0f) < 0.00001f);
            float previous = game.players[0].energy;
            for (int tick_index = 0; tick_index < 5 * rate - 1; ++tick_index) {
                update_with_dt(&game, dt);
                CHECK(game.players[0].boosting);
                CHECK(game.players[0].energy < previous);
                CHECK(game.players[0].energy > 0.0f);
                previous = game.players[0].energy;
                if (rate % 2 == 0 && tick_index + 1 == 5 * rate / 2)
                    CHECK(fabsf(game.players[0].energy - 0.5f) < 0.00001f);
            }
            update_with_dt(&game, dt);
            CHECK(!game.players[0].boosting);
            CHECK(game.players[0].energy >= 0.0f && game.players[0].energy < 0.00001f);
        }
        CHECK(game.state == BB_STATE_PLAY);
    }

    BBGame game;
    fixture(&game);
    set_energy_phase(&game.players[0], false, 14.99);
    update_with_dt(&game, 0.025f);
    CHECK(game.players[0].boosting);
    CHECK(fabsf(game.players[0].energy - 0.997f) < 0.000001f);
    set_energy_phase(&game.players[0], true, 4.99);
    update_with_dt(&game, 0.025f);
    CHECK(!game.players[0].boosting);
    CHECK(fabsf(game.players[0].energy - 0.001f) < 0.000001f);
}

static void test_energy_horizontal_speed_and_momentum(void)
{
    CHECK(BB_PLAYER_GROUND_SPEED > 0.0f && BB_PLAYER_GROUND_SPEED < 8.0f);
    CHECK(BB_PLAYER_AIR_SPEED > 0.0f && BB_PLAYER_AIR_SPEED < 4.0f);
    for (int direction = -1; direction <= 1; direction += 2) {
        BBGame normal, boosted;
        fixture(&normal);
        fixture(&boosted);
        normal.players[0].body.x = boosted.players[0].body.x = 16.0f;
        set_energy_phase(&normal.players[0], false, 0.0);
        set_energy_phase(&boosted.players[0], true, 0.0);
        tick(&normal, (float)direction, false, false);
        tick(&boosted, (float)direction, false, false);
        CHECK(fabsf(normal.players[0].body.vx - direction * BB_PLAYER_GROUND_SPEED) < 0.0001f);
        CHECK(fabsf(boosted.players[0].body.vx - normal.players[0].body.vx * 2.0f) < 0.0001f);
        CHECK(normal.players[0].body.vy == boosted.players[0].body.vy);
        tick(&normal, (float)direction, true, false);
        tick(&boosted, (float)direction, true, false);
        CHECK(normal.players[0].body.vy == boosted.players[0].body.vy);
        CHECK(normal.players[0].body.y == boosted.players[0].body.y);
        CHECK(fabsf(boosted.players[0].body.vx - normal.players[0].body.vx * 2.0f) < 0.0001f);
        tick(&normal, (float)-direction, false, false);
        tick(&boosted, (float)-direction, false, false);
        CHECK(fabsf(normal.players[0].body.vx + direction * BB_PLAYER_AIR_SPEED) < 0.0001f);
        CHECK(fabsf(boosted.players[0].body.vx - normal.players[0].body.vx * 2.0f) < 0.0001f);
        CHECK(normal.players[0].body.vy == boosted.players[0].body.vy);

        BBGame momentum;
        fixture(&momentum);
        momentum.players[0].body = (BBBody){ .x = 16.0f, .y = 10.0f,
            .vx = direction * BB_PLAYER_GROUND_SPEED, .vy = -5.0f };
        momentum.players[0].falling = false;
        momentum.players[0].air_control = false;
        momentum.players[0].jump_start_y = 20.0f;
        set_energy_phase(&momentum.players[0], false, 14.999);
        tick(&momentum, 0.0f, false, false);
        CHECK(momentum.players[0].boosting);
        CHECK(fabsf(momentum.players[0].body.vx - direction * BB_PLAYER_GROUND_SPEED * 2.0f) < 0.0001f);
        CHECK(fabsf(momentum.players[0].body.vy + 4.75f) < 0.0001f);
        set_energy_phase(&momentum.players[0], true, 4.999);
        tick(&momentum, 0.0f, false, false);
        CHECK(!momentum.players[0].boosting);
        CHECK(fabsf(momentum.players[0].body.vx - direction * BB_PLAYER_GROUND_SPEED) < 0.0001f);
        CHECK(fabsf(momentum.players[0].body.vy + 4.5f) < 0.0001f);
    }
}

static void test_energy_lifecycle(void)
{
    BBGame game;
    fixture(&game);
    set_energy_phase(&game.players[0], true, 2.0);
    bb_game_menu(&game);
    float before = game.players[0].energy;
    wait_ticks(&game, 120);
    CHECK(game.players[0].energy == before);
    CHECK(game.players[0].energy_elapsed == 2.0);

    bb_game_start(&game);
    CHECK(game.state == BB_STATE_INTRO);
    CHECK(game.players[0].energy == 0.0f);
    CHECK(game.players[0].energy_elapsed == 0.0);
    wait_ticks(&game, 120);
    CHECK(game.players[0].energy == 0.0f);
    bb_game_skip_intro(&game);
    CHECK(game.state == BB_STATE_PLAY);
    wait_ticks(&game, 120);
    CHECK(fabsf(game.players[0].energy - 1.0f / 15.0f) < 0.000001f);
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
    for (int step = 0; step < 60000; ++step) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        BBInput inputs[BB_MAX_PLAYERS] = {
            { .move = (float)((int)(random % 3) - 1),
              .jump = (random & 15) == 0, .fire = (random & 7) == 0 }
        };
        if (step % 100 == 0)
            inputs[0].move = NAN;
        bb_game_update(&a, inputs, BB_FIXED_DT);
        bb_game_check_bump(&a);
        bb_game_update(&b, inputs, BB_FIXED_DT);
        bb_game_check_bump(&b);
        CHECK(a.rng == b.rng);
        CHECK(a.state == BB_STATE_PLAY && b.state == BB_STATE_PLAY);
        CHECK(a.players[0].body.x == b.players[0].body.x);
        CHECK(a.players[0].body.y == b.players[0].body.y);
        CHECK(isfinite(a.players[0].body.x) && isfinite(a.players[0].body.y));
        CHECK(a.players[0].body.x >= 0.0f && a.players[0].body.x <= 32.0f);
        CHECK(a.players[0].body.y > -20.0f && a.players[0].body.y < 31.0f);
        CHECK(isfinite(a.players[0].energy) && a.players[0].energy >= 0.0f && a.players[0].energy <= 1.0f);
        CHECK(a.players[0].energy == b.players[0].energy);
        CHECK(a.players[0].boosting == b.players[0].boosting);
        for (int i = 0; i < BB_MAX_BUBBLES; ++i)
            if (a.bubbles[i].active)
                CHECK(isfinite(a.bubbles[i].body.x) && isfinite(a.bubbles[i].body.y));
    }
}

int main(void)
{
    test_single_level_stays_playable();
    test_platforms_and_momentum();
    test_walls_wrap_and_fire();
    test_bubble_lifetime_and_player_pop();
    test_energy_cycle_and_boundaries();
    test_energy_horizontal_speed_and_momentum();
    test_energy_lifecycle();
    test_finite_input_and_determinism();
    printf("Core regression tests passed (%d checks, 60000 deterministic stress ticks).\n", checks);
    return EXIT_SUCCESS;
}
