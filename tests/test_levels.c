#include "bb_levels.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 실제 다섯 맵의 타일 범위, 발판 수, 서로 다른 배치, 바닥 순환 구멍을 확인한다.
 * 이어 각 층의 겹치는 가로 구간에서 1P/2P가 동시에 기본 점프로 윗발판에 도달하는지
 * 실제 게임 물리를 사용해 검사한다. 단순 높이 비교보다 충돌 처리까지 함께 검증할 수 있다. */
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

    /* 게임이 읽는 것과 같은 평탄한 메모리 시작 주소로 전체 맵을 생성한다. */
    bb_levels_build(&maps[0][0][0]);
    for (int level = 0; level < BB_LEVEL_COUNT; ++level) {
        count = 0;
        /* 각 y줄의 발판은 하나의 연속 구간이어야 도달성 계산이 유효하다. */
        for (int y = 0; y < BB_MAP_HEIGHT; ++y) {
            /* -1은 해당 높이에 아직 발판을 찾지 못했다는 표시다. */
            left[y] = right[y] = -1;
            for (int x = 0; x < BB_MAP_WIDTH; ++x) {
                assert(maps[level][y][x] <= 3);
                /* 가장 아래 바닥을 제외한 공중 발판만 다섯 개에 포함한다. */
                if (y < 26 && maps[level][y][x] == 2) {
                    if (left[y] == -1) { left[y] = x; ++count; }
                    else assert(right[y] == x - 1);
                    right[y] = x;
                }
            }
        }
        /* 라운드가 달라도 동일한 맵을 잘못 재사용하지 않는지 이전 맵 전부와 비교한다. */
        assert(count == 5);
        for (int other = 0; other < level; ++other)
            assert(memcmp(maps[level], maps[other], sizeof maps[level]) != 0);
        /* 바닥과 장식 줄 모두 비어야 순환 통로가 화면과 충돌에서 일치한다. */
        for (int x = 14; x < 18; ++x) {
            assert(maps[level][26][x] == 0);
            assert(maps[level][27][x] == 0);
        }

        /* 각 라운드의 바닥부터 시작하여 낮은 발판에서 높은 발판 순서로 검사한다.
         * 가속 없이도 이웃한 두 발판의 겹치는 구간에서 제자리 점프로 오를 수 있어야 한다. */
        lower_y = 26;
        lower_left = 2;
        lower_right = 29;
        for (int y = 25; y >= 0; --y) {
            if (left[y] < 0) continue;
            /* 두 발판의 교집합을 구한다. 최소 두 타일이 겹쳐야 안전한 출발점을 잡을 수 있다. */
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
            /* 전투와 자동 클리어가 점프 검사 중 위치를 바꾸지 않도록 대기 중인 적만 둔다. */
            memset(game.enemies, 0, sizeof game.enemies);
            game.enemies[0].active = true;
            game.enemies[0].state = BB_ENEMY_SPAWNING;
            game.enemies[0].spawn_delay = 1000000.0f;
            /* 몸 중심은 발판 높이에서 반높이 0.975만큼 위에 있어야 정확히 서 있는 상태다. */
            for (int p = 0; p < BB_MAX_PLAYERS; ++p) {
                game.players[p].body.x = jump_x;
                game.players[p].body.y = (float)lower_y - 0.975f;
                game.players[p].body.grounded = true;
                inputs[p].jump = true;
            }
            /* 점프 입력은 첫 틱에만 전달한다. 최대 2초를 넘어가면 착지 실패로 판정한다. */
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
            /* 방금 도달한 발판을 다음 점프의 출발 발판으로 사용한다. */
            lower_y = y;
            lower_left = left[y];
            lower_right = right[y];
        }
    }
}

/* 실제 레벨 맵을 사용하는 이동 발판 시험의 공통 준비 단계다.
 * 대기 중인 적 하나가 자동 클리어를 막고, 두 플레이어는 정상적으로 활성화한다. */
static void moving_fixture(BBGame *game)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    bb_levels_build(&maps[0][0][0]);
    bb_game_init(game, &maps[0][0][0], 17);
    bb_game_start(game, 2);
    bb_game_skip_intro(game);
    memset(game->enemies, 0, sizeof game->enemies);
    game->enemies[BB_MAX_ENEMIES - 1] = (BBEnemy){
        .active = true, .state = BB_ENEMY_SPAWNING, .spawn_delay = 1000000.0f
    };
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        game->players[p].invulnerable = 1000.0f;
}

/* 가장 낮은 공중 발판 위에 두 플레이어를 서로 떨어뜨려 세운다.
 * 몸 중심은 y=22에서 반높이를 뺀 21.025이며 가로 입력 없이 발판 이동만 관찰한다. */
static void place_platform_riders(BBGame *game)
{
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        game->players[p].body = (BBBody){
            .x = 6.0f + (float)p * 4.0f + game->platform_offset,
            .y = 22.0f - 0.975f, .grounded = true
        };
}

