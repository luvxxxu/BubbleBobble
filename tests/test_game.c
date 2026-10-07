#include "bb_game.h"

#include <limits.h>
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
}

static void wait_ticks(BBGame *game, int count)
{
    for (int i = 0; i < count; ++i)
        tick(game, 0.0f, false, false);
}

static void isolate_physics(BBGame *game)
{
    /* 이동 검사 중 자동 클리어를 막되 플레이어와 충돌하지 않는 대기 적이다. */
    memset(game->enemies, 0, sizeof game->enemies);
    game->enemies[BB_MAX_ENEMIES - 1] = (BBEnemy){
        .body = { .x = 16.0f, .y = 0.0f }, .active = true,
        .state = BB_ENEMY_SPAWNING, .spawn_delay = 1000000.0f };
}

static void fixture_mode(BBGame *game, int player_count)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{{0}}};
    for (int level = 0; level < BB_LEVEL_COUNT; ++level)
        for (int x = 0; x < BB_MAP_WIDTH; ++x)
            maps[level][26][x] = 3;
    bb_game_init(game, &maps[0][0][0], UINT32_C(1234567));
    bb_game_start(game, player_count);
    bb_game_skip_intro(game);
    isolate_physics(game);
    wait_ticks(game, 30);
    CHECK(game->state == BB_STATE_PLAY);
    CHECK(game->players[0].body.grounded);
    if (player_count == 2)
        CHECK(game->players[1].body.grounded);
}

static void fixture(BBGame *game)
{
    fixture_mode(game, 1);
}

static int active_bubbles(const BBGame *game)
{
    int count = 0;
    for (int i = 0; i < BB_MAX_BUBBLES; ++i)
        count += game->bubbles[i].active ? 1 : 0;
    return count;
}


static void test_platforms_and_momentum(void)
{
    BBGame game;
    fixture(&game);
    /* 아래에서 일방 통과 발판을 지나 올라간 뒤 위에서 착지한다. */
    for (int x = 2; x <= 10; ++x)
        game.maps[0][22][x] = 2;
    CHECK(fabsf(game.players[0].body.y - 25.025f) < 0.001f);
    tick(&game, 0.0f, true, false);
    CHECK(game.events.jump);
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
        game.maps[0][y][12] = 3;
    game.players[0].body.x = 10.0f;
    for (int i = 0; i < 90; ++i)
        tick(&game, 1.0f, false, false);
    CHECK(fabsf(game.players[0].body.x - 11.1f) < 0.001f);
    tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);
    CHECK(game.events.fire);
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
    CHECK(game.events.pop);
    wait_ticks(&game, 32);
    CHECK(active_bubbles(&game) == 0);
    CHECK(game.state == BB_STATE_PLAY);

    fixture(&game);
    game.bubbles[0] = (BBBubble){ .body = { .x = 10.0f, .y = 20.0f },
                                   .active = true, .age = 2.0f, .captured_enemy = -1 };
    game.players[0].body = (BBBody){ .x = 10.0f, .y = 21.0f, .vy = -18.0f };
    game.players[0].falling = false;
    game.players[0].air_control = true;
    game.players[0].jump_start_y = 25.0f;
    tick(&game, 0.0f, false, false);
    CHECK(game.bubbles[0].popping);
    CHECK(game.events.pop);
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
}

static void test_energy_cycle_and_boundaries(void)
{
    const int rates[] = {30, 60, 120};
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
    /* 메뉴/인트로/점수 화면과 사망 중에는 멈추고 클리어 중에는 충전한다. */
    const BBState frozen_states[] = {BB_STATE_MENU, BB_STATE_INTRO, BB_STATE_SCORE};
    BBGame game;
    for(unsigned state_index = 0; state_index < sizeof frozen_states / sizeof frozen_states[0]; ++state_index)
    {
        for(int boosting = 0; boosting <= 1; ++boosting)
        {
            fixture(&game);
            set_energy_phase(&game.players[0], boosting != 0, 1.0);
            float before = game.players[0].energy;
            game.state = frozen_states[state_index];
            game.state_time = 0.0f;
            wait_ticks(&game, 120);
            CHECK(game.players[0].energy == before);
            CHECK(game.players[0].energy_elapsed == 1.0);
            CHECK(game.players[0].boosting == (boosting != 0));
        }
    }
    for(int state = BB_PLAYER_DEAD; state <= BB_PLAYER_OUT; ++state)
    {
        fixture(&game);
        set_energy_phase(&game.players[0], true, 1.0);
        float before = game.players[0].energy;
        game.players[0].state = (BBPlayerState)state;
        game.players[0].death_timer = 2.0f;
        wait_ticks(&game, 120);
        CHECK(game.players[0].energy == before);
        CHECK(game.players[0].energy_elapsed == 1.0);
    }
    fixture(&game);
    set_energy_phase(&game.players[0], false, 0.0);
    game.state = BB_STATE_CLEAR;
    game.state_time = 0.0f;
    wait_ticks(&game, 120);
    CHECK(fabsf(game.players[0].energy - 1.0f / 15.0f) < 0.000001f);

    set_energy_phase(&game.players[0], true, 2.0);
    bb_game_start(&game, 1);
    CHECK(game.players[0].lives == BB_STARTING_LIVES);
    CHECK(game.players[0].energy == 0.0f);
    CHECK(game.players[0].energy_elapsed == 0.0);
    CHECK(!game.players[0].boosting);
    wait_ticks(&game, 120);
    CHECK(game.players[0].energy == 0.0f);
    bb_game_skip_intro(&game);
    CHECK(game.players[0].energy == 0.0f);
}

