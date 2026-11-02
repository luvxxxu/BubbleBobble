/* 맵 배열과 움직이는 발판의 자동 검사 모음이다. 실제 다섯 라운드를 만들고
 * 물리 시간을 직접 진행하므로, 화면 없이도 착지·동승·소멸·재생성을 확인한다.
 * assert는 조건이 틀리면 즉시 중단하는 검사이며 Release 빌드에서도 켜 둔다. */
#include "bb_levels.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 테스트에서도 라운드 번호만 덮어쓰지 않고 정상 전환을 거쳐
 * 맵과 각 발판의 시작 좌표/속도가 항상 같은 라운드를 가리키게 한다. */
static void enter_test_level(BBGame *game, int level)
{
    while(game->level < level)
    {
        game->state = BB_STATE_CLEAR;
        game->state_time = 6.999f;
        bb_game_update(game, NULL, BB_FIXED_DT);
    }
    assert(game->level == level && game->state == BB_STATE_PLAY);
}

/* 알려진 원본 타일이 속한 발판을 찾는다. 배열 순서는 테스트의 가정이 아니다. */
static BBMovingPlatform *platform_at_tile(BBGame *game, int x, int y)
{
    BBMovingPlatform *found = NULL;
    for(int i = 0; i < game->platform_count; ++i)
    {
        BBMovingPlatform *platform = &game->platforms[i];
        if(platform->y == y && x >= platform->x && x < platform->x + platform->width)
        {
            found = platform;
            break;
        }
    }
    assert(found != NULL);
    return found;
}

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
            enter_test_level(&game, level);
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
    const BBMovingPlatform *platform = platform_at_tile(game, 3, 22);
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        game->players[p].body = (BBBody){
            .x = 6.0f + (float)p * 4.0f + platform->offset,
            .y = 22.0f - 0.975f, .grounded = true
        };
}

/* 다른 발판에 선 두 플레이어와 음식을 각자의 속도로 운반해야 한다.
 * 30/60/120Hz에서 8초 동안 여러 번 왕복하며 경계와 동승을 검사한다. */
