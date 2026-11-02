/* 게임 규칙의 자동 검사 모음이다. 창이나 키보드 대신 BBInput 값을 직접 주고
 * bb_game_update를 호출한다. fixture로 출발 상황을 만든 뒤, 시간을 진행하고,
 * CHECK로 기대한 위치·목숨·점수·상태가 맞는지 확인하는 순서로 읽으면 된다.
 * 시험 중 직접 바꾸는 위치와 타이머는 특정 경계 상황을 빨리 재현하기 위한 값이다. */
#include "bb_game.h"
#include "bb_levels.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 실패 위치와 조건식 문자열을 함께 출력하고 성공/실패와 관계없이 실행한 검사 수를 센다.
 * do/while 형태는 if문의 본문에서도 매크로 전체를 한 문장처럼 사용하기 위한 것이다. */
static int checks;
#define CHECK(condition) do { \
    ++checks; \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

/* 한 번의 고정 물리 갱신만 진행한다. 첫 플레이어에게만 입력을 전달하고
 * 두 번째 입력은 0으로 채워 단독 조작 시험에 다른 조작이 섞이지 않게 한다. */
static void tick(BBGame *game, float move, bool jump, bool fire)
{
    const BBInput inputs[BB_MAX_PLAYERS] = {{ .move = move, .jump = jump, .fire = fire }};
    bb_game_update(game, inputs, BB_FIXED_DT);
}

/* 새 입력 없이 지정한 횟수만큼 시간을 진행한다. 실제로 잠들지 않으므로
 * 발사 대기, 부활, 라운드 전환을 컴퓨터 성능에 관계없이 재현할 수 있다. */
static void wait_ticks(BBGame *game, int count)
{
    for (int i = 0; i < count; ++i)
        tick(game, 0.0f, false, false);
}

/* 현재 적을 지운 뒤 아주 오래 등장하지 않는 적 하나만 남긴다.
 * 적이 없다는 이유로 다음 라운드가 시작되어 시험용 위치가 초기화되는 것을 막는다. */
static void isolate_physics(BBGame *game)
{
    /* 이동 검사 중 자동 클리어를 막되 플레이어와 충돌하지 않는 대기 적이다. */
    memset(game->enemies, 0, sizeof game->enemies);
    game->enemies[BB_MAX_ENEMIES - 1] = (BBEnemy){
        .body = { .x = 16.0f, .y = 0.0f }, .active = true,
        .state = BB_ENEMY_SPAWNING, .spawn_delay = 1000000.0f };
}

/* 모든 라운드에 높이 26의 단단한 바닥을 만든 공통 시험 환경이다.
 * 고정 난수 시드로 새 게임을 시작하고 착지까지 기다려 1인/2인 시험의
 * 출발 상태를 같게 만든다. 실제 맵 배치는 test_levels.c에서 별도로 검증한다. */
static void fixture_mode(BBGame *game, int player_count)
{
    /* 장식/공중 발판 없이 바닥만 있어 이동량 계산을 다른 충돌과 분리할 수 있다. */
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{{0}}};
    for (int level = 0; level < BB_LEVEL_COUNT; ++level)
        for (int x = 0; x < BB_MAP_WIDTH; ++x)
            maps[level][26][x] = 3;
    /* 임시 맵은 초기화 함수가 복사하므로 이 함수가 끝나도 게임의 맵은 유효하다. */
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

/* 플레이어 수를 따로 지정하지 않는 시험에는 동일한 1인 환경을 제공한다. */
static void fixture(BBGame *game)
{
    fixture_mode(game, 1);
}

/* 시험용 맵에 발판을 추가한 뒤 실제 라운드 진입 경로로 발판 정보를 다시 만든다.
 * 초기 위치로 돌아간 플레이어는 고정 바닥에 착지시켜 기존 물리 시험과 조건을 맞춘다. */
static void restart_physics_fixture(BBGame *game)
{
    bb_game_start(game, game->player_count);
    bb_game_skip_intro(game);
    isolate_physics(game);
    wait_ticks(game, 30);
}

/* 버블 배열의 사용 중인 슬롯을 센다. 터지는 연출 중에도 active가 참이므로
 * 연출 종료와 실제 슬롯 반환을 구분하여 검사할 수 있다. */
static int active_bubbles(const BBGame *game)
{
    int count = 0;
    for (int i = 0; i < BB_MAX_BUBBLES; ++i)
        count += game->bubbles[i].active ? 1 : 0;
    return count;
}


/* 일방 통과 발판의 아래쪽 통과/위쪽 착지와 점프 중 가로 관성을 확인한다.
 * 처음에는 제자리 점프로 발판을 검사하고 새 환경에서 방향 전환만 따로 검사한다. */
static void test_platforms_and_momentum(void)
{
    BBGame game;
    fixture(&game);
    /* 아래에서 일방 통과 발판을 지나 올라간 뒤 위에서 착지한다. */
    for (int x = 2; x <= 10; ++x)
        game.maps[0][22][x] = 2;
    restart_physics_fixture(&game);
    CHECK(fabsf(game.players[0].body.y - 25.025f) < 0.001f);
    tick(&game, 0.0f, true, false);
    CHECK(game.events.jump);
    CHECK(game.players[0].body.vy < -17.0f);
    /* 최종 착지만 맞는 가짜 성공을 막으려고 점프 도중 발판 위로 올랐는지도 기록한다. */
    bool rose_above = false;
    for (int i = 0; i < 180; ++i) {
        tick(&game, 0.0f, false, false);
        rose_above = rose_above || game.players[0].body.y < 21.025f;
    }
    CHECK(rose_above);
    CHECK(game.players[0].body.grounded);
    CHECK(fabsf(game.players[0].body.y - 21.025f) < 0.001f);

    /* 입력을 놓아도 지상에서 얻은 관성은 남고, 반대 입력을 주면 공중 속도로 바뀐다. */
    fixture(&game);
    tick(&game, 1.0f, true, false);
    CHECK(fabsf(game.players[0].body.vx - BB_PLAYER_GROUND_SPEED) < 0.001f);
    tick(&game, 0.0f, false, false);
    CHECK(fabsf(game.players[0].body.vx - BB_PLAYER_GROUND_SPEED) < 0.001f);
    tick(&game, -1.0f, false, false);
    CHECK(fabsf(game.players[0].body.vx + BB_PLAYER_AIR_SPEED) < 0.001f);
}

/* 단단한 벽 앞에서 이동과 발사가 벽을 관통하지 않는지 검사한다.
 * 발사 대기시간 중 중복 생성 금지와 맵 아래에서 위로 순환하는 경계도 확인한다. */
static void test_walls_wrap_and_fire(void)
{
    BBGame game;
    fixture(&game);
    /* 바닥 바로 위에 세로 벽을 세우고 오른쪽 이동을 충분히 오래 유지한다. */
    for (int y = 22; y <= 25; ++y)
        game.maps[0][y][12] = 3;
    game.players[0].body.x = 10.0f;
    for (int i = 0; i < 90; ++i)
        tick(&game, 1.0f, false, false);
    /* 플레이어 중심 11.1 + 반폭 0.9가 벽 시작 x=12와 일치해야 한다. */
    CHECK(fabsf(game.players[0].body.x - 11.1f) < 0.001f);
    tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);
    CHECK(game.events.fire);
    CHECK(game.bubbles[0].body.x + 0.75f <= 12.001f);
    CHECK(game.bubbles[0].body.y + 0.75f < 26.0f);
    /* 20/120초는 발사 대기 0.4초보다 짧으므로 계속 요청해도 하나만 남아야 한다. */
    for (int i = 0; i < 20; ++i)
        tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);

    fixture(&game);
    /* 바닥까지 지워 아래 경계를 통과하도록 만들고 순환 뒤 과도한 이탈도 검사한다. */
    memset(game.maps, 0, sizeof game.maps);
    game.players[0].body.y = 28.99f;
    game.players[0].body.grounded = false;
    game.players[0].falling = true;
    wait_ticks(&game, 3);
    CHECK(game.players[0].body.y < 1.0f);
    CHECK(game.players[0].body.y > -1.1f);
}

/* 발판 타일을 좌우로 거의 한 칸 옮겨 발사 경로가 새 경계를 사용하는지 확인한다.
 * 실제 경계가 사거리 안에 있으면 그 앞에서 멈추고, 옛 위치만 사거리 안에 있으면
 * 빈 공간처럼 정상 속도로 발사해야 한다. 좌우 발사 모두 같은 규칙을 따라야 한다. */
static void test_shifted_platform_fire_raycast(void)
{
    for(int direction = -1; direction <= 1; direction += 2)
    {
        for(int in_range = 0; in_range <= 1; ++in_range)
        {
            BBGame game;
            fixture(&game);
            /* 원본 타일은 x=12..13, y=20이다. 게임은 이번 틱에도 같은 방향으로 움직인다. */
            game.maps[0][20][12] = 2;
            restart_physics_fixture(&game);
            CHECK(game.platform_count == 1);
            game.platforms[0].offset = 0.9f * (float)direction;
            game.platforms[0].step = 0.0f;
            game.platforms[0].direction = direction;
            float player_x;
            if(direction > 0)
                player_x = in_range ? 9.9f : 9.5f;
            else
                player_x = in_range ? 15.1f : 15.5f;
            /* 발사 높이를 타일과 같은 줄에 맞추고 공중에 두어 바닥 운반을 배제한다. */
            game.players[0].body = (BBBody){ .x = player_x, .y = 20.5f };
            game.players[0].falling = true;
            game.players[0].facing = direction;
            tick(&game, 0.0f, false, true);
            CHECK(game.events.fire && active_bubbles(&game) == 1);
            if(in_range)
            {
                /* 발판 갱신 뒤 좌표로 계산한다. 오른쪽 발사는 왼쪽 면, 왼쪽 발사는
                 * 오른쪽 면에서 버블 반지름 0.75와 여유 0.001만큼 떨어져 생성된다. */
                float boundary = (direction > 0 ? 12.0f : 13.0f) + game.platforms[0].offset;
                float expected_x = boundary - (float)direction * 0.751f;
                /* 생성 직후 같은 틱에 난수 힘이 적용되므로 미세한 이동은 허용한다. */
                CHECK(fabsf(game.bubbles[0].body.x - expected_x) < 0.003f);
                CHECK(fabsf(game.bubbles[0].body.vx) < 0.2f);
            }
            else
            {
                /* 옛 타일만 감지하면 속도가 0이 된다. 새 경계가 사거리 밖인 경우에는
                 * 기본 발사 속도 20에 한 틱의 항력/난수 힘만 반영되어야 한다. */
                CHECK(game.bubbles[0].body.vx * (float)direction > 19.0f);
                CHECK((game.bubbles[0].body.x - player_x) * (float)direction > 2.5f);
            }
        }
    }
}

/* 위쪽 발판을 향한 적의 점프 판단도 이동한 위치를 사용해야 한다.
 * 타일의 새 점유 구간 아래에서는 점프하고, 타일이 떠난 옛 구간에서는 계속 걷는다. */
static void test_shifted_platform_enemy_jump_target(void)
{
    for(int direction = -1; direction <= 1; direction += 2)
    {
        for(int under_platform = 0; under_platform <= 1; ++under_platform)
        {
            BBGame game;
            fixture(&game);
            game.maps[0][22][12] = 2;
            restart_physics_fixture(&game);
            CHECK(game.platform_count == 1);
            game.platforms[0].offset = 0.9f * (float)direction;
            game.platforms[0].step = 0.0f;
            game.platforms[0].direction = direction;
            float enemy_x;
            if(direction > 0)
                enemy_x = under_platform ? 13.5f : 12.4f;
            else
                enemy_x = under_platform ? 11.5f : 12.6f;
            /* 젠찬을 고정 바닥에 세우고 점프 대기시간을 채운다. 몬스타처럼
             * 발판 유무와 관계없이 도약하는 종류를 사용하면 이 검사를 할 수 없다. */
            game.enemies[0] = (BBEnemy){
                .body = { .x = enemy_x, .y = 26.0f - 0.95f, .grounded = true },
                .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING,
                .facing = direction, .jump_timer = 2.0f
            };
            /* 추적 대상은 적의 정면이면서 한 층 위에 둔다. 위쪽 발판 존재 여부만
             * 달라지도록 방향/높이/대기시간이라는 다른 점프 조건은 모두 만족시킨다. */
            game.players[0].body = (BBBody){
                .x = enemy_x + 2.0f * (float)direction, .y = 21.0f
            };
            game.players[0].falling = true;
            tick(&game, 0.0f, false, false);
            if(under_platform)
            {
                CHECK(game.enemies[0].state == BB_ENEMY_JUMPING);
                CHECK(!game.enemies[0].body.grounded && game.enemies[0].body.vy < 0.0f);
                CHECK(game.enemies[0].jump_timer == 0.0f);
            }
            else
            {
                CHECK(game.enemies[0].state == BB_ENEMY_WALKING);
                CHECK(game.enemies[0].body.grounded && game.enemies[0].body.vy == 0.0f);
                CHECK(game.enemies[0].jump_timer > 2.0f);
            }
        }
    }
}

/* 빈 버블의 자연 소멸과 플레이어 충돌에 의한 파괴를 각각 검사한다.
 * 소멸 시작 이벤트뿐 아니라 0.25초 연출이 끝난 뒤 슬롯이 비워지는지도 확인한다. */