static void test_two_player_controls(void)
{
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {
        { .move = 1.0f, .jump = true, .fire = true },
        { .move = -1.0f, .jump = true, .fire = true }
    };
    fixture_mode(&game, 2);
    CHECK(game.player_count == 2);
    CHECK(game.players[0].active && game.players[1].active);
    CHECK(game.players[0].body.x == 4.0f && game.players[1].body.x == 28.0f);
    CHECK(game.players[0].facing == 1 && game.players[1].facing == -1);
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(game.players[0].body.x > 4.0f && game.players[1].body.x < 28.0f);
    CHECK(game.players[0].body.vy < -17.0f && game.players[1].body.vy < -17.0f);
    CHECK(game.events.jump && game.events.fire);
    CHECK(active_bubbles(&game) == 2);
    CHECK(game.bubbles[0].body.vx > 0.0f && game.bubbles[1].body.vx < 0.0f);
    CHECK(game.players[0].fire_cooldown > 0.0f && game.players[1].fire_cooldown > 0.0f);

    memset(inputs, 0, sizeof inputs);
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(game.players[0].body.vx == BB_PLAYER_GROUND_SPEED);
    CHECK(game.players[1].body.vx == -BB_PLAYER_GROUND_SPEED);
    CHECK(!game.events.jump && !game.events.fire);
    inputs[0].move = -1.0f;
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(game.players[0].body.vx == -BB_PLAYER_AIR_SPEED);
    CHECK(game.players[1].body.vx == -BB_PLAYER_GROUND_SPEED);

    /* 1P의 발사 대기시간이 남아 있어도 2P는 따로 발사할 수 있다. */
    inputs[0].fire = inputs[1].fire = true;
    game.players[1].fire_cooldown = 0.0f;
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(active_bubbles(&game) == 3);
    CHECK(game.players[0].fire_cooldown < game.players[1].fire_cooldown);
    CHECK(game.players[1].fire_cooldown == 0.4f);
}

static void test_independent_energy_and_momentum(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    for (int p = 0; p < BB_MAX_PLAYERS; ++p) {
        game.players[p].body = (BBBody){ .x = 10.0f + 12.0f * (float)p,
            .y = 10.0f, .vx = p == 0 ? 6.0f : -12.0f, .vy = -5.0f };
        game.players[p].falling = false;
        game.players[p].air_control = false;
        game.players[p].jump_start_y = 20.0f;
    }
    set_energy_phase(&game.players[0], false, 14.999);
    set_energy_phase(&game.players[1], true, 4.0);
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.players[0].boosting && game.players[1].boosting);
    CHECK(game.players[0].body.vx == 12.0f);
    CHECK(game.players[1].body.vx == -12.0f);
    CHECK(game.players[0].energy > 0.99f && game.players[1].energy < 0.2f);
    CHECK(game.players[0].body.vy == game.players[1].body.vy);

    set_energy_phase(&game.players[1], true, 4.999);
    float first_energy = game.players[0].energy;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.players[0].boosting && !game.players[1].boosting);
    CHECK(game.players[0].body.vx == 12.0f);
    CHECK(game.players[1].body.vx == -6.0f);
    CHECK(game.players[0].energy < first_energy && game.players[0].energy > 0.99f);
    CHECK(game.players[1].energy > 0.0f && game.players[1].energy < 0.001f);
    CHECK(game.players[0].body.vy == game.players[1].body.vy);
}

static void test_player_two_bubble_interaction(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    game.bubbles[0] = (BBBubble){ .body = { .x = 22.0f, .y = 20.0f },
        .active = true, .age = 2.0f, .captured_enemy = -1 };
    game.players[1].body = (BBBody){ .x = 22.0f, .y = 21.0f, .vy = -18.0f };
    game.players[1].falling = false;
    game.players[1].air_control = true;
    game.players[1].jump_start_y = 25.0f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.bubbles[0].popping && game.events.pop);

    fixture_mode(&game, 2);
    game.bubbles[0] = (BBBubble){ .body = { .x = 22.0f, .y = 20.0f },
        .active = true, .age = 2.0f, .captured_enemy = -1 };
    game.players[1].body = (BBBody){ .x = 22.0f, .y = 18.6f, .vy = 6.0f };
    game.players[1].falling = true;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(!game.bubbles[0].popping && !game.events.pop);
    CHECK(game.players[1].body.grounded);
    CHECK(fabsf(game.players[1].body.y - (game.bubbles[0].body.y - 1.725f)) < 0.0001f);
    CHECK(game.players[1].body.vy == game.bubbles[0].body.vy);
    CHECK(game.players[0].body.x == 4.0f);
}

