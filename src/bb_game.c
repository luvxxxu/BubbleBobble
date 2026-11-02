/* 게임 규칙과 물리 계산을 담당한다. 그래픽/키보드/파일 API를 사용하지 않아
 * 같은 입력과 난수 시드로 창 없이도 게임 전체를 재현할 수 있다.
 * 위치는 타일, 속도는 타일/초, 시간은 초이며 화면 아래쪽이 +y이다.
 * game은 게임 전체를 담은 구조체의 주소다. game->players[i]는 그 안의
 * i번째 플레이어이며, &는 주소를 얻고 ->는 주소로 받은 구조체의 항목을 읽는다.
 * static 함수는 이 파일 안에서만 쓰는 작업이고, bb_game_ 함수는 다른 파일도 쓴다.
 * dt는 이번 갱신에서 흐른 초다. 속도에 dt를 곱하면 이번 이동 거리가 된다.
 * sizeof는 데이터 크기, memset은 0으로 지우기, memcpy는 배열 복사에 쓴다.
 * fabsf는 절댓값, fminf/fmaxf는 작은/큰 값, floorf는 아래 정수로 내림이다. */
#include "bb_game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 벽과 천장/바닥 충돌을 각각 참/거짓으로 기록한다. */
typedef struct BBHits {
    bool x;
    bool y;
} BBHits;

/* 범위를 벗어난 값은 가까운 끝값으로 제한한다.
 * 입력 세기, 좌표, 연출 진행률처럼 상한/하한이 있는 값에 공통으로 쓴다. */
static float clamp(float value, float low, float high)
{
    if(value < low)
    {
        return low;
    }
    if(value > high)
    {
        return high;
    }
    return value;
}

/* 남은 시간을 한 틱만큼 줄이되 음수로 누적시키지 않는다.
 * 0을 '대기 종료'로 사용하는 무적/공격/사망 타이머에 적용한다. */
static float decrease(float value, float dt)
{
    value -= dt;
    if(value < 0.0f)
    {
        return 0.0f;
    }
    return value;
}

/* 지정된 난수 상태를 한 번 진행하고 0 이상 1 미만을 반환한다.
 * 부활 버블은 별도 상태를 써서 동료의 행동이나 적 AI에 경로가 바뀌지 않는다. */
static float random_from_state(uint32_t *state)
{
    /* 곱셈과 덧셈으로 다음 난수를 만든다. 같은 시드면 테스트도 재현된다.
     * unsigned 32비트 정수의 넘침은 C에서 정의된 순환 연산이다. */
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    /* 0..9999를 나누므로 1.0은 나오지 않아 배열 인덱스 추첨에도 쓸 수 있다. */
    return (float)(*state % 10000u) / 10000.0f;
}

/* 적과 발판 등 일반 게임 규칙에서 사용할 난수를 한 번 뽑는다. */
static float random_unit(BBGame *game)
{
    return random_from_state(&game->rng);
}

/* 원본 맵은 고정 크기로 보존하고 공중 발판은 중앙/바닥 기준으로 배치한다.
 * 넓어진 공간에서는 측면 벽을 실제 양끝으로 옮기고 바닥만 좌우로 연장한다.
 * 기본 크기는 시험용 맵을 포함해 원래 타일 배열을 그대로 읽는다. */
int bb_game_tile(const BBGame *game, int x, int y)
{
    /* 맵 밖은 빈 공간이다. 아래쪽 순환 통로와 맵 위 진입에 벽을 만들지 않는다. */
    if(game == NULL || x < 0 || x >= game->world_width || y < 0 || y >= game->world_height)
    {
        return 0;
    }
    if(game->world_width == BB_MAP_WIDTH && game->world_height == BB_MAP_HEIGHT)
    {
        return game->maps[game->level][y][x];
    }

    int offset_x = (game->world_width - BB_MAP_WIDTH) / 2;
    int offset_y = game->world_height - BB_MAP_HEIGHT;
    int source_x = x - offset_x;
    int source_y = y - offset_y;
    if(x < 2 || x >= game->world_width - 2)
    {
        /* 위쪽 두 줄은 원래 진입 공간이고, 늘어난 높이의 측면은 세 번째
         * 원본 줄의 벽을 이어 쓴다. 이후에는 각 원본 줄의 벽을 보존한다. */
        int wall_x = x;
        if(x >= 2)
        {
            wall_x = BB_MAP_WIDTH - (game->world_width - x);
        }
        int wall_y = y;
        if(y >= 2)
        {
            wall_y = source_y;
            if(source_y < 2)
            {
                wall_y = 2;
            }
        }
        return game->maps[game->level][wall_y][wall_x];
    }
    if(source_y < 0)
    {
        return 0;
    }
    if(source_x >= 2 && source_x < BB_MAP_WIDTH - 2)
    {
        return game->maps[game->level][source_y][source_x];
    }
    if(y >= game->world_height - 2)
    {
        int floor_x = 2;
        if(source_x >= 2)
        {
            floor_x = BB_MAP_WIDTH - 3;
        }
        return game->maps[game->level][source_y][floor_x];
    }
    return 0;
}

/* 한 발판에 속한 원본 기준 타일을 찾는다. 생성 예고나 부재 중에도
 * 발판 자체의 좌표와 이동 상태는 남아 다음 생성 위치를 표시할 수 있다. */
static const BBMovingPlatform *platform_for_tile(const BBGame *game, int x, int y)
{
    for(int i = 0; i < game->platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game->platforms[i];
        if(platform->y == y && x >= platform->x && x < platform->x + platform->width)
        {
            return platform;
        }
    }
    return NULL;
}

/* 소멸 예고가 끝날 때까지는 기존 발판을 밟을 수 있다.
 * 생성 예고는 아직 실체가 없으므로 아래/위에서 모두 통과한다. */
bool bb_game_platform_solid(const BBMovingPlatform *platform)
{
    if(platform == NULL)
    {
        return false;
    }
    if(platform->state == BB_PLATFORM_SOLID || platform->state == BB_PLATFORM_DISAPPEARING)
    {
        return true;
    }
    return false;
}

/* 0.2초 점선 표시와 0.2초 숨김을 세 번 반복한다. 렌더링 프레임이 아닌
 * 물리 시계로 판정해 일시 정지와 서로 다른 화면 갱신 주기에서도 일치한다. */
bool bb_game_platform_blink_visible(const BBMovingPlatform *platform)
{
    if(platform == NULL || (platform->state != BB_PLATFORM_DISAPPEARING &&
                            platform->state != BB_PLATFORM_APPEARING) ||
       !isfinite(platform->state_time) || platform->state_time < 0.0 ||
       platform->state_time >= BB_PLATFORM_WARNING_SECONDS)
        return false;
    int interval = (int)(platform->state_time / BB_PLATFORM_BLINK_SECONDS);
    return interval % 2 == 0;
}

/* 공개 맵 조회는 원본 배치를 유지하고, 물리 조회만 부재/생성 예고 중인
 * 발판을 빈 공간으로 취급한다. 착지·거품 발사·적 AI가 같은 상태를 읽는다. */
static int tile_at(const BBGame *game, int x, int y)
{
    int tile = bb_game_tile(game, x, y);
    if(tile == 2 && y < game->world_height - 2)
    {
        const BBMovingPlatform *platform = platform_for_tile(game, x, y);
        if(platform != NULL && !bb_game_platform_solid(platform))
        {
            return 0;
        }
    }
    return tile;
}

/* 같은 줄에서 이어진 발판 타일들을 한 덩어리로 등록한다. 각 덩어리는
 * 라운드마다 자기 속도를 뽑고, 구성 타일은 같은 이동량을 써서 갈라지지 않는다.
 * 최대 개수는 한 줄에 타일/빈칸이 번갈아 놓인 경우까지 담을 수 있다. */
static void reset_platforms(BBGame *game)
{
    memset(game->platforms, 0, sizeof game->platforms);
    game->platform_count = 0;
    game->platform_warning_cooldown = 0.0;
    game->platform_last_choice = 0;
    for(int y = 0; y < BB_MAP_HEIGHT - 2; ++y)
    {
        for(int x = 0; x < BB_MAP_WIDTH; ++x)
        {
            if(game->maps[game->level][y][x] != 2)
            {
                continue;
            }
            int first = x;
            while(x + 1 < BB_MAP_WIDTH && game->maps[game->level][y][x + 1] == 2)
            {
                ++x;
            }
            /* 난수 호출 순서를 문장으로 고정해 컴파일러의 초기화식 평가 순서에
             * 따라 속도와 수명이 서로 바뀌는 일을 막는다. */
            float speed = BB_PLATFORM_MIN_SPEED + random_unit(game) *
                          (BB_PLATFORM_MAX_SPEED - BB_PLATFORM_MIN_SPEED);
            /* 1라운드에는 충분히 익힐 시간을 주고 다음 라운드마다 유지 시간을
             * 3.5초 줄인다. 작은 무작위 차이로 첫 소멸 시점도 서로 엇갈린다. */
            float duration = BB_PLATFORM_FIRST_SOLID_SECONDS -
                             BB_PLATFORM_ROUND_ACCELERATION * (float)game->level +
                             random_unit(game) * BB_PLATFORM_SOLID_VARIATION;
            /* 배열의 빈 칸 하나를 골라 위치와 규칙을 차례대로 적는다.
             * 나머지 항목은 함수 맨 위의 memset에서 이미 0으로 지웠다. */
            BBMovingPlatform *platform = &game->platforms[game->platform_count];
            platform->x = first + (game->world_width - BB_MAP_WIDTH) / 2;
            platform->y = y + game->world_height - BB_MAP_HEIGHT;
            platform->width = x - first + 1;
            platform->direction = 1;
            platform->speed = speed;
            platform->state = BB_PLATFORM_SOLID;
            platform->solid_duration = duration;
            game->platform_count++;
        }
    }
}

/* 맵은 원본 좌표를 유지한다. 물리와 렌더링 모두 이 함수로 같은 발판의
 * 이동량을 읽어 눈에 보이는 위치와 충돌 위치가 서로 달라지지 않게 한다. */
float bb_game_tile_offset(const BBGame *game, int x, int y)
{
    if(game == NULL || y < 0 || y >= game->world_height - 2 || bb_game_tile(game, x, y) != 2)
    {
        return 0.0f;
    }
    const BBMovingPlatform *platform = platform_for_tile(game, x, y);
    if(platform == NULL)
    {
        return 0.0f;
    }
    return platform->offset;
}

/* 기본 맵의 이동 폭은 유지하고 화면 확장으로 생긴 좌우 공간만큼 왕복한다. */
float bb_game_platform_travel(const BBGame *game)
{
    float extra_travel = 0.0f;
    if(game != NULL)
    {
        extra_travel = fmaxf(0.0f, (float)(game->world_width - BB_MAP_WIDTH)) * 0.5f;
    }
    return BB_PLATFORM_TRAVEL + extra_travel;
}

/* 발판 전체가 두 칸 두께의 양옆 벽 안에 남도록 각 발판의 범위를 구한다.
 * 벽 사이보다 큰 시험용 구간처럼 유효한 이동 범위가 없으면 제자리에 둔다. */
static void platform_bounds(const BBGame *game, int x, int width, float *minimum, float *maximum)
{
    float travel = bb_game_platform_travel(game);
    *minimum = fmaxf(-travel, 2.0f - (float)x);
    *maximum = fminf(travel, (float)(game->world_width - 2 - x - width));
    if(*minimum > *maximum)
    {
        *minimum = 0.0f;
        *maximum = 0.0f;
    }
}