static void test_bubble_lifetime_and_player_pop(void)
{
    BBGame game;
    fixture(&game);
    tick(&game, 0.0f, false, true);
    CHECK(active_bubbles(&game) == 1);
    /* 빈 버블은 8초를 넘는 첫 갱신에서 파열을 시작한다. */
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
    /* 버블 아래에서 상승하는 플레이어를 두어 상대 속도가 파괴 기준을 넘게 만든다. */
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

/* 충전 또는 가속 구간의 경과 시간과 표시 에너지를 함께 맞추는 준비 함수다.
 * 경계 직전 상태를 직접 만들 수 있어 매번 수 초를 진행할 필요가 없다. */
static void set_energy_phase(BBPlayer *player, bool boosting, double elapsed)
{
    player->boosting = boosting;
    player->energy_elapsed = elapsed;
    player->energy = boosting ? 1.0f - (float)(elapsed / BB_ENERGY_BOOST_SECONDS)
                              : (float)(elapsed / BB_ENERGY_CHARGE_SECONDS);
}

/* 입력 없이 지정한 시간 간격으로 갱신한다. 고정 120Hz 도우미와 달리
 * 30/60/120Hz에서 시간 계산이 같은 결과를 내는지 확인할 때 사용한다. */
static void update_with_dt(BBGame *game, float dt)
{
    bb_game_update(game, NULL, dt);
}

/* 15초 충전과 5초 가속을 세 가지 갱신 주기로 세 번 반복한다.
 * 구간의 마지막 틱, 에너지의 단조 감소, 구간을 넘어 남은 시간의 이월을 검증한다. */
static void test_energy_cycle_and_boundaries(void)
{
    /* 모두 지원되는 dt 범위이며 같은 초 수를 다른 틱 수로 표현한다. */
    const int rates[] = {30, 60, 120};
    for (unsigned rate_index = 0; rate_index < sizeof rates / sizeof rates[0]; ++rate_index) {
        BBGame game;
        int rate = rates[rate_index];
        float dt = 1.0f / (float)rate;
        fixture(&game);
        set_energy_phase(&game.players[0], false, 0.0);
        for (int cycle = 0; cycle < 3; ++cycle) {
            /* 경계 바로 전에는 아직 충전 중이어야 하고 다음 한 틱에서만 가속으로 바뀐다. */
            for (int tick_index = 0; tick_index < 15 * rate - 1; ++tick_index)
                update_with_dt(&game, dt);
            CHECK(!game.players[0].boosting);
            CHECK(game.players[0].energy > 0.99f && game.players[0].energy < 1.0f);
            update_with_dt(&game, dt);
            CHECK(game.players[0].boosting);
            CHECK(fabsf(game.players[0].energy - 1.0f) < 0.00001f);
            /* 가속 구간에서는 매 틱 감소해야 하며 절반 시점의 에너지는 정확히 0.5이다. */
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
    /* 남은 0.01초보다 큰 dt를 넣어 초과분 0.015초가 다음 구간에 이월되는지 확인한다. */
    set_energy_phase(&game.players[0], false, 14.99);
    update_with_dt(&game, 0.025f);
    CHECK(game.players[0].boosting);
    CHECK(fabsf(game.players[0].energy - 0.997f) < 0.000001f);
    set_energy_phase(&game.players[0], true, 4.99);
    update_with_dt(&game, 0.025f);
    CHECK(!game.players[0].boosting);
    CHECK(fabsf(game.players[0].energy - 0.001f) < 0.000001f);
}

/* 정상/가속 게임을 같은 입력으로 나란히 갱신하여 가로 속도만 두 배인지 확인한다.
 * 좌우 방향, 지상 이동, 점프, 반대 방향 공중 조작, 가속 시작/종료 관성을 포함한다. */
static void test_energy_horizontal_speed_and_momentum(void)
{
    for (int direction = -1; direction <= 1; direction += 2) {
        /* 두 게임은 에너지 상태만 다르게 두어 속도 배율의 효과만 비교한다. */
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

        /* 공중 조작을 꺼 둔 상태에서 가속 경계를 통과하면 기존 관성 자체가 조정되어야 한다. */
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

/* 에너지가 진행되는 게임 상태와 멈추는 상태를 구분하여 검사한다.
 * 사망/탈락 중에는 보존하고, 라운드 클리어 대기에서는 진행하며, 새 게임은 0부터 시작한다. */
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
    /* 죽은 플레이어는 부활 대기시간을 길게 두어 검사 도중 정상 상태로 돌아오지 않게 한다. */
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

    /* 새 게임은 이전 진행을 이어받는 라운드 전환과 달리 에너지와 목숨을 초기화한다. */
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

/* 두 플레이어의 반대 방향 이동/점프/발사가 같은 틱에 독립적으로 처리되는지 확인한다.
 * 이후 한쪽 입력만 바꾸어 다른 쪽의 관성이나 발사 대기시간이 영향을 받지 않는지 검사한다. */
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

    /* 첫 입력 뒤 버튼을 놓아 점프/발사가 다시 발생하지 않는지 확인한다. */
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

/* 서로 다른 에너지 구간에 있는 두 플레이어를 만들어 각자의 가속 전환만
 * 자기 가로 속도를 바꾸는지 확인한다. 같은 세로 속도는 가속의 범위를 검증한다. */
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

    /* 이번에는 2P만 가속 종료 경계를 넘긴다. 1P의 가속과 속도는 계속 유지되어야 한다. */
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

/* 2P도 버블을 아래에서 빠르게 치면 터뜨리고 위에서 느리게 내려오면
 * 올라설 수 있어야 한다. 두 상황을 분리하고 1P 위치는 그대로인지 확인한다. */
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
    /* 버블 위쪽의 느린 낙하는 파괴 대신 발판 접촉으로 처리되어야 한다. */
    game.players[1].body = (BBBody){ .x = 22.0f, .y = 18.6f, .vy = 6.0f };
    game.players[1].falling = true;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(!game.bubbles[0].popping && !game.events.pop);
    CHECK(game.players[1].body.grounded);
    CHECK(fabsf(game.players[1].body.y - (game.bubbles[0].body.y - 1.725f)) < 0.0001f);
    CHECK(game.players[1].body.vy == game.bubbles[0].body.vy);
    CHECK(game.players[0].body.x == 4.0f);
}

/* 1인 모드의 비활성 2P에 입력과 버블을 일부러 겹쳐 둔다.
 * 그 슬롯의 전체 상태가 그대로이고 발사/점프/파괴 이벤트도 생기지 않아야 한다. */
static void test_inactive_player_is_ignored(void)
{
    BBGame game;
    BBInput inputs[BB_MAX_PLAYERS] = {{0}, { .move = 1.0f, .jump = true, .fire = true }};
    fixture(&game);
    CHECK(game.player_count == 1 && !game.players[1].active);
    /* 비활성 플레이어와 버블이 겹쳐도 이동, 충전, 발사, 충돌이 없어야 한다. */
    game.players[1].body = (BBBody){ .x = 22.0f, .y = 21.0f, .vy = -18.0f };
    set_energy_phase(&game.players[1], false, 14.999);
    /* 개별 필드뿐 아니라 전체 구조체를 비교해 의도치 않은 타이머 갱신도 잡는다. */
    BBPlayer inactive;
    memcpy(&inactive, &game.players[1], sizeof inactive);
    game.bubbles[0] = (BBBubble){ .body = { .x = 22.0f, .y = 20.0f },
        .active = true, .age = 2.0f, .captured_enemy = -1 };
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(memcmp(&game.players[1], &inactive, sizeof inactive) == 0);
    CHECK(active_bubbles(&game) == 1 && !game.bubbles[0].popping);
    CHECK(!game.events.fire && !game.events.jump && !game.events.pop);
}

/* 메뉴/인트로에서 조작이 멈추는지, 새 게임에서 인원수에 맞춰 플레이어와
 * 투사체가 초기화되는지 확인한다. 맵은 보존하고 범위 밖 인원수는 1..2로 제한한다. */
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

    /* 양 끝 정수와 정상 범위를 함께 검사해 잘못된 인원수로 배열 범위를 넘지 않게 한다. */
    const int requested[] = {INT_MIN, 0, 1, 2, INT_MAX};
    const int expected[] = {1, 1, 1, 2, 2};
    for (unsigned i = 0; i < sizeof requested / sizeof requested[0]; ++i) {
        bb_game_start(&game, requested[i]);
        CHECK(game.player_count == expected[i]);
        CHECK(game.players[0].active && game.players[1].active == (expected[i] == 2));
    }
}

/* 공유 버블 배열이 가득 찼을 때 생성 실패가 발사 대기시간을 소비하지 않는지 검사한다.
 * 슬롯 하나를 비워 두 사람의 경쟁과 연출이 끝난 슬롯의 다음 틱 재사용도 확인한다. */
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

    /* 파열은 플레이어 발사 처리 뒤에 끝나므로 그 슬롯은 다음 틱부터 재사용된다. */
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

/* 사용 중인 보상 슬롯 수를 센다. 적 한 마리당 보상 한 개 생성과
 * 수집 직후 슬롯 반환을 배열 전체에서 검사하기 위한 도우미다. */
static int active_pickups(const BBGame *game)
{
    int count = 0;
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        count += game->pickups[i].active ? 1 : 0;
    return count;
}

/* 모든 적 종류와 발사 플레이어 조합에서 포획 및 시간 만료 후 탈출을 확인한다.
 * 포획 중에도 남은 적 수에 포함되고, 탈출한 적은 유형을 유지해야 한다. */
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
        /* 포획한 버블은 기존 8초 경계를 지나도 적을 유지하고, 5초 뒤인 13초에 파열한다. */
        game.bubbles[0].age = 7.999f;
        tick(&game, 0.0f, false, false);
        CHECK(game.bubbles[0].active && !game.bubbles[0].popping);
        CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);
        game.bubbles[0].age = 12.999f;
        tick(&game, 0.0f, false, false);
        CHECK(game.bubbles[0].popping);
        CHECK(game.bubbles[0].releasing);
        CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);
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

/* 포획 버블의 시간 경계를 다른 갱신 주기에서도 확인하고, 수명 종료 직전에
 * 포획한 경우에도 기존 남은 시간에 정확히 5초가 더해지는지 검증한다. */
static void test_capture_timeout_boundaries_and_late_capture(void)
{
    const int rates[] = {30, 60, 120};
    for(unsigned i = 0; i < sizeof rates / sizeof rates[0]; ++i)
    {
        BBGame game;
        float dt = 1.0f / (float)rates[i];
        fixture(&game);
        /* 플레이어와 떨어진 공중에 두어 접촉 파괴와 자연 소멸을 분리한다. */
        game.enemies[0] = (BBEnemy){ .active = true, .type = BB_ENEMY_MAITA,
            .state = BB_ENEMY_CAPTURED };
        game.bubbles[0] = (BBBubble){ .body = { .x = 16.0f, .y = 12.0f },
            .active = true, .age = 13.0f - 1.5f * dt, .captured_enemy = 0 };
        bb_game_update(&game, NULL, dt);
        CHECK(game.bubbles[0].active && !game.bubbles[0].popping);
        CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);
        bb_game_update(&game, NULL, dt);
        CHECK(game.bubbles[0].popping && game.bubbles[0].releasing);
        CHECK(game.events.pop);
        CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);

        /* 0.25초 파열 연출이 끝나기 전에는 적이 다시 움직이지 않아야 한다. */
        for(int pop_ticks = 0; game.bubbles[0].active && pop_ticks < rates[i]; ++pop_ticks)
            bb_game_update(&game, NULL, dt);
        CHECK(!game.bubbles[0].active);
        CHECK(game.enemies[0].active && game.enemies[0].type == BB_ENEMY_MAITA);
        CHECK(game.enemies[0].state == BB_ENEMY_FALLING);
    }

    BBGame game;
    fixture(&game);
    /* 발사 후 7.75초가 된 빈 버블에 적을 겹친다. 포획 시 age를 0으로 돌리면
     * 기존보다 5초가 아니라 최대 13초를 더 주게 되므로 그 동작도 방지한다. */
    game.enemies[0] = (BBEnemy){ .body = { .x = 16.0f, .y = 12.0f },
        .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_FALLING, .facing = 1 };
    game.bubbles[0] = (BBBubble){ .body = { .x = 16.0f, .y = 12.0f },
        .active = true, .age = 7.75f, .captured_enemy = -1 };
    tick(&game, 0.0f, false, false);
    CHECK(game.bubbles[0].captured_enemy == 0);
    CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);
    CHECK(fabsf(game.bubbles[0].age - (7.75f + BB_FIXED_DT)) < 0.00001f);
    int elapsed_ticks = 1;
    while(!game.bubbles[0].popping && elapsed_ticks < 700)
    {
        tick(&game, 0.0f, false, false);
        ++elapsed_ticks;
    }
    /* 원래 남은 약 0.25초 + 추가 5초 뒤 소멸해야 한다. 한 틱의 반올림 오차만 허용한다. */
    CHECK(game.bubbles[0].popping && game.bubbles[0].releasing);
    CHECK(fabsf((float)elapsed_ticks * BB_FIXED_DT - 5.25f) <= BB_FIXED_DT * 1.1f);
    CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED);
    wait_ticks(&game, 32);
    CHECK(!game.bubbles[0].active);
    CHECK(game.enemies[0].state != BB_ENEMY_CAPTURED && game.enemies[0].state != BB_ENEMY_DEAD);
    CHECK(game.enemies[0].angry);
}

/* 포획 버블 파괴부터 적 사망, 보상 생성, 수집 점수까지 한 흐름으로 검사한다.
 * 마지막 적을 처치한 뒤에도 라운드 전환 전에 보상을 먹을 수 있어야 한다. */
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
    /* 보상과 플레이어를 겹쳐 실제 수집 충돌 경로로 점수가 더해지는지 검사한다. */
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