static void test_inactive_player_is_ignored(void)
{
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {{0}, { .move = 1.0f, .jump = true, .fire = true }};
    fixture(&game);
    CHECK(game.player_count == 1 && !game.players[1].active);
    /* 비활성 플레이어와 버블이 겹쳐도 이동, 충전, 발사, 충돌이 없어야 한다. */
    game.players[1].body = (BBBody){ .x = 22.0f, .y = 21.0f, .vy = -18.0f };
    set_energy_phase(&game.players[1], false, 14.999);
    BBPlayer inactive;
    memcpy(&inactive, &game.players[1], sizeof inactive);
    game.bubbles[0] = (BBBubble){ .body = { .x = 22.0f, .y = 20.0f },
        .active = true, .age = 2.0f, .captured_enemy = -1 };
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(memcmp(&game.players[1], &inactive, sizeof inactive) == 0);
    CHECK(active_bubbles(&game) == 1 && !game.bubbles[0].popping);
    CHECK(!game.events.fire && !game.events.jump && !game.events.pop);
}

static void test_player_modes_and_reset(void)
{
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {
        { .move = 1.0f, .jump = true, .fire = true },
        { .move = -1.0f, .jump = true, .fire = true }
    };
    fixture_mode(&game, 2);
    set_energy_phase(&game.players[0], true, 2.0);
    set_energy_phase(&game.players[1], false, 9.0);
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(active_bubbles(&game) == 2);
    bb_game_menu(&game);
    BBPlayer before[BB_MAX_PLAYERS];
    memcpy(before, game.players, sizeof before);
    uint64_t menu_ticks = game.ticks;
    for (int i = 0; i < 120; ++i)
        bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(memcmp(before, game.players, sizeof before) == 0);
    CHECK(game.ticks == menu_ticks && !game.events.fire && !game.events.jump);

    for (int player_count = 1; player_count <= 2; ++player_count) {
        bb_game_start(&game, player_count);
        CHECK(game.player_count == player_count && game.state == BB_STATE_INTRO);
        CHECK(game.ticks == 0 && active_bubbles(&game) == 0);
        CHECK(game.level == 0 && !game.won && bb_game_enemies_left(&game) == 0);
        CHECK(game.maps[0][26][4] == 3);
        for (int p = 0; p < BB_MAX_PLAYERS; ++p) {
            CHECK(game.players[p].active == (p < player_count));
            CHECK(game.players[p].energy == 0.0f && game.players[p].energy_elapsed == 0.0);
            CHECK(!game.players[p].boosting && game.players[p].fire_cooldown == 0.0f);
            CHECK(game.players[p].score == 0);
            CHECK(game.players[p].lives == (p < player_count ? BB_STARTING_LIVES : 0));
        }
        memcpy(before, game.players, sizeof before);
        for (int i = 0; i < 120; ++i)
            bb_game_update(&game, inputs, BB_FIXED_DT);
        CHECK(memcmp(before, game.players, sizeof before) == 0);
        CHECK(active_bubbles(&game) == 0);
        bb_game_skip_intro(&game);
        wait_ticks(&game, 120);
        for (int p = 0; p < player_count; ++p)
            CHECK(fabsf(game.players[p].energy - 1.0f / 15.0f) < 0.000001f);
    }

    const int requested[] = {INT_MIN, 0, 1, 2, INT_MAX};
    const int expected[] = {1, 1, 1, 2, 2};
    for (unsigned i = 0; i < sizeof requested / sizeof requested[0]; ++i) {
        bb_game_start(&game, requested[i]);
        CHECK(game.player_count == expected[i]);
        CHECK(game.players[0].active && game.players[1].active == (expected[i] == 2));
    }
}

static void test_shared_bubble_pool_exhaustion_and_reuse(void)
{
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {{ .fire = true }, { .fire = true }};
    fixture_mode(&game, 2);
    for (int i = 0; i < BB_MAX_BUBBLES; ++i)
        game.bubbles[i] = (BBBubble){ .body = { .x = 16.0f, .y = 4.0f }, .active = true, .captured_enemy = -1 };
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(active_bubbles(&game) == BB_MAX_BUBBLES && !game.events.fire);
    CHECK(game.players[0].fire_cooldown == 0.0f && game.players[1].fire_cooldown == 0.0f);

    /* 슬롯 하나가 비면 한 명만 발사하며, 나머지 플레이어의 대기시간은 소비하지 않는다. */
    game.bubbles[7].active = false;
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(active_bubbles(&game) == BB_MAX_BUBBLES && game.events.fire);
    CHECK(game.players[0].fire_cooldown == 0.4f && game.players[1].fire_cooldown == 0.0f);
    CHECK(game.bubbles[7].body.vx > 0.0f && game.bubbles[7].age < 0.01f);

    game.bubbles[13].popping = true;
    game.bubbles[13].pop_timer = BB_FIXED_DT * 0.5f;
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(active_bubbles(&game) == BB_MAX_BUBBLES - 1);
    CHECK(!game.events.fire && game.players[1].fire_cooldown == 0.0f);
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(active_bubbles(&game) == BB_MAX_BUBBLES && game.events.fire);
    CHECK(game.players[1].fire_cooldown == 0.4f);
    CHECK(!game.bubbles[13].popping && game.bubbles[13].pop_timer == 0.0f);
    CHECK(game.bubbles[13].body.vx < 0.0f && game.bubbles[13].age < 0.01f);
}

static int active_pickups(const BBGame *game)
{
    int count = 0;
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        count += game->pickups[i].active ? 1 : 0;
    return count;
}