/* 진행 중인 예고/부재 시간을 갱신한다. SOLID는 시간이 다 되어도 순서를
 * 기다리고, 실제 소멸 시작은 아래 스케줄러가 남은 발판 수를 보고 허용한다. */
static void update_platform_state(BBMovingPlatform *platform, float dt)
{
    bool was_solid = bb_game_platform_solid(platform);
    platform->state_time += (double)dt;
    for(;;)
    {
        if(platform->state == BB_PLATFORM_SOLID)
        {
            break;
        }
        double duration = BB_PLATFORM_WARNING_SECONDS;
        if(platform->state == BB_PLATFORM_ABSENT)
        {
            duration = BB_PLATFORM_ABSENT_SECONDS;
        }
        if(platform->state_time < duration)
        {
            break;
        }
        platform->state_time -= duration;
        switch(platform->state)
        {
            case BB_PLATFORM_SOLID:
                break;
            case BB_PLATFORM_DISAPPEARING:
                platform->state = BB_PLATFORM_ABSENT;
                break;
            case BB_PLATFORM_ABSENT:
                platform->state = BB_PLATFORM_APPEARING;
                break;
            case BB_PLATFORM_APPEARING:
                platform->state = BB_PLATFORM_SOLID;
                break;
        }
    }
    platform->vanished = was_solid && !bb_game_platform_solid(platform);
}

/* 소멸 예고부터 재생성 완료까지 한 자리를 차지한다. 다섯 발판 중
 * 1/2라운드는 하나, 3/4라운드는 둘, 마지막 라운드는 셋까지만 순환시켜
 * 항상 최소 두 발판을 정상 표시한다. 시험용 한두 발판 맵도 순환할 수 있다. */
static void schedule_platform_disappearance(BBGame *game, float dt)
{
    if(game->platform_count == 0)
    {
        return;
    }
    int limit = 1 + game->level / 2;
    int available = 1;
    if(game->platform_count > 2)
    {
        available = game->platform_count - 2;
    }
    if(limit > available)
    {
        limit = available;
    }
    int cycling = 0;
    for(int i = 0; i < game->platform_count; ++i)
        if(game->platforms[i].state != BB_PLATFORM_SOLID)
        {
            ++cycling;
        }
    if(cycling >= limit || game->platform_warning_cooldown > 0.0)
    {
        return;
    }
    /* 시간이 된 발판만 후보에 넣는다. 실제로 시작할 때 한 번만 추첨하므로
     * 대기 중인 틱 수가 난수 상태를 불필요하게 바꾸지 않는다. */
    int candidates[BB_MAX_PLATFORMS];
    int candidate_count = 0;
    for(int index = 0; index < game->platform_count; ++index)
    {
        const BBMovingPlatform *platform = &game->platforms[index];
        double duration = BB_PLATFORM_FIRST_SOLID_SECONDS;
        if(platform->solid_duration > 0.0f)
        {
            duration = platform->solid_duration;
        }
        if(platform->state == BB_PLATFORM_SOLID && platform->state_time >= duration)
        {
            candidates[candidate_count++] = index;
        }
    }
    if(candidate_count == 0)
    {
        return;
    }
    int selected = (int)(random_unit(game) * (float)candidate_count);
    int index = candidates[selected];
    BBMovingPlatform *platform = &game->platforms[index];
    double duration = BB_PLATFORM_FIRST_SOLID_SECONDS;
    if(platform->solid_duration > 0.0f)
    {
        duration = platform->solid_duration;
    }
    double overdue = platform->state_time - duration;
    platform->state = BB_PLATFORM_DISAPPEARING;
    /* 오래 기다린 시간으로 예고를 건너뛰지 않고 세 번 모두 보여준다. */
    platform->state_time = 0.0;
    if(overdue < (double)dt)
    {
        platform->state_time = overdue;
    }
    /* 초반에는 다음 발판까지도 여유를 주고 라운드마다 간격을 줄인다.
     * 유지 시간만 줄이면 동시 소멸 제한에 막혀 1/2라운드의 실제 빈도가
     * 같아질 수 있으므로 시작 간격도 8/6.5/5/3.5/2초로 조절한다. */
    game->platform_warning_cooldown = BB_PLATFORM_FIRST_WARNING_GAP_SECONDS -
                                      BB_PLATFORM_WARNING_GAP_REDUCTION * (double)game->level;
    /* 마지막 선택은 재현/진단용으로만 기록하며 다음 추첨 순서에는 쓰지 않는다. */
    game->platform_last_choice = index;
}

/* 현재 화면의 한 점에 있는 벽/발판을 찾는다. 고정 타일과 각 발판의
 * 실제 구간을 따로 검사해 원본에서 여러 칸 이동한 발판도 정확히 찾는다.
 * left에는 실제 타일의 왼쪽 면을 돌려주어 거품 생성 경계에도 사용한다. */
static int solid_tile_at(const BBGame *game, float x, int y, float *left)
{
    int column = (int)floorf(x);
    int tile = tile_at(game, column, y);
    if(tile == 3 || (tile == 2 && (y >= game->world_height - 2 ||
                                  platform_for_tile(game, column, y) == NULL)))
    {
        *left = (float)column;
        return tile;
    }
    for(int i = 0; i < game->platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game->platforms[i];
        if(platform->y != y || !bb_game_platform_solid(platform))
        {
            continue;
        }
        float platform_left = (float)platform->x + platform->offset;
        if(x >= platform_left && x < platform_left + (float)platform->width)
        {
            *left = platform_left + floorf(x - platform_left);
            return 2;
        }
    }
    return 0;
}

/* 이번 틱의 발판 위치와 이동 거리를 함께 구한다. 경계를 조금 넘은 거리는
 * 반대쪽으로 되돌려 느린 프레임에서도 왕복 속도가 달라지지 않게 한다. */
static void update_platforms(BBGame *game, float dt)
{
    game->platform_warning_cooldown = fmax(0.0, game->platform_warning_cooldown - (double)dt);
    for(int i = 0; i < game->platform_count; ++i)
    {
        BBMovingPlatform *platform = &game->platforms[i];
        float old_offset = platform->offset;
        float minimum, maximum;
        platform_bounds(game, platform->x, platform->width, &minimum, &maximum);
        platform->offset = clamp(platform->offset, minimum, maximum);
        if(minimum < maximum)
        {
            platform->offset += (float)platform->direction * platform->speed * dt;
        }
        if(platform->offset >= maximum)
        {
            platform->offset = 2.0f * maximum - platform->offset;
            platform->direction = -1;
        }
        else if(platform->offset <= minimum)
        {
            platform->offset = 2.0f * minimum - platform->offset;
            platform->direction = 1;
        }
        platform->step = platform->offset - old_offset;
        update_platform_state(platform, dt);
    }
    schedule_platform_disappearance(game, dt);
}

/* grounded만으로는 거품 위에 선 경우와 구별할 수 없다. 물체의 발이
 * 이전 틱의 공중 발판 윗면에 실제로 닿아 있었는지 좌표까지 확인한다.
 * 점프 중이거나 바닥/거품 위에 있으면 발판에 끌려가지 않는다. */
static float platform_carry(const BBGame *game, const BBBody *body, float half_width, float half_height)
{
    if(!body->grounded || body->vy < 0.0f)
    {
        return 0.0f;
    }
    float feet = body->y + half_height;
    int row = (int)floorf(feet + 0.001f);
    if(row < 0 || row >= game->world_height - 2 || fabsf(feet - (float)row) > 0.001f)
    {
        return 0.0f;
    }
    /* 두 발판의 끝을 동시에 밟으면 더 넓게 겹치는 발판 하나만 따른다.
     * 이전 틱의 위치로 판정하므로 움직이기 전의 실제 지지면을 찾는다. */
    float carry = 0.0f;
    float best_overlap = 0.0001f;
    for(int i = 0; i < game->platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game->platforms[i];
        if(platform->y != row || !bb_game_platform_solid(platform))
        {
            continue;
        }
        float left = (float)platform->x + platform->offset - platform->step;
        float right = left + (float)platform->width;
        float overlap = fminf(body->x + half_width, right) - fmaxf(body->x - half_width, left);
        if(overlap > best_overlap)
        {
            best_overlap = overlap;
            carry = platform->step;
        }
    }
    return carry;
}

/* 막 사라진 발판의 탑승 상태를 입력 처리 전에 해제한다. 지난 틱의 grounded로
 * 허공 점프를 하거나 한 틱 더 발판에 끌려가지 않게 한다. 다른 발판이나 고정
 * 벽 윗면도 동시에 밟고 있었다면 그 지지를 유지한다. */
static void release_platform_rider(const BBGame *game, BBBody *body, float half_width, float half_height)
{
    if(!body->grounded || body->vy < 0.0f)
    {
        return;
    }
    float feet = body->y + half_height;
    int row = (int)floorf(feet + 0.001f);
    if(fabsf(feet - (float)row) > 0.001f)
    {
        return;
    }
    bool lost_support = false;
    for(int i = 0; i < game->platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game->platforms[i];
        if(platform->y != row)
        {
            continue;
        }
        float left = (float)platform->x + platform->offset - platform->step;
        float right = left + (float)platform->width;
        if(body->x + half_width <= left + 0.0001f || body->x - half_width >= right - 0.0001f)
        {
            continue;
        }
        if(bb_game_platform_solid(platform))
        {
            return;
        }
        lost_support = lost_support || platform->vanished;
    }
    if(!lost_support)
    {
        return;
    }
    for(int x = (int)floorf(body->x - half_width + 0.0001f);
        x <= (int)floorf(body->x + half_width - 0.0001f); ++x)
        if(tile_at(game, x, row) == 3)
        {
            return;
        }
    body->grounded = false;
}

/* a_half_width/ah와 b_half_width/bh는 전체 너비/높이가 아니라 중심에서 가장자리까지의 거리다.
 * 두 축 모두 겹쳐야 사각형 충돌이며, 경계에 닿기만 한 경우는 제외한다. */
static bool rects_overlap(const BBBody *a, float a_half_width, float a_half_height,
                          const BBBody *b, float b_half_width, float b_half_height)
{
    return fabsf(a->x - b->x) < a_half_width + b_half_width && fabsf(a->y - b->y) < a_half_height + b_half_height;
}

/* 중심 거리와 반지름 합을 비교한다. 제곱끼리 비교해 매번 제곱근을
 * 구하지 않아도 같은 결과를 얻으며, 버블/바위/아이템 접촉에 사용한다. */
static bool circles_overlap(const BBBody *a, float a_radius,
                            const BBBody *b, float b_radius)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float radius = a_radius + b_radius;
    return dx * dx + dy * dy < radius * radius;
}

/* 몸체를 한 틱 이동하고 가로/세로 충돌 여부를 반환한다.
 * half_width/hh는 반너비/반높이, one_way는 일방 통과 발판 착지 허용 여부,
 * wrap은 화면 아래로 떨어졌을 때 위로 순환할지 여부다.
 * 가로 -> 세로 순서로 검사하여 벽 모서리에서 축별 속도를 따로 정리한다.
 * 타일 2는 내려오는 물체만 받으므로 아래에서 위로 점프해 통과할 수 있다. */