/* 지정한 플레이어의 무적 시간을 없애고 같은 위치에 적을 한 틱 배치한다.
 * 피해를 준 적은 즉시 제거하여 이후 부활 대기 중 추가 충돌이 시험을 방해하지 않게 한다. */
static void collide_enemy_with_player(BBGame *game, int index)
{
    game->players[index].invulnerable = 0.0f;
    game->enemies[0] = (BBEnemy){ .body = game->players[index].body,
        .active = true, .state = BB_ENEMY_WALKING, .facing = 1 };
    tick(game, 0.0f, false, false);
    game->enemies[0].active = false;
}

/* 목숨 감소, 사망 연출, 부활 무적, 마지막 목숨 소진 후 종료를 순서대로 확인한다.
 * 마지막 사망도 연출을 마친 다음 탈락과 점수 화면으로 이어져야 한다. */
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
    /* 남은 목숨을 전부 소비하되 각 피격 사이에는 사망 연출을 끝까지 진행한다. */
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

/* 게임 시작과 5개 라운드 전체 진행을 검사한다. 각 라운드 적 수와 종류 분포,
 * 클리어 대기, 최종 승리, 메뉴 복귀를 확인하고 필수 규모는 컴파일 시점에도 검사한다. */
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
        /* 진행 규칙만 검사하므로 적을 제거하고 플레이어 피해를 막아 다음 라운드를 기다린다. */
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

/* 젠찬의 분노 속도, 마이타의 바위 발사/수명, 몬스타의 주기적 도약과
 * 공중 전진을 각각 검사하여 적 종류별 행동이 서로 섞이지 않게 한다. */
static void test_enemy_ai_and_boulders(void)
{
    BBGame game;
    fixture(&game);
    game.enemies[0] = (BBEnemy){ .body = { .x = 16.0f, .y = 25.05f, .grounded = true },
        .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING,
        .active = true, .angry = true, .facing = 1 };
    tick(&game, 0.0f, false, false);
    CHECK(fabsf(game.enemies[0].body.vx - 10.8f) < 0.001f);
    /* 같은 적을 마이타로 바꾸고 발사 경계에 두어 바위 생성과 3초 소멸을 확인한다. */
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

/* 어떤 적 종류든 포획 버블을 직접 터뜨리면 사망 연출 뒤 보상 하나로
 * 바뀌는지 확인한다. 자연 탈출과 달리 원래 적으로 돌아오면 안 된다. */
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

/* 20종 보상을 중복 없이 한 번씩 지급하는 순환을 두 차례 검사한다.
 * 순환 중 라운드를 바꾸어 순서가 보존되는지, 각 보상 점수가 누적되는지도 확인한다. */
static void test_all_pickups_and_round_persistence(void)
{
    BBGame game;
    int total_score = 0;
    fixture(&game);
    CHECK(bb_game_pickup_score(BB_PICKUP_WATERMELON) == 100);
    CHECK(bb_game_pickup_score(BB_PICKUP_FRIES) == 200);
    /* 열거형 범위를 벗어난 값은 점수표를 잘못 인덱싱하지 않고 0점으로 처리한다. */
    CHECK(bb_game_pickup_score((BBPickupType)-1) == 0);
    CHECK(bb_game_pickup_score(BB_PICKUP_TYPE_COUNT) == 0);
    for(int cycle = 0; cycle < 2; ++cycle)
    {
        /* 한 순환 동안 등장한 종류를 기록하여 난수 결과의 순서와 무관하게 중복을 찾는다. */
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
            /* 죽은 적을 보상 변환 제한 직전에 두어 긴 튀김 연출 없이 생성 경로를 실행한다. */
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

/* 한 플레이어가 포획한 버블을 동료가 터뜨릴 수 있는지 양방향으로 검사한다.
 * 점수는 포획자나 파괴자에게 미리 주지 않고 실제 보상을 먹은 사람에게만 지급한다. */
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

/* 2인 모드에서 목숨/점수/에너지의 독립성, 사망 중 에너지 보존,
 * 라운드 전환 시 유지, 한 사람 탈락 후 동료의 계속 플레이와 최종 패배를 확인한다. */
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
    /* 매 틱 죽은 쪽의 에너지를 확인한다. 반복 한도는 부활이 고장 났을 때 무한 대기를 막는다. */
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
    /* 마지막 목숨을 잃은 쪽은 구조 버블로 남고 동료의 플레이는 계속된다. */
    game.players[0].lives = 1;
    collide_enemy_with_player(&game, 0);
    wait_ticks(&game, 190);
    CHECK(game.players[0].state == BB_PLAYER_BUBBLE && game.players[0].lives == 0);
    CHECK(game.state == BB_STATE_PLAY && game.players[1].state == BB_PLAYER_NORMAL);
    float out_energy = game.players[0].energy;
    BBInput inputs[BB_MAX_PLAYERS] = {{ .move = 1.0f, .jump = true, .fire = true }, {0}};
    game.pickups[0] = (BBPickup){ .body = game.players[0].body, .active = true, .type = BB_PICKUP_FRIES };
    bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(!game.events.fire && !game.events.jump);
    CHECK(game.players[0].energy == out_energy && game.players[0].score == 300);
    CHECK(game.pickups[0].active);

    /* 라운드를 넘겨도 구조 버블은 동료가 구할 때까지 그대로 유지한다. */
    memset(game.enemies, 0, sizeof game.enemies);
    bb_game_update(&game, NULL, BB_FIXED_DT);
    game.state_time = 6.999f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.level == 2 && game.players[0].state == BB_PLAYER_BUBBLE);
    CHECK(game.players[0].energy == out_energy);
    isolate_physics(&game);
    game.players[1].lives = 1;
    collide_enemy_with_player(&game, 1);
    wait_ticks(&game, 190);
    CHECK(game.players[0].state == BB_PLAYER_OUT && game.players[1].state == BB_PLAYER_OUT);
    CHECK(game.state == BB_STATE_SCORE && !game.won);
}

/* 적이 가장 가까운 정상 플레이어를 추적하고 탈락한 플레이어는 제외하는지 확인한다.
 * 바위가 2P와 충돌할 때 2P만 피해를 받는지도 별도 환경에서 검사한다. */
static void test_enemy_targeting_and_player_two_projectile_hit(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    game.enemies[0] = (BBEnemy){ .body = { .x = 18.0f, .y = 25.05f, .grounded = true },
        .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING,
        .facing = -1, .turn_timer = 10.0f };
    bb_game_update(&game, NULL, BB_FIXED_DT);
    CHECK(game.enemies[0].facing == 1 && game.enemies[0].body.vx > 0.0f);
    /* 가까운 2P를 탈락시키면 더 먼 1P가 새 추적 대상이 된다. */
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

/* 클리어 시간 경계에서 마지막 목숨의 사망이 겹치는 경우를 검사한다.
 * 동료가 살아 있으면 진행하되 사망자를 되살리지 않고, 모두 소진했으면 승리하지 않는다. */
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
    CHECK(game.players[0].state == BB_PLAYER_BUBBLE);

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

/* 같은 시드의 게임 둘에 같은 무작위 입력을 60000틱 전달하는 장기 회귀 검사다.
 * 잘못된 시간/입력을 섞어도 좌표가 유한하고 범위를 지키며, 게임 상태와 난수 진행이
 * 항상 같아야 한다. 종료한 게임은 다시 시작하여 실제 플레이 구간을 계속 검사한다. */
static void test_finite_input_and_determinism(void)
{
    BBGame a, b;
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    bb_levels_build(&maps[0][0][0]);
    bb_game_init(&a, &maps[0][0][0], UINT32_C(1234567));
    bb_game_init(&b, &maps[0][0][0], UINT32_C(1234567));
    bb_game_start(&a, 2);
    bb_game_start(&b, 2);
    bb_game_skip_intro(&a);
    bb_game_skip_intro(&b);
    /* 시간 간격 거절은 시뮬레이션 시계를 전혀 진행하지 않아야 한다. */
    uint64_t previous_tick = a.ticks;
    bb_game_update(&a, NULL, NAN);
    bb_game_update(&a, NULL, INFINITY);
    bb_game_update(&a, NULL, -1.0f);
    bb_game_update(&a, NULL, 1.0f);
    CHECK(a.ticks == previous_tick);
    /* 게임 내부 RNG와 별도인 고정 시드로 입력을 만든다. 실패한 입력열도 재현할 수 있다. */
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
        /* 비유한 값은 중립 입력, 범위 밖 유한 값은 허용 범위로 제한되어야 한다. */
        if (step % 100 == 0)
            inputs[0].move = NAN;
        if (step % 137 == 0)
            inputs[1].move = INFINITY;
        if (step % 199 == 0)
            inputs[1].move = -100.0f;
        /* 점수 화면만 수만 틱 비교하지 않도록 종료 시 동일한 절차로 재시작한다. */
        if (a.state == BB_STATE_SCORE) {
            CHECK(b.state == BB_STATE_SCORE);
            bb_game_start(&a, 2);
            bb_game_start(&b, 2);
            bb_game_skip_intro(&a);
            bb_game_skip_intro(&b);
        }
        bb_game_update(&a, inputs, BB_FIXED_DT);
        bb_game_update(&b, inputs, BB_FIXED_DT);
        /* 구조체 전체 비교 대신 실제 의미가 있는 필드를 비교해 패딩 바이트 영향을 배제한다. */
        CHECK(a.rng == b.rng);
        CHECK(a.state == b.state && a.level == b.level);
        CHECK(a.round_enemy_quota == b.round_enemy_quota);
        CHECK(a.platform_warning_cooldown == b.platform_warning_cooldown);
        CHECK(a.platform_last_choice == b.platform_last_choice);
        /* 발판 이동도 같은 시간/입력에서 동일해야 하며 왕복 범위를 벗어나지 않는다. */
        CHECK(a.platform_count == BB_LEVEL_PLATFORM_COUNT && a.platform_count == b.platform_count);
        for (int i = 0; i < a.platform_count; ++i) {
            const BBMovingPlatform *ap = &a.platforms[i];
            const BBMovingPlatform *bp = &b.platforms[i];
            CHECK(ap->x == bp->x && ap->y == bp->y && ap->width == bp->width);
            CHECK(isfinite(ap->offset) && fabsf(ap->offset) <= BB_PLATFORM_TRAVEL);
            CHECK(isfinite(ap->step) && fabsf(ap->step) <= ap->speed * BB_FIXED_DT + 0.00001f);
            CHECK(ap->speed >= BB_PLATFORM_MIN_SPEED && ap->speed < BB_PLATFORM_MAX_SPEED);
            CHECK(ap->offset == bp->offset && ap->step == bp->step && ap->speed == bp->speed);
            CHECK(ap->direction == bp->direction && (ap->direction == -1 || ap->direction == 1));
            CHECK(ap->state == bp->state && ap->state_time == bp->state_time);
            CHECK(ap->solid_duration == bp->solid_duration && ap->vanished == bp->vanished);
        }
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
            CHECK(a.players[p].rescue_pop_timer == b.players[p].rescue_pop_timer);
            CHECK(isfinite(a.players[p].rescue_pop_timer) && a.players[p].rescue_pop_timer >= 0.0f);
            CHECK(a.players[p].rescue_time_left == b.players[p].rescue_time_left);
            CHECK(isfinite(a.players[p].rescue_time_left) && a.players[p].rescue_time_left >= 0.0 &&
                  a.players[p].rescue_time_left <= BB_RESCUE_BUBBLE_SECONDS);
            CHECK(a.players[p].rescue_turn_timer == b.players[p].rescue_turn_timer);
            CHECK(isfinite(a.players[p].rescue_turn_timer) && a.players[p].rescue_turn_timer >= 0.0);
            CHECK(a.players[p].rescue_drift_vx == b.players[p].rescue_drift_vx &&
                  a.players[p].rescue_drift_vy == b.players[p].rescue_drift_vy);
            CHECK(isfinite(a.players[p].rescue_drift_vx) && isfinite(a.players[p].rescue_drift_vy));
            CHECK(a.players[p].rescue_rng == b.players[p].rescue_rng);
            CHECK(a.players[p].score == b.players[p].score);
        }
        /* 비활성 슬롯은 이전 값을 남길 수 있으므로 활성 여부를 먼저 맞추고 사용 중인 값만 검사한다. */
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
    /* 두 게임이 똑같이 멈춰 있어도 통과하는 일을 막기 위해 실제 적 이동을 한 번 이상 요구한다. */
    CHECK(saw_enemy_movement);
}

/* 크기 변경은 좌표만 옮겨야 하며 속도와 지지 상태는 그대로 유지해야 한다. */
static void check_translated_body(const BBBody *body, const BBBody *previous, float dx, float dy)
{
    CHECK(fabsf(body->x - (previous->x + dx)) < 0.0001f);
    CHECK(fabsf(body->y - (previous->y + dy)) < 0.0001f);
    CHECK(body->vx == previous->vx && body->vy == previous->vy);
    CHECK(body->grounded == previous->grounded);
}

/* 플레이 도중 해상도를 바꿔도 독립 발판의 위상, 타이머, 점수와 난수열을
 * 초기화하지 않는다. 발판과 탑승자가 함께 옮겨진 뒤 다음 틱에도 동승이 이어진다. */
static void test_world_resize_preserves_gameplay(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame game, previous;
    bb_levels_build(&maps[0][0][0]);
    bb_game_init(&game, &maps[0][0][0], 17);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    isolate_physics(&game);
    wait_ticks(&game, 120);
    int lower = 0;
    while(lower < game.platform_count && game.platforms[lower].y != 22)
        ++lower;
    CHECK(lower < game.platform_count);
    game.players[0].body = (BBBody){ .x = 6.0f + game.platforms[lower].offset,
        .y = 22.0f - 0.975f, .vx = 1.75f, .grounded = true };
    game.players[0].jump_start_y = game.players[0].body.y;
    game.players[0].score = 1234;
    game.players[0].lives = 4;
    game.players[0].energy = 0.3f;
    game.players[0].energy_elapsed = 4.5;
    game.players[0].fire_cooldown = 0.2f;
    game.enemies[0] = (BBEnemy){ .body = {.x = 20.0f, .y = 10.0f, .vx = 2.0f, .vy = -1.0f},
        .active = true, .state = BB_ENEMY_CAPTURED, .spawn_y = 9.25f, .age = 2.0f };
    /* 등장 연출도 중간 높이를 유지해야 하며 다음 틱에 옛 원점으로 되감기면 안 된다. */
    game.enemies[1] = (BBEnemy){ .body = {.x = 24.0f, .y = 4.0f},
        .active = true, .state = BB_ENEMY_SPAWNING, .spawn_y = 8.0f, .age = 1.0f };
    /* 두 적을 직접 주입한 시험이다. 새 적이 필요 없는 경우의 상태 보존만 검사한다. */
    game.round_enemy_quota = 2;
    game.bubbles[0] = (BBBubble){ .body = game.enemies[0].body,
        .active = true, .captured_enemy = 0, .age = 3.5f };
    game.boulders[0] = (BBBoulder){ .body = {.x = 18.0f, .y = 8.0f, .vx = -2.0f, .vy = 1.0f},
        .active = true, .age = 1.0f };
    game.pickups[0] = (BBPickup){ .body = {.x = 12.0f, .y = 18.0f - 0.95f, .grounded = true},
        .active = true, .age = 1.5f, .type = BB_PICKUP_WATERMELON };
    previous = game;
    bb_game_resize(&game, 64, 40);
    CHECK(game.world_width == 64 && game.world_height == 40);
    CHECK(game.rng == previous.rng && game.ticks == previous.ticks);
    CHECK(game.level == previous.level && game.state == previous.state);
    CHECK(game.level_time == previous.level_time && game.state_time == previous.state_time);
    CHECK(memcmp(game.maps, previous.maps, sizeof game.maps) == 0);
    CHECK(memcmp(game.pickup_order, previous.pickup_order, sizeof game.pickup_order) == 0);
    CHECK(game.pickup_cursor == previous.pickup_cursor);
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        BBBody expected = previous.players[i].body;
        expected.vx *= 2.0f;
        check_translated_body(&game.players[i].body, &expected, 16.0f, 12.0f);
        CHECK(game.players[i].jump_start_y == previous.players[i].jump_start_y + 12.0f);
        CHECK(game.players[i].score == previous.players[i].score && game.players[i].lives == previous.players[i].lives);
        CHECK(game.players[i].energy == previous.players[i].energy);
        CHECK(game.players[i].energy_elapsed == previous.players[i].energy_elapsed);
        CHECK(game.players[i].fire_cooldown == previous.players[i].fire_cooldown);
    }
    check_translated_body(&game.enemies[0].body, &previous.enemies[0].body, 16.0f, 12.0f);
    check_translated_body(&game.enemies[1].body, &previous.enemies[1].body, 16.0f, 12.0f);
    check_translated_body(&game.bubbles[0].body, &previous.bubbles[0].body, 16.0f, 12.0f);
    check_translated_body(&game.boulders[0].body, &previous.boulders[0].body, 16.0f, 12.0f);
    check_translated_body(&game.pickups[0].body, &previous.pickups[0].body, 16.0f, 12.0f);
    CHECK(game.enemies[0].spawn_y == previous.enemies[0].spawn_y + 12.0f);
    CHECK(game.enemies[0].age == previous.enemies[0].age && game.bubbles[0].age == previous.bubbles[0].age);
    CHECK(game.boulders[0].age == previous.boulders[0].age && game.pickups[0].age == previous.pickups[0].age);
    CHECK(game.bubbles[0].captured_enemy == previous.bubbles[0].captured_enemy);
    for(int i = 0; i < game.platform_count; ++i)
    {
        CHECK(game.platforms[i].x == previous.platforms[i].x + 16);
        CHECK(game.platforms[i].y == previous.platforms[i].y + 12);
        CHECK(game.platforms[i].offset == previous.platforms[i].offset && game.platforms[i].step == previous.platforms[i].step);
        CHECK(game.platforms[i].speed == previous.platforms[i].speed && game.platforms[i].direction == previous.platforms[i].direction);
    }
    previous = game;
    bb_game_resize(&game, 64, 40);
    CHECK(memcmp(&game, &previous, sizeof game) == 0);
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].body.grounded);
    CHECK(fabsf(game.players[0].body.x - (previous.players[0].body.x + game.platforms[lower].step)) < 0.0001f);
    CHECK(fabsf(game.players[0].body.y - previous.players[0].body.y) < 0.0001f);
    CHECK(game.enemies[1].body.y > previous.enemies[1].body.y);
    CHECK(game.enemies[1].body.y < previous.enemies[1].body.y + 0.1f);
}