static void test_capture_and_release(void)
{
    /* 세 적 유형 모두 제한 시간이 지나면 포획 전 유형을 유지하며 복귀한다. */
    for(int type = 0; type < BB_ENEMY_TYPE_COUNT; ++type)
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
    {
        BBGame game;
        BBInput inputs[BB_MAX_PLAYERS] = {{0}};
        fixture_mode(&game, 2);
        float enemy_x = game.players[p].body.x + 2.8f * (float)game.players[p].facing;
        game.enemies[0] = (BBEnemy){ .body = { .x = enemy_x, .y = 25.05f, .grounded = true },
            .type = (BBEnemyType)type, .active = true,
            .state = BB_ENEMY_WALKING, .facing = 1 };
        inputs[p].fire = true;
        bb_game_update(&game, inputs, BB_FIXED_DT);
        CHECK(game.bubbles[0].captured_enemy == 0);
        CHECK(game.bubbles[0].owner == p);
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
        CHECK(game.enemies[0].active);
        CHECK(game.enemies[0].type == (BBEnemyType)type);
        if(type == BB_ENEMY_ZENCHAN)
            CHECK(game.enemies[0].angry);
    }
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
    int reward = bb_game_pickup_score(game.pickups[0].type);
    game.players[0].body = game.pickups[0].body;
    game.players[0].body.vy = 0.0f;
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].score == reward);
    CHECK(active_pickups(&game) == 0);
    CHECK(game.events.pickup);

    game.pickups[0] = (BBPickup){ .body = game.players[0].body, .type = BB_PICKUP_FRIES, .active = true };
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].score == reward + bb_game_pickup_score(BB_PICKUP_FRIES));
}

static void collide_enemy_with_player(BBGame *game, int index)
{
    game->players[index].invulnerable = 0.0f;
    game->enemies[0] = (BBEnemy){ .body = game->players[index].body,
        .active = true, .state = BB_ENEMY_WALKING, .facing = 1 };
    tick(game, 0.0f, false, false);
    game->enemies[0].active = false;
}

static void test_lives_and_respawn(void)
{
    BBGame game;
    fixture(&game);
    collide_enemy_with_player(&game, 0);
    CHECK(game.players[0].lives == BB_STARTING_LIVES - 1);
    CHECK(game.players[0].state == BB_PLAYER_DEAD);
    CHECK(game.events.death);
    wait_ticks(&game, 190);
    CHECK(game.players[0].state == BB_PLAYER_NORMAL);
    CHECK(game.players[0].invulnerable > 2.8f);
    /* 부활 직후 적과 겹쳐도 무적 시간이 남아 목숨이 다시 줄지 않는다. */
    game.enemies[0] = (BBEnemy){ .body = game.players[0].body,
        .active = true, .state = BB_ENEMY_WALKING };
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].lives == BB_STARTING_LIVES - 1);
    game.enemies[0].active = false;
    for(int remaining = BB_STARTING_LIVES - 2; remaining >= 0; --remaining)
    {
        collide_enemy_with_player(&game, 0);
        CHECK(game.players[0].lives == remaining);
        wait_ticks(&game, 190);
        if(remaining > 0)
        {
            CHECK(game.state == BB_STATE_PLAY);
            CHECK(game.players[0].state == BB_PLAYER_NORMAL);
        }
    }
    CHECK(game.players[0].lives == 0);
    CHECK(game.players[0].state == BB_PLAYER_OUT);
    CHECK(game.state == BB_STATE_SCORE);
    CHECK(!game.won);

}

static void test_levels(void)
{
    BBGame game;
    bb_game_init(&game, NULL, 99);
    CHECK(game.state == BB_STATE_MENU);
    _Static_assert(BB_MAX_PLAYERS == 2, "Two player slots are required");
    _Static_assert(BB_LEVEL_COUNT == 5, "Five rounds are required");
    _Static_assert(BB_STARTING_LIVES == 5, "Players start with five lives");
    _Static_assert(BB_ENEMY_TYPE_COUNT == 3, "Three monster types are required");
    _Static_assert(BB_PICKUP_TYPE_COUNT == 20, "Twenty food types are required");
    bb_game_start(&game, 1);
    CHECK(game.state == BB_STATE_INTRO);
    /* 고정 틱 841회는 7초 경계를 넘어 자동 인트로 종료를 검증한다. */
    wait_ticks(&game, 841);
    CHECK(game.state == BB_STATE_PLAY);
    CHECK(game.players[0].lives == 5);
    for(int level = 0; level < BB_LEVEL_COUNT; ++level)
    {
        bool seen[BB_ENEMY_TYPE_COUNT] = {false};
        int unique_types = 0;
        CHECK(game.level == level);
        CHECK(game.state == BB_STATE_PLAY);
        CHECK(bb_game_enemies_left(&game) == level + 1);
        for(int i = 0; i < BB_MAX_ENEMIES; ++i)
        {
            if(!game.enemies[i].active)
                continue;
            CHECK(game.enemies[i].type >= 0 && game.enemies[i].type < BB_ENEMY_TYPE_COUNT);
            seen[game.enemies[i].type] = true;
        }
        for(int type = 0; type < BB_ENEMY_TYPE_COUNT; ++type)
            unique_types += seen[type] ? 1 : 0;
        CHECK(unique_types == (level < 2 ? level + 1 : BB_ENEMY_TYPE_COUNT));
        game.players[0].invulnerable = 1000.0f;
        memset(game.enemies, 0, sizeof game.enemies);
        tick(&game, 0.0f, false, false);
        CHECK(game.state == BB_STATE_CLEAR);
        wait_ticks(&game, 841);
    }
    CHECK(game.state == BB_STATE_SCORE);
    CHECK(game.won);
    bb_game_menu(&game);
    CHECK(game.state == BB_STATE_MENU);
}