static void test_platform_motion_and_riders(void)
{
    const int rates[] = {30, 60, 120};
    float final_offsets[3][BB_LEVEL_PLATFORM_COUNT];
    for(unsigned rate_index = 0; rate_index < sizeof rates / sizeof rates[0]; ++rate_index)
    {
        BBGame game;
        uint8_t original_maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
        float minimum[BB_LEVEL_PLATFORM_COUNT] = {0};
        float maximum[BB_LEVEL_PLATFORM_COUNT] = {0};
        float initial_speed[BB_LEVEL_PLATFORM_COUNT];
        int rate = rates[rate_index];
        float dt = 1.0f / (float)rate;
        moving_fixture(&game);
        assert(game.platform_count == BB_LEVEL_PLATFORM_COUNT);
        BBMovingPlatform *lower = platform_at_tile(&game, 3, 22);
        BBMovingPlatform *upper = platform_at_tile(&game, 17, 14);
        BBMovingPlatform *food = platform_at_tile(&game, 9, 18);
        game.players[0].body = (BBBody){ .x = 6.0f, .y = 22.0f - 0.975f, .grounded = true };
        game.players[1].body = (BBBody){ .x = 22.0f, .y = 14.0f - 0.975f, .grounded = true };
        memcpy(original_maps, game.maps, sizeof original_maps);
        for(int i = 0; i < game.platform_count; ++i)
        {
            initial_speed[i] = game.platforms[i].speed;
            /* 이 시험은 왕복 이동만 검사한다. 생성/소멸 주기는 별도로 검사한다. */
            game.platforms[i].solid_duration = 1000.0f;
        }
        /* 음식은 어느 플레이어도 닿지 않는 중간 층에 두어 수집 없이 검사한다. */
        game.pickups[0] = (BBPickup){ .body = {
            .x = 12.0f, .y = 18.0f - 0.95f, .grounded = true },
            .active = true, .type = BB_PICKUP_WATERMELON };
        bool independent_directions = false;
        for(int step = 0; step < 8 * rate; ++step)
        {
            float old_offset[BB_LEVEL_PLATFORM_COUNT];
            for(int i = 0; i < game.platform_count; ++i)
                old_offset[i] = game.platforms[i].offset;
            bb_game_update(&game, NULL, dt);
            for(int i = 0; i < game.platform_count; ++i)
            {
                const BBMovingPlatform *platform = &game.platforms[i];
                assert(fabsf(platform->offset) <= BB_PLATFORM_TRAVEL);
                assert(fabsf(platform->step - (platform->offset - old_offset[i])) < 0.00001f);
                assert(fabsf(platform->step) <= platform->speed * dt + 0.00001f);
                assert(platform->speed == initial_speed[i]);
                /* 한 틱의 구현을 복제하지 않고 총 이동 거리의 삼각파 해와 비교한다. */
                float distance = platform->speed * (float)(step + 1) / (float)rate;
                float expected = BB_PLATFORM_TRAVEL - fabsf(fmodf(distance + BB_PLATFORM_TRAVEL,
                    4.0f * BB_PLATFORM_TRAVEL) - 2.0f * BB_PLATFORM_TRAVEL);
                assert(fabsf(platform->offset - expected) < 0.0005f);
                if(platform->offset < minimum[i]) minimum[i] = platform->offset;
                if(platform->offset > maximum[i]) maximum[i] = platform->offset;
                independent_directions = independent_directions || platform->direction != game.platforms[0].direction;
                /* 그리기에 전달하는 원본 타일 오프셋은 한 발판 안에서 모두 같아야 한다. */
                for(int x = platform->x; x < platform->x + platform->width; ++x)
                    assert(bb_game_tile_offset(&game, x, platform->y) == platform->offset);
            }
            assert(game.players[0].body.grounded && game.players[1].body.grounded);
            assert(fabsf(game.players[0].body.x - (6.0f + lower->offset)) < 0.002f);
            assert(fabsf(game.players[1].body.x - (22.0f + upper->offset)) < 0.002f);
            assert(fabsf(game.players[0].body.y - (22.0f - 0.975f)) < 0.0001f);
            assert(fabsf(game.players[1].body.y - (14.0f - 0.975f)) < 0.0001f);
            assert(game.players[0].body.vx == 0.0f && game.players[1].body.vx == 0.0f);
            assert(game.pickups[0].active && game.pickups[0].body.grounded);
            assert(fabsf(game.pickups[0].body.x - (12.0f + food->offset)) < 0.002f);
            assert(game.state == BB_STATE_PLAY);
            assert(bb_game_tile_offset(&game, 3, 26) == 0.0f);
            assert(bb_game_tile_offset(&game, 0, 22) == 0.0f);
            assert(bb_game_tile_offset(&game, 0, -1) == 0.0f);
        }
        for(int i = 0; i < game.platform_count; ++i)
        {
            float sample_tolerance = game.platforms[i].speed * dt + 0.001f;
            assert(minimum[i] <= -BB_PLATFORM_TRAVEL + sample_tolerance);
            assert(maximum[i] >= BB_PLATFORM_TRAVEL - sample_tolerance);
            final_offsets[rate_index][i] = game.platforms[i].offset;
            if(rate_index > 0)
                assert(fabsf(final_offsets[rate_index][i] - final_offsets[0][i]) < 0.0005f);
        }
        assert(independent_directions);
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
    assert(platform_at_tile(&game, 3, 22)->offset > 0.0f);
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
    assert(fabsf(game.players[0].body.x - (6.0f + BB_PLAYER_GROUND_SPEED * BB_FIXED_DT + platform_at_tile(&game, 3, 22)->step)) < 0.0001f);
    assert(fabsf(game.enemies[0].body.x - (22.0f + 6.0f * BB_FIXED_DT + platform_at_tile(&game, 17, 14)->step)) < 0.0001f);
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
    assert(fabsf(game.players[1].body.x - (standing_x + platform_at_tile(&game, 3, 22)->step)) < 0.0001f);
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
    platform_at_tile(&game, 3, 22)->offset = 0.9f;
    platform_at_tile(&game, 3, 22)->step = 0.0f;
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
    assert(platform_at_tile(&game, 3, 22)->offset > 0.0f);
}

/* 새 라운드는 모든 발판의 위치를 원점으로 돌리고 유효한 속도를 배정한다. */
static void check_platform_origins(const BBGame *game)
{
    assert(game->platform_count == BB_LEVEL_PLATFORM_COUNT);
    assert(game->platform_warning_cooldown == 0.0 && game->platform_last_choice == 0);
    float minimum_duration = 20.0f - 3.5f * (float)game->level;
    for(int i = 0; i < game->platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game->platforms[i];
        assert(platform->offset == 0.0f && platform->step == 0.0f);
        assert(platform->direction == 1);
        assert(platform->speed >= BB_PLATFORM_MIN_SPEED && platform->speed < BB_PLATFORM_MAX_SPEED);
        assert(platform->state == BB_PLATFORM_SOLID && platform->state_time == 0.0);
        assert(platform->solid_duration >= minimum_duration);
        assert(platform->solid_duration < minimum_duration + 2.0f);
    }
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
        float speeds[BB_LEVEL_PLATFORM_COUNT];
        for(int p = 0; p < game.platform_count; ++p)
        {
            game.platforms[p].offset = 0.1f * (float)p;
            game.platforms[p].step = 0.01f;
            game.platforms[p].direction = -1;
            game.platforms[p].state = BB_PLATFORM_DISAPPEARING;
            game.platforms[p].state_time = 0.35;
            speeds[p] = game.platforms[p].speed;
        }
        game.platform_warning_cooldown = 0.75;
        game.platform_last_choice = 3;
        game.state = frozen[i];
        uint32_t rng = game.rng;
        bb_game_update(&game, NULL, BB_FIXED_DT);
        assert(game.rng == rng);
        assert(game.platform_warning_cooldown == 0.75 && game.platform_last_choice == 3);
        for(int p = 0; p < game.platform_count; ++p)
        {
            assert(game.platforms[p].offset == 0.1f * (float)p && game.platforms[p].step == 0.01f);
            assert(game.platforms[p].direction == -1 && game.platforms[p].speed == speeds[p]);
            assert(game.platforms[p].state == BB_PLATFORM_DISAPPEARING);
            assert(game.platforms[p].state_time == 0.35);
        }
    }
    moving_fixture(&game);
    const float invalid_dt[] = {NAN, INFINITY, -1.0f, 0.0f, 1.0f};
    uint32_t rng = game.rng;
    for(unsigned i = 0; i < sizeof invalid_dt / sizeof invalid_dt[0]; ++i)
    {
        bb_game_update(&game, NULL, invalid_dt[i]);
        check_platform_origins(&game);
        assert(game.rng == rng);
    }
    game.state = BB_STATE_CLEAR;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    float old_speeds[BB_LEVEL_PLATFORM_COUNT];
    for(int i = 0; i < game.platform_count; ++i)
    {
        assert(game.platforms[i].offset > 0.0f && game.platforms[i].step > 0.0f);
        assert(game.platforms[i].state_time == (double)BB_FIXED_DT);
        old_speeds[i] = game.platforms[i].speed;
    }
    /* 7초 클리어 경계를 넘는 틱에서 새 맵과 발판 속도를 함께 초기화한다. */
    game.platform_warning_cooldown = 0.75;
    game.platform_last_choice = 3;
    game.state_time = 6.999f;
    bb_game_update(&game, NULL, BB_FIXED_DT);
    assert(game.level == 1 && game.state == BB_STATE_PLAY);
    check_platform_origins(&game);
    bool new_speeds = false;
    for(int i = 0; i < game.platform_count; ++i)
    {
        new_speeds = new_speeds || old_speeds[i] != game.platforms[i].speed;
        game.platforms[i].offset = -0.8f;
        game.platforms[i].step = -0.01f;
        game.platforms[i].direction = -1;
        game.platforms[i].state = BB_PLATFORM_ABSENT;
        game.platforms[i].state_time = 1.5;
    }
    assert(new_speeds);
    game.platform_warning_cooldown = 0.75;
    game.platform_last_choice = 3;
    bb_game_start(&game, 2);
    assert(game.platform_count == 0);
    assert(game.platform_warning_cooldown == 0.0 && game.platform_last_choice == 0);
    for(int i = 0; i < BB_LEVEL_PLATFORM_COUNT; ++i)
        assert(game.platforms[i].offset == 0.0f && game.platforms[i].step == 0.0f);
    bb_game_skip_intro(&game);
    assert(game.level == 0);
    check_platform_origins(&game);
}