/* 실제 플레이 영역이 넓어졌다면 원래 오른쪽 끝인 x=32를 넘어 걸을 수 있고,
 * 양 끝에서 새 벽에 멈추며 거품도 새 벽 앞에서 생성되어야 한다. */
static void test_expanded_world_movement_and_wrap(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame game;
    bb_levels_build(&maps[0][0][0]);
    bb_game_init(&game, &maps[0][0][0], 17);
    bb_game_resize(&game, 64, 40);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    isolate_physics(&game);
    game.players[0].body = (BBBody){ .x = 36.0f, .y = 38.0f - 0.975f, .grounded = true };
    game.players[1].body = (BBBody){ .x = 20.0f, .y = 38.0f - 0.975f, .grounded = true };
    BBInput inputs[BB_MAX_PLAYERS] = {{.move = 1.0f}, {.move = -1.0f}};
    for(int i = 0; i < 600; ++i)
        bb_game_update(&game, inputs, BB_FIXED_DT);
    CHECK(fabsf(game.players[0].body.x - 61.1f) < 0.001f);
    CHECK(fabsf(game.players[1].body.x - 2.9f) < 0.001f);
    CHECK(game.players[0].body.grounded && game.players[1].body.grounded);
    tick(&game, 0.0f, false, true);
    CHECK(game.events.fire && game.bubbles[0].active);
    CHECK(game.bubbles[0].body.x > 60.0f && game.bubbles[0].body.x + 0.75f <= 62.001f);

    /* 원래 높이의 하단을 지났다는 이유로 일찍 순환하지 않고 실제 하단을 기준으로 한다. */
    game.players[0].body = (BBBody){ .x = 32.0f, .y = 29.1f };
    game.players[0].falling = true;
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].body.y > 29.1f && game.players[0].body.y < 30.0f);
    game.players[0].body = (BBBody){ .x = 32.0f, .y = 41.01f };
    game.players[0].falling = true;
    game.bubbles[0] = (BBBubble){ .body = {.x = 40.0f, .y = 41.01f}, .active = true, .captured_enemy = -1 };
    game.boulders[0] = (BBBoulder){ .body = {.x = 45.0f, .y = 41.01f}, .active = true };
    game.pickups[0] = (BBPickup){ .body = {.x = 50.0f, .y = 41.01f}, .active = true };
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].body.y > -1.1f && game.players[0].body.y < 0.0f);
    CHECK(game.bubbles[0].body.y > -1.1f && game.bubbles[0].body.y < 0.0f);
    CHECK(game.boulders[0].body.y > -1.1f && game.boulders[0].body.y < 0.0f);
    CHECK(game.pickups[0].body.y > -1.1f && game.pickups[0].body.y < 0.0f);
}

/* 폭을 줄일 때 바깥에 있던 객체가 새 벽 속에 남지 않도록 각 반폭으로 제한한다.
 * 공개 크기 입력의 상하한, NULL과 기본 크기에서의 원본 타일 호환성도 확인한다. */
static void test_world_resize_limits_and_shrink(void)
{
    BBGame game;
    fixture_mode(&game, 2);
    CHECK(game.world_width == BB_MAP_WIDTH && game.world_height == BB_MAP_HEIGHT);
    for(int y = 0; y < BB_MAP_HEIGHT; ++y)
        for(int x = 0; x < BB_MAP_WIDTH; ++x)
            CHECK(bb_game_tile(&game, x, y) == game.maps[game.level][y][x]);
    bb_game_resize(NULL, 64, 40);
    CHECK(bb_game_tile(NULL, 0, 0) == 0);
    bb_game_resize(&game, INT_MAX, INT_MAX);
    CHECK(game.world_width == BB_WORLD_MAX_WIDTH && game.world_height == BB_WORLD_MAX_HEIGHT);
    bb_game_resize(&game, 64, 40);
    game.players[0].body.x = 3.0f;
    game.players[0].body.y = 3.0f;
    game.players[0].jump_start_y = 1.0f;
    game.players[1].body.x = 61.0f;
    game.enemies[0] = (BBEnemy){ .body = {.x = 61.0f, .y = 20.0f, .vx = -1.0f}, .active = true };
    game.bubbles[0] = (BBBubble){ .body = {.x = 61.0f, .y = 20.0f, .vx = -2.0f}, .active = true, .captured_enemy = -1 };
    game.boulders[0] = (BBBoulder){ .body = {.x = 3.0f, .y = 20.0f, .vx = 1.0f}, .active = true };
    game.pickups[0] = (BBPickup){ .body = {.x = 61.0f, .y = 20.0f}, .active = true };
    uint32_t rng = game.rng;
    bb_game_resize(&game, INT_MIN, INT_MIN);
    CHECK(game.world_width == BB_MAP_WIDTH && game.world_height == BB_MAP_HEIGHT);
    CHECK(game.rng == rng);
    CHECK(game.players[0].body.x >= 2.9f && game.players[1].body.x <= 29.1f);
    CHECK(game.players[0].body.y == -1.0f && !game.players[0].body.grounded);
    CHECK(game.players[0].jump_start_y == -3.0f);
    CHECK(game.enemies[0].body.x <= 29.1f && game.enemies[0].body.vx == -1.0f);
    CHECK(game.bubbles[0].body.x <= 29.25f && game.bubbles[0].body.vx == -2.0f);
    CHECK(game.boulders[0].body.x >= 2.75f && game.boulders[0].body.vx == 1.0f);
    CHECK(game.pickups[0].body.x <= 29.05f);
}

/* 폭 비례 이동은 플레이어의 가로축에만 적용한다. 기본/2배/3배/최대 폭에서
 * 지상, 점프 관성, 공중 제어와 마나 가속을 검사하고 적의 속도는 따로 비교한다. */
static void test_player_width_speed(void)
{
    const int widths[] = {32, 64, 96, BB_WORLD_MAX_WIDTH};
    for(unsigned w = 0; w < sizeof widths / sizeof widths[0]; ++w)
    {
        for(int boosted = 0; boosted <= 1; ++boosted)
        {
            for(int direction = -1; direction <= 1; direction += 2)
            {
                BBGame game;
                fixture(&game);
                bb_game_resize(&game, widths[w], BB_MAP_HEIGHT);
                isolate_physics(&game);
                game.players[0].body = (BBBody){ .x = (float)widths[w] * 0.5f,
                    .y = 25.025f, .grounded = true };
                set_energy_phase(&game.players[0], boosted != 0, 1.0);
                float multiplier = (float)widths[w] / BB_MAP_WIDTH * (boosted ? 2.0f : 1.0f);
                float before_x = game.players[0].body.x;
                tick(&game, (float)direction, false, false);
                CHECK(fabsf(game.players[0].body.vx - (float)direction * BB_PLAYER_GROUND_SPEED * multiplier) < 0.0001f);
                CHECK(fabsf(game.players[0].body.x - before_x - game.players[0].body.vx * BB_FIXED_DT) < 0.0001f);
                tick(&game, (float)direction, true, false);
                CHECK(fabsf(game.players[0].body.vy + 17.75f) < 0.0001f);
                CHECK(fabsf(game.players[0].body.vx - (float)direction * BB_PLAYER_GROUND_SPEED * multiplier) < 0.0001f);
                tick(&game, 0.0f, false, false);
                CHECK(fabsf(game.players[0].body.vx - (float)direction * BB_PLAYER_GROUND_SPEED * multiplier) < 0.0001f);
                tick(&game, (float)-direction, false, false);
                CHECK(fabsf(game.players[0].body.vx + (float)direction * BB_PLAYER_AIR_SPEED * multiplier) < 0.0001f);
                CHECK(fabsf(game.players[0].body.vy + 17.25f) < 0.0001f);
                game.players[0].falling = true;
                tick(&game, (float)direction, false, false);
                CHECK(fabsf(game.players[0].body.vx - (float)direction * BB_PLAYER_AIR_SPEED * multiplier) < 0.0001f);
                CHECK(game.players[0].body.vy == 6.0f);
                game.enemies[0] = (BBEnemy){ .body = {.x = 4.0f, .y = 25.05f, .grounded = true},
                    .active = true, .state = BB_ENEMY_WALKING, .type = BB_ENEMY_ZENCHAN, .facing = 1 };
                tick(&game, 0.0f, false, false);
                CHECK(fabsf(game.enemies[0].body.vx - 6.0f) < 0.0001f);
            }
        }
    }
}

/* 입력이 없는 점프 관성도 즉시 새 폭에 맞아야 한다. 확대/축소를 반복해도
 * 배율이 중복 누적되지 않고, 마나 가속의 시작/끝은 정확히 두 배/절반이다. */