static void test_enemy_ai_and_boulders(void)
{
    BBGame game;
    fixture(&game);
    game.enemies[0] = (BBEnemy){ .body = { .x = 16.0f, .y = 25.05f, .grounded = true },
        .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING,
        .active = true, .angry = true, .facing = 1 };
    tick(&game, 0.0f, false, false);
    CHECK(fabsf(game.enemies[0].body.vx - 10.8f) < 0.001f);
    game.enemies[0].type = BB_ENEMY_MAITA;
    game.enemies[0].throw_timer = 5.0f;
    tick(&game, 0.0f, false, false);
    CHECK(game.boulders[0].active);
    CHECK(game.boulders[0].body.vx > 13.0f);
    game.enemies[0].active = false;
    wait_ticks(&game, 365);
    CHECK(!game.boulders[0].active);

    fixture(&game);
    game.enemies[0] = (BBEnemy){ .body = { .x = 16.0f, .y = 25.05f, .grounded = true },
        .type = BB_ENEMY_MONSTA, .state = BB_ENEMY_WALKING,
        .active = true, .facing = 1, .jump_timer = 1.1f };
    tick(&game, 0.0f, false, false);
    CHECK(game.enemies[0].state == BB_ENEMY_JUMPING);
    CHECK(game.enemies[0].body.vy < -8.0f && game.enemies[0].body.vx == 4.5f);
    game.enemies[0].state = BB_ENEMY_FALLING;
    game.enemies[0].body.y = 10.0f;
    game.enemies[0].facing = -1;
    tick(&game, 0.0f, false, false);
    CHECK(game.enemies[0].body.vx == -4.5f && game.enemies[0].body.vy == 5.5f);
}

static void test_all_enemy_pop_rewards(void)
{
    for(int type = 0; type < BB_ENEMY_TYPE_COUNT; ++type)
    {
        BBGame game;
        fixture(&game);
        game.enemies[0] = (BBEnemy){ .active = true, .type = (BBEnemyType)type,
            .state = BB_ENEMY_CAPTURED };
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
        CHECK(game.enemies[0].type == (BBEnemyType)type);
        game.players[0].body.x = 2.0f;
        wait_ticks(&game, 500);
        CHECK(!game.enemies[0].active);
        CHECK(active_pickups(&game) == 1);
    }
}

static void test_all_pickups_and_round_persistence(void)
{
    BBGame game;
    int total_score = 0;
    fixture(&game);
    CHECK(bb_game_pickup_score(BB_PICKUP_WATERMELON) == 100);
    CHECK(bb_game_pickup_score(BB_PICKUP_FRIES) == 200);
    CHECK(bb_game_pickup_score((BBPickupType)-1) == 0);
    CHECK(bb_game_pickup_score(BB_PICKUP_TYPE_COUNT) == 0);
    for(int cycle = 0; cycle < 2; ++cycle)
    {
        bool seen[BB_PICKUP_TYPE_COUNT] = {false};
        for(int reward = 0; reward < BB_PICKUP_TYPE_COUNT; ++reward)
        {
            if(cycle == 0 && reward == 10)
            {
                /* 라운드가 바뀌어도 보상 순환이 초기화되어 다양성이 줄면 안 된다. */
                memset(game.enemies, 0, sizeof game.enemies);
                tick(&game, 0.0f, false, false);
                CHECK(game.state == BB_STATE_CLEAR);
                wait_ticks(&game, 841);
                CHECK(game.level == 1);
                isolate_physics(&game);
            }
            game.players[0].body.x = 2.0f;
            game.enemies[0] = (BBEnemy){ .body = { .x = 16.0f, .y = 10.0f },
                .active = true, .type = (BBEnemyType)(reward % BB_ENEMY_TYPE_COUNT),
                .state = BB_ENEMY_DEAD, .age = 4.0f };
            tick(&game, 0.0f, false, false);
            CHECK(!game.enemies[0].active);
            CHECK(active_pickups(&game) == 1);
            BBPickupType type = game.pickups[0].type;
            CHECK(type >= 0 && type < BB_PICKUP_TYPE_COUNT);
            CHECK(!seen[type]);
            seen[type] = true;
            int score = bb_game_pickup_score(type);
            CHECK(score > 0);
            game.players[0].body = game.pickups[0].body;
            game.players[0].body.vy = 0.0f;
            tick(&game, 0.0f, false, false);
            total_score += score;
            CHECK(game.players[0].score == total_score);
            CHECK(active_pickups(&game) == 0);
            CHECK(game.events.pickup);
        }
        for(int type = 0; type < BB_PICKUP_TYPE_COUNT; ++type)
            CHECK(seen[type]);
    }
}