/* 같은 시드로는 발판별 추첨 결과가 재현되어야 하고, 다른 시드는 다른
 * 속도 조합을 만들어야 한다. 모든 발판이 하나의 공통 속도를 쓰는 회귀도 검출한다. */
static void test_platform_random_speeds(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBGame a, b, c;
    bb_levels_build(&maps[0][0][0]);
    bb_game_init(&a, &maps[0][0][0], 17);
    bb_game_init(&b, &maps[0][0][0], 17);
    bb_game_init(&c, &maps[0][0][0], 18);
    bb_game_start(&a, 2);
    bb_game_start(&b, 2);
    bb_game_start(&c, 2);
    bb_game_skip_intro(&a);
    bb_game_skip_intro(&b);
    bb_game_skip_intro(&c);
    check_platform_origins(&a);
    check_platform_origins(&b);
    check_platform_origins(&c);
    bool different_seed = false;
    bool different_platforms = false;
    bool different_timing = false;
    for(int i = 0; i < a.platform_count; ++i)
    {
        assert(a.platforms[i].speed == b.platforms[i].speed);
        assert(a.platforms[i].solid_duration == b.platforms[i].solid_duration);
        different_seed = different_seed || a.platforms[i].speed != c.platforms[i].speed;
        different_platforms = different_platforms || a.platforms[i].speed != a.platforms[0].speed;
        different_timing = different_timing || a.platforms[i].solid_duration != a.platforms[0].solid_duration;
    }
    assert(different_seed && different_platforms && different_timing);
}

/* 두 주기를 실제 틱으로 진행해 단계별 길이, 점멸 세 번, 남은 시간 보존을
 * 확인한다. 서로 다른 갱신 빈도에서도 같은 실제 시간에 전환해야 한다. */
static void test_platform_phase_cycles(void)
{
    const int rates[] = {30, 60, 120};
    const BBPlatformState next_states[] = {
        BB_PLATFORM_DISAPPEARING, BB_PLATFORM_ABSENT,
        BB_PLATFORM_APPEARING, BB_PLATFORM_SOLID
    };
    const double boundaries[] = {6.0, 7.2, 10.2, 11.4};
    for(unsigned r = 0; r < sizeof rates / sizeof rates[0]; ++r)
    {
        BBGame game;
        moving_fixture(&game);
        /* 순서 조정은 별도 시험에서 검사하고 여기서는 한 발판의 주기를 격리한다. */
        for(int i = 0; i < game.platform_count; ++i)
            game.platforms[i].solid_duration = 1000.0f;
        BBMovingPlatform *platform = platform_at_tile(&game, 3, 22);
        platform->solid_duration = 6.0f;
        float dt = 1.0f / (float)rates[r];
        double elapsed = 0.0;
        int transitions = 0;
        int blinks = 0;
        bool was_visible = false;
        bool moved[4] = {false};
        for(int tick = 0; tick < 30 * rates[r] && transitions < 8; ++tick)
        {
            BBPlatformState before = platform->state;
            bb_game_update(&game, NULL, dt);
            elapsed += (double)dt;
            if(platform->state != before)
            {
                if(before == BB_PLATFORM_DISAPPEARING || before == BB_PLATFORM_APPEARING)
                    assert(blinks == BB_PLATFORM_BLINK_COUNT);
                blinks = 0;
                was_visible = false;
                assert(platform->state == next_states[transitions % 4]);
                double boundary = boundaries[transitions % 4] + 11.4 * (double)(transitions / 4);
                assert(elapsed >= boundary - 0.000001 && elapsed < boundary + (double)dt + 0.000001);
                assert(fabs(platform->state_time - (elapsed - boundary)) < 0.000001);
                ++transitions;
            }
            bool visible = bb_game_platform_blink_visible(platform);
            if(visible && !was_visible) ++blinks;
            was_visible = visible;
            if(platform->state == BB_PLATFORM_SOLID || platform->state == BB_PLATFORM_ABSENT)
                assert(!visible);
            assert(bb_game_platform_solid(platform) ==
                   (platform->state == BB_PLATFORM_SOLID || platform->state == BB_PLATFORM_DISAPPEARING));
            assert(fabsf(platform->offset) <= BB_PLATFORM_TRAVEL);
            if(fabsf(platform->step) > 0.00001f) moved[platform->state] = true;
            /* 원본 레이아웃은 소멸 중에도 보존되고 충돌만 상태에 따라 달라진다. */
            assert(bb_game_tile(&game, platform->x, platform->y) == 2);
            assert(game.state == BB_STATE_PLAY);
        }
        assert(transitions == 8);
        for(int phase = 0; phase < 4; ++phase) assert(moved[phase]);
    }
}