static void test_width_resize_momentum(void)
{
    const int widths[] = {64, 96, 32, 256, 64, 32};
    BBGame game;
    fixture(&game);
    game.players[0].body = (BBBody){ .x = 16.0f, .y = 10.0f, .vx = 6.0f, .vy = -5.0f };
    game.players[0].falling = false;
    game.players[0].air_control = false;
    game.players[0].jump_start_y = 20.0f;
    uint64_t ticks = game.ticks;
    for(unsigned i = 0; i < sizeof widths / sizeof widths[0]; ++i)
    {
        bb_game_resize(&game, widths[i], BB_MAP_HEIGHT);
        CHECK(fabsf(game.players[0].body.vx - 6.0f * (float)widths[i] / BB_MAP_WIDTH) < 0.0001f);
        CHECK(game.players[0].body.vy == -5.0f && game.players[0].body.y == 10.0f);
        CHECK(game.ticks == ticks && !game.players[0].air_control && !game.players[0].falling);
        float vx = game.players[0].body.vx;
        bb_game_resize(&game, widths[i], BB_MAP_HEIGHT);
        CHECK(game.players[0].body.vx == vx);
    }
    isolate_physics(&game);
    bb_game_resize(&game, 96, BB_MAP_HEIGHT);
    set_energy_phase(&game.players[0], false, 14.999);
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].boosting && fabsf(game.players[0].body.vx - 36.0f) < 0.0001f);
    bb_game_resize(&game, 64, BB_MAP_HEIGHT);
    CHECK(fabsf(game.players[0].body.vx - 24.0f) < 0.0001f);
    set_energy_phase(&game.players[0], true, 4.999);
    tick(&game, 0.0f, false, false);
    CHECK(!game.players[0].boosting && fabsf(game.players[0].body.vx - 12.0f) < 0.0001f);
}

/* 최대 폭의 가속 속도는 96타일/초다. 가장 큰 허용 dt에서도 외벽 두 칸과
 * 내부 한 칸짜리 벽을 건너뛰지 않고, 양방향에서 첫 경계에 멈춰야 한다. */
static void test_max_width_swept_walls(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame game;
    bb_levels_build(&maps[0][0][0]);
    maps[0][24][16] = maps[0][25][16] = 3;
    bb_game_init(&game, &maps[0][0][0], 17);
    bb_game_resize(&game, BB_WORLD_MAX_WIDTH, BB_MAP_HEIGHT);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    isolate_physics(&game);
    BBInput inputs[BB_MAX_PLAYERS] = {{.move = 1.0f}, {.move = -1.0f}};
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        set_energy_phase(&game.players[p], true, 1.0);
    game.players[0].body = (BBBody){.x = 253.0f, .y = 25.025f, .grounded = true};
    game.players[1].body = (BBBody){.x = 3.0f, .y = 25.025f, .grounded = true};
    bb_game_update(&game, inputs, 1.0f / 30.0f);
    CHECK(fabsf(game.players[0].body.x - 253.1f) < 0.0001f);
    CHECK(fabsf(game.players[1].body.x - 2.9f) < 0.0001f);
    game.players[0].body = (BBBody){.x = 126.9f, .y = 25.025f, .grounded = true};
    game.players[1].body = (BBBody){.x = 130.1f, .y = 25.025f, .grounded = true};
    bb_game_update(&game, inputs, 1.0f / 30.0f);
    CHECK(fabsf(game.players[0].body.x - 127.1f) < 0.0001f);
    CHECK(fabsf(game.players[1].body.x - 129.9f) < 0.0001f);
    CHECK(game.players[0].body.vx == 0.0f && game.players[1].body.vx == 0.0f);
}

/* 모든 라운드에서 폭 비례 올림을 적용하며 최대 40마리가 배열 안에 들어간다.
 * 기본 폭의 기존 1..5마리 규칙과 홀수 폭의 올림 경계도 함께 확인한다. */
static void test_width_enemy_counts(void)
{
    const int widths[] = {32, 33, 64, 96, BB_WORLD_MAX_WIDTH};
    _Static_assert(BB_MAX_ENEMIES >= 40, "Maximum-width round five needs forty enemy slots");
    for(unsigned w = 0; w < sizeof widths / sizeof widths[0]; ++w)
    {
        BBGame game;
        bb_game_init(&game, NULL, 99);
        bb_game_resize(&game, widths[w], BB_MAP_HEIGHT);
        bb_game_start(&game, 2);
        CHECK(game.round_enemy_quota == 0);
        bb_game_skip_intro(&game);
        for(int level = 0; level < BB_LEVEL_COUNT; ++level)
        {
            int expected = ((level + 1) * widths[w] + BB_MAP_WIDTH - 1) / BB_MAP_WIDTH;
            CHECK(game.level == level && game.state == BB_STATE_PLAY);
            CHECK(game.round_enemy_quota == expected && bb_game_enemies_left(&game) == expected);
            for(int i = 0; i < BB_MAX_ENEMIES; ++i)
            {
                CHECK(game.enemies[i].active == (i < expected));
                if(i >= expected) continue;
                CHECK(game.enemies[i].state == BB_ENEMY_SPAWNING);
                CHECK(game.enemies[i].body.x >= 2.9f && game.enemies[i].body.x <= (float)widths[w] - 2.9f);
                CHECK(game.enemies[i].type == (BBEnemyType)((i + level) % BB_ENEMY_TYPE_COUNT));
            }
            game.state = BB_STATE_CLEAR;
            game.state_time = 6.999f;
            tick(&game, 0.0f, false, false);
        }
        CHECK(game.state == BB_STATE_SCORE && game.won);
    }
}

/* 플레이 중 확장 즉시 새 적을 예약하되, 기존 포획과 처치 기록은 유지한다.
 * 축소/재확대, 클리어 확정 이후와 새 게임의 생성 할당량도 구분한다. */
static void test_live_width_enemy_additions(void)
{
    BBGame game;
    bb_game_init(&game, NULL, 99);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    game.enemies[0].state = BB_ENEMY_CAPTURED;
    game.enemies[0].body = (BBBody){.x = 16.0f, .y = 10.0f};
    game.enemies[0].age = 2.0f;
    game.bubbles[0] = (BBBubble){.body = game.enemies[0].body, .active = true,
        .captured_enemy = 0, .age = 1.0f};
    game.players[0].score = 1234;
    uint64_t ticks = game.ticks;
    bb_game_resize(&game, 96, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 3 && bb_game_enemies_left(&game) == 3);
    CHECK(game.ticks == ticks && game.players[0].score == 1234);
    CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED && game.enemies[0].age == 2.0f);
    CHECK(game.enemies[0].body.x == 48.0f && game.bubbles[0].body.x == 48.0f);
    CHECK(game.bubbles[0].captured_enemy == 0 && game.bubbles[0].age == 1.0f);
    for(int i = 1; i < 3; ++i)
    {
        CHECK(game.enemies[i].active && game.enemies[i].state == BB_ENEMY_SPAWNING);
        CHECK(game.enemies[i].age == 0.0f && game.enemies[i].spawn_delay >= 0.45f);
    }
    game.enemies[1].active = false;
    game.enemies[1].state = BB_ENEMY_DEAD;
    uint32_t rng = game.rng;
    bb_game_resize(&game, 32, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 3 && bb_game_enemies_left(&game) == 2);
    bb_game_resize(&game, 96, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 3 && bb_game_enemies_left(&game) == 2 && game.rng == rng);
    CHECK(!game.enemies[1].active && game.enemies[1].state == BB_ENEMY_DEAD);
    bb_game_resize(&game, 128, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 4 && bb_game_enemies_left(&game) == 3);
    CHECK(game.enemies[3].active && !game.enemies[1].active);
    CHECK(game.enemies[0].state == BB_ENEMY_CAPTURED && game.bubbles[0].captured_enemy == 0);

    memset(game.enemies, 0, sizeof game.enemies);
    memset(game.bubbles, 0, sizeof game.bubbles);
    game.state = BB_STATE_CLEAR;
    game.state_time = 2.0f;
    rng = game.rng;
    bb_game_resize(&game, 256, BB_MAP_HEIGHT);
    CHECK(game.state == BB_STATE_CLEAR && game.state_time == 2.0f);
    CHECK(game.round_enemy_quota == 4 && bb_game_enemies_left(&game) == 0 && game.rng == rng);
    game.state_time = 6.999f;
    tick(&game, 0.0f, false, false);
    CHECK(game.level == 1 && game.round_enemy_quota == 16 && bb_game_enemies_left(&game) == 16);
    bb_game_start(&game, 2);
    CHECK(game.round_enemy_quota == 0 && bb_game_enemies_left(&game) == 0);
    bb_game_resize(&game, 64, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 0 && bb_game_enemies_left(&game) == 0);
    bb_game_skip_intro(&game);
    CHECK(game.round_enemy_quota == 2 && bb_game_enemies_left(&game) == 2);
    bb_game_menu(&game);
    rng = game.rng;
    bb_game_resize(&game, 96, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 2 && bb_game_enemies_left(&game) == 2 && game.rng == rng);
    game.state = BB_STATE_SCORE;
    bb_game_resize(&game, 128, BB_MAP_HEIGHT);
    CHECK(game.round_enemy_quota == 2 && bb_game_enemies_left(&game) == 2 && game.rng == rng);
}

/* 최대 폭의 마지막 라운드 적이 동시에 음식으로 바뀌어도 보상을 버리지 않는다.
 * 20종 드롭 묶음 두 번이 모두 살아 있어야 하며, 후속 틱에도 슬롯을 덮어쓰면 안 된다. */
static void test_maximum_enemy_pickup_capacity(void)
{
    BBGame game;
    bb_game_init(&game, NULL, 99);
    bb_game_resize(&game, BB_WORLD_MAX_WIDTH, BB_MAP_HEIGHT);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    while(game.level < BB_LEVEL_COUNT - 1)
    {
        game.state = BB_STATE_CLEAR;
        game.state_time = 6.999f;
        tick(&game, 0.0f, false, false);
    }
    CHECK(game.round_enemy_quota == 40 && bb_game_enemies_left(&game) == 40);
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
    {
        game.enemies[i].state = BB_ENEMY_DEAD;
        game.enemies[i].body = (BBBody){.x = 4.0f + (float)i * 6.0f, .y = 10.0f};
        game.enemies[i].bounces = 4;
    }
    tick(&game, 0.0f, false, false);
    CHECK(active_pickups(&game) == 40);
    int types[BB_PICKUP_TYPE_COUNT] = {0};
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
    {
        CHECK(!game.enemies[i].active);
        CHECK(game.pickups[i].active);
        CHECK(game.pickups[i].type >= 0 && game.pickups[i].type < BB_PICKUP_TYPE_COUNT);
        ++types[game.pickups[i].type];
    }
    for(int type = 0; type < BB_PICKUP_TYPE_COUNT; ++type)
        CHECK(types[type] == 2);
    tick(&game, 0.0f, false, false);
    CHECK(active_pickups(&game) == 40);
}

/* 최대 폭에서 서로 반대 방향으로 빠르게 이동하면 한 30Hz 갱신 사이에
 * 바위를 완전히 지나칠 수 있다. 내부 120Hz 접촉과 외부 입력/이벤트 계약을 함께 검사한다. */
static void test_max_width_substep_contacts(void)
{
    BBGame game, reference;
    fixture(&game);
    bb_game_resize(&game, BB_WORLD_MAX_WIDTH, BB_MAP_HEIGHT);
    isolate_physics(&game);
    game.players[0].body = (BBBody){.x = 100.0f, .y = 25.025f, .grounded = true};
    game.players[0].invulnerable = 0.0f;
    set_energy_phase(&game.players[0], true, 1.0);
    game.boulders[0] = (BBBoulder){.body = {.x = 101.8f, .y = 25.025f, .vx = -15.0f}, .active = true};
    int lives = game.players[0].lives;
    uint64_t ticks = game.ticks;
    BBInput inputs[BB_MAX_PLAYERS] = {{.move = 1.0f}};
    bb_game_update(&game, inputs, 1.0f / 30.0f);
    CHECK(game.players[0].state == BB_PLAYER_DEAD);
    CHECK(game.players[0].lives == lives - 1 && game.events.death);
    CHECK(game.ticks == ticks + 1);

    /* 첫 소단계의 점프/발사 이벤트를 외부 호출 종료까지 보존한다.
     * 같은 입력을 직접 120Hz 네 번 전달한 결과와 위치, 난수 진행도 일치해야 한다. */
    fixture(&game);
    bb_game_resize(&game, BB_WORLD_MAX_WIDTH, BB_MAP_HEIGHT);
    isolate_physics(&game);
    game.players[0].body = (BBBody){.x = 100.0f, .y = 25.025f, .grounded = true};
    set_energy_phase(&game.players[0], true, 1.0);
    inputs[0] = (BBInput){.move = 1.0f, .jump = true, .fire = true};
    reference = game;
    ticks = game.ticks;
    bb_game_update(&game, inputs, 1.0f / 30.0f);
    for(int step = 0; step < 4; ++step)
    {
        BBInput once[BB_MAX_PLAYERS] = {{.move = 1.0f, .jump = step == 0, .fire = step == 0}};
        bb_game_update(&reference, once, BB_FIXED_DT);
    }
    CHECK(game.ticks == ticks + 1 && reference.ticks == ticks + 4);
    CHECK(game.events.jump && game.events.fire && active_bubbles(&game) == 1);
    CHECK(fabsf(game.players[0].body.x - reference.players[0].body.x) < 0.0001f);
    CHECK(fabsf(game.players[0].body.y - reference.players[0].body.y) < 0.0001f);
    CHECK(game.players[0].body.vx == reference.players[0].body.vx);
    CHECK(game.players[0].body.vy == reference.players[0].body.vy && game.rng == reference.rng);

    /* 첫 소단계에서는 공중이고 발사 대기도 남아 있다. 뒤 소단계에서 착지하거나
     * 대기가 끝나더라도 이미 소비한 눌림 입력으로 점프/발사를 뒤늦게 해서는 안 된다. */
    fixture(&game);
    bb_game_resize(&game, BB_WORLD_MAX_WIDTH, BB_MAP_HEIGHT);
    isolate_physics(&game);
    game.players[0].body = (BBBody){.x = 100.0f, .y = 25.015f};
    game.players[0].falling = true;
    game.players[0].fire_cooldown = 2.0f * BB_FIXED_DT;
    set_energy_phase(&game.players[0], true, 1.0);
    ticks = game.ticks;
    bb_game_update(&game, inputs, 1.0f / 30.0f);
    CHECK(game.ticks == ticks + 1 && game.players[0].body.grounded);
    CHECK(fabsf(game.players[0].body.y - 25.025f) < 0.0001f);
    CHECK(!game.events.jump && !game.events.fire && active_bubbles(&game) == 0);
}