static void test_teammate_pop_and_pickup_ownership(void)
{
    for (int owner = 0; owner < BB_MAX_PLAYERS; ++owner) {
        BBGame game;
        int teammate = 1 - owner;
        BBInput inputs[BB_MAX_PLAYERS] = {{0}};
        fixture_mode(&game, 2);
        game.enemies[0] = (BBEnemy){
            .body = { .x = game.players[owner].body.x + 2.8f * (float)game.players[owner].facing,
                      .y = 25.05f, .grounded = true },
            .type = BB_ENEMY_MAITA, .active = true, .state = BB_ENEMY_WALKING, .facing = 1 };
        inputs[owner].fire = true;
        bb_game_update(&game, inputs, BB_FIXED_DT);
        CHECK(game.bubbles[0].owner == owner && game.bubbles[0].captured_enemy == 0);
        game.bubbles[0].body = (BBBody){ .x = 16.0f, .y = 20.0f };
        game.bubbles[0].age = 2.0f;
        game.players[owner].body.x = owner == 0 ? 2.0f : 30.0f;
        game.players[teammate].body = (BBBody){ .x = 16.0f, .y = 21.0f, .vy = -18.0f };
        game.players[teammate].falling = false;
        game.players[teammate].air_control = true;
        game.players[teammate].jump_start_y = 25.0f;
        bb_game_update(&game, NULL, BB_FIXED_DT);
        CHECK(game.bubbles[0].popping && !game.bubbles[0].releasing);
        CHECK(game.players[owner].score == 0 && game.players[teammate].score == 0);
        wait_ticks(&game, 32);
        CHECK(game.enemies[0].state == BB_ENEMY_DEAD);
        game.players[teammate].body.x = teammate == 0 ? 2.0f : 30.0f;
        wait_ticks(&game, 500);
        CHECK(active_pickups(&game) == 1);
        CHECK(game.players[owner].score == 0 && game.players[teammate].score == 0);
        int reward = bb_game_pickup_score(game.pickups[0].type);
        game.players[teammate].body = game.pickups[0].body;
        game.players[teammate].body.vy = 0.0f;
        bb_game_update(&game, NULL, BB_FIXED_DT);
        CHECK(game.events.pickup && active_pickups(&game) == 0);
        CHECK(game.players[teammate].score == reward && game.players[owner].score == 0);
    }
}

static void test_two_player_lives_and_round_mana(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    game.players[0].score = 300;
    game.players[1].score = 500;
    set_energy_phase(&game.players[0], false, 9.0);
    set_energy_phase(&game.players[1], true, 2.0);
    collide_enemy_with_player(&game, 1);
    CHECK(game.players[0].lives == BB_STARTING_LIVES);
    CHECK(game.players[1].lives == BB_STARTING_LIVES - 1);
    CHECK(game.players[1].state == BB_PLAYER_DEAD);
    float dead_energy = game.players[1].energy;
    double dead_elapsed = game.players[1].energy_elapsed;
    float living_energy = game.players[0].energy;
    int waited = 0;
    while (game.players[1].state == BB_PLAYER_DEAD && waited < 190) {
        bb_game_update(&game, NULL, BB_FIXED_DT);
        CHECK(game.players[1].energy == dead_energy && game.players[1].energy_elapsed == dead_elapsed);
        ++waited;
    }
    CHECK(waited < 190 && game.players[1].state == BB_PLAYER_NORMAL);
    CHECK(game.players[1].body.x == 28.0f && game.players[1].facing == -1);
    CHECK(game.players[1].invulnerable > 2.99f);
    CHECK(game.players[0].energy > living_energy);
    CHECK(game.players[0].score == 300 && game.players[1].score == 500);
    game.enemies[0] = (BBEnemy){ .body = game.players[1].body,
        .active = true, .state = BB_ENEMY_WALKING, .facing = 1 };
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.players[1].state == BB_PLAYER_NORMAL && game.players[1].lives == BB_STARTING_LIVES - 1);

    /* 같은 라운드에서 마지막 적을 없애고 넘어가도 각자의 마나와 기록이 이어진다. */
    memset(game.enemies, 0, sizeof game.enemies);
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.state == BB_STATE_CLEAR);
    set_energy_phase(&game.players[0], false, 9.0);
    set_energy_phase(&game.players[1], true, 2.0);
    game.state_time = 6.999f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.level == 1 && game.state == BB_STATE_PLAY && game.events.level);
    CHECK(fabs(game.players[0].energy_elapsed - (9.0 + BB_FIXED_DT)) < 0.000001);
    CHECK(fabs(game.players[1].energy_elapsed - (2.0 + BB_FIXED_DT)) < 0.000001);
    CHECK(!game.players[0].boosting && game.players[1].boosting);
    CHECK(game.players[0].lives == BB_STARTING_LIVES && game.players[1].lives == BB_STARTING_LIVES - 1);
    CHECK(game.players[0].score == 300 && game.players[1].score == 500);
    CHECK(game.players[0].body.x == 4.0f && game.players[1].body.x == 28.0f);

    isolate_physics(&game);
    game.players[0].lives = 1;
    collide_enemy_with_player(&game, 0);
    wait_ticks(&game, 190);
    CHECK(game.players[0].state == BB_PLAYER_OUT && game.players[0].lives == 0);
    CHECK(game.state == BB_STATE_PLAY && game.players[1].state == BB_PLAYER_NORMAL);
    float out_energy = game.players[0].energy;
    BBInput inputs[BB_MAX_PLAYERS] = {{ .move = 1.0f, .jump = true, .fire = true }, {0}};
    game.pickups[0] = (BBPickup){ .body = game.players[0].body, .active = true, .type = BB_PICKUP_FRIES };
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(!game.events.fire && !game.events.jump);
    CHECK(game.players[0].energy == out_energy && game.players[0].score == 300);
    CHECK(game.pickups[0].active);

    /* 남은 플레이어가 라운드를 넘겨도 탈락한 플레이어를 되살리지 않는다. */
    memset(game.enemies, 0, sizeof game.enemies);
    bb_game_update(&game, NULL, BB_FIXED_DT);
    game.state_time = 6.999f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.level == 2 && game.players[0].state == BB_PLAYER_OUT);
    CHECK(game.players[0].energy == out_energy);
    isolate_physics(&game);
    game.players[1].lives = 1;
    collide_enemy_with_player(&game, 1);
    wait_ticks(&game, 190);
    CHECK(game.players[0].state == BB_PLAYER_OUT && game.players[1].state == BB_PLAYER_OUT);
    CHECK(game.state == BB_STATE_SCORE && !game.won);
}