/* 적과 전투 없이 긴 시간 동안 같은 라운드의 발판 순서만 관찰한다. */
static void keep_phase_test_running(BBGame *game)
{
    memset(game->enemies, 0, sizeof game->enemies);
    game->enemies[BB_MAX_ENEMIES - 1] = (BBEnemy){
        .active = true, .state = BB_ENEMY_SPAWNING, .spawn_delay = 1000000.0f
    };
    for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        game->players[p].invulnerable = 1000.0f;
}

/* 실제 생성/소멸을 반복하면서 64번의 선택을 기록한다. 선택할 수 없는
 * 대기 틱은 난수를 소비하지 않으며 마지막 선택 기록은 순서에 영향을 주지 않는다. */
static void record_platform_choices(BBGame *game, int choices[64], bool change_last_choice)
{
    int count = 0;
    int selected[BB_LEVEL_PLATFORM_COUNT] = {0};
    assert(game->platform_count == BB_LEVEL_PLATFORM_COUNT);
    for(int tick = 0; tick < 300 * 60 && count < 64; ++tick)
    {
        BBPlatformState previous[BB_LEVEL_PLATFORM_COUNT];
        for(int i = 0; i < game->platform_count; ++i)
            previous[i] = game->platforms[i].state;
        if(change_last_choice)
            game->platform_last_choice = (game->platform_last_choice + 2) % game->platform_count;
        uint32_t rng = game->rng;
        bb_game_update(game, NULL, 1.0f / 60.0f);
        int started = -1;
        for(int i = 0; i < game->platform_count; ++i)
        {
            if(previous[i] == BB_PLATFORM_SOLID && game->platforms[i].state == BB_PLATFORM_DISAPPEARING)
            {
                assert(started == -1);
                started = i;
            }
        }
        if(started < 0)
            assert(game->rng == rng);
        else
        {
            assert(game->rng != rng);
            assert(game->platform_last_choice == started);
            choices[count++] = started;
            ++selected[started];
        }
        assert(game->state == BB_STATE_PLAY);
    }
    assert(count == 64);
    for(int i = 0; i < game->platform_count; ++i) assert(selected[i] > 0);
}

/* 같은 시드는 같은 무작위 순서를 재현하고 다른 시드는 다른 순서를 만든다.
 * 고정 배열 순회나 마지막 인덱스에서 재개하는 방식으로 돌아가면 실패한다. */
static void test_platform_random_disappearance_order(void)
{
    const uint32_t seeds[] = {17, 17, 18};
    int choices[3][64];
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    bb_levels_build(&maps[0][0][0]);
    for(int sample = 0; sample < 3; ++sample)
    {
        BBGame game;
        bb_game_init(&game, &maps[0][0][0], seeds[sample]);
        bb_game_start(&game, 2);
        bb_game_skip_intro(&game);
        enter_test_level(&game, BB_LEVEL_COUNT - 1);
        keep_phase_test_running(&game);
        for(int i = 0; i < game.platform_count; ++i)
        {
            assert(game.platforms[i].speed >= 1.5f && game.platforms[i].speed < 3.0f);
            /* 최초 준비 시점을 같게 맞춰 유지 시간 차이와 추첨 순서를 구분한다. */
            game.platforms[i].solid_duration = 6.0f;
            game.platforms[i].state_time = 6.0;
        }
        record_platform_choices(&game, choices[sample], sample == 1);

        /* 시간이 된 후보가 하나라면 다른 발판은 선택하지 않는다. */
        game.platform_warning_cooldown = 0.0;
        for(int i = 0; i < game.platform_count; ++i)
        {
            game.platforms[i].state = BB_PLATFORM_SOLID;
            game.platforms[i].state_time = 0.0;
        }
        game.platforms[2].state_time = 6.0;
        bb_game_update(&game, NULL, BB_FIXED_DT);
        for(int i = 0; i < game.platform_count; ++i)
            assert(game.platforms[i].state == (i == 2 ? BB_PLATFORM_DISAPPEARING : BB_PLATFORM_SOLID));
    }
    bool different_seed = false;
    bool noncyclic = false;
    for(int i = 0; i < 64; ++i)
    {
        assert(choices[0][i] == choices[1][i]);
        different_seed = different_seed || choices[0][i] != choices[2][i];
        if(i > 0)
            noncyclic = noncyclic || choices[0][i] != (choices[0][i - 1] + 1) % BB_LEVEL_PLATFORM_COUNT;
    }
    assert(different_seed && noncyclic);
}