/* 넓은 맵의 이동만 검사할 때는 소멸 주기를 멀리 미뤄 다른 규칙과 분리한다. */
static void hold_test_platforms(BBGame *game)
{
    for(int i = 0; i < game->platform_count; ++i)
    {
        game->platforms[i].state = BB_PLATFORM_SOLID;
        game->platforms[i].state_time = 0.0;
        game->platforms[i].solid_duration = 1000.0f;
        game->platforms[i].vanished = false;
    }
}

/* 배열 순서에 의존하지 않고 해당 기준 타일을 포함하는 발판을 찾는다. */
static BBMovingPlatform *test_platform_at(BBGame *game, int x, int y)
{
    BBMovingPlatform *found = NULL;
    for(int i = 0; i < game->platform_count; ++i)
        if(game->platforms[i].y == y && x >= game->platforms[i].x &&
           x < game->platforms[i].x + game->platforms[i].width)
            found = &game->platforms[i];
    CHECK(found != NULL);
    return found;
}

/* 원래 위치에서 열두 칸 이동한 발판의 새 위치에만 착지하고, 플레이어/적/음식
 * 각각이 같은 발판의 실제 한 틱 이동량을 따라야 한다. 옛 위치는 빈 공간이다. */
static void test_far_platform_landing_and_riders(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame game;
    bb_levels_build(&maps[0][0][0]);
    bb_game_init(&game, &maps[0][0][0], 17);
    CHECK(bb_game_platform_travel(&game) == 1.0f);
    bb_game_resize(&game, 64, BB_MAP_HEIGHT);
    CHECK(bb_game_platform_travel(&game) == 17.0f);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    isolate_physics(&game);
    hold_test_platforms(&game);
    BBMovingPlatform *platform = test_platform_at(&game, 19, 22);
    platform->offset = 12.0f;
    platform->step = 0.0f;
    game.players[0].body = (BBBody){.x = 35.0f, .y = 22.0f - 0.975f - 0.01f};
    game.players[1].body = (BBBody){.x = 21.0f, .y = 22.0f - 0.975f - 0.01f};
    game.players[0].falling = game.players[1].falling = true;
    game.pickups[0] = (BBPickup){.body = {.x = 38.0f, .y = 22.0f - 0.95f, .grounded = true},
        .active = true, .type = BB_PICKUP_WATERMELON};
    game.enemies[0] = (BBEnemy){.body = {.x = 39.0f, .y = 22.0f - 0.95f, .grounded = true},
        .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING, .facing = 1};
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].body.grounded && !game.players[1].body.grounded);
    CHECK(fabsf(game.players[0].body.y - (22.0f - 0.975f)) < 0.0001f);
    CHECK(game.players[1].body.y > 22.0f - 0.975f);
    float player_x = game.players[0].body.x;
    float enemy_x = game.enemies[0].body.x;
    float food_x = game.pickups[0].body.x;
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].body.grounded && game.enemies[0].body.grounded && game.pickups[0].body.grounded);
    CHECK(fabsf(game.players[0].body.x - player_x - platform->step) < 0.0001f);
    CHECK(fabsf(game.enemies[0].body.x - enemy_x - 6.0f * BB_FIXED_DT - platform->step) < 0.0001f);
    CHECK(fabsf(game.pickups[0].body.x - food_x - platform->step) < 0.0001f);
}

/* 한 칸 검색 범위를 벗어난 발판도 거품의 짧은 레이캐스트와 AI 점프 목표에
 * 반영되어야 한다. 좌우 이동과 원래 위치가 빈 경우를 모두 구분한다. */
static void test_far_platform_raycast_and_ai(void)
{
    for(int direction = -1; direction <= 1; direction += 2)
    {
        for(int shifted = 0; shifted <= 1; ++shifted)
        {
            BBGame game;
            fixture(&game);
            game.maps[0][20][12] = 2;
            restart_physics_fixture(&game);
            bb_game_resize(&game, 64, BB_MAP_HEIGHT);
            isolate_physics(&game);
            hold_test_platforms(&game);
            BBMovingPlatform *platform = &game.platforms[0];
            platform->offset = 12.0f * (float)direction;
            platform->step = 0.0f;
            platform->direction = direction;
            float boundary = (float)platform->x + (direction > 0 ? 0.0f : 1.0f);
            if(shifted) boundary += platform->offset;
            game.players[0].body = (BBBody){.x = boundary - 3.0f * (float)direction, .y = 20.5f};
            game.players[0].falling = true;
            game.players[0].facing = direction;
            tick(&game, 0.0f, false, true);
            CHECK(game.events.fire && active_bubbles(&game) == 1);
            if(shifted)
            {
                float actual = (float)platform->x + platform->offset + (direction > 0 ? 0.0f : 1.0f);
                CHECK(fabsf(game.bubbles[0].body.x - (actual - 0.751f * (float)direction)) < 0.003f);
                CHECK(fabsf(game.bubbles[0].body.vx) < 0.2f);
            }
            else
                CHECK(game.bubbles[0].body.vx * (float)direction > 19.0f);

            fixture(&game);
            game.maps[0][22][12] = 2;
            restart_physics_fixture(&game);
            bb_game_resize(&game, 64, BB_MAP_HEIGHT);
            isolate_physics(&game);
            hold_test_platforms(&game);
            platform = &game.platforms[0];
            platform->offset = 12.0f * (float)direction;
            platform->step = 0.0f;
            platform->direction = direction;
            float enemy_x = (float)platform->x + 0.5f + (shifted ? platform->offset : 0.0f);
            game.enemies[0] = (BBEnemy){.body = {.x = enemy_x, .y = 25.05f, .grounded = true},
                .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING,
                .facing = direction, .jump_timer = 2.0f};
            game.players[0].body = (BBBody){.x = enemy_x + 2.0f * (float)direction, .y = 21.0f};
            game.players[0].falling = true;
            tick(&game, 0.0f, false, false);
            CHECK(game.enemies[0].state == (shifted ? BB_ENEMY_JUMPING : BB_ENEMY_WALKING));
        }
    }
}

/* 공통 이동 범위가 넓어져도 각 발판의 끝은 양벽 안에 남아야 한다.
 * 오른쪽 추가 발판은 +17 전체를 쓰면 벽을 침범하므로 +16에서 돌아선다. */
static void test_wide_platform_wall_reversal(void)
{
    const int rates[] = {30, 60, 120};
    for(unsigned r = 0; r < sizeof rates / sizeof rates[0]; ++r)
    {
        uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
        BBGame game;
        bb_levels_build(&maps[0][0][0]);
        for(int x = 20; x < 30; ++x) maps[0][20][x] = 2;
        bb_game_init(&game, &maps[0][0][0], 17);
        bb_game_resize(&game, 64, BB_MAP_HEIGHT);
        bb_game_start(&game, 1);
        bb_game_skip_intro(&game);
        isolate_physics(&game);
        hold_test_platforms(&game);
        BBMovingPlatform *left = test_platform_at(&game, 19, 22);
        BBMovingPlatform *right = test_platform_at(&game, 36, 20);
        left->offset = -16.999f;
        left->direction = -1;
        right->offset = 15.999f;
        right->direction = 1;
        float left_speed = left->speed;
        float right_speed = right->speed;
        bb_game_update(&game, NULL, 1.0f / (float)rates[r]);
        CHECK(left->direction == 1 && right->direction == -1);
        CHECK((float)left->x + left->offset >= 2.0f);
        CHECK((float)right->x + (float)right->width + right->offset <= 62.0f);
        float maximum_step = BB_PLATFORM_MAX_SPEED / (float)rates[r];
        CHECK(left->offset < -17.0f + maximum_step + 0.001f);
        CHECK(right->offset > 16.0f - maximum_step - 0.001f);
        CHECK(left->speed == left_speed && right->speed == right_speed);
    }
}

/* 폭 축소로 offset을 ±12에서 ±1로 제한할 때 탑승자는 같은 추가 보정을
 * 받아야 한다. 공중 물체는 원점 이동만 받고 다음 틱에는 정상 step만 더한다. */
static void test_shrinking_far_platform_riders(void)
{
    for(int direction = -1; direction <= 1; direction += 2)
    {
        uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
        BBGame game;
        bb_levels_build(&maps[0][0][0]);
        bb_game_init(&game, &maps[0][0][0], 17);
        bb_game_resize(&game, 64, BB_MAP_HEIGHT);
        bb_game_start(&game, 2);
        bb_game_skip_intro(&game);
        isolate_physics(&game);
        hold_test_platforms(&game);
        BBMovingPlatform *platform = test_platform_at(&game, 19, 22);
        platform->offset = 12.0f * (float)direction;
        platform->step = 0.009f * (float)direction;
        platform->direction = direction;
        float left = (float)platform->x + platform->offset;
        game.players[0].body = (BBBody){.x = left + 3.0f, .y = 21.025f, .grounded = true};
        game.players[1].body = (BBBody){.x = left + 3.0f, .y = 19.0f};
        game.players[1].falling = true;
        game.enemies[0] = (BBEnemy){.body = {.x = left + 7.0f, .y = 21.05f, .grounded = true},
            .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING, .facing = 1};
        game.boulders[0] = (BBBoulder){.body = {.x = left + 5.0f, .y = 21.25f, .grounded = true}, .active = true};
        game.pickups[0] = (BBPickup){.body = {.x = left + 8.5f, .y = 21.05f, .grounded = true},
            .active = true, .type = BB_PICKUP_WATERMELON};
        game.bubbles[0] = (BBBubble){.body = {.x = left + 3.0f, .y = 16.0f},
            .active = true, .captured_enemy = -1};
        float speed = platform->speed;
        uint32_t rng = game.rng;
        bb_game_resize(&game, BB_MAP_WIDTH, BB_MAP_HEIGHT);
        CHECK(platform->offset == (float)direction && platform->speed == speed);
        CHECK(game.rng == rng && bb_game_platform_travel(&game) == 1.0f);
        float player_x = 6.0f + (float)direction;
        float enemy_x = 10.0f + (float)direction;
        float boulder_x = 8.0f + (float)direction;
        float food_x = 11.5f + (float)direction;
        CHECK(fabsf(game.players[0].body.x - player_x) < 0.0001f && game.players[0].body.grounded);
        CHECK(fabsf(game.enemies[0].body.x - enemy_x) < 0.0001f);
        CHECK(fabsf(game.boulders[0].body.x - boulder_x) < 0.0001f);
        CHECK(fabsf(game.pickups[0].body.x - food_x) < 0.0001f);
        float airborne_x = direction < 0 ? 2.9f : 18.0f;
        float bubble_x = direction < 0 ? 2.75f : 18.0f;
        CHECK(fabsf(game.players[1].body.x - airborne_x) < 0.0001f && !game.players[1].body.grounded);
        CHECK(fabsf(game.bubbles[0].body.x - bubble_x) < 0.0001f);
        bb_game_resize(&game, BB_MAP_WIDTH, BB_MAP_HEIGHT);
        CHECK(fabsf(game.players[0].body.x - player_x) < 0.0001f);
        tick(&game, 0.0f, false, false);
        CHECK(game.players[0].body.grounded);
        CHECK(fabsf(game.players[0].body.x - player_x - platform->step) < 0.0001f);
        CHECK(fabsf(game.enemies[0].body.x - enemy_x - 6.0f * BB_FIXED_DT - platform->step) < 0.0001f);
        CHECK(fabsf(game.boulders[0].body.x - boulder_x - platform->step) < 0.0001f);
        CHECK(fabsf(game.pickups[0].body.x - food_x - platform->step) < 0.0001f);
        CHECK(fabsf(game.players[1].body.x - airborne_x) < 0.0001f);
    }
}

/* 실제 마지막 피격과 1.5초 사망 연출을 거쳐 구조 대기 상태를 만든다.
 * 동료는 떨어뜨려 두고 공통 대기 적으로 자동 클리어를 막는다. */
static void rescue_fixture_seed(BBGame *game, int waiting, bool full_pool, uint32_t seed)
{
    fixture_mode(game, 2);
    game->players[waiting].lives = 1;
    game->players[waiting].score = 1234;
    set_energy_phase(&game->players[waiting], true, 1.0);
    game->players[waiting].body = (BBBody){.x = 12.0f, .y = 18.0f};
    game->players[waiting].falling = true;
    game->players[1 - waiting].body = (BBBody){.x = 26.0f, .y = 25.025f, .grounded = true};
    if(full_pool)
        for(int i = 0; i < BB_MAX_BUBBLES; ++i)
            game->bubbles[i] = (BBBubble){.body = {.x = 16.0f, .y = 4.0f},
                .active = true, .owner = waiting, .captured_enemy = -1};
    game->rng = seed;
    collide_enemy_with_player(game, waiting);
    CHECK(game->players[waiting].lives == 0 && game->players[waiting].state == BB_PLAYER_DEAD);
    wait_ticks(game, 170);
    CHECK(game->players[waiting].state == BB_PLAYER_DEAD);
    for(int ticks = 0; ticks < 20 && game->players[waiting].state == BB_PLAYER_DEAD; ++ticks)
        tick(game, 0.0f, false, false);
    CHECK(game->players[waiting].state == BB_PLAYER_BUBBLE && game->players[waiting].lives == 0);
    CHECK(game->players[waiting].rescue_time_left == BB_RESCUE_BUBBLE_SECONDS);
    CHECK(game->state == BB_STATE_PLAY);
}