static void test_enemy_targeting_and_player_two_projectile_hit(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    game.enemies[0] = (BBEnemy){ .body = { .x = 18.0f, .y = 25.05f, .grounded = true },
        .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING,
        .facing = -1, .turn_timer = 10.0f };
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.enemies[0].facing == 1 && game.enemies[0].body.vx > 0.0f);
    game.players[1].state = BB_PLAYER_OUT;
    game.players[1].lives = 0;
    game.enemies[0].turn_timer = 10.0f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.enemies[0].facing == -1);

    fixture_mode(&game, 2);
    game.players[1].invulnerable = 0.0f;
    game.boulders[0] = (BBBoulder){ .body = game.players[1].body, .active = true };
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.players[1].state == BB_PLAYER_DEAD && game.players[1].lives == BB_STARTING_LIVES - 1);
    CHECK(game.players[0].state == BB_PLAYER_NORMAL && game.players[0].lives == BB_STARTING_LIVES);
    CHECK(game.events.death);
}

static void test_last_life_during_clear(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    memset(game.enemies, 0, sizeof game.enemies);
    game.state = BB_STATE_CLEAR;
    game.state_time = 6.999f;
    game.players[0].state = BB_PLAYER_DEAD;
    game.players[0].lives = 0;
    game.players[0].death_timer = 1.0f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.level == 1 && game.state == BB_STATE_PLAY);
    CHECK(game.players[0].state == BB_PLAYER_DEAD && game.players[0].lives == 0);
    wait_ticks(&game, 121);
    CHECK(game.players[0].state == BB_PLAYER_OUT);

    /* 최종 클리어 경계와 마지막 목숨의 사망이 겹쳐도 승리로 바뀌지 않는다. */
    game.level = BB_LEVEL_COUNT - 1;
    game.state = BB_STATE_CLEAR;
    game.state_time = 6.999f;
    memset(game.enemies, 0, sizeof game.enemies);
    game.players[1].state = BB_PLAYER_DEAD;
    game.players[1].lives = 0;
    game.players[1].death_timer = 0.02f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.state == BB_STATE_CLEAR && !game.won);
    wait_ticks(&game, 4);
    CHECK(game.state == BB_STATE_SCORE && !game.won);
    CHECK(game.players[0].state == BB_PLAYER_OUT && game.players[1].state == BB_PLAYER_OUT);
}