static BBHits move_body(const BBGame *game, BBBody *body, float half_width, float half_height,
                          float dt, bool one_way, bool wrap)
{
    BBHits hit = {false, false};
    float old_x = body->x;
    float old_y = body->y;
    /* 아래의 작은 오차 허용값은 정확히 맞닿은 타일을 중복 충돌로 보거나,
     * float 반올림 때문에 발판에 선 몸체가 매번 떠 있는 것으로 보이는 일을 막는다. */
    /* 자기 속도에 발판의 이번 이동 거리를 더한다. 속도 자체는 바꾸지 않아
     * 점프 관성, 적의 걷기 속도, 튕기는 속도에 발판 속도가 누적되지 않는다. */
    float horizontal_step = body->vx * dt;
    if(one_way)
    {
        horizontal_step += platform_carry(game, body, half_width, half_height);
    }
    /* 발판에 밀려 벽에 닿은 경우에도 같은 벽 충돌 처리를 거친다. */
    body->x += horizontal_step;
    /* 넓은 화면의 가속 이동은 한 틱에 벽 두께보다 멀리 갈 수 있다.
     * 출발부터 도착까지 지나간 열을 모두 검사해 얇은 벽을 건너뛰지 않는다. */
    int first_column = (int)floorf(fminf(old_x, body->x) - half_width + 0.0001f);
    int last_column = (int)floorf(fmaxf(old_x, body->x) + half_width - 0.0001f);
    for(int y = (int)floorf(body->y - half_height + 0.0001f); y <= (int)floorf(body->y + half_height - 0.0001f); ++y)
    {
        for(int x = first_column; x <= last_column; ++x)
        {
            if(tile_at(game, x, y) != 3)
            {
                continue;
            }
            /* 이전 위치가 벽 바깥이었을 때만 진입 방향의 경계로 되돌린다.
             * 타일 1(장식), 2(발판)는 옆면을 막지 않는다. */
            if(horizontal_step > 0.0f && old_x + half_width <= (float)x + 0.001f)
            {
                body->x = fminf(body->x, (float)x - half_width);
                hit.x = true;
            }
            else if(horizontal_step < 0.0f && old_x - half_width >= (float)(x + 1) - 0.001f)
            {
                body->x = fmaxf(body->x, (float)(x + 1) + half_width);
                hit.x = true;
            }
        }
    }
    if(body->x < half_width || body->x > (float)game->world_width - half_width)
    {
        body->x = clamp(body->x, half_width, (float)game->world_width - half_width);
        hit.x = true;
    }
    if(hit.x)
    {
        body->vx = 0.0f;
    }

    /* 세로 이동 후 발판 또는 천장에 닿았는지 검사한다. */
    body->y += body->vy * dt;
    /* 지난 틱에 착지했어도 이번 틱에 발판을 벗어날 수 있으므로 재검사한다. */
    body->grounded = false;
    for(int y = (int)floorf(body->y - half_height + 0.0001f); y <= (int)floorf(body->y + half_height + 0.0001f); ++y)
    {
        /* 벽과 고정 바닥은 현재 몸체가 겹치는 원본 타일만 검사한다.
         * 등록된 이동 발판은 아래에서 실제 사각형으로 한 번씩 검사한다. */
        for(int x = (int)floorf(body->x - half_width + 0.0001f);
            x <= (int)floorf(body->x + half_width - 0.0001f); ++x)
        {
            int tile = tile_at(game, x, y);
            if(tile != 3 && !(one_way && tile == 2))
            {
                continue;
            }
            if(tile == 2 && y < game->world_height - 2 && platform_for_tile(game, x, y) != NULL)
            {
                continue;
            }
            float tile_left = (float)x;
            if(body->x + half_width <= tile_left + 0.0001f || body->x - half_width >= tile_left + 1.0f - 0.0001f)
            {
                continue;
            }
            if(body->vy >= 0.0f && old_y + half_height <= (float)y + 0.001f && body->y + half_height >= (float)y)
            {
                /* 내려오며 윗면을 통과한 경우 발을 윗면에 맞춘다. 여러 타일과
                 * 겹쳐도 fminf로 가장 위쪽의 착지면을 유지한다. */
                body->y = fminf(body->y, (float)y - half_height);
                body->grounded = true;
                hit.y = true;
            }
            else if(tile == 3 && body->vy < 0.0f && old_y - half_height >= (float)(y + 1) - 0.001f)
            {
                /* 올라갈 때 머리를 막는 것은 단단한 벽/천장(3)뿐이다. */
                body->y = fmaxf(body->y, (float)(y + 1) + half_height);
                hit.y = true;
            }
        }
        if(one_way && body->vy >= 0.0f && old_y + half_height <= (float)y + 0.001f && body->y + half_height >= (float)y)
        {
            for(int i = 0; i < game->platform_count; ++i)
            {
                const BBMovingPlatform *platform = &game->platforms[i];
                if(platform->y != y || !bb_game_platform_solid(platform))
                {
                    continue;
                }
                float left = (float)platform->x + platform->offset;
                float right = left + (float)platform->width;
                if(body->x + half_width <= left + 0.0001f || body->x - half_width >= right - 0.0001f)
                {
                    continue;
                }
                body->y = fminf(body->y, (float)y - half_height);
                body->grounded = true;
                hit.y = true;
            }
        }
    }
    if(hit.y)
    {
        body->vy = 0.0f;
    }
    /* 바닥 아래로 완전히 내려간 뒤에만 위쪽으로 순환시킨다. */
    if(wrap && body->y > (float)game->world_height + 1.0f)
    {
        body->y -= (float)game->world_height + 2.0f;
    }
    return hit;
}

/* 새 라운드/부활에서 공통으로 쓰는 배치다. 1P는 왼쪽, 2P는 오른쪽에서
 * 중앙을 바라본다. 생명/점수/마나는 건드리지 않아 게임 중 기록이 이어진다. */
static void place_player(const BBGame *game, BBPlayer *player, int number)
{
    /* memset은 몸체의 모든 항목을 0으로 지운다. 그 뒤 시작 위치를 넣는다. */
    memset(&player->body, 0, sizeof player->body);
    player->body.x = 4.0f;
    player->facing = 1;
    if(number != 0)
    {
        player->body.x = (float)game->world_width - 4.0f;
        player->facing = -1;
    }
    player->body.y = (float)game->world_height - 4.0f;
    player->state = BB_PLAYER_NORMAL;
    player->death_timer = 0.0f;
    player->rescue_pop_timer = 0.0f;
    player->rescue_time_left = 0.0;
    player->rescue_turn_timer = 0.0;
    player->rescue_drift_vx = 0.0f;
    player->rescue_drift_vy = 0.0f;
    player->rescue_rng = 0;
    player->falling = true;
    player->air_control = true;
    player->jump_start_y = player->body.y;
    player->attack_timer = 0.0f;
    player->fire_cooldown = 0.0f;
}

/* 큰 부활 버블과 HELP 표시를 화면 안에 유지한다. 가장자리에서는 현재 속도와
 * 무작위 목표 방향을 함께 반사해 다음 방향 추첨까지 벽에 붙어 있지 않게 한다. */
static void clamp_rescue_bubble(const BBGame *game, BBPlayer *player)
{
    float margin = 2.0f + BB_RESCUE_BUBBLE_RADIUS;
    float x = clamp(player->body.x, margin, (float)game->world_width - margin);
    float y = clamp(player->body.y, 5.5f, (float)game->world_height - 3.5f);
    if((player->body.x - x) * player->body.vx > 0.0f)
    {
        player->body.vx = -player->body.vx;
        if(player->body.x > x)
        {
            player->rescue_drift_vx = -fabsf(player->rescue_drift_vx);
        }
        else
        {
            player->rescue_drift_vx = fabsf(player->rescue_drift_vx);
        }
    }
    if((player->body.y - y) * player->body.vy > 0.0f)
    {
        player->body.vy = -player->body.vy;
        if(player->body.y > y)
        {
            player->rescue_drift_vy = -fabsf(player->rescue_drift_vy);
        }
        else
        {
            player->rescue_drift_vy = fabsf(player->rescue_drift_vy);
        }
    }
    player->body.x = x;
    player->body.y = y;
}

/* 각 버블이 1~1.8초마다 새 방향과 속도를 뽑는다. 뽑은 값은 다음 추첨까지
 * 유지해 매 틱 떨리는 움직임을 피하고, 이전 구간의 초과 시간도 보존한다. */
static void choose_rescue_drift(BBPlayer *player)
{
    float angle = random_from_state(&player->rescue_rng) * 6.28318531f;
    float speed = 1.8f + random_from_state(&player->rescue_rng) * 1.4f;
    /* 원의 한 바퀴는 약 6.283라디안이다. cos/sin으로 선택한 각도를
     * 가로/세로 속도로 나누면 어느 방향으로도 같은 속도로 움직일 수 있다. */
    player->rescue_drift_vx = cosf(angle) * speed;
    player->rescue_drift_vy = sinf(angle) * speed;
    player->rescue_turn_timer += 1.0 + (double)random_from_state(&player->rescue_rng) * 0.8;
}

/* 마지막 목숨의 사망 연출이 끝나면 일반 버블 칸을 쓰지 않고 구조를 시작한다.
 * 여기에서만 10초를 설정하므로 화면이나 라운드가 바뀌어도 연장되지 않는다. */
static void begin_rescue_bubble(BBGame *game, BBPlayer *player)
{
    player->state = BB_PLAYER_BUBBLE;
    player->body.vx = 0.0f;
    player->body.vy = 0.0f;
    player->body.grounded = false;
    player->invulnerable = 0.0f;
    player->attack_timer = 0.0f;
    player->fire_cooldown = 0.0f;
    player->rescue_pop_timer = 0.0f;
    player->rescue_time_left = BB_RESCUE_BUBBLE_SECONDS;
    player->rescue_turn_timer = 0.0;
    random_unit(game);
    player->rescue_rng = game->rng;
    choose_rescue_drift(player);
    clamp_rescue_bubble(game, player);
}

/* 10초 안에 구조되지 못하면 이 라운드에서는 다시 나타나지 않는다.
 * 제한 시간은 이동/접촉 판정보다 먼저 처리해 만료와 접촉이 같은 틱에
 * 겹쳐도 뒤늦게 구조되지 않게 한다. 입력과 마나 갱신도 계속 멈춘다. */
static void update_rescue_bubble(const BBGame *game, BBPlayer *player, int index, float dt)
{
    player->rescue_time_left = fmax(0.0, player->rescue_time_left - (double)dt);
    if(player->rescue_time_left == 0.0)
    {
        player->state = BB_PLAYER_WAIT_ROUND;
        player->body.vx = 0.0f;
        player->body.vy = 0.0f;
        player->body.grounded = false;
        player->rescue_turn_timer = 0.0;
        player->rescue_drift_vx = 0.0f;
        player->rescue_drift_vy = 0.0f;
        return;
    }
    player->rescue_turn_timer -= (double)dt;
    if(player->rescue_turn_timer <= 0.0)
    {
        choose_rescue_drift(player);
    }

    /* 동료의 위치는 읽지 않는다. 무작위 진행 방향에 작은 물결을 더하고
     * 중력/발판 충돌을 생략해 공중 발판에 걸리지 않고 떠다닌다. */
    float age = (float)(BB_RESCUE_BUBBLE_SECONDS - player->rescue_time_left);
    float phase = (float)index * 3.14159265f;
    float vx = player->rescue_drift_vx + sinf(age * 2.0f + phase) * 0.55f;
    float vy = player->rescue_drift_vy + cosf(age * 1.7f + phase) * 0.8f;

    /* 목표 속도로 한 번에 바꾸지 않고 매번 일부만 따라간다. blend는 이번에
     * 따라갈 비율(0~1)이다. expf 식은 갱신 간격이 달라도 같은 시간 동안
     * 비슷하게 가속하도록 만든다. 버블 나이는 라운드가 바뀌어도 이어진다. */
    float blend = 1.0f - expf(-3.0f * dt);
    player->body.vx += (vx - player->body.vx) * blend;
    player->body.vy += (vy - player->body.vy) * blend;
    player->body.x += player->body.vx * dt;
    player->body.y += player->body.vy * dt;
    player->body.grounded = false;
    clamp_rescue_bubble(game, player);
}