/* 자연 경과와 모든 발판이 오래 대기한 경우 모두 두 분 동안 관찰한다.
 * 오래 누적한 대기 시간을 예고 시간으로 넘겨 깜빡임이 생략되어서는 안 된다. */
static double observe_platform_schedule(BBGame *game, int rate, int limit, bool queued, int *warning_count)
{
    int starts[BB_MAX_PLATFORMS] = {0};
    BBPlatformState previous[BB_MAX_PLATFORMS];
    double first_warning = -1.0;
    double last_warning = -10.0;
    double elapsed = 0.0;
    double minimum_gap = 8.0 - 1.5 * (double)game->level;
    float dt = 1.0f / (float)rate;
    if(queued)
        for(int i = 0; i < game->platform_count; ++i)
            game->platforms[i].state_time = (double)game->platforms[i].solid_duration + 1000.0;
    for(int tick = 0; tick < 120 * rate; ++tick)
    {
        for(int i = 0; i < game->platform_count; ++i)
            previous[i] = game->platforms[i].state;
        bb_game_update(game, NULL, dt);
        elapsed += (double)dt;
        int cycling = 0;
        int new_warnings = 0;
        for(int i = 0; i < game->platform_count; ++i)
        {
            const BBMovingPlatform *platform = &game->platforms[i];
            if(platform->state != BB_PLATFORM_SOLID) ++cycling;
            if(previous[i] == BB_PLATFORM_SOLID && platform->state != BB_PLATFORM_SOLID)
            {
                /* 새 소멸은 반드시 3회 점멸의 시작 구간부터 출발한다. */
                assert(platform->state == BB_PLATFORM_DISAPPEARING);
                assert(platform->state_time >= 0.0 && platform->state_time <= (double)dt + 0.000001);
                assert(bb_game_platform_blink_visible(platform));
                assert(elapsed - last_warning >= minimum_gap - 0.000001);
                last_warning = elapsed;
                if(first_warning < 0.0) first_warning = elapsed;
                ++new_warnings;
                ++starts[i];
            }
            if(!queued && game->level == 0 && elapsed < 20.0)
                assert(platform->state == BB_PLATFORM_SOLID);
        }
        assert(new_warnings <= 1);
        assert(cycling <= limit);
        if(game->platform_count >= 3)
            assert(game->platform_count - cycling >= 2);
        assert(game->state == BB_STATE_PLAY);
    }
    /* 선택 순서의 무작위성은 별도 장기 시험에서 확인하고 여기서는 빈도를 센다. */
    int total = 0;
    for(int i = 0; i < game->platform_count; ++i)
        total += starts[i];
    if(warning_count != NULL) *warning_count = total;
    return first_warning;
}

/* 라운드마다 안전 시간과 시작 간격이 줄어들고, 동시 소멸 수는 1/1/2/2/3이다.
 * 실제 두 분 동안 소멸 횟수도 증가해야 하며 초반 안전 시간과 두 발판을 보장한다. */
static void test_platform_round_difficulty_and_schedule(void)
{
    const int rates[] = {30, 60, 120};
    const uint32_t seeds[] = {17, 18, 12345};
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    bb_levels_build(&maps[0][0][0]);
    for(unsigned r = 0; r < sizeof rates / sizeof rates[0]; ++r)
    {
        for(unsigned s = 0; s < sizeof seeds / sizeof seeds[0]; ++s)
        {
            double previous_first_warning = 100.0;
            int previous_warning_count[2] = {0};
            for(int level = 0; level < BB_LEVEL_COUNT; ++level)
            {
                for(int queued = 0; queued < 2; ++queued)
                {
                    BBGame game;
                    bb_game_init(&game, &maps[0][0][0], seeds[s]);
                    bb_game_start(&game, 2);
                    bb_game_skip_intro(&game);
                    enter_test_level(&game, level);
                    check_platform_origins(&game);
                    keep_phase_test_running(&game);
                    int warning_count = 0;
                    double first_warning = observe_platform_schedule(&game, rates[r], 1 + level / 2,
                                                                     queued != 0, &warning_count);
                    assert(warning_count > previous_warning_count[queued]);
                    previous_warning_count[queued] = warning_count;
                    if(queued == 0)
                    {
                        double minimum = 20.0 - 3.5 * (double)level;
                        assert(first_warning >= minimum);
                        assert(first_warning < minimum + 2.0 + 1.0 / (double)rates[r]);
                        assert(first_warning < previous_first_warning);
                        previous_first_warning = first_warning;
                    }
                }
            }
        }
    }
}

/* 발판이 없는 맵과 1~5개인 맵에서도 배열 경계와 동시 소멸 제한을 지킨다.
 * 작은 맵은 하나씩, 3개 이상인 맵은 항상 두 개 이상을 정상 표시한다. */