static void test_finite_input_and_determinism(void)
{
    BBGame a, b;
    fixture_mode(&a, 2);
    fixture_mode(&b, 2);
    bb_game_start(&a, 2);
    bb_game_start(&b, 2);
    bb_game_skip_intro(&a);
    bb_game_skip_intro(&b);
    uint64_t previous_tick = a.ticks;
    bb_game_update(&a, NULL, NAN);
    bb_game_update(&a, NULL, INFINITY);
    bb_game_update(&a, NULL, -1.0f);
    bb_game_update(&a, NULL, 1.0f);
    CHECK(a.ticks == previous_tick);
    uint32_t random = 67891;
    bool saw_enemy_movement = false;
    for (int step = 0; step < 60000; ++step) {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        BBInput inputs[BB_MAX_PLAYERS] = {{0}};
        for (int p = 0; p < BB_MAX_PLAYERS; ++p) {
            random = random * UINT32_C(1664525) + UINT32_C(1013904223);
            inputs[p].move = (float)((int)((random / 256u) % 3u) - 1);
            inputs[p].jump = (random / 4096u) % 16u == 0;
            inputs[p].fire = (random / 65536u) % 8u == 0;
        }
        if (step % 100 == 0)
            inputs[0].move = NAN;
        if (step % 137 == 0)
            inputs[1].move = INFINITY;
        if (step % 199 == 0)
            inputs[1].move = -100.0f;
        if (a.state == BB_STATE_SCORE) {
            CHECK(b.state == BB_STATE_SCORE);
            bb_game_start(&a, 2);
            bb_game_start(&b, 2);
            bb_game_skip_intro(&a);
            bb_game_skip_intro(&b);
        }
        bb_game_update(&a, inputs, BB_FIXED_DT);
        bb_game_update(&b, inputs, BB_FIXED_DT);
        CHECK(a.rng == b.rng);
        CHECK(a.state == b.state && a.level == b.level);
        for (int p = 0; p < BB_MAX_PLAYERS; ++p) {
            CHECK(a.players[p].body.x == b.players[p].body.x);
            CHECK(a.players[p].body.y == b.players[p].body.y);
            CHECK(a.players[p].body.vx == b.players[p].body.vx);
            CHECK(a.players[p].body.vy == b.players[p].body.vy);
            CHECK(isfinite(a.players[p].body.x) && isfinite(a.players[p].body.y));
            CHECK(isfinite(a.players[p].body.vx) && isfinite(a.players[p].body.vy));
            CHECK(a.players[p].body.x >= 0.0f && a.players[p].body.x <= 32.0f);
            CHECK(a.players[p].body.y > -20.0f && a.players[p].body.y < 31.0f);
            CHECK(isfinite(a.players[p].energy) && a.players[p].energy >= 0.0f && a.players[p].energy <= 1.0f);
            CHECK(a.players[p].energy == b.players[p].energy);
            CHECK(a.players[p].boosting == b.players[p].boosting);
            CHECK(a.players[p].lives == b.players[p].lives);
            CHECK(a.players[p].lives >= 0 && a.players[p].lives <= BB_STARTING_LIVES);
            CHECK(a.players[p].state == b.players[p].state);
            CHECK(a.players[p].score == b.players[p].score);
        }
        for (int i = 0; i < BB_MAX_BUBBLES; ++i) {
            CHECK(a.bubbles[i].active == b.bubbles[i].active);
            if (a.bubbles[i].active) {
                CHECK(isfinite(a.bubbles[i].body.x) && isfinite(a.bubbles[i].body.y));
                CHECK(isfinite(a.bubbles[i].body.vx) && isfinite(a.bubbles[i].body.vy));
                CHECK(a.bubbles[i].body.x == b.bubbles[i].body.x);
                CHECK(a.bubbles[i].body.y == b.bubbles[i].body.y);
                CHECK(a.bubbles[i].popping == b.bubbles[i].popping);
                CHECK(a.bubbles[i].captured_enemy == b.bubbles[i].captured_enemy);
            }
        }
        for (int i = 0; i < BB_MAX_ENEMIES; ++i) {
            CHECK(a.enemies[i].active == b.enemies[i].active);
            if (a.enemies[i].active) {
                CHECK(a.enemies[i].state == b.enemies[i].state);
                CHECK(a.enemies[i].type == b.enemies[i].type);
                CHECK(isfinite(a.enemies[i].body.x) && isfinite(a.enemies[i].body.y));
                CHECK(isfinite(a.enemies[i].body.vx) && isfinite(a.enemies[i].body.vy));
                CHECK(a.enemies[i].body.x == b.enemies[i].body.x && a.enemies[i].body.y == b.enemies[i].body.y);
                if (a.enemies[i].state != BB_ENEMY_SPAWNING &&
                    (a.enemies[i].body.vx != 0.0f || a.enemies[i].body.vy != 0.0f))
                    saw_enemy_movement = true;
            }
        }
        for (int i = 0; i < BB_MAX_BOULDERS; ++i) {
            CHECK(a.boulders[i].active == b.boulders[i].active);
            if (a.boulders[i].active) {
                CHECK(isfinite(a.boulders[i].body.x) && isfinite(a.boulders[i].body.y));
                CHECK(a.boulders[i].body.x == b.boulders[i].body.x && a.boulders[i].body.y == b.boulders[i].body.y);
            }
        }
        for (int i = 0; i < BB_MAX_PICKUPS; ++i) {
            CHECK(a.pickups[i].active == b.pickups[i].active);
            if (a.pickups[i].active) {
                CHECK(a.pickups[i].type == b.pickups[i].type);
                CHECK(isfinite(a.pickups[i].body.x) && isfinite(a.pickups[i].body.y));
                CHECK(a.pickups[i].body.x == b.pickups[i].body.x && a.pickups[i].body.y == b.pickups[i].body.y);
            }
        }
    }
    CHECK(saw_enemy_movement);
}

int main(void)
{
    test_capture_and_release();
    test_pop_drop_and_score();
    test_lives_and_respawn();
    test_levels();
    test_enemy_ai_and_boulders();
    test_all_enemy_pop_rewards();
    test_all_pickups_and_round_persistence();
    test_teammate_pop_and_pickup_ownership();
    test_two_player_lives_and_round_mana();
    test_enemy_targeting_and_player_two_projectile_hit();
    test_last_life_during_clear();
    test_platforms_and_momentum();
    test_walls_wrap_and_fire();
    test_bubble_lifetime_and_player_pop();
    test_energy_cycle_and_boundaries();
    test_energy_horizontal_speed_and_momentum();
    test_energy_lifecycle();
    test_two_player_controls();
    test_independent_energy_and_momentum();
    test_player_two_bubble_interaction();
    test_inactive_player_is_ignored();
    test_player_modes_and_reset();
    test_shared_bubble_pool_exhaustion_and_reuse();
    test_finite_input_and_determinism();
    printf("Core regression tests passed (%d checks, 60000 deterministic stress ticks).\n", checks);
    return EXIT_SUCCESS;
}