/* 고정 크기 enemies 배열의 지정 칸을 등장 대기 상태로 덮어쓴다.
 * 먼저 모두 0으로 지워 이전 적의 속도/타이머가 새 적에게 남지 않게 한다. */
static void spawn_enemy(BBGame *game, int index, BBEnemyType type, float x, float y, float delay)
{
    BBEnemy *enemy = &game->enemies[index];
    memset(enemy, 0, sizeof *enemy);
    enemy->body.x = x;
    enemy->body.y = (float)(game->world_height - BB_MAP_HEIGHT);
    enemy->type = type;
    enemy->state = BB_ENEMY_SPAWNING;
    enemy->active = true;
    enemy->facing = 1;
    if(random_unit(game) < 0.5f)
    {
        enemy->facing = -1;
    }
    enemy->spawn_delay = delay;
    enemy->spawn_y = y;
}

/* 기본 폭에서는 라운드 번호만큼, 넓은 화면에서는 폭 비례로 올림한 수를 쓴다.
 * 최대 256타일/5라운드에서 40마리이며 고정 배열 용량 안에 들어간다. */
static int round_enemy_target(const BBGame *game)
{
    return ((game->level + 1) * game->world_width + BB_MAP_WIDTH - 1) / BB_MAP_WIDTH;
}

/* 각 슬롯은 한 라운드에서 한 번만 쓴다. 잡힌 적이나 처치 완료 슬롯을
 * 재사용하지 않아 폭을 줄였다 다시 늘려도 적이 부활하지 않는다. */
static void add_round_enemies(BBGame *game, bool expanding)
{
    int target = round_enemy_target(game);
    int first = game->round_enemy_quota;
    int base_count = game->level + 1;
    for(int i = first; i < target; ++i)
    {
        /* 원래 등장 위치는 유지하고 추가 적은 그 좌우에 번갈아 배치한다.
         * 기존 적은 resize의 좌표 변환만 받으므로 위치/포획 상태가 유지된다. */
        float offset = ((float)i - (float)(base_count - 1) * 0.5f) * 3.0f;
        if(i >= base_count)
        {
            int extra = i - base_count;
            float distance = ((float)(base_count + 1) * 0.5f + (float)(extra / 2)) * 3.0f;
            offset = distance;
            if(extra % 2 == 0)
            {
                offset = -distance;
            }
        }
        float x = clamp((float)game->world_width * 0.5f + offset, 3.0f, (float)game->world_width - 3.0f);
        float spawn_height = 4.0f;
        if(i % 2 == 0)
        {
            spawn_height = 8.0f;
        }
        float y = (float)(game->world_height - BB_MAP_HEIGHT) + spawn_height;
        BBEnemyType type = (BBEnemyType)((i + game->level) % BB_ENEMY_TYPE_COUNT);
        /* 추가 등장에는 최소 대기 시간을 두고, 큰 화면에서도 마지막 적을
         * 수십 초 기다리지 않도록 서로 다른 위치의 다섯 마리씩 시간을 묶는다. */
        float delay;
        if(expanding)
        {
            delay = 0.45f + (float)((i - first) % 5) * 0.15f;
        }
        else
        {
            delay = (float)(i % 5) * 0.45f;
        }
        spawn_enemy(game, i, type, x, y, delay);
    }
    if(target > game->round_enemy_quota)
    {
        game->round_enemy_quota = target;
    }
}

/* 유효한 라운드 번호(0..4)로 들어가는 내부 함수다. 라운드 전용 시간과
 * 발판 이동을 초기화한 뒤 참가자를 재배치하고 라운드 수만큼 적을 만든다. */
static void enter_level(BBGame *game, int level)
{
    game->level = level;
    game->level_time = 0.0f;
    reset_platforms(game);
    game->state_time = 0.0f;
    game->state = BB_STATE_PLAY;
    game->events.level = true;
    /* 적과 투사체, 보상을 비우고 플레이어를 재배치한다.
     * 플레이어의 목숨, 점수, 에너지와 보상 순서는 이어진다. */
    memset(game->enemies, 0, sizeof game->enemies);
    memset(game->bubbles, 0, sizeof game->bubbles);
    memset(game->boulders, 0, sizeof game->boulders);
    memset(game->pickups, 0, sizeof game->pickups);
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        /* 제한 시간이 끝난 참가자는 다음 라운드 시작 위치에서 한 목숨으로
         * 돌아온다. 아직 구조 가능한 버블과 사망 연출의 남은 시간은 유지한다. */
        if(game->player_count == 2 && game->players[i].active &&
           game->players[i].state == BB_PLAYER_WAIT_ROUND)
            game->players[i].lives = 1;
        if(game->players[i].active && game->players[i].lives > 0)
        {
            place_player(game, &game->players[i], i);
            game->players[i].invulnerable = 3.0f;
        }
    }
    game->round_enemy_quota = 0;
    add_round_enemies(game, false);
}

/* 음식 20종의 등장 순서를 한 묶음으로 만든다. 커서는 다음에 사용할 위치다.
 * 한 묶음을 모두 소비한 뒤에만 다시 섞으므로 묶음 안에서는 중복되지 않는다. */
static void shuffle_pickups(BBGame *game)
{
    for(int i = 0; i < BB_PICKUP_TYPE_COUNT; ++i)
    {
        game->pickup_order[i] = (BBPickupType)i;
    }
    /* 모든 보상 종류가 한 번씩 나오는 순서를 Fisher-Yates 방식으로 섞는다. */
    for(int i = BB_PICKUP_TYPE_COUNT - 1; i > 0; --i)
    {
        int j = (int)(random_unit(game) * (float)(i + 1));
        BBPickupType saved = game->pickup_order[i];
        game->pickup_order[i] = game->pickup_order[j];
        game->pickup_order[j] = saved;
    }
    game->pickup_cursor = 0;
}

/* 처음 사용할 게임 저장 공간을 초기화한다. 전달된 맵은 내부 배열로 복사하여
 * 호출자가 원본 버퍼를 없애도 안전하다. NULL 맵이면 빈 맵에서 시작한다. */
void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed)
{
    if(game == NULL)
    {
        return;
    }
    memset(game, 0, sizeof *game);
    game->world_width = BB_MAP_WIDTH;
    game->world_height = BB_MAP_HEIGHT;
    if(tiles != NULL)
    {
        memcpy(game->maps, tiles, sizeof game->maps);
    }
    game->rng = seed;
    game->player_count = 1;
    game->state = BB_STATE_MENU;
}

/* 축소 전의 실제 지지 발판을 찾고 새 이동 범위로 제한되는 만큼만 보정한다.
 * 몸체마다 가장 넓게 겹친 발판 하나를 따라가므로 다른 발판/공중 객체는 움직이지 않는다.
 * game의 크기는 이미 새 크기이고 발판/몸체 좌표는 아직 이전 좌표다. */
static float platform_resize_shift(const BBGame *game, const BBBody *body, float half_width, float half_height, int dx)
{
    if(!body->grounded || body->vy < 0.0f)
    {
        return 0.0f;
    }
    float feet = body->y + half_height;
    int row = (int)floorf(feet + 0.001f);
    if(fabsf(feet - (float)row) > 0.001f)
    {
        return 0.0f;
    }
    float shift = 0.0f;
    float best_overlap = 0.0001f;
    for(int i = 0; i < game->platform_count; ++i)
    {
        const BBMovingPlatform *platform = &game->platforms[i];
        if(platform->y != row || !bb_game_platform_solid(platform))
        {
            continue;
        }
        float left = (float)platform->x + platform->offset;
        float right = left + (float)platform->width;
        float overlap = fminf(body->x + half_width, right) - fmaxf(body->x - half_width, left);
        if(overlap > best_overlap)
        {
            float minimum, maximum;
            platform_bounds(game, platform->x + dx, platform->width, &minimum, &maximum);
            best_overlap = overlap;
            shift = clamp(platform->offset, minimum, maximum) - platform->offset;
        }
    }
    return shift;
}

/* 화면 크기 변경은 속도를 다시 계산하는 물리 틱이 아니다. 중심 좌표만
 * 새 원점으로 옮기고, 폭이 줄었을 때 새 외벽에 갇히지 않게 안쪽으로 넣는다. */
static void resize_body(BBBody *body, float half_width, int dx, int dy, float platform_shift,
                        int width, bool narrower, bool shorter)
{
    body->x += (float)dx + platform_shift;
    body->y += (float)dy;
    if(narrower)
    {
        body->x = clamp(body->x, 2.0f + half_width, (float)width - 2.0f - half_width);
    }
    /* 사라진 상단 확장 공간의 물체는 바로 위 진입 위치에 둔다. 수십 타일
     * 밖에서 오랫동안 떨어지거나 메뉴 복귀가 필요해지는 상황을 막는다. */
    if(shorter && body->y < -1.0f)
    {
        body->y = -1.0f;
        body->grounded = false;
    }
}

/* 게임을 재시작하지 않고 월드 크기를 바꾼다. 발판과 물체를 함께 옮겨
 * 탑승/진행 시간을 유지하고 플레이어의 가로 속도와 추가 적 수를 새 폭에 맞춘다. */
