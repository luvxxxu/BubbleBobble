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
    const BBInput inputs[BB_MAX_PLAYERS] = {{ .move = move, .jump = jump, .fire = fire }};
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
    bb_game_start(game);
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
    /* 바닥에서 점프해 일방 통과 발판의 아랫면을 지나 윗면에 착지한다. */
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
    /* 세 적 유형 모두 제한 시간이 지나면 포획 전 유형을 유지하며 복귀한다. */
    for(int type = 0; type < BB_ENEMY_TYPE_COUNT; ++type)
    {
        BBGame game;
        fixture(&game);
        game.enemies[0] = (BBEnemy){ .body = { .x = 6.8f, .y = 25.05f, .grounded = true },
            .type = (BBEnemyType)type, .active = true,
            .state = BB_ENEMY_WALKING, .facing = 1 };
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
    CHECK((game.events & BB_EVENT_PICKUP) != 0);

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
    CHECK((game.events & BB_EVENT_DEATH) != 0);
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
    CHECK(BB_MAX_PLAYERS == 1);
    CHECK(BB_LEVEL_COUNT == 5);
    CHECK(BB_STARTING_LIVES == 5);
    CHECK(BB_ENEMY_TYPE_COUNT == 3);
    CHECK(BB_PICKUP_TYPE_COUNT == 20);
    bb_game_start(&game);
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
                memset(game.enemies, 0, sizeof game.enemies);
                game.enemies[BB_MAX_ENEMIES - 1] = (BBEnemy){
                    .active = true, .state = BB_ENEMY_CAPTURED };
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
            CHECK((game.events & BB_EVENT_PICKUP) != 0);
        }
        for(int type = 0; type < BB_PICKUP_TYPE_COUNT; ++type)
            CHECK(seen[type]);
    }
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
    /* 30/60/120Hz에서 충전/가속 경계와 반복 주기 오차를 확인한다. */
    const int rates[] = {30, 60, 120};
    CHECK(BB_ENERGY_CHARGE_SECONDS == 15.0f);
    CHECK(BB_ENERGY_BOOST_SECONDS == 5.0f);
    for(unsigned rate_index = 0; rate_index < sizeof rates / sizeof rates[0]; ++rate_index)
    {
        BBGame game;
        int rate = rates[rate_index];
        float dt = 1.0f / (float)rate;
        fixture(&game);
        set_energy_phase(&game.players[0], false, 0.0);
        for(int cycle = 0; cycle < 3; ++cycle)
        {
            for(int tick_index = 0; tick_index < 15 * rate - 1; ++tick_index)
                update_with_dt(&game, dt);
            CHECK(!game.players[0].boosting);
            CHECK(game.players[0].energy > 0.99f && game.players[0].energy < 1.0f);
            update_with_dt(&game, dt);
            CHECK(game.players[0].boosting);
            CHECK(fabsf(game.players[0].energy - 1.0f) < 0.00001f);
            float previous = game.players[0].energy;
            for(int tick_index = 0; tick_index < 5 * rate - 1; ++tick_index)
            {
                update_with_dt(&game, dt);
                CHECK(game.players[0].boosting);
                CHECK(game.players[0].energy < previous);
                CHECK(game.players[0].energy > 0.0f);
                previous = game.players[0].energy;
                if(rate % 2 == 0 && tick_index + 1 == 5 * rate / 2)
                    CHECK(fabsf(game.players[0].energy - 0.5f) < 0.00001f);
            }
            update_with_dt(&game, dt);
            CHECK(!game.players[0].boosting);
            CHECK(game.players[0].energy >= 0.0f && game.players[0].energy < 0.00001f);
        }
    }

    /* 한 틱이 구간 경계를 넘어갈 때 남은 시간을 다음 구간으로 이월한다. */
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
    for(int direction = -1; direction <= 1; direction += 2)
    {
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

        /* 점프 관성도 가속 시작과 종료 시 현재 배율을 따라야 한다. */
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
    bb_game_start(&game);
    CHECK(game.players[0].lives == BB_STARTING_LIVES);
    CHECK(game.players[0].energy == 0.0f);
    CHECK(game.players[0].energy_elapsed == 0.0);
    CHECK(!game.players[0].boosting);
    wait_ticks(&game, 120);
    CHECK(game.players[0].energy == 0.0f);
    bb_game_skip_intro(&game);
    CHECK(game.players[0].energy == 0.0f);
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
    /* 동일한 시드와 입력열은 긴 실행에서도 같은 상태를 만들고 NaN 이동 입력을 견딘다. */
    uint32_t random = 67891;
    for(int step = 0; step < 60000; ++step)
    {
        random = random * UINT32_C(1664525) + UINT32_C(1013904223);
        BBInput inputs[BB_MAX_PLAYERS] = {
            { .move = (float)((int)(random % 3) - 1), .jump = (random & 15) == 0, .fire = (random & 7) == 0 }
        };
        if(step % 100 == 0)
            inputs[0].move = NAN;
        if(a.state == BB_STATE_SCORE)
        {
            bb_game_start(&a);
            bb_game_start(&b);
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
            CHECK(a.players[p].lives >= 0 && a.players[p].lives <= BB_STARTING_LIVES);
            CHECK(isfinite(a.players[p].energy) && a.players[p].energy >= 0.0f && a.players[p].energy <= 1.0f);
            CHECK(a.players[p].energy == b.players[p].energy);
            CHECK(a.players[p].boosting == b.players[p].boosting);
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
    test_lives_and_respawn();
    test_levels();
    test_enemy_ai_and_boulders();
    test_all_enemy_pop_rewards();
    test_all_pickups_and_round_persistence();
    test_energy_cycle_and_boundaries();
    test_energy_horizontal_speed_and_momentum();
    test_energy_lifecycle();
    test_finite_input_and_determinism();
    printf("Core regression tests passed (%d checks, 60000 deterministic stress ticks).\n", checks);
    return EXIT_SUCCESS;
}
