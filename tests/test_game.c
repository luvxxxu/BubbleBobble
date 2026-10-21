#include "bb_game.h"

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
            game.platform_offset = 0.9f * (float)direction;
            game.platform_step = 0.0f;
            game.platform_direction = direction;
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
                float boundary = (direction > 0 ? 12.0f : 13.0f) + game.platform_offset;
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
            game.platform_offset = 0.9f * (float)direction;
            game.platform_step = 0.0f;
            game.platform_direction = direction;
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
    /* 한 명이 완전히 탈락해도 다른 플레이어의 게임은 계속된다. */
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

/* 같은 시드의 게임 둘에 같은 무작위 입력을 60000틱 전달하는 장기 회귀 검사다.
 * 잘못된 시간/입력을 섞어도 좌표가 유한하고 범위를 지키며, 게임 상태와 난수 진행이
 * 항상 같아야 한다. 종료한 게임은 다시 시작하여 실제 플레이 구간을 계속 검사한다. */
static void test_finite_input_and_determinism(void)
{
    BBGame a, b;
    fixture_mode(&a, 2);
    fixture_mode(&b, 2);
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
        /* 발판 이동도 같은 시간/입력에서 동일해야 하며 왕복 범위를 벗어나지 않는다. */
        CHECK(isfinite(a.platform_offset) && a.platform_offset >= -1.0f && a.platform_offset <= 1.0f);
        CHECK(a.platform_offset == b.platform_offset && a.platform_step == b.platform_step);
        CHECK(a.platform_direction == b.platform_direction);
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
    printf("Core regression tests passed (%d checks, 60000 deterministic stress ticks).\n", checks);
    return EXIT_SUCCESS;
}