void bb_game_resize(BBGame *game, int width, int height)
{
    if(game == NULL)
    {
        return;
    }
    if(width < BB_MAP_WIDTH)
    {
        width = BB_MAP_WIDTH;
    }
    if(width > BB_WORLD_MAX_WIDTH)
    {
        width = BB_WORLD_MAX_WIDTH;
    }
    if(height < BB_MAP_HEIGHT)
    {
        height = BB_MAP_HEIGHT;
    }
    if(height > BB_WORLD_MAX_HEIGHT)
    {
        height = BB_WORLD_MAX_HEIGHT;
    }
    if(width == game->world_width && height == game->world_height)
    {
        return;
    }
    int dx = (width - BB_MAP_WIDTH) / 2 - (game->world_width - BB_MAP_WIDTH) / 2;
    int dy = height - game->world_height;
    float player_speed_ratio = (float)width / (float)game->world_width;
    bool narrower = width < game->world_width;
    bool wider = width > game->world_width;
    bool shorter = height < game->world_height;
    game->world_width = width;
    game->world_height = height;
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        BBPlayer *player = &game->players[i];
        float translated_y = player->body.y + (float)dy;
        float half_width = 0.9f;
        if(player->state == BB_PLAYER_BUBBLE)
        {
            half_width = BB_RESCUE_BUBBLE_RADIUS;
        }
        float shift = 0.0f;
        if(narrower && player->active && player->state != BB_PLAYER_BUBBLE)
        {
            shift = platform_resize_shift(game, &player->body, 0.9f, 0.975f, dx);
        }
        resize_body(&player->body, half_width, dx, dy, shift, width, narrower, shorter);
        if(player->state == BB_PLAYER_BUBBLE)
        {
            clamp_rescue_bubble(game, player);
        }
        /* 공중 관성은 입력이 없어도 유지되므로 현재 vx도 폭 변화율로 한 번만
         * 보정한다. 부활 버블은 별도의 떠다니는 속도를 그대로 유지한다. */
        if(player->state != BB_PLAYER_BUBBLE)
        {
            player->body.vx *= player_speed_ratio;
        }
        player->jump_start_y += (float)dy;
        if(player->body.y != translated_y)
        {
            player->jump_start_y += player->body.y - translated_y;
        }
    }
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
    {
        BBEnemy *enemy = &game->enemies[i];
        float half_width = 0.9f;
        if(enemy->state == BB_ENEMY_DEAD)
        {
            half_width = 0.95f;
        }
        float shift = 0.0f;
        if(narrower && enemy->active && enemy->state != BB_ENEMY_CAPTURED &&
           enemy->state != BB_ENEMY_SPAWNING)
        {
            shift = platform_resize_shift(game, &enemy->body, half_width, 0.95f, dx);
        }
        resize_body(&enemy->body, half_width, dx, dy, shift, width, narrower, shorter);
        enemy->spawn_y += (float)dy;
    }
    for(int i = 0; i < BB_MAX_BUBBLES; ++i)
    {
        resize_body(&game->bubbles[i].body, 0.75f, dx, dy, 0.0f, width, narrower, shorter);
    }
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
    {
        BBBoulder *boulder = &game->boulders[i];
        float shift = 0.0f;
        if(narrower && boulder->active)
        {
            shift = platform_resize_shift(game, &boulder->body, 0.75f, 0.75f, dx);
        }
        resize_body(&boulder->body, 0.75f, dx, dy, shift, width, narrower, shorter);
    }
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
    {
        BBPickup *pickup = &game->pickups[i];
        float shift = 0.0f;
        if(narrower && pickup->active)
        {
            shift = platform_resize_shift(game, &pickup->body, 0.95f, 0.95f, dx);
        }
        resize_body(&pickup->body, 0.95f, dx, dy, shift, width, narrower, shorter);
    }
    for(int i = 0; i < game->platform_count; ++i)
    {
        game->platforms[i].x += dx;
        game->platforms[i].y += dy;
        if(narrower)
        {
            BBMovingPlatform *platform = &game->platforms[i];
            float minimum, maximum;
            platform_bounds(game, platform->x, platform->width, &minimum, &maximum);
            float offset = clamp(platform->offset, minimum, maximum);
            if(offset != platform->offset)
            {
                platform->offset = offset;
                platform->step = 0.0f;
                platform->direction = -1;
                if(offset <= minimum)
                {
                    platform->direction = 1;
                }
            }
        }
    }
    if(wider && game->state == BB_STATE_PLAY)
    {
        add_round_enemies(game, true);
    }
}

/* 메뉴에서 새 게임을 선택했을 때 호출한다. 맵/난수 상태를 보존하면서 이전
 * 플레이의 객체/기록/마나를 초기화한다. 인원은 배열 용량인 1..2명으로 제한한다. */
void bb_game_start(BBGame *game, int player_count)
{
    if(game == NULL)
    {
        return;
    }
    if(player_count < 1)
    {
        player_count = 1;
    }
    if(player_count > BB_MAX_PLAYERS)
    {
        player_count = BB_MAX_PLAYERS;
    }
    game->player_count = player_count;
    memset(game->players, 0, sizeof game->players);
    memset(game->enemies, 0, sizeof game->enemies);
    memset(game->bubbles, 0, sizeof game->bubbles);
    memset(game->boulders, 0, sizeof game->boulders);
    memset(game->pickups, 0, sizeof game->pickups);
    game->state = BB_STATE_INTRO;
    game->state_time = 0.0f;
    game->level_time = 0.0f;
    memset(game->platforms, 0, sizeof game->platforms);
    game->platform_count = 0;
    game->ticks = 0;
    game->platform_warning_cooldown = 0.0;
    game->platform_last_choice = 0;
    memset(&game->events, 0, sizeof game->events);
    game->level = 0;
    game->round_enemy_quota = 0;
    game->won = false;
    shuffle_pickups(game);
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        BBPlayer *player = &game->players[i];
        /* 두 칸 모두 위치는 준비하지만, 참가 인원만 활성화하고 생명을 준다. */
        place_player(game, player, i);
        player->active = i < player_count;
        if(player->active)
        {
            player->lives = BB_STARTING_LIVES;
        }
    }
}

/* 플레이를 멈추고 메뉴 상태로 바꾼다. 기존 객체/맵을 지울 필요는 없으며,
 * 다시 시작할 때 bb_game_start가 정리한다. 지난 효과음 이벤트는 즉시 비운다. */
void bb_game_menu(BBGame *game)
{
    if(game == NULL)
    {
        return;
    }
    game->state = BB_STATE_MENU;
    game->state_time = 0.0f;
    memset(&game->events, 0, sizeof game->events);
}

/* Enter 또는 스모크 테스트에서 인트로만 건너뛴다.
 * 이미 플레이 중일 때 호출해도 라운드가 초기화되지 않도록 상태를 제한한다. */
void bb_game_skip_intro(BBGame *game)
{
    if(game != NULL && game->state == BB_STATE_INTRO)
    {
        enter_level(game, 0);
    }
}

/* 아직 처치하지 않은 적 수를 센다. 등장 대기와 포획 중인 적도 포함하며,
 * 죽어서 보상으로 변하는 연출 중인 적은 제외해 클리어 대기를 시작할 수 있다. */
int bb_game_enemies_left(const BBGame *game)
{
    int count = 0;
    if(game == NULL)
    {
        return 0;
    }
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
        if(game->enemies[i].active && game->enemies[i].state != BB_ENEMY_DEAD)
        {
            ++count;
        }
    return count;
}

/* 음식 열거형의 순서와 같은 표에서 점수를 찾는다.
 * 잘못된 종류가 들어오면 배열 밖을 읽는 대신 점수 0을 반환한다. */
int bb_game_pickup_score(BBPickupType type)
{
    static const int scores[BB_PICKUP_TYPE_COUNT] = {
        100, 200, 100, 150, 200, 150, 250, 200, 300, 150,
        100, 200, 150, 200, 100, 150, 300, 500, 300, 400
    };
    if(type < 0 || type >= BB_PICKUP_TYPE_COUNT)
    {
        return 0;
    }
    return scores[type];
}

/* 실제 피격이 가능한 참가자만 생명을 한 번 차감한다. 즉시 DEAD가 되므로
 * 같은 틱에 적/바위가 여러 번 겹쳐도 추가 차감하지 않는다. 부활은 update_player가 맡는다. */
static void damage_player(BBGame *game, BBPlayer *player)
{
    if(!player->active || player->state != BB_PLAYER_NORMAL || player->invulnerable > 0.0f)
    {
        return;
    }
    --player->lives;
    player->state = BB_PLAYER_DEAD;
    player->death_timer = 1.5f;
    player->body.vx = 0.0f;
    player->body.vy = 0.0f;
    player->rescue_pop_timer = 0.0f;
    game->events.death = true;
}

/* 발사 대기시간이 끝났으면 공유 버블 배열의 첫 빈 칸을 사용한다.
 * 슬롯이 꽉 찼을 때는 대기시간/이벤트를 소비하지 않아 빈 칸이 생기면 다시 쏠 수 있다. */
static void fire_bubble(BBGame *game, BBPlayer *player, int owner)
{
    if(player->fire_cooldown > 0.0f)
    {
        return;
    }
    for(int i = 0; i < BB_MAX_BUBBLES; ++i)
    {
        BBBubble *bubble = &game->bubbles[i];
        if(bubble->active)
        {
            continue;
        }
        float x = player->body.x + (float)player->facing * 2.4f;
        float y = player->body.y;
        float speed = 20.0f * (float)player->facing;
        /* 원본의 짧은 벽 레이캐스트를 따른다. 유한한 크기의 버블이
         * 벽 타일 내부에서 생성되지 않도록 벽 바깥에 배치한다. */
        for(float distance = 0.1f; distance <= 3.15f; distance += 0.1f)
        {
            float cast_x = player->body.x + (float)player->facing * distance;
            int row = (int)floorf(y);
            float tile_left = 0.0f;
            int tile = solid_tile_at(game, cast_x, row, &tile_left);
            if(tile == 2 || tile == 3)
            {
                float side = 1.0f;
                if(player->facing > 0)
                {
                    side = 0.0f;
                }
                float boundary = tile_left + side;
                x = boundary - (float)player->facing * 0.751f;
                speed = 0.0f;
                break;
            }
        }
        memset(bubble, 0, sizeof *bubble);
        bubble->body.x = clamp(x, 0.75f, (float)game->world_width - 0.75f);
        bubble->body.y = y;
        bubble->body.vx = speed;
        bubble->active = true;
        bubble->owner = owner;
        bubble->captured_enemy = -1;
        /* 거품은 플레이어의 앞쪽에서 시작한다. 포획 번호 -1은 빈 거품이며,
         * 생성 성공 후에만 발사 간격(0.4초)과 공격 그림(0.2초)을 설정한다. */
        player->fire_cooldown = 0.4f;
        player->attack_timer = 0.2f;
        game->events.fire = true;
        break;
    }
}

/* 한 플레이어의 마나만 진행한다. 충전 15초 -> 가속 5초를 반복하며,
 * 경계에서 수평 관성도 배율에 맞춰 바꾼다. 누적 시간은 double로 보관한다. */
static void update_mana(BBPlayer *player, float dt)
{
    float duration = BB_ENERGY_CHARGE_SECONDS;
    if(player->boosting)
    {
        duration = BB_ENERGY_BOOST_SECONDS;
    }
    player->energy_elapsed += (double)dt;
    if(player->energy_elapsed >= duration) {
        player->energy_elapsed -= duration;
        player->boosting = !player->boosting;
        if(player->boosting)
        {
            player->body.vx *= 2.0f;
        }
        else
        {
            player->body.vx *= 0.5f;
        }
    }
    /* 경계를 넘은 남은 시간도 보존한다. 점프의 수직 속도는 바꾸지 않는다. */
    if(player->boosting) {
        player->energy = 1.0f - (float)(player->energy_elapsed / BB_ENERGY_BOOST_SECONDS);
    } else {
        player->energy = (float)(player->energy_elapsed / BB_ENERGY_CHARGE_SECONDS);
    }
    player->energy = clamp(player->energy, 0.0f, 1.0f);
}

/* 참가 여부/사망 -> 타이머 -> 마나 -> 입력/점프 -> 이동 충돌 -> 발사 순이다.
 * 입력은 이 함수에서 해당 플레이어에게만 적용하며, 다른 플레이어를 밀지 않는다. */