/* 난수 자체를 비교하지 않는 구조 시험은 같은 시드를 사용해 출발 조건을 맞춘다.
 * waiting은 구조를 기다릴 플레이어 번호이고, full_pool은 일반 버블 포화 여부다. */
static void rescue_fixture(BBGame *game, int waiting, bool full_pool)
{
    rescue_fixture_seed(game, waiting, full_pool, UINT32_C(1234567));
}

/* 일반 버블 배열이 가득 차도 구조 버블은 생성된다. 10초 구조 기회 동안
 * 조작·마나·음식은 정지하며, 만료된 뒤에는 해당 라운드에서 부활할 수 없다. */
static void test_rescue_bubble_lifetime_and_capacity(void)
{
    for(int waiting = 0; waiting < BB_MAX_PLAYERS; ++waiting)
    {
        BBGame game;
        rescue_fixture(&game, waiting, true);
        CHECK(active_bubbles(&game) == BB_MAX_BUBBLES);
        float energy = game.players[waiting].energy;
        double elapsed = game.players[waiting].energy_elapsed;
        game.players[1 - waiting].state = BB_PLAYER_DEAD;
        game.players[1 - waiting].death_timer = 60.0f;
        BBInput inputs[BB_MAX_PLAYERS] = {{0}};
        inputs[waiting] = (BBInput){.move = 1.0f, .jump = true, .fire = true};
        game.pickups[0] = (BBPickup){.body = game.players[waiting].body,
            .active = true, .type = BB_PICKUP_FRIES};
        for(int step = 0; step < 1080; ++step)
        {
            bb_game_update(&game, inputs, BB_FIXED_DT);
            CHECK(game.players[waiting].state == BB_PLAYER_BUBBLE && game.players[waiting].lives == 0);
            CHECK(game.players[waiting].energy == energy && game.players[waiting].energy_elapsed == elapsed);
            CHECK(!game.events.jump && !game.events.fire);
            const BBBody *body = &game.players[waiting].body;
            CHECK(isfinite(body->vx) && isfinite(body->vy) && !body->grounded);
            CHECK(body->x >= 2.0f + BB_RESCUE_BUBBLE_RADIUS);
            CHECK(body->x <= BB_MAP_WIDTH - 2.0f - BB_RESCUE_BUBBLE_RADIUS);
            CHECK(body->y >= 5.5f && body->y <= BB_MAP_HEIGHT - 3.5f);
        }
        CHECK(game.players[waiting].score == 1234);
        CHECK(game.state == BB_STATE_PLAY && active_bubbles(&game) == 0);
        wait_ticks(&game, 120);
        CHECK(game.players[waiting].state == BB_PLAYER_WAIT_ROUND && game.players[waiting].lives == 0);
        CHECK(game.players[waiting].rescue_time_left == 0.0);
        CHECK(game.players[waiting].body.vx == 0.0f && game.players[waiting].body.vy == 0.0f);
        BBBody expired_position = game.players[waiting].body;
        game.players[1 - waiting].state = BB_PLAYER_NORMAL;
        game.players[1 - waiting].body = expired_position;
        game.players[1 - waiting].falling = true;
        game.players[1 - waiting].invulnerable = 100.0f;
        game.bubbles[0] = (BBBubble){.body = expired_position, .active = true,
            .owner = 1 - waiting, .captured_enemy = -1, .age = 0.5f};
        for(int step = 0; step < 120; ++step)
        {
            bb_game_update(&game, inputs, BB_FIXED_DT);
            CHECK(game.players[waiting].state == BB_PLAYER_WAIT_ROUND && game.players[waiting].lives == 0);
            CHECK(game.players[waiting].body.x == expired_position.x && game.players[waiting].body.y == expired_position.y);
            CHECK(game.players[waiting].energy == energy && game.players[waiting].energy_elapsed == elapsed);
            CHECK(game.players[waiting].score == 1234 && !game.events.fire && !game.events.jump);
        }
    }
}

/* 같은 시드의 경로는 재현되지만 다른 시드는 다른 경로를 만든다.
 * 살아 있는 동료의 위치를 바꿔도 버블 경로는 같아 추적하지 않음을 확인한다. */
static void test_rescue_bubble_random_motion(void)
{
    for(int waiting = 0; waiting < BB_MAX_PLAYERS; ++waiting)
    {
        BBGame a, b, different;
        rescue_fixture_seed(&a, waiting, false, UINT32_C(17));
        rescue_fixture_seed(&different, waiting, false, UINT32_C(9321));
        bb_game_resize(&a, 64, 64);
        bb_game_resize(&different, 64, 64);
        a.players[waiting].body = different.players[waiting].body = (BBBody){.x = 32.0f, .y = 44.0f};
        a.players[1 - waiting].body = different.players[1 - waiting].body =
            (BBBody){.x = 4.0f, .y = 61.025f, .grounded = true};
        b = a;
        b.players[1 - waiting].body.x = 60.0f;
        /* 부활 버블의 경로 위에 내부 벽을 놓아 일반 몸체 충돌과도 분리한다. */
        for(int y = 0; y < BB_MAP_HEIGHT - 2; ++y)
            for(int x = 2; x < BB_MAP_WIDTH - 2; ++x)
                b.maps[b.level][y][x] = 3;
        float min_x = 32.0f, max_x = 32.0f, min_y = 44.0f, max_y = 44.0f;
        bool different_path = false;
        for(int step = 0; step < 720; ++step)
        {
            tick(&a, 0.0f, false, false);
            tick(&b, 0.0f, false, false);
            tick(&different, 0.0f, false, false);
            CHECK(a.players[waiting].state == BB_PLAYER_BUBBLE && b.players[waiting].state == BB_PLAYER_BUBBLE);
            CHECK(different.players[waiting].state == BB_PLAYER_BUBBLE);
            const BBBody *body = &a.players[waiting].body;
            const BBBody *other = &b.players[waiting].body;
            CHECK(body->x == other->x && body->y == other->y);
            CHECK(body->vx == other->vx && body->vy == other->vy);
            CHECK(a.players[waiting].rescue_rng == b.players[waiting].rescue_rng);
            min_x = fminf(min_x, body->x); max_x = fmaxf(max_x, body->x);
            min_y = fminf(min_y, body->y); max_y = fmaxf(max_y, body->y);
            different_path = different_path || fabsf(body->x - different.players[waiting].body.x) > 0.1f ||
                             fabsf(body->y - different.players[waiting].body.y) > 0.1f;
        }
        CHECK(different_path);
        CHECK(max_x - min_x > 1.0f && max_y - min_y > 1.0f);
    }
}

/* 갱신률이 달라도 거의 같은 경로를 이동하고 생성 후 10초에 정확히 만료된다.
 * 화면 변경은 위치만 옮기며 속도와 남은 구조 시간을 초기화하지 않는다. */
static void test_rescue_bubble_rates_and_bounds(void)
{
    const int rates[] = {30, 60, 120};
    BBBody endpoints[3];
    for(int waiting = 0; waiting < BB_MAX_PLAYERS; ++waiting)
    {
        for(int rate = 0; rate < 3; ++rate)
        {
            BBGame game;
            rescue_fixture(&game, waiting, false);
            game.players[1 - waiting].state = BB_PLAYER_DEAD;
            game.players[1 - waiting].death_timer = 60.0f;
            float dt = 1.0f / (float)rates[rate];
            for(int frame = 0; frame < rates[rate] * 10 - 1; ++frame)
            {
                bb_game_update(&game, NULL, dt);
                CHECK(game.players[waiting].state == BB_PLAYER_BUBBLE);
                CHECK(game.players[waiting].rescue_time_left > 0.0);
                if(frame == rates[rate] * 2 - 1)
                    endpoints[rate] = game.players[waiting].body;
            }
            CHECK(fabs(game.players[waiting].rescue_time_left - dt) < 0.00001);
            bb_game_update(&game, NULL, dt);
            CHECK(game.players[waiting].state == BB_PLAYER_WAIT_ROUND && game.players[waiting].rescue_time_left == 0.0);
            CHECK(game.state == BB_STATE_PLAY);
        }
        for(int rate = 0; rate < 2; ++rate)
        {
            CHECK(fabsf(endpoints[rate].x - endpoints[2].x) < 0.2f);
            CHECK(fabsf(endpoints[rate].y - endpoints[2].y) < 0.2f);
        }
    }

    BBGame game;
    rescue_fixture(&game, 0, false);
    wait_ticks(&game, 120);
    BBBody before_resize = game.players[0].body;
    double remaining = game.players[0].rescue_time_left;
    bb_game_resize(&game, 64, 40);
    CHECK(game.players[0].body.vx == before_resize.vx && game.players[0].body.vy == before_resize.vy);
    CHECK(fabsf(game.players[0].body.x - before_resize.x - 16.0f) < 0.0001f);
    CHECK(fabsf(game.players[0].body.y - before_resize.y - 12.0f) < 0.0001f);
    CHECK(game.players[0].rescue_time_left == remaining);
    bb_game_resize(&game, BB_MAP_WIDTH, BB_MAP_HEIGHT);
    CHECK(game.players[0].body.vx == before_resize.vx && game.players[0].body.vy == before_resize.vy);
    CHECK(game.players[0].rescue_time_left == remaining);
    for(int corner = 0; corner < 4; ++corner)
    {
        rescue_fixture(&game, 0, false);
        game.players[1].state = BB_PLAYER_DEAD;
        game.players[1].death_timer = 60.0f;
        game.players[0].body = (BBBody){
            .x = corner % 2 ? BB_MAP_WIDTH - 2.0f - BB_RESCUE_BUBBLE_RADIUS : 2.0f + BB_RESCUE_BUBBLE_RADIUS,
            .y = corner / 2 ? BB_MAP_HEIGHT - 3.5f : 5.5f,
            .vx = corner % 2 ? 3.0f : -3.0f,
            .vy = corner / 2 ? 3.0f : -3.0f};
        for(int step = 0; step < 720; ++step)
        {
            tick(&game, 0.0f, false, false);
            const BBBody *body = &game.players[0].body;
            CHECK(game.players[0].state == BB_PLAYER_BUBBLE);
            CHECK(body->x >= 2.0f + BB_RESCUE_BUBBLE_RADIUS);
            CHECK(body->x <= BB_MAP_WIDTH - 2.0f - BB_RESCUE_BUBBLE_RADIUS);
            CHECK(body->y >= 5.5f && body->y <= BB_MAP_HEIGHT - 3.5f);
            if(step == 0)
            {
                CHECK((corner % 2 ? -body->vx : body->vx) >= 0.0f);
                CHECK((corner / 2 ? -body->vy : body->vy) >= 0.0f);
            }
        }
    }
}

/* 동료 접촉은 두 플레이어 모두 구조할 수 있다. 그 자리에서 한 목숨과
 * 3초 무적을 얻고, 점수/마나를 보존하며 뒤따르는 적/바위 피해도 막는다. */
static void test_teammate_contact_rescue(void)
{
    for(int waiting = 0; waiting < BB_MAX_PLAYERS; ++waiting)
    {
        BBGame game;
        rescue_fixture(&game, waiting, false);
        int teammate = 1 - waiting;
        float energy = game.players[waiting].energy;
        double elapsed = game.players[waiting].energy_elapsed;
        BBBody position = game.players[waiting].body;
        game.players[teammate].body = (BBBody){.x = position.x + 2.0f, .y = position.y};
        game.players[teammate].falling = true;
        tick(&game, 0.0f, false, false);
        CHECK(game.players[waiting].state == BB_PLAYER_NORMAL && game.players[waiting].lives == 1);
        CHECK(game.players[waiting].invulnerable > 2.99f && game.players[waiting].rescue_pop_timer > 0.24f);
        CHECK(game.events.pop && !game.events.death);
        CHECK(fabsf(game.players[waiting].body.x - position.x) < 0.05f &&
              fabsf(game.players[waiting].body.y - position.y) < 0.05f);
        CHECK(game.players[waiting].body.vy == 0.0f && game.players[waiting].falling && game.players[waiting].air_control);
        CHECK(game.players[waiting].score == 1234 && game.players[waiting].energy == energy);
        CHECK(game.players[waiting].energy_elapsed == elapsed);
        game.enemies[0] = (BBEnemy){.body = game.players[waiting].body, .active = true,
            .state = BB_ENEMY_WALKING, .type = BB_ENEMY_ZENCHAN, .facing = 1};
        game.boulders[0] = (BBBoulder){.body = game.players[waiting].body, .active = true};
        wait_ticks(&game, 32);
        CHECK(game.players[waiting].state == BB_PLAYER_NORMAL && game.players[waiting].lives == 1);
        CHECK(game.players[waiting].rescue_pop_timer == 0.0f && game.players[waiting].invulnerable > 2.7f);
    }
}

/* 동료가 쏜 빈 일반 버블은 구조 후 파열한다. 같은 위치의 몬스터를
 * 먼저 포획해 구조 기회를 빼앗지 않도록 구조가 일반 포획보다 앞서야 한다. */