static void test_platform_schedule_small_maps(void)
{
    const int limits[] = {0, 1, 1, 1, 2, 3};
    for(int count = 0; count <= 5; ++count)
    {
        uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{{0}}};
        for(int level = 0; level < BB_LEVEL_COUNT; ++level)
        {
            for(int x = 0; x < BB_MAP_WIDTH; ++x) maps[level][26][x] = 2;
            for(int i = 0; i < count; ++i)
                maps[level][22][4 + i * 4] = maps[level][22][5 + i * 4] = 2;
        }
        BBGame game;
        bb_game_init(&game, &maps[0][0][0], 17);
        bb_game_start(&game, 2);
        bb_game_skip_intro(&game);
        enter_test_level(&game, BB_LEVEL_COUNT - 1);
        assert(game.platform_count == count);
        keep_phase_test_running(&game);
        double first_warning = observe_platform_schedule(&game, 60, limits[count], true, NULL);
        if(count == 0) assert(first_warning < 0.0);
        else assert(first_warning > 0.0);
    }
}

/* 점선이 꺼지는 소멸 예고 중에도 발판은 탑승자를 받친다. 실제 소멸하는
 * 틱에서는 점프 입력보다 먼저 지지를 해제하고, 음식도 함께 떨어져야 한다. */
static void test_platform_disappearance_collision(void)
{
    const int rates[] = {30, 60, 120};
    for(unsigned r = 0; r < sizeof rates / sizeof rates[0]; ++r)
    {
        BBGame game;
        moving_fixture(&game);
        place_platform_riders(&game);
        float dt = 1.0f / (float)rates[r];
        BBMovingPlatform *platform = platform_at_tile(&game, 3, 22);
        BBMovingPlatform *food = platform_at_tile(&game, 9, 18);
        platform->state = food->state = BB_PLATFORM_DISAPPEARING;
        platform->state_time = food->state_time = 0.25;
        game.pickups[0] = (BBPickup){ .body = {
            .x = 12.0f, .y = 18.0f - 0.95f, .grounded = true },
            .active = true, .type = BB_PICKUP_WATERMELON };
        bb_game_update(&game, NULL, dt);
        assert(!bb_game_platform_blink_visible(platform));
        assert(game.players[0].body.grounded && game.players[1].body.grounded);
        assert(game.pickups[0].active && game.pickups[0].body.grounded);
        assert(fabsf(game.players[0].body.x - (6.0f + platform->offset)) < 0.0001f);

        platform->state_time = food->state_time = BB_PLATFORM_WARNING_SECONDS - (double)dt * 0.5;
        BBInput inputs[BB_MAX_PLAYERS] = {{ .jump = true }, {0}};
        float player_x = game.players[0].body.x;
        float food_x = game.pickups[0].body.x;
        bb_game_update(&game, inputs, dt);
        assert(platform->state == BB_PLATFORM_ABSENT && food->state == BB_PLATFORM_ABSENT);
        assert(!game.events.jump);
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            assert(!game.players[p].body.grounded && game.players[p].body.vy > 0.0f);
            assert(game.players[p].body.y > 22.0f - 0.975f);
        }
        assert(game.players[0].body.x == player_x);
        assert(game.pickups[0].active && !game.pickups[0].body.grounded);
        assert(game.pickups[0].body.y > 18.0f - 0.95f);
        assert(game.pickups[0].body.x == food_x);
        float falling_y = game.players[0].body.y;
        bb_game_update(&game, NULL, dt);
        assert(game.players[0].body.y > falling_y && game.players[0].body.x == player_x);
    }
}

/* 생성 예고는 아직 밟을 수 없고, 생성 완료 순간 위에서 내려오는 몸만 받친다.
 * 이미 발판 안에 있던 몸을 위로 당기거나 아래에서 올라오는 점프를 막지 않는다. */
static void test_platform_reappearance_collision(void)
{
    const int rates[] = {30, 60, 120};
    for(unsigned r = 0; r < sizeof rates / sizeof rates[0]; ++r)
    {
        BBGame game;
        moving_fixture(&game);
        float dt = 1.0f / (float)rates[r];
        BBMovingPlatform *platform = platform_at_tile(&game, 3, 22);
        platform->state = BB_PLATFORM_APPEARING;
        platform->state_time = 0.25;
        game.players[0].body = (BBBody){ .x = 6.0f, .y = 22.0f - 0.975f - 0.01f };
        game.players[0].falling = true;
        bb_game_update(&game, NULL, dt);
        assert(!bb_game_platform_solid(platform));
        assert(!game.players[0].body.grounded && game.players[0].body.y > 22.0f - 0.975f);

        game.players[0].body = (BBBody){ .x = 6.0f, .y = 22.0f - 0.975f - 0.01f };
        game.players[1].body = (BBBody){ .x = 10.0f, .y = 21.5f };
        game.players[0].falling = game.players[1].falling = true;
        platform->state_time = BB_PLATFORM_WARNING_SECONDS - (double)dt * 0.5;
        bb_game_update(&game, NULL, dt);
        assert(platform->state == BB_PLATFORM_SOLID);
        assert(game.players[0].body.grounded);
        assert(fabsf(game.players[0].body.y - (22.0f - 0.975f)) < 0.0001f);
        assert(!game.players[1].body.grounded && game.players[1].body.y > 21.5f);

        platform->state = BB_PLATFORM_APPEARING;
        platform->state_time = BB_PLATFORM_WARNING_SECONDS - (double)dt * 0.5;
        game.players[0].body = (BBBody){ .x = 6.0f, .y = 23.5f, .vy = -18.0f };
        game.players[0].jump_start_y = 24.0f;
        game.players[0].falling = false;
        for(int tick = 0; tick < rates[r] / 3; ++tick)
        {
            bb_game_update(&game, NULL, dt);
            assert(!game.players[0].body.grounded && game.players[0].body.vy < 0.0f);
        }
        assert(platform->state == BB_PLATFORM_SOLID);
        assert(game.players[0].body.y + 0.975f < 22.0f);
    }
}