/* 발판이 양쪽 끝에서 왕복하고, 가만히 선 두 플레이어와 보상을 같은 거리만큼
 * 운반하는지 확인한다. 30/60/120Hz에서 6초씩 검사해 갱신 주기 의존성과 방향 반전을 함께 다룬다. */
static void test_platform_motion_and_riders(void)
{
    const int rates[] = {30, 60, 120};
    for(unsigned rate_index = 0; rate_index < sizeof rates / sizeof rates[0]; ++rate_index)
    {
        BBGame game;
        uint8_t original_maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
        int rate = rates[rate_index];
        float dt = 1.0f / (float)rate;
        bool moved_left = false;
        bool moved_right = false;
        float minimum = 0.0f;
        float maximum = 0.0f;
        moving_fixture(&game);
        place_platform_riders(&game);
        memcpy(original_maps, game.maps, sizeof original_maps);
        /* 보상은 플레이어가 닿지 않는 한 층 위에 두어 수집 없이 운반을 검사한다. */
        game.pickups[0] = (BBPickup){ .body = {
            .x = 12.0f, .y = 18.0f - 0.95f, .grounded = true },
            .active = true, .type = BB_PICKUP_WATERMELON };
        for(int step = 0; step < 6 * rate; ++step)
        {
            float old_offset = game.platform_offset;
            bb_game_update(&game, NULL, dt);
            assert(game.platform_offset >= -1.0f && game.platform_offset <= 1.0f);
            assert(fabsf(game.platform_step - (game.platform_offset - old_offset)) < 0.00001f);
            assert(fabsf(game.platform_step) <= 0.75f * dt + 0.00001f);
            moved_left = moved_left || game.platform_step < 0.0f;
            moved_right = moved_right || game.platform_step > 0.0f;
            if(game.platform_offset < minimum) minimum = game.platform_offset;
            if(game.platform_offset > maximum) maximum = game.platform_offset;
            for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            {
                assert(game.players[p].body.grounded);
                assert(fabsf(game.players[p].body.x - (6.0f + (float)p * 4.0f + game.platform_offset)) < 0.002f);
                assert(fabsf(game.players[p].body.y - (22.0f - 0.975f)) < 0.0001f);
                assert(game.players[p].body.vx == 0.0f);
            }
            assert(game.pickups[0].active && game.pickups[0].body.grounded);
            assert(fabsf(game.pickups[0].body.x - (12.0f + game.platform_offset)) < 0.002f);
            assert(game.state == BB_STATE_PLAY);
        }
        /* 화면용 좌표만 바꾸며 원본 타일 배열을 덮어쓰거나 끝없이 한쪽으로 이동하면 안 된다. */
        assert(moved_left && moved_right && minimum < -0.99f && maximum > 0.99f);
        assert(memcmp(original_maps, game.maps, sizeof original_maps) == 0);
    }
}

/* 발판 운반에 자신의 걷기 속도를 더하는 경우, 점프로 운반에서 빠지는 경우,
 * 맨 아래 고정 바닥을 구분한다. 적도 동일한 이동/충돌 함수를 사용해야 한다. */
static void test_platform_walk_jump_and_fixed_floor(void)
{
    BBGame game;
    moving_fixture(&game);
    /* 시작 지점은 고정 바닥 위이므로 공중 발판이 움직여도 x가 바뀌지 않는다. */
    for(int step = 0; step < 60; ++step)
        bb_game_update(&game, NULL, BB_FIXED_DT);
    assert(game.platform_offset > 0.0f);
    assert(game.players[0].body.x == 4.0f && game.players[1].body.x == 28.0f);
    assert(game.players[0].body.grounded && game.players[1].body.grounded);

    moving_fixture(&game);
    place_platform_riders(&game);
    /* 플레이어보다 높은 발판의 적은 추적 점프 없이 오른쪽으로 걷게 둔다. */
    game.enemies[0] = (BBEnemy){ .body = {
        .x = 22.0f, .y = 14.0f - 0.95f, .grounded = true },
        .active = true, .type = BB_ENEMY_ZENCHAN, .state = BB_ENEMY_WALKING, .facing = 1 };
    BBInput inputs[BB_MAX_PLAYERS] = {{ .move = 1.0f }, {0}};
    bb_game_update(&game, inputs, BB_FIXED_DT);
    assert(fabsf(game.players[0].body.x - (6.0f + BB_PLAYER_GROUND_SPEED * BB_FIXED_DT + game.platform_step)) < 0.0001f);
    assert(fabsf(game.enemies[0].body.x - (22.0f + 6.0f * BB_FIXED_DT + game.platform_step)) < 0.0001f);
    assert(game.enemies[0].body.grounded);
    game.enemies[0].active = false;

    /* 정지 상태로 다시 세운 뒤 1P만 점프한다. 2P는 계속 발판과 함께 움직인다. */
    place_platform_riders(&game);
    float jumping_x = game.players[0].body.x;
    float standing_x = game.players[1].body.x;
    inputs[0] = (BBInput){ .jump = true };
    bb_game_update(&game, inputs, BB_FIXED_DT);
    assert(!game.players[0].body.grounded && game.players[0].body.vy < 0.0f);
    assert(game.players[0].body.x == jumping_x);
    assert(fabsf(game.players[1].body.x - (standing_x + game.platform_step)) < 0.0001f);
    for(int step = 0; step < 15; ++step)
        bb_game_update(&game, NULL, BB_FIXED_DT);
    assert(game.players[0].body.x == jumping_x);
    assert(game.players[1].body.grounded);
}