static void test_teammate_projectile_rescue(void)
{
    for(int waiting = 0; waiting < BB_MAX_PLAYERS; ++waiting)
    {
        BBGame game;
        rescue_fixture(&game, waiting, false);
        game.bubbles[0] = (BBBubble){.body = game.players[waiting].body, .active = true,
            .owner = 1 - waiting, .captured_enemy = -1, .age = 0.5f};
        game.enemies[0] = (BBEnemy){.body = game.players[waiting].body, .active = true,
            .state = BB_ENEMY_WALKING, .type = BB_ENEMY_ZENCHAN, .facing = 1};
        tick(&game, 0.0f, false, false);
        CHECK(game.players[waiting].state == BB_PLAYER_NORMAL && game.players[waiting].lives == 1);
        CHECK(game.players[waiting].invulnerable > 2.99f && game.events.pop);
        CHECK(game.bubbles[0].active && game.bubbles[0].popping && !game.bubbles[0].releasing);
        CHECK(game.bubbles[0].captured_enemy == -1 && game.enemies[0].state != BB_ENEMY_CAPTURED);
    }
}

/* 자기 버블/포획 버블/파열 중/비활성/잘못된 소유자는 구조에 쓰지 않는다.
 * 살아 있던 동료의 과거 발사라도 그 동료가 사망 대기 중이면 구조할 수 없다. */
static void test_invalid_rescue_projectiles(void)
{
    for(int invalid = 0; invalid < 6; ++invalid)
    {
        BBGame game;
        rescue_fixture(&game, 0, false);
        game.bubbles[0] = (BBBubble){.body = game.players[0].body, .active = true,
            .owner = 1, .captured_enemy = -1, .age = 0.5f};
        if(invalid == 0) game.bubbles[0].owner = 0;
        if(invalid == 1)
        {
            game.bubbles[0].captured_enemy = 0;
            game.enemies[0] = (BBEnemy){.active = true, .state = BB_ENEMY_CAPTURED};
        }
        if(invalid == 2) { game.bubbles[0].popping = true; game.bubbles[0].pop_timer = 0.2f; }
        if(invalid == 3) game.bubbles[0].active = false;
        if(invalid == 4) game.bubbles[0].owner = INT_MAX;
        if(invalid == 5)
        {
            game.players[1].state = BB_PLAYER_DEAD;
            game.players[1].lives = 1;
            game.players[1].death_timer = 5.0f;
        }
        tick(&game, 0.0f, false, false);
        CHECK(game.players[0].state == BB_PLAYER_BUBBLE && game.players[0].lives == 0);
        CHECK(game.state == BB_STATE_PLAY);
    }
}

/* 동료가 자동 부활할 목숨이 있으면 구조 대기를 유지한다. 두 사람 모두
 * 목숨을 잃으면 사망 연출이 끝난 뒤만 종료하며, 구조와 피격이 겹치면 구조가 먼저다. */
static void test_rescue_game_over_and_collision_order(void)
{
    BBGame game;
    rescue_fixture(&game, 0, false);
    game.players[1].state = BB_PLAYER_DEAD;
    game.players[1].lives = 1;
    game.players[1].death_timer = 0.02f;
    tick(&game, 0.0f, false, false);
    CHECK(game.state == BB_STATE_PLAY && game.players[0].state == BB_PLAYER_BUBBLE);
    wait_ticks(&game, 4);
    CHECK(game.players[1].state == BB_PLAYER_NORMAL && game.players[0].state == BB_PLAYER_BUBBLE);

    fixture_mode(&game, 2);
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
    {
        game.players[p].state = BB_PLAYER_DEAD;
        game.players[p].lives = 0;
        game.players[p].death_timer = 0.02f;
    }
    tick(&game, 0.0f, false, false);
    CHECK(game.state == BB_STATE_PLAY);
    wait_ticks(&game, 4);
    CHECK(game.state == BB_STATE_SCORE && !game.won);
    CHECK(game.players[0].state == BB_PLAYER_OUT && game.players[1].state == BB_PLAYER_OUT);

    rescue_fixture(&game, 0, false);
    game.players[1].body = game.players[0].body;
    game.players[1].falling = true;
    game.players[1].lives = 1;
    game.players[1].invulnerable = 0.0f;
    game.enemies[0] = (BBEnemy){.body = game.players[0].body, .active = true,
        .state = BB_ENEMY_WALKING, .type = BB_ENEMY_ZENCHAN, .facing = 1};
    tick(&game, 0.0f, false, false);
    CHECK(game.players[0].state == BB_PLAYER_NORMAL && game.players[0].lives == 1);
    CHECK(game.players[1].state == BB_PLAYER_DEAD && game.players[1].lives == 0);
    CHECK(game.events.pop && game.events.death && game.state == BB_STATE_PLAY);
}

/* 만료 틱의 접촉/발사체보다 시간 제한이 우선한다. 실패한 플레이어는
 * 현재 라운드에서는 대기하고 다음 라운드 시작 때만 한 목숨으로 돌아온다. */
static void test_rescue_timeout_and_next_round(void)
{
    for(int waiting = 0; waiting < BB_MAX_PLAYERS; ++waiting)
    {
        BBGame game;
        rescue_fixture(&game, waiting, false);
        game.players[waiting].rescue_time_left = (double)BB_FIXED_DT * 2.0;
        game.players[1 - waiting].body = game.players[waiting].body;
        game.players[1 - waiting].falling = true;
        tick(&game, 0.0f, false, false);
        CHECK(game.players[waiting].state == BB_PLAYER_NORMAL && game.players[waiting].lives == 1);
        CHECK(game.players[waiting].rescue_time_left == 0.0 && game.events.pop);

        rescue_fixture(&game, waiting, false);
        game.players[waiting].rescue_time_left = BB_FIXED_DT;
        game.players[1 - waiting].body = game.players[waiting].body;
        game.players[1 - waiting].falling = true;
        game.bubbles[0] = (BBBubble){.body = game.players[waiting].body, .active = true,
            .owner = 1 - waiting, .captured_enemy = -1, .age = 0.5f};
        tick(&game, 0.0f, false, false);
        CHECK(game.players[waiting].state == BB_PLAYER_WAIT_ROUND && game.players[waiting].lives == 0);
        CHECK(game.players[waiting].rescue_time_left == 0.0 && game.players[waiting].rescue_pop_timer == 0.0f);
        wait_ticks(&game, 120);
        CHECK(game.players[waiting].state == BB_PLAYER_WAIT_ROUND);
        float energy = game.players[waiting].energy;
        double elapsed = game.players[waiting].energy_elapsed;
        int score = game.players[waiting].score;
        memset(game.enemies, 0, sizeof game.enemies);
        tick(&game, 0.0f, false, false);
        CHECK(game.state == BB_STATE_CLEAR && game.players[waiting].state == BB_PLAYER_WAIT_ROUND);
        game.state_time = 6.999f;
        tick(&game, 0.0f, false, false);
        CHECK(game.state == BB_STATE_PLAY && game.level == 1 && game.events.level);
        CHECK(game.players[waiting].state == BB_PLAYER_NORMAL && game.players[waiting].lives == 1);
        CHECK(game.players[waiting].invulnerable == 3.0f);
        CHECK(game.players[waiting].body.x == (waiting == 0 ? 4.0f : 28.0f));
        CHECK(game.players[waiting].energy == energy && game.players[waiting].energy_elapsed == elapsed);
        CHECK(game.players[waiting].score == score);
        CHECK(game.players[waiting].rescue_time_left == 0.0 && game.players[waiting].rescue_turn_timer == 0.0);
        CHECK(game.players[waiting].rescue_drift_vx == 0.0f && game.players[waiting].rescue_drift_vy == 0.0f);
        CHECK(game.players[waiting].rescue_rng == 0);

        /* 구조 제한과 라운드 전환이 같은 틱이면 새 라운드에 바로 복귀한다. */
        rescue_fixture(&game, waiting, false);
        game.players[waiting].rescue_time_left = BB_FIXED_DT;
        memset(game.enemies, 0, sizeof game.enemies);
        game.state = BB_STATE_CLEAR;
        game.state_time = 6.999f;
        tick(&game, 0.0f, false, false);
        CHECK(game.level == 1 && game.players[waiting].state == BB_PLAYER_NORMAL && game.players[waiting].lives == 1);

        /* 다음 라운드를 기다리는 사람만 남았다고 패배를 막지 않는다. */
        rescue_fixture(&game, waiting, false);
        game.players[waiting].rescue_time_left = BB_FIXED_DT;
        tick(&game, 0.0f, false, false);
        CHECK(game.players[waiting].state == BB_PLAYER_WAIT_ROUND);
        game.players[1 - waiting].state = BB_PLAYER_DEAD;
        game.players[1 - waiting].lives = 0;
        game.players[1 - waiting].death_timer = 0.02f;
        wait_ticks(&game, 4);
        CHECK(game.state == BB_STATE_SCORE && !game.won);
        CHECK(game.players[0].state == BB_PLAYER_OUT && game.players[1].state == BB_PLAYER_OUT);
    }
}

/* 라운드가 바뀌어도 구조 대기 위치와 기록을 유지하며, 크기 변경에서는
 * 버블 반경으로 벽 안에 제한한다. 새 1인/2인 게임에서는 대기와 파열 연출을 지운다. */
static void test_rescue_round_resize_and_reset(void)
{
    BBGame game;
    rescue_fixture(&game, 0, false);
    BBBody position = game.players[0].body;
    float energy = game.players[0].energy;
    double remaining = game.players[0].rescue_time_left;
    const BBState frozen[] = {BB_STATE_MENU, BB_STATE_INTRO, BB_STATE_SCORE};
    for(unsigned i = 0; i < sizeof frozen / sizeof frozen[0]; ++i)
    {
        game.state = frozen[i];
        game.state_time = 0.0f;
        wait_ticks(&game, 120);
        CHECK(game.players[0].rescue_time_left == remaining);
        CHECK(game.players[0].body.x == position.x && game.players[0].body.y == position.y);
    }
    game.state = BB_STATE_CLEAR;
    game.state_time = 6.999f;
    tick(&game, 0.0f, false, false);
    CHECK(game.level == 1 && game.players[0].state == BB_PLAYER_BUBBLE && game.players[0].lives == 0);
    CHECK(fabs(game.players[0].rescue_time_left - (remaining - BB_FIXED_DT)) < 0.000001);
    CHECK(fabsf(game.players[0].body.x - position.x) < 0.05f &&
          fabsf(game.players[0].body.y - position.y) < 0.05f);
    CHECK(game.players[0].score == 1234 && game.players[0].energy == energy);
    position = game.players[0].body;
    bb_game_resize(&game, 64, 40);
    CHECK(game.players[0].body.x == position.x + 16.0f);
    CHECK(fabsf(game.players[0].body.y - position.y - 12.0f) < 0.0001f);
    CHECK(game.players[0].state == BB_PLAYER_BUBBLE && game.players[0].lives == 0);
    game.players[0].body.x = 4.0f;
    bb_game_resize(&game, BB_MAP_WIDTH, BB_MAP_HEIGHT);
    CHECK(game.players[0].body.x >= 2.0f + BB_RESCUE_BUBBLE_RADIUS);
    CHECK(game.players[0].body.x <= BB_MAP_WIDTH - 2.0f - BB_RESCUE_BUBBLE_RADIUS);
    for(int count = 1; count <= BB_MAX_PLAYERS; ++count)
    {
        game.players[0].rescue_pop_timer = 0.2f;
        game.players[0].state = BB_PLAYER_WAIT_ROUND;
        bb_game_start(&game, count);
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            CHECK(game.players[p].state == BB_PLAYER_NORMAL && game.players[p].rescue_pop_timer == 0.0f);
            CHECK(game.players[p].rescue_time_left == 0.0 && game.players[p].rescue_turn_timer == 0.0);
            CHECK(game.players[p].rescue_drift_vx == 0.0f && game.players[p].rescue_drift_vy == 0.0f);
            CHECK(game.players[p].rescue_rng == 0);
            CHECK(game.players[p].lives == (p < count ? BB_STARTING_LIVES : 0));
        }
    }
}

/* 작은 물리/규칙 회귀 시험과 장기 재현성 시험을 모두 실행한다.
 * CHECK가 하나라도 실패하면 즉시 종료되므로 마지막 메시지는 모든 검사 통과를 뜻한다. */
int main(void)
{
    test_capture_and_release();
    test_capture_timeout_boundaries_and_late_capture();
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
    test_shifted_platform_fire_raycast();
    test_shifted_platform_enemy_jump_target();
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
    test_world_resize_preserves_gameplay();
    test_expanded_world_movement_and_wrap();
    test_world_resize_limits_and_shrink();
    test_player_width_speed();
    test_width_resize_momentum();
    test_max_width_swept_walls();
    test_width_enemy_counts();
    test_live_width_enemy_additions();
    test_maximum_enemy_pickup_capacity();
    test_max_width_substep_contacts();
    test_far_platform_landing_and_riders();
    test_far_platform_raycast_and_ai();
    test_wide_platform_wall_reversal();
    test_shrinking_far_platform_riders();
    test_rescue_bubble_lifetime_and_capacity();
    test_rescue_bubble_random_motion();
    test_rescue_bubble_rates_and_bounds();
    test_teammate_contact_rescue();
    test_teammate_projectile_rescue();
    test_invalid_rescue_projectiles();
    test_rescue_game_over_and_collision_order();
    test_rescue_timeout_and_next_round();
    test_rescue_round_resize_and_reset();
    printf("Core regression tests passed (%d checks, 60000 deterministic stress ticks).\n", checks);
    return EXIT_SUCCESS;
}