/* 같은 높이에서 떨어져 있는 두 구간도 서로 다른 발판이다. 한 발판이
 * 끝에서 돌아설 때 다른 발판의 방향이나 탑승자의 이동을 바꾸면 안 된다. */
static void test_separate_platforms_on_same_row(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{{0}}};
    BBGame game;
    for(int x = 5; x < 11; ++x) maps[0][22][x] = 2;
    for(int x = 20; x < 26; ++x) maps[0][22][x] = 2;
    bb_game_init(&game, &maps[0][0][0], 17);
    bb_game_start(&game, 2);
    bb_game_skip_intro(&game);
    assert(game.platform_count == 2);
    memset(game.enemies, 0, sizeof game.enemies);
    game.enemies[0] = (BBEnemy){ .active = true, .state = BB_ENEMY_SPAWNING, .spawn_delay = 1000.0f };
    BBMovingPlatform *left = platform_at_tile(&game, 5, 22);
    BBMovingPlatform *right = platform_at_tile(&game, 20, 22);
    left->speed = BB_PLATFORM_MIN_SPEED;
    right->speed = BB_PLATFORM_MAX_SPEED;
    left->offset = 0.999f;
    game.players[0].body = (BBBody){ .x = 7.0f + left->offset, .y = 22.0f - 0.975f, .grounded = true };
    game.players[1].body = (BBBody){ .x = 22.0f, .y = 22.0f - 0.975f, .grounded = true };
    /* 빠른 오른쪽 발판이 첫 경계에 닿기 전까지만 서로 다른 방향을 비교한다. */
    for(int step = 0; step < 30; ++step)
    {
        bb_game_update(&game, NULL, BB_FIXED_DT);
        assert(left->direction == -1 && right->direction == 1);
        assert(game.players[0].body.grounded && game.players[1].body.grounded);
        assert(fabsf(game.players[0].body.x - (7.0f + left->offset)) < 0.001f);
        assert(fabsf(game.players[1].body.x - (22.0f + right->offset)) < 0.001f);
        assert(bb_game_tile_offset(&game, 5, 22) == left->offset);
        assert(bb_game_tile_offset(&game, 20, 22) == right->offset);
        assert(bb_game_tile_offset(&game, 15, 22) == 0.0f);
    }
    assert(left->offset < 0.8f && right->offset > 0.6f);
}

/* 타일을 한 칸씩 띄워 놓은 최악의 입력에서도 모든 공중 구간을 등록한다.
 * 고정 크기 배열을 넘거나 마지막 구간을 조용히 생략하면 실패한다. */
static void test_platform_capacity(void)
{
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH] = {{{0}}};
    BBGame game;
    for(int y = 0; y < BB_MAP_HEIGHT - 2; ++y)
        for(int x = 0; x < BB_MAP_WIDTH; x += 2)
            maps[0][y][x] = 2;
    bb_game_init(&game, &maps[0][0][0], 17);
    bb_game_start(&game, 1);
    bb_game_skip_intro(&game);
    assert(game.platform_count == BB_MAX_PLATFORMS);
    for(int i = 0; i < game.platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game.platforms[i];
        assert(platform->width == 1);
        assert(platform->x >= 0 && platform->x < BB_MAP_WIDTH && platform->x % 2 == 0);
        assert(platform->y >= 0 && platform->y < BB_MAP_HEIGHT - 2);
        assert(platform->speed >= BB_PLATFORM_MIN_SPEED && platform->speed < BB_PLATFORM_MAX_SPEED);
    }
}

/* 창 비율에 맞춰 실제 월드가 커져도 원본 발판과 순환 구멍은 중앙/바닥에
 * 정렬되고, 충돌용 타일 조회가 화면 양끝까지 이어지는 벽과 바닥을 돌려줘야 한다. */