/* 이동한 발판의 끝에서는 원래 타일 좌표가 아니라 현재 보이는 구간으로 착지해야 한다.
 * 또한 발판에 밀린 플레이어도 단단한 벽을 관통하지 않도록 검사한다. */
static void test_shifted_platform_collision_and_wall(void)
{
    BBGame game;
    moving_fixture(&game);
    game.platform_offset = 0.9f;
    game.platform_step = 0.0f;
    /* 원래 x=3..13 발판이 약 x=3.9..13.9로 옮겨진 상태다.
     * 1P의 왼쪽 끝 13.8은 새 발판만 밟고, 2P의 오른쪽 끝 3.9는 옛 발판 위치에만 있다. */
    game.players[0].body = (BBBody){ .x = 14.7f, .y = 22.0f - 0.975f - 0.01f };
    game.players[1].body = (BBBody){ .x = 3.0f, .y = 22.0f - 0.975f - 0.01f };
    game.players[0].falling = game.players[1].falling = true;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    assert(game.players[0].body.grounded);
    assert(fabsf(game.players[0].body.y - (22.0f - 0.975f)) < 0.0001f);
    assert(!game.players[1].body.grounded);
    assert(game.players[1].body.y > 22.0f - 0.975f);

    moving_fixture(&game);
    place_platform_riders(&game);
    game.maps[0][20][13] = 3;
    game.maps[0][21][13] = 3;
    game.players[0].body.x = 12.1f;
    for(int step = 0; step < 20; ++step)
    {
        bb_game_update(&game, NULL, BB_FIXED_DT);
        assert(game.players[0].body.x + 0.9f <= 13.0001f);
        assert(game.players[0].body.grounded);
    }
    assert(game.platform_offset > 0.1f);
}

/* 메뉴/인트로/점수 화면과 잘못된 dt는 발판 시간을 진행하지 않아야 한다.
 * 클리어 대기에서는 계속 움직이며 새 게임과 다음 라운드는 원점에서 다시 시작한다. */
static void test_platform_state_and_reset(void)
{
    const BBState frozen[] = {BB_STATE_MENU, BB_STATE_INTRO, BB_STATE_SCORE};
    BBGame game;
    for(unsigned i = 0; i < sizeof frozen / sizeof frozen[0]; ++i)
    {
        moving_fixture(&game);
        game.platform_offset = 0.5f;
        game.platform_step = 0.01f;
        game.platform_direction = -1;
        game.state = frozen[i];
        bb_game_update(&game, NULL, BB_FIXED_DT);
        assert(game.platform_offset == 0.5f && game.platform_step == 0.01f);
        assert(game.platform_direction == -1);
    }
    moving_fixture(&game);
    const float invalid_dt[] = {NAN, INFINITY, -1.0f, 0.0f, 1.0f};
    for(unsigned i = 0; i < sizeof invalid_dt / sizeof invalid_dt[0]; ++i)
    {
        bb_game_update(&game, NULL, invalid_dt[i]);
        assert(game.platform_offset == 0.0f && game.platform_step == 0.0f);
        assert(game.platform_direction == 1);
    }
    game.state = BB_STATE_CLEAR;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    assert(game.platform_offset > 0.0f && game.platform_step > 0.0f);
    /* 7초 클리어 경계를 넘는 틱에서 새 라운드의 발판 상태가 초기화된다. */
    game.state_time = 6.999f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    assert(game.level == 1 && game.state == BB_STATE_PLAY);
    assert(game.platform_offset == 0.0f && game.platform_step == 0.0f);
    assert(game.platform_direction == 1);
    game.platform_offset = -0.8f;
    game.platform_step = -0.01f;
    game.platform_direction = -1;
    bb_game_start(&game, 2);
    assert(game.platform_offset == 0.0f && game.platform_step == 0.0f);
    assert(game.platform_direction == 1);
    bb_game_skip_intro(&game);
    assert(game.platform_offset == 0.0f && game.platform_step == 0.0f);
}

/* 맵 구조/25개 점프와 발판의 이동/동승/충돌/상태 초기화를 함께 검사한다. */
int main(void)
{
    test_layouts_and_jumps();
    test_platform_motion_and_riders();
    test_platform_walk_jump_and_fixed_floor();
    test_shifted_platform_collision_and_wall();
    test_platform_state_and_reset();
    puts("Five layouts, 25 jumps, moving platform riders, collisions, and lifecycle passed.");
    return 0;
}