static void update_player(BBGame *game, BBPlayer *player, int index, BBInput input, float dt)
{
    if(!player->active || player->state == BB_PLAYER_OUT || player->state == BB_PLAYER_WAIT_ROUND)
    {
        return;
    }
    if(player->state == BB_PLAYER_BUBBLE)
    {
        /* 구조 제한 시간과 이동만 갱신한다. */
        update_rescue_bubble(game, player, index, dt);
        return;
    }
    player->rescue_pop_timer = decrease(player->rescue_pop_timer, dt);
    player->invulnerable = decrease(player->invulnerable, dt);
    player->attack_timer = decrease(player->attack_timer, dt);
    player->fire_cooldown = decrease(player->fire_cooldown, dt);
    if(player->state == BB_PLAYER_DEAD)
    {
        /* 사망 대기 중에는 에너지 구간을 진행하지 않고, 부활 시에도 보존한다. */
        player->death_timer = decrease(player->death_timer, dt);
        if(player->death_timer <= 0.0f)
        {
            if(player->lives > 0)
            {
                place_player(game, player, index);
                player->invulnerable = 3.0f;
            }
            else if(game->player_count == 2)
            {
                begin_rescue_bubble(game, player);
            }
            else
                player->state = BB_PLAYER_OUT;
        }
        return;
    }
    update_mana(player, dt);
    float boost_multiplier = 1.0f;
    if(player->boosting)
    {
        boost_multiplier = 2.0f;
    }
    float speed_multiplier = boost_multiplier * (float)game->world_width / (float)BB_MAP_WIDTH;
    /* NaN/무한대 입력이 좌표까지 오염시키지 않게 중립으로 바꾸고,
     * 게임패드 아날로그 입력도 -1..1 범위 안에서만 속도로 변환한다. */
    float move = 0.0f;
    if(isfinite(input.move))
    {
        move = clamp(input.move, -1.0f, 1.0f);
    }
    if(move < 0.0f)
    {
        player->facing = -1;
    }
    if(move > 0.0f)
    {
        player->facing = 1;
    }
    if(player->body.grounded)
    {
        /* 서 있을 때도 작은 하강 속도를 주어 이번 틱의 발판 접촉을 재확인한다. */
        player->body.vx = move * BB_PLAYER_GROUND_SPEED * speed_multiplier;
        player->body.vy = 30.0f * dt;
        player->falling = true;
        if(input.jump)
        {
            /* y가 위쪽으로 갈수록 작으므로 점프 속도는 음수다.
             * 출발 높이를 기억해 원래 높이 아래로 내려갈 때 느린 낙하로 바꾼다. */
            player->body.vy = -18.0f;
            player->body.grounded = false;
            player->jump_start_y = player->body.y;
            player->falling = false;
            player->air_control = false;
            game->events.jump = true;
        }
    }
    if(!player->body.grounded)
    {
        if(player->body.y > player->jump_start_y && player->body.vy > 0.0f)
        {
            player->falling = true;
        }
        if(player->falling)
        {
            /* 떨어지는 동안은 느린 일정 속도로 낙하하고 좌우 제어를 허용한다. */
            player->body.vx = move * BB_PLAYER_AIR_SPEED * speed_multiplier;
            player->body.vy = 6.0f;
        }
        else
        {
            /* 플레이어가 반대 방향으로 움직일 때까지 관성을 유지한다. */
            if(player->body.vx == 0.0f || player->body.vx * move < 0.0f)
            {
                player->air_control = true;
            }
            if(player->air_control)
            {
                player->body.vx = move * BB_PLAYER_AIR_SPEED * speed_multiplier;
            }
            player->body.vy += 30.0f * dt;
        }
    }
    move_body(game, &player->body, 0.9f, 0.975f, dt, true, true);
    if(input.fire)
    {
        fire_bubble(game, player, index);
    }
}

/* 살아 있는 참가자 중 가장 가까운 사람을 AI 목표로 고른다.
 * 거리의 제곱으로 비교하고, 목표가 없으면 NULL을 반환해 추적을 생략한다. */
static const BBPlayer *closest_player(const BBGame *game, const BBBody *body)
{
    const BBPlayer *target = NULL;
    float nearest = 1.0e9f;
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        const BBPlayer *player = &game->players[i];
        if(!player->active || player->state != BB_PLAYER_NORMAL)
        {
            continue;
        }
        float dx = player->body.x - body->x;
        float dy = player->body.y - body->y;
        float distance = dx * dx + dy * dy;
        if(distance < nearest)
        {
            target = player;
            nearest = distance;
        }
    }
    return target;
}

/* 적의 위쪽 다섯 줄을 검사해 점프로 올라갈 발판/벽이 있는지 확인한다.
 * 공중 발판마다 현재 이동량을 적용하므로 빈 과거 위치로 뛰지 않는다. */
static bool platform_above(const BBGame *game, const BBBody *body)
{
    int start = (int)floorf(body->y - 1.0f);
    for(int y = start; y > start - 5; --y)
    {
        float tile_left = 0.0f;
        if(solid_tile_at(game, body->x, y, &tile_left) != 0)
        {
            return true;
        }
    }
    return false;
}

/* 마이타의 바위를 첫 빈 슬롯에 생성한다. 정면을 중심으로 각도를 조금
 * 흩뜨리며, 배열이 가득 차 있으면 발사 타이머를 유지해 다음 틱에 재시도한다. */
static void throw_boulder(BBGame *game, BBEnemy *enemy)
{
    if(enemy->throw_timer < 4.0f)
    {
        return;
    }
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
    {
        BBBoulder *boulder = &game->boulders[i];
        if(boulder->active)
        {
            continue;
        }
        float angle = random_unit(game) - 0.5f;
        memset(boulder, 0, sizeof *boulder);
        boulder->body.x = enemy->body.x;
        boulder->body.y = enemy->body.y;
        boulder->body.vx = cosf(angle) * 15.0f * (float)enemy->facing;
        boulder->body.vy = sinf(angle) * 15.0f;
        boulder->active = true;
        enemy->throw_timer = 0.0f;
        return;
    }
}

/* 죽은 적의 마지막 위치에 음식 한 개를 만든다. 타입은 미리 섞은 순서에서
 * 하나 소비하고, 적의 튕김 속도는 지워 음식이 곧바로 아래로 떨어지게 한다. */
static void spawn_pickup(BBGame *game, const BBEnemy *enemy)
{
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
    {
        BBPickup *pickup = &game->pickups[i];
        if(pickup->active)
        {
            continue;
        }
        if(game->pickup_cursor >= BB_PICKUP_TYPE_COUNT)
        {
            shuffle_pickups(game);
        }
        memset(pickup, 0, sizeof *pickup);
        pickup->body = enemy->body;
        pickup->active = true;
        pickup->type = game->pickup_order[game->pickup_cursor];
        game->pickup_cursor++;
        pickup->body.vx = 0.0f;
        pickup->body.vy = 9.0f;
        return;
    }
}

/* 적의 상태별 움직임을 처리한다. 포획된 적은 버블이 대신 이동/탈출/처치를
 * 담당하므로 건너뛴다. 등장/사망 연출은 일반 AI보다 먼저 처리하고 반환한다. */
static void update_enemy(BBGame *game, BBEnemy *enemy, int index, float dt)
{
    if(!enemy->active || enemy->state == BB_ENEMY_CAPTURED)
    {
        return;
    }
    enemy->age += dt;
    if(enemy->state == BB_ENEMY_SPAWNING)
    {
        if(enemy->age <= enemy->spawn_delay)
        {
            return;
        }
        float progress = clamp((enemy->age - enemy->spawn_delay) / 2.0f, 0.0f, 1.0f);
        /* 원본 맵의 위쪽에서 2초 동안 지정 높이까지 내려온다. 시작점과
         * 목표점에 같은 오프셋을 써서 크기 변경 중에도 등장 위치가 이어진다. */
        float origin_y = (float)(game->world_height - BB_MAP_HEIGHT);
        enemy->body.y = origin_y + (enemy->spawn_y - origin_y) * progress;
        if(progress >= 1.0f)
        {
            enemy->state = BB_ENEMY_FALLING;
        }
        return;
    }
    if(enemy->state == BB_ENEMY_DEAD)
    {
        enemy->body.vy += 9.81f * dt;
        float vx = enemy->body.vx;
        float vy = enemy->body.vy;
        /* move_body가 충돌 축의 속도를 0으로 만들기 전에 저장해야 반사할 수 있다. */
        enemy->bounce_timer += dt;
        BBHits hit = move_body(game, &enemy->body, 0.95f, 0.95f, dt, true, true);
        if(hit.x)
        {
            enemy->body.vx = -vx;
        }
        if(hit.y)
        {
            enemy->body.vy = -vy;
        }
        if((hit.x || hit.y) && enemy->bounce_timer >= 0.5f)
        {
            /* 모서리에 연속 접촉한 여러 틱을 별개의 튕김으로 세지 않는다. */
            ++enemy->bounces;
            enemy->bounce_timer = 0.0f;
        }
        /* 일반적인 네 번 튀김은 유지하면서, 순환 통로의 시체가 영원히
         * 아이템으로 바뀌지 않는 경우를 막기 위한 유한 시간 보정이다. */
        if(enemy->bounces > 3 || enemy->age > 4.0f)
        {
            spawn_pickup(game, enemy);
            enemy->active = false;
        }
        return;
    }
    /* 종류별 차이를 한 곳에서 읽을 수 있도록 기본값과 switch로 정한다. */
    float speed = 4.5f;
    float jump_speed = -12.0f;
    float fall_speed = 8.0f;
    float jump_horizontal = 0.5f;
    float turn_delay = 2.0f + (float)(index % 4) * 0.5f;
    switch(enemy->type) {
        case BB_ENEMY_ZENCHAN:
            speed = 6.0f;
            jump_speed = -10.0f;
            fall_speed = 5.0f;
            jump_horizontal = 0.2f;
            break;
        case BB_ENEMY_MONSTA:
            jump_speed = -9.0f;
            fall_speed = 5.5f;
            jump_horizontal = 1.0f;
            turn_delay = 1.0f;
            break;
        case BB_ENEMY_MAITA:
        default:
            break;
    }
    enemy->jump_timer += dt;
    enemy->turn_timer += dt;
    enemy->throw_timer += dt;
    bool jump = false;
    const BBPlayer *target = closest_player(game, &enemy->body);
    if(target != NULL)
    {
        float dx = target->body.x - enemy->body.x;
        /* 매 틱 뒤집지 않고 종류별 대기시간과 수평거리 조건을 만족할 때 추적한다. */
        if(enemy->turn_timer >= turn_delay && fabsf(dx) > 5.0f)
        {
            enemy->facing = 1;
            if(dx < 0.0f)
            {
                enemy->facing = -1;
            }
            enemy->turn_timer = 0.0f;
        }
        jump = target->body.y < enemy->body.y - 1.0f && dx * (float)enemy->facing >= 0.0f &&
               enemy->jump_timer > 1.5f && platform_above(game, &enemy->body);
    }
    /* 몬스타는 발판 유무와 무관하게 짧게 도약하며 공중에서도 전진한다. */
    if(enemy->type == BB_ENEMY_MONSTA && enemy->jump_timer >= 1.1f)
    {
        jump = true;
    }
    float move = (float)enemy->facing;
    if(enemy->type == BB_ENEMY_MAITA && enemy->throw_timer >= 5.0f)
    {
        throw_boulder(game, enemy);
    }
    float multiplier = 1.0f;
    if(enemy->angry && enemy->type == BB_ENEMY_ZENCHAN)
    {
        multiplier = 1.8f;
    }
    /* 지상에서는 걷고, 점프 중에는 중력으로 포물선을 만든다.
     * 발판을 벗어나 떨어질 때에는 종류별 고정 낙하 속도를 적용한다. */
    if(enemy->body.grounded)
    {
        enemy->state = BB_ENEMY_WALKING;
        enemy->body.vx = move * speed * multiplier;
        enemy->body.vy = 9.81f * dt;
        if(jump)
        {
            enemy->body.vx *= jump_horizontal;
            enemy->body.vy = jump_speed;
            enemy->state = BB_ENEMY_JUMPING;
            enemy->body.grounded = false;
            enemy->jump_timer = 0.0f;
        }
    }
    else if(enemy->state == BB_ENEMY_JUMPING)
    {
        enemy->body.vy += 9.81f * dt;
    }
    else
    {
        enemy->state = BB_ENEMY_FALLING;
        enemy->body.vx = 0.0f;
        if(enemy->type == BB_ENEMY_MONSTA)
        {
            enemy->body.vx = move * speed;
        }
        enemy->body.vy = fall_speed * multiplier;
    }
    BBHits hit = move_body(game, &enemy->body, 0.9f, 0.95f, dt, true, true);
    if(hit.x)
    {
        /* 벽을 만나면 즉시 돌아선다. 추적 타이머도 비워 곧바로 벽 쪽을 보지 않게 한다. */
        enemy->facing = -enemy->facing;
        enemy->turn_timer = 0.0f;
    }
}