static void test_expanded_world_tiles(void)
{
    const int sizes[][2] = {{32, 28}, {64, 40}, {65, 41}, {32, 44}, {80, 28}, {256, 256}};
    for(int level = 0; level < BB_LEVEL_COUNT; ++level)
    {
        for(unsigned size = 0; size < sizeof sizes / sizeof sizes[0]; ++size)
        {
            BBGame game;
            BBMovingPlatform original[BB_LEVEL_PLATFORM_COUNT];
            moving_fixture(&game);
            enter_test_level(&game, level);
            for(int i = 0; i < game.platform_count; ++i)
            {
                game.platforms[i].state = (BBPlatformState)(i % 4);
                game.platforms[i].state_time = 0.1 * (double)(i + 1);
            }
            game.platform_warning_cooldown = 0.75;
            game.platform_last_choice = 3;
            memcpy(original, game.platforms, sizeof original);
            int width = sizes[size][0];
            int height = sizes[size][1];
            int dx = (width - BB_MAP_WIDTH) / 2;
            int dy = height - BB_MAP_HEIGHT;
            uint32_t rng = game.rng;
            bb_game_resize(&game, width, height);
            assert(game.world_width == width && game.world_height == height);
            assert(game.level == level);
            assert(game.platform_warning_cooldown == 0.75 && game.platform_last_choice == 3);
            /* 폭이 늘면 추가 몬스터의 방향 추첨으로 난수가 소비될 수 있다. */
            if(width == BB_MAP_WIDTH) assert(game.rng == rng);
            assert(game.platform_count == BB_LEVEL_PLATFORM_COUNT);
            /* 양 끝의 두 칸 벽은 위쪽 진입 두 줄을 제외한 모든 높이에 존재한다. */
            for(int y = 2; y < height; ++y)
            {
                assert(bb_game_tile(&game, 0, y) == 3 && bb_game_tile(&game, 1, y) == 3);
                assert(bb_game_tile(&game, width - 2, y) == 3 && bb_game_tile(&game, width - 1, y) == 3);
            }
            for(int y = 0; y < BB_MAP_HEIGHT; ++y)
                for(int x = 2; x < BB_MAP_WIDTH - 2; ++x)
                    assert(bb_game_tile(&game, x + dx, y + dy) == game.maps[level][y][x]);
            for(int y = 0; y < dy; ++y)
                for(int x = 2; x < width - 2; ++x)
                    assert(bb_game_tile(&game, x, y) == 0);
            for(int x = 2; x < width - 2; ++x)
            {
                bool hole = x >= 14 + dx && x < 18 + dx;
                assert(bb_game_tile(&game, x, height - 2) == (hole ? 0 : 2));
                assert(bb_game_tile(&game, x, height - 1) == (hole ? 0 : 1));
                assert(bb_game_tile_offset(&game, x, height - 2) == 0.0f);
            }
            for(int i = 0; i < game.platform_count; ++i)
            {
                const BBMovingPlatform *platform = &game.platforms[i];
                assert(platform->x == original[i].x + dx && platform->y == original[i].y + dy);
                assert(platform->width == original[i].width && platform->speed == original[i].speed);
                assert(platform->offset == original[i].offset && platform->direction == original[i].direction);
                assert(platform->state == original[i].state && platform->state_time == original[i].state_time);
                assert(platform->solid_duration == original[i].solid_duration);
                for(int x = platform->x; x < platform->x + platform->width; ++x)
                    assert(bb_game_tile(&game, x, platform->y) == 2);
            }
            assert(bb_game_tile(&game, -1, 3) == 0 && bb_game_tile(&game, width, 3) == 0);
            assert(bb_game_tile(&game, 3, -1) == 0 && bb_game_tile(&game, 3, height) == 0);
        }
    }
}

/* 확대 뒤 새 게임/라운드에 들어가도 해상도에서 정한 월드 크기를 유지한다.
 * 실제 시작 위치와 새로 등록한 발판도 확장된 좌표계 안에 있어야 한다. */
static void test_expanded_world_lifecycle(void)
{
    BBGame game;
    moving_fixture(&game);
    bb_game_resize(&game, 64, 40);
    bb_game_start(&game, 2);
    assert(game.world_width == 64 && game.world_height == 40);
    assert(game.players[0].body.x == 4.0f && game.players[1].body.x == 60.0f);
    assert(game.players[0].body.y == 36.0f && game.players[1].body.y == 36.0f);
    bb_game_skip_intro(&game);
    for(int level = 0; level < BB_LEVEL_COUNT; ++level)
    {
        enter_test_level(&game, level);
        assert(game.world_width == 64 && game.world_height == 40);
        assert(game.platform_count == BB_LEVEL_PLATFORM_COUNT);
        for(int i = 0; i < game.platform_count; ++i)
        {
            const BBMovingPlatform *platform = &game.platforms[i];
            assert(platform->x >= 18 && platform->x + platform->width <= 46);
            assert(platform->y >= 12 && platform->y < 38);
            assert(bb_game_tile(&game, platform->x, platform->y) == 2);
        }
        assert(bb_game_enemies_left(&game) == (level + 1) * 2);
        for(int i = 0; i < (level + 1) * 2; ++i)
        {
            assert(game.enemies[i].active);
            assert(game.enemies[i].body.x >= 3.0f && game.enemies[i].body.x <= 61.0f);
            assert(game.enemies[i].spawn_y >= 16.0f && game.enemies[i].spawn_y <= 20.0f);
        }
    }
}

/* 맵 구조/25개 점프와 발판의 독립 이동/동승/충돌/초기화를 함께 검사한다. */
int main(void)
{
    test_layouts_and_jumps();
    test_platform_motion_and_riders();
    test_platform_walk_jump_and_fixed_floor();
    test_shifted_platform_collision_and_wall();
    test_platform_state_and_reset();
    test_platform_random_speeds();
    test_platform_phase_cycles();
    test_platform_random_disappearance_order();
    test_platform_round_difficulty_and_schedule();
    test_platform_schedule_small_maps();
    test_platform_disappearance_collision();
    test_platform_reappearance_collision();
    test_separate_platforms_on_same_row();
    test_platform_capacity();
    test_expanded_world_tiles();
    test_expanded_world_lifecycle();
    puts("Five layouts, 25 jumps, random platform motion, phase warnings, collisions, and expanded worlds passed.");
    return 0;
}