/* 파열을 시작하되 0.25초 연출이 끝날 때까지 슬롯/포획 관계를 유지한다.
 * release=true는 시간 만료에 따른 탈출, false는 플레이어 공격에 따른 처치다. */
static void pop_bubble(BBGame *game, BBBubble *bubble, bool release)
{
    if(bubble->popping)
    {
        return;
    }
    bubble->popping = true;
    bubble->releasing = release;
    bubble->pop_timer = 0.25f;
    bubble->body.vx = 0.0f;
    bubble->body.vy = 0.0f;
    game->events.pop = true;
}

/* 파열 연출 종료 시 포획된 적을 버블의 마지막 위치에 돌려놓는다.
 * 자연 소멸/공격에 맞는 다음 상태를 설정한 뒤 버블 슬롯을 반환한다. */
static void finish_pop(BBGame *game, BBBubble *bubble)
{
    if(bubble->captured_enemy >= 0 && bubble->captured_enemy < BB_MAX_ENEMIES)
    {
        BBEnemy *enemy = &game->enemies[bubble->captured_enemy];
        enemy->body = bubble->body;
        enemy->body.grounded = false;
        enemy->age = 0.0f;
        /* 자연 소멸은 적을 놓아주고, 플레이어가 터뜨린 경우에는 보상 낙하로 이어진다. */
        if(bubble->releasing)
        {
            enemy->state = BB_ENEMY_FALLING;
            if(enemy->type == BB_ENEMY_ZENCHAN)
            {
                enemy->angry = true;
            }
        }
        else
        {
            float angle = (50.0f + 20.0f * random_unit(game)) * 0.01745329252f;
            /* 50..70도를 라디안으로 바꾸고 좌우 방향을 추첨해 위로 날려 보낸다. */
            /* Julgen의 밀도 0 동적 픽스처는 Box2D에서 질량 1로 동작한다. */
            float speed = 15.0f + 5.0f * random_unit(game);
            enemy->state = BB_ENEMY_DEAD;
            float direction = 1.0f;
            if(random_unit(game) < 0.5f)
            {
                direction = -1.0f;
            }
            enemy->body.vx = cosf(angle) * speed * direction;
            enemy->body.vy = -sinf(angle) * speed;
            enemy->bounce_timer = 0.0f;
            enemy->bounces = 0;
        }
    }
    bubble->active = false;
}

/* 거품 한 개의 수명/파열/부유를 갱신한다. center_x/y는 이번 틱 시작 시
 * 살아 있는 거품들의 평균 위치이며, 조금씩 모여드는 힘의 기준점이다. */
static void update_bubble(BBGame *game, BBBubble *bubble, float dt, float center_x, float center_y)
{
    if(!bubble->active)
    {
        return;
    }
    if(bubble->popping)
    {
        bubble->pop_timer -= dt;
        if(bubble->pop_timer <= 0.0f)
        {
            finish_pop(game, bubble);
        }
        return;
    }
    bubble->age += dt;
    /* age는 발사부터의 시간이다. 포획 시 age를 되돌리지 않고 수명만
     * 8초에서 13초로 늘리면 늦게 포획해도 기존 남은 시간에 정확히 5초가
     * 더해진다. 빈 거품의 수명, 부딪쳐 터뜨리기, 파열 연출은 그대로다. */
    float lifetime = 8.0f;
    if(bubble->captured_enemy >= 0)
    {
        lifetime = 13.0f;
    }
    if(bubble->age > lifetime)
    {
        pop_bubble(game, bubble, true);
        return;
    }
    /* 고정 시간 간격에서 안정적으로 항력, 난수 힘, 상승 힘, 무리 중심을 적용한다. */
    bubble->body.vx += ((random_unit(game) * 2.0f - 1.0f) * 20.0f - bubble->body.vx) * dt;
    bubble->body.vy += ((random_unit(game) * 2.0f - 1.0f) * 20.0f - bubble->body.vy) * dt;
    if(bubble->age > 1.0f)
    {
        /* 첫 1초는 발사 관성을 살리고, 이후에는 무리 쪽 힘과 위쪽 힘을 더한다. */
        bubble->body.vx += (center_x - bubble->body.x) * 0.3f * dt;
        bubble->body.vy += (center_y - bubble->body.y) * 0.3f * dt;
        if(bubble->body.y > (float)(game->world_height - BB_MAP_HEIGHT) + 11.0f)
        {
            bubble->body.vy -= 3.0f * dt;
        }
    }
    float vx = bubble->body.vx;
    float vy = bubble->body.vy;
    /* 거품은 일방 통과 발판을 양쪽 방향으로 지나며 단단한 벽에만 반사된다. */
    BBHits hit = move_body(game, &bubble->body, 0.75f, 0.75f, dt, false, false);
    if(hit.x)
    {
        bubble->body.vx = -vx * 0.8f;
    }
    if(hit.y)
    {
        bubble->body.vy = -vy * 0.8f;
    }
    if(bubble->body.y < 0.75f)
    {
        /* 맵 위에는 타일 벽이 없으므로 반지름을 기준으로 별도 상단 경계를 둔다. */
        bubble->body.y = 0.75f;
        bubble->body.vy = fabsf(bubble->body.vy) * 0.8f;
    }
    if(bubble->body.y > (float)game->world_height + 1.0f)
    {
        bubble->body.y -= (float)game->world_height + 2.0f;
    }
}

/* 바위는 3초 동안 중력으로 움직이고 벽/발판에서 속도의 90%로 튕긴다.
 * 플레이어 피해는 모든 이동이 끝난 뒤 접촉 검사에서 따로 처리한다. */
static void update_boulder(BBGame *game, BBBoulder *boulder, float dt)
{
    if(!boulder->active)
    {
        return;
    }
    boulder->age += dt;
    if(boulder->age >= 3.0f)
    {
        boulder->active = false;
        return;
    }
    boulder->body.vy += 9.81f * dt;
    float vx = boulder->body.vx;
    float vy = boulder->body.vy;
    BBHits hit = move_body(game, &boulder->body, 0.75f, 0.75f, dt, true, true);
    if(hit.x)
    {
        boulder->body.vx = -vx * 0.9f;
    }
    if(hit.y)
    {
        boulder->body.vy = -vy * 0.9f;
    }
}

/* 음식은 10초 후 사라진다. 자체 수평 속도는 0이지만 발판 위에 착지했다면
 * move_body가 발판 이동량을 더해 함께 이동시킨다. 바닥 통로로도 순환한다. */
static void update_pickup(BBGame *game, BBPickup *pickup, float dt)
{
    if(!pickup->active)
    {
        return;
    }
    pickup->age += dt;
    if(pickup->age >= 10.0f)
    {
        pickup->active = false;
        return;
    }
    pickup->body.vx = 0.0f;
    pickup->body.vy = 9.0f;
    move_body(game, &pickup->body, 0.95f, 0.95f, dt, true, true);
}

/* 구조된 위치에서 목숨 하나로 돌아온다. 점수와 마나는 보존하며, 같은 틱에
 * 적이나 바위가 겹쳐 있어도 바로 다시 죽지 않도록 부활 무적을 준다. */
static void revive_player(BBGame *game, int index)
{
    BBPlayer *player = &game->players[index];
    BBBody position = player->body;
    place_player(game, player, index);
    player->body.x = position.x;
    player->body.y = position.y;
    player->jump_start_y = position.y;
    player->lives = 1;
    player->invulnerable = 3.0f;
    player->rescue_pop_timer = 0.25f;
    game->events.pop = true;
}

/* 구조 버블은 살아 있는 동료의 접촉 또는 그 동료가 쏜 빈 거품으로 터뜨린다.
 * 자기 거품/포획 거품/파열 중인 거품은 제외한다. 발사자가 이미 죽었다면
 * 남은 발사체로 구조하지 않으므로 전원 탈락 시 게임 오버가 지연되지 않는다. */
static void check_rescue_bump(BBGame *game)
{
    if(game->player_count != 2)
    {
        return;
    }
    for(int target = 0; target < game->player_count; ++target)
    {
        BBPlayer *waiting = &game->players[target];
        if(!waiting->active || waiting->state != BB_PLAYER_BUBBLE)
        {
            continue;
        }
        for(int p = 0; p < game->player_count; ++p)
        {
            const BBPlayer *rescuer = &game->players[p];
            if(p == target || !rescuer->active || rescuer->state != BB_PLAYER_NORMAL || rescuer->lives <= 0)
            {
                continue;
            }
            bool rescued = circles_overlap(&waiting->body, BB_RESCUE_BUBBLE_RADIUS, &rescuer->body, 0.95f);
            if(!rescued)
            {
                for(int b = 0; b < BB_MAX_BUBBLES; ++b)
                {
                    BBBubble *bubble = &game->bubbles[b];
                    if(!bubble->active || bubble->popping || bubble->captured_enemy >= 0 || bubble->owner != p)
                    {
                        continue;
                    }
                    if(circles_overlap(&waiting->body, BB_RESCUE_BUBBLE_RADIUS, &bubble->body, 0.75f))
                    {
                        pop_bubble(game, bubble, false);
                        rescued = true;
                        break;
                    }
                }
            }
            if(rescued)
            {
                revive_player(game, target);
                break;
            }
        }
    }
}

/* 거품별로 적 포획을 먼저 검사하고, 이어 플레이어의 터뜨리기/올라타기를 검사한다.
 * 파열 중인 거품은 제외하므로 한 번 시작한 파열의 이유나 적이 바뀌지 않는다. */
static void check_bubble_bump(BBGame *game)
{
    for(int b = 0; b < BB_MAX_BUBBLES; ++b)
    {
        BBBubble *bubble = &game->bubbles[b];
        if(!bubble->active || bubble->popping)
        {
            continue;
        }
        if(bubble->captured_enemy < 0)
        {
            /* 비어 있는 거품 하나는 적 하나만 포획한다. CAPTURED로 즉시 바꾸면
             * 뒤에서 검사하는 다른 거품이 같은 적을 중복 포획할 수 없다. */
            for(int e = 0; e < BB_MAX_ENEMIES; ++e)
            {
                BBEnemy *enemy = &game->enemies[e];
                if(!enemy->active || enemy->state == BB_ENEMY_SPAWNING ||
                   enemy->state == BB_ENEMY_CAPTURED || enemy->state == BB_ENEMY_DEAD)
                    continue;
                if(circles_overlap(&bubble->body, 0.75f, &enemy->body, 0.95f))
                {
                    /* 적은 버블이 끝날 때까지 활성 상태로 남지만 다른 충돌은 건너뛴다. */
                    bubble->captured_enemy = e;
                    enemy->state = BB_ENEMY_CAPTURED;
                    break;
                }
            }
        }
        /* 발사 직후에는 어느 플레이어도 버블을 곧바로 터뜨리지 못하게 한다. */
        if(bubble->age < 1.0f)
        {
            continue;
        }
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            BBPlayer *player = &game->players[p];
            if(!player->active || player->state != BB_PLAYER_NORMAL ||
               !circles_overlap(&bubble->body, 0.75f, &player->body, 0.95f))
                continue;
            float dx = player->body.vx - bubble->body.vx;
            float dy = player->body.vy - bubble->body.vy;
            /* 절대 속도 대신 상대 속도를 사용한다. 오래된 거품일수록 작은
             * 충격으로 터지며, 충분한 충격이 아니면 위쪽에서 발판처럼 탈 수 있다. */
            if(sqrtf(dx * dx + dy * dy) + bubble->age >= 15.0f)
            {
                pop_bubble(game, bubble, false);
                break;
            }
            if(player->body.vy >= bubble->body.vy && player->body.y < bubble->body.y - 0.5f)
            {
                player->body.y = bubble->body.y - 1.725f;
                player->body.vy = bubble->body.vy;
                player->body.grounded = true;
            }
        }
    }
}

/* 살아 있는 바위와 각 플레이어의 원형 접촉을 검사한다.
 * 비활성/무적/사망 여부는 damage_player에서 한 번에 판단한다. */
static void check_boulder_bump(BBGame *game)
{
    for(int b = 0; b < BB_MAX_BOULDERS; ++b)
    {
        BBBoulder *boulder = &game->boulders[b];
        if(!boulder->active)
        {
            continue;
        }
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            if(circles_overlap(&boulder->body, 0.75f, &game->players[p].body, 0.95f))
            {
                damage_player(game, &game->players[p]);
            }
    }
}

/* 음식에 닿은 살아 있는 참가자에게 점수를 준다. 수집 즉시 비활성화하고
 * 반복을 끝내므로 두 플레이어가 동시에 닿아도 점수가 중복 지급되지 않는다. */
static void check_pickup_bump(BBGame *game)
{
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
    {
        BBPickup *pickup = &game->pickups[i];
        if(!pickup->active)
        {
            continue;
        }
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            BBPlayer *player = &game->players[p];
            if(player->active && player->state == BB_PLAYER_NORMAL &&
               circles_overlap(&pickup->body, 0.95f, &player->body, 0.95f))
            {
                player->score += bb_game_pickup_score(pickup->type);
                pickup->active = false;
                game->events.pickup = true;
                break;
            }
        }
    }
}

/* 이동이 끝난 좌표로 동료 구조 -> 포획 -> 적 피해 -> 바위 피해 -> 음식 -> 상태 전환 순서로
 * 검사한다. 검사 순서 자체가 같은 틱에 여러 사건이 일어났을 때의 게임 규칙이다. */
static void check_collisions_and_progress(BBGame *game)
{
    check_rescue_bump(game);
    check_bubble_bump(game);
    /* 버블 포획을 먼저 처리하면 같은 틱에 적이 플레이어를 맞히지 않는다. */
    for(int e = 0; e < BB_MAX_ENEMIES; ++e)
    {
        BBEnemy *enemy = &game->enemies[e];
        if(!enemy->active || enemy->state == BB_ENEMY_SPAWNING || enemy->state == BB_ENEMY_CAPTURED || enemy->state == BB_ENEMY_DEAD)
        {
            continue;
        }
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            if(rects_overlap(&enemy->body, 0.9f, 0.95f,
                             &game->players[p].body, 0.9f, 0.975f))
                damage_player(game, &game->players[p]);
    }
    check_boulder_bump(game);
    check_pickup_bump(game);

    bool living_player = false;
    bool has_lives = false;
    /* 사망 연출은 끝까지 기다리되, 구조 버블 자체는 살아 있는 플레이어가
     * 아니다. 모두 버블/탈락 상태가 되면 서로를 구할 수 없어 게임을 끝낸다. */
    for(int i = 0; i < game->player_count; ++i) {
        if(!game->players[i].active)
        {
            continue;
        }
        if(game->players[i].state == BB_PLAYER_NORMAL || game->players[i].state == BB_PLAYER_DEAD)
        {
            living_player = true;
        }
        if(game->players[i].lives > 0)
        {
            has_lives = true;
        }
    }
    if(!living_player)
    {
        for(int i = 0; i < game->player_count; ++i)
            if(game->players[i].state == BB_PLAYER_BUBBLE || game->players[i].state == BB_PLAYER_WAIT_ROUND)
            {
                game->players[i].state = BB_PLAYER_OUT;
            }
        game->state = BB_STATE_SCORE;
        game->state_time = 0.0f;
        game->won = false;
        return;
    }
    /* 마지막 피격의 사망 그림이 끝날 때까지 기다린다.
     * 이때 다음 라운드나 승리로 넘어가면 목숨 0인 플레이어가 되살아난다. */
    if(!has_lives)
    {
        return;
    }
    /* 클리어 대기 중에도 아이템을 먹을 수 있도록 전환은 충돌 처리 뒤에 한다. */
    if(game->state == BB_STATE_PLAY && bb_game_enemies_left(game) == 0)
    {
        game->state = BB_STATE_CLEAR;
        game->state_time = 0.0f;
    }
    else if(game->state == BB_STATE_CLEAR && game->state_time >= 7.0f)
    {
        if(game->level + 1 < BB_LEVEL_COUNT)
        {
            enter_level(game, game->level + 1);
        }
        else
        {
            game->state = BB_STATE_SCORE;
            game->state_time = 0.0f;
            game->won = true;
        }
    }
}

/* 한 내부 단계의 입력 -> 이동 -> 충돌 -> 라운드 진행을 처리한다.
 * 틱 수와 사건 초기화는 공개 진입점에서 한 번만 처리해 여러 단계의 사건을 모은다. */
static void step_game(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt)
{
    BBInput neutral = {0};
    game->state_time += dt;
    if(game->state == BB_STATE_INTRO)
    {
        /* 인트로 중에는 시간만 진행하고 7초 뒤 첫 라운드를 초기 배치한다. */
        if(game->state_time >= 7.0f)
        {
            enter_level(game, 0);
        }
        return;
    }
    game->level_time += dt;
    /* 모든 물체가 같은 새 발판 위치를 보게 이동/충돌보다 먼저 갱신한다.
     * 메뉴, 인트로, 결과 상태는 위에서 반환하므로 발판도 멈춘다. */
    update_platforms(game, dt);
    /* 소멸한 지지면은 점프 입력과 AI를 읽기 전에 제거한다. 같은 발판에
     * 있던 플레이어·적·바위·음식 모두 이번 틱부터 자연스럽게 떨어진다. */
    for(int i = 0; i < game->player_count; ++i)
        if(game->players[i].active)
        {
            release_platform_rider(game, &game->players[i].body, 0.9f, 0.975f);
        }
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
    {
        if(game->enemies[i].active)
        {
            float half_width = 0.9f;
            if(game->enemies[i].state == BB_ENEMY_DEAD)
            {
                half_width = 0.95f;
            }
            release_platform_rider(game, &game->enemies[i].body, half_width, 0.95f);
        }
    }
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
        if(game->boulders[i].active)
        {
            release_platform_rider(game, &game->boulders[i].body, 0.75f, 0.75f);
        }
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        if(game->pickups[i].active)
        {
            release_platform_rider(game, &game->pickups[i].body, 0.95f, 0.95f);
        }
    for(int i = 0; i < game->player_count; ++i)
    {
        BBInput input = neutral;
        if(inputs != NULL)
        {
            input = inputs[i];
        }
        update_player(game, &game->players[i], i, input, dt);
    }
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
    {
        update_enemy(game, &game->enemies[i], i, dt);
    }
    float center_x = 0.0f;
    float center_y = 0.0f;
    int bubbles = 0;
    /* 모든 거품이 동일한 중심을 사용하도록 개별 이동 전에 평균을 구한다.
     * 거품이 없을 때는 나눗셈을 건너뛰어 0으로 나누는 일을 막는다. */
    for(int i = 0; i < BB_MAX_BUBBLES; ++i)
    {
        if(game->bubbles[i].active && !game->bubbles[i].popping)
        {
            center_x += game->bubbles[i].body.x;
            center_y += game->bubbles[i].body.y;
            ++bubbles;
        }
    }
    if(bubbles > 0)
    {
        center_x /= (float)bubbles;
        center_y /= (float)bubbles;
    }
    for(int i = 0; i < BB_MAX_BUBBLES; ++i)
    {
        update_bubble(game, &game->bubbles[i], dt, center_x, center_y);
    }
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
    {
        update_boulder(game, &game->boulders[i], dt);
    }
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
    {
        update_pickup(game, &game->pickups[i], dt);
    }
    /* 각 객체를 모두 움직인 뒤에 접촉을 검사해야 배열 순서에 따라
     * 어떤 객체만 이전 좌표로 충돌 판정을 받는 일이 생기지 않는다. */
    check_collisions_and_progress(game);
}

/* 외부 한 틱의 유일한 진입점이다. 입력 NULL은 두 플레이어 모두 중립 입력이며
 * dt는 1/30초까지 허용한다. 폭 비례로 빨라진 플레이어가 바위/거품을 한 번에
 * 지나치지 않도록 넓은 월드에서는 실제 앱처럼 1/120초 이하 간격으로 나눈다. */
void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt)
{
    if(game == NULL)
    {
        return;
    }
    memset(&game->events, 0, sizeof game->events);
    if(!isfinite(dt) || dt <= 0.0f || dt > (1.0f / 30.0f))
    {
        return;
    }
    if(game->state == BB_STATE_MENU || game->state == BB_STATE_SCORE)
    {
        return;
    }
    ++game->ticks;
    int steps = 1;
    if(game->world_width > BB_MAP_WIDTH && dt > BB_FIXED_DT)
    {
        steps = (int)ceilf(dt / BB_FIXED_DT);
    }
    float step_dt = dt / (float)steps;
    BBInput held_inputs[BB_MAX_PLAYERS] = {0};
    if(inputs != NULL)
        for(int i = 0; i < BB_MAX_PLAYERS; ++i)
        {
            held_inputs[i].move = inputs[i].move;
        }
    for(int step = 0; step < steps; ++step)
    {
        /* 마지막 단계에 나눗셈의 나머지를 모아 총 경과 시간을 보존한다.
         * 점프/발사는 첫 단계에서만 소비하고 이후에는 방향 입력만 유지한다. */
        float elapsed = step_dt;
        if(step + 1 == steps)
        {
            elapsed = dt - step_dt * (float)step;
        }
        if(step == 0)
        {
            step_game(game, inputs, elapsed);
        }
        else
        {
            step_game(game, held_inputs, elapsed);
        }
        if(game->state == BB_STATE_MENU || game->state == BB_STATE_SCORE)
        {
            break;
        }
    }
}
