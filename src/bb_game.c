/* 게임 규칙과 물리 계산을 담당한다. 그래픽/키보드/파일 API를 사용하지 않아
 * 같은 입력과 난수 시드로 창 없이도 게임 전체를 재현할 수 있다.
 * 위치는 타일, 속도는 타일/초, 시간은 초이며 화면 아래쪽이 +y이다. */
#include "bb_game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 공중 발판은 원래 위치에서 좌우 한 타일씩, 초당 0.75타일로 왕복한다.
 * 모든 발판에 같은 이동량을 적용하면 층 사이의 겹치는 구간이 유지된다.
 * 가장 바깥 발판도 x=2..30인 양쪽 벽 안쪽을 벗어나지 않는다. */
#define BB_PLATFORM_TRAVEL 1.0f
#define BB_PLATFORM_SPEED 0.75f

/* 벽과 천장/바닥 충돌을 각각 참/거짓으로 기록한다. */
typedef struct BBHits {
    bool x;
    bool y;
} BBHits;

/* 범위를 벗어난 값은 가까운 끝값으로 제한한다.
 * 입력 세기, 좌표, 연출 진행률처럼 상한/하한이 있는 값에 공통으로 쓴다. */
static float clamp(float value, float low, float high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

/* 남은 시간을 한 틱만큼 줄이되 음수로 누적시키지 않는다.
 * 0을 '대기 종료'로 사용하는 무적/공격/사망 타이머에 적용한다. */
static float decrease(float value, float dt)
{
    value -= dt;
    return value < 0.0f ? 0.0f : value;
}

/* 게임 안에 저장된 난수 상태를 한 번 진행하고 0 이상 1 미만을 반환한다.
 * 전역 rand()를 쓰지 않으므로 서로 다른 게임/테스트가 영향을 주지 않는다. */
static float random_unit(BBGame *game)
{
    /* 곱셈과 덧셈으로 다음 난수를 만든다. 같은 시드면 테스트도 재현된다.
     * unsigned 32비트 정수의 넘침은 C에서 정의된 순환 연산이다. */
    game->rng = game->rng * UINT32_C(1664525) + UINT32_C(1013904223);
    /* 0..9999를 나누므로 1.0은 나오지 않아 배열 인덱스 추첨에도 쓸 수 있다. */
    return (float)(game->rng % 10000u) / 10000.0f;
}

/* 현재 라운드의 원본 타일을 읽는다. 움직이는 발판은 배열을 옮기지 않고
 * 호출부에서 실제 위치를 원본 좌표로 환산하거나 tile_offset을 더한다. */
static int tile_at(const BBGame *game, int x, int y)
{
    /* 맵 밖은 빈 공간이다. 아래쪽 순환 통로와 맵 위 진입에 벽을 만들지 않는다. */
    if(x < 0 || x >= BB_MAP_WIDTH || y < 0 || y >= BB_MAP_HEIGHT)
        return 0;
    return game->maps[game->level][y][x];
}

/* 맵 배열은 처음 만든 타일 좌표 그대로 둔다. 공중의 일방 통과 발판만
 * 실제 충돌/그리기 위치에 이동량을 더하므로 벽과 맨 아래 바닥은 고정된다. */
static float tile_offset(const BBGame *game, int tile, int y)
{
    return tile == 2 && y < BB_MAP_HEIGHT - 2 ? game->platform_offset : 0.0f;
}

/* 이번 틱의 발판 위치와 이동 거리를 함께 구한다. 경계를 조금 넘은 거리는
 * 반대쪽으로 되돌려 느린 프레임에서도 왕복 속도가 달라지지 않게 한다. */
static void update_platforms(BBGame *game, float dt)
{
    float old_offset = game->platform_offset;
    game->platform_offset += (float)game->platform_direction * BB_PLATFORM_SPEED * dt;
    if(game->platform_offset >= BB_PLATFORM_TRAVEL)
    {
        game->platform_offset = 2.0f * BB_PLATFORM_TRAVEL - game->platform_offset;
        game->platform_direction = -1;
    }
    else if(game->platform_offset <= -BB_PLATFORM_TRAVEL)
    {
        game->platform_offset = -2.0f * BB_PLATFORM_TRAVEL - game->platform_offset;
        game->platform_direction = 1;
    }
    game->platform_step = game->platform_offset - old_offset;
}

/* grounded만으로는 거품 위에 선 경우와 구별할 수 없다. 물체의 발이
 * 이전 틱의 공중 발판 윗면에 실제로 닿아 있었는지 좌표까지 확인한다.
 * 점프 중이거나 바닥/거품 위에 있으면 발판에 끌려가지 않는다. */
static bool on_moving_platform(const BBGame *game, const BBBody *body, float hw, float hh)
{
    if(!body->grounded || body->vy < 0.0f)
        return false;
    float feet = body->y + hh;
    int row = (int)floorf(feet + 0.001f);
    if(row < 0 || row >= BB_MAP_HEIGHT - 2 || fabsf(feet - (float)row) > 0.001f)
        return false;
    float previous_offset = game->platform_offset - game->platform_step;
    int left = (int)floorf(body->x - hw - previous_offset + 0.0001f);
    int right = (int)floorf(body->x + hw - previous_offset - 0.0001f);
    for(int x = left; x <= right; ++x)
        if(tile_at(game, x, row) == 2)
            return true;
    return false;
}

/* aw/ah와 bw/bh는 전체 너비/높이가 아니라 중심에서 가장자리까지의 거리다.
 * 두 축 모두 겹쳐야 사각형 충돌이며, 경계에 닿기만 한 경우는 제외한다. */
static bool rects_overlap(const BBBody *a, float aw, float ah,
                          const BBBody *b, float bw, float bh)
{
    return fabsf(a->x - b->x) < aw + bw && fabsf(a->y - b->y) < ah + bh;
}

/* 중심 거리와 반지름 합을 비교한다. 제곱끼리 비교해 매번 제곱근을
 * 구하지 않아도 같은 결과를 얻으며, 버블/바위/아이템 접촉에 사용한다. */
static bool circles_overlap(const BBBody *a, float ar,
                            const BBBody *b, float br)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float radius = ar + br;
    return dx * dx + dy * dy < radius * radius;
}

/* 몸체를 한 틱 이동하고 가로/세로 충돌 여부를 반환한다.
 * hw/hh는 반너비/반높이, one_way는 일방 통과 발판 착지 허용 여부,
 * wrap은 화면 아래로 떨어졌을 때 위로 순환할지 여부다.
 * 가로 -> 세로 순서로 검사하여 벽 모서리에서 축별 속도를 따로 정리한다.
 * 타일 2는 내려오는 물체만 받으므로 아래에서 위로 점프해 통과할 수 있다. */
static BBHits move_body(const BBGame *game, BBBody *body, float hw, float hh,
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
    if(one_way && on_moving_platform(game, body, hw, hh))
        horizontal_step += game->platform_step;
    /* 발판에 밀려 벽에 닿은 경우에도 같은 벽 충돌 처리를 거친다. */
    body->x += horizontal_step;
    for(int y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh - 0.0001f); ++y)
    {
        for(int x = (int)floorf(body->x - hw + 0.0001f); x <= (int)floorf(body->x + hw - 0.0001f); ++x)
        {
            if(tile_at(game, x, y) != 3)
                continue;
            /* 이전 위치가 벽 바깥이었을 때만 진입 방향의 경계로 되돌린다.
             * 타일 1(장식), 2(발판)는 옆면을 막지 않는다. */
            if(horizontal_step > 0.0f && old_x + hw <= (float)x + 0.001f)
            {
                body->x = fminf(body->x, (float)x - hw);
                hit.x = true;
            }
            else if(horizontal_step < 0.0f && old_x - hw >= (float)(x + 1) - 0.001f)
            {
                body->x = fmaxf(body->x, (float)(x + 1) + hw);
                hit.x = true;
            }
        }
    }
    if(body->x < hw || body->x > (float)BB_MAP_WIDTH - hw)
    {
        body->x = clamp(body->x, hw, (float)BB_MAP_WIDTH - hw);
        hit.x = true;
    }
    if(hit.x)
        body->vx = 0.0f;

    /* 세로 이동 후 발판 또는 천장에 닿았는지 검사한다. */
    body->y += body->vy * dt;
    /* 지난 틱에 착지했어도 이번 틱에 발판을 벗어날 수 있으므로 재검사한다. */
    body->grounded = false;
    for(int y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh + 0.0001f); ++y)
    {
        /* 움직인 발판의 원래 타일 번호는 물체보다 한 칸 바깥에 있을 수 있다.
         * 검색 범위를 한 칸 넓히고 실제 이동한 사각형과 겹치는지 다시 검사한다. */
        for(int x = (int)floorf(body->x - hw - BB_PLATFORM_TRAVEL + 0.0001f);
            x <= (int)floorf(body->x + hw + BB_PLATFORM_TRAVEL - 0.0001f); ++x)
        {
            int tile = tile_at(game, x, y);
            if(tile != 3 && !(one_way && tile == 2))
                continue;
            float tile_left = (float)x + tile_offset(game, tile, y);
            if(body->x + hw <= tile_left + 0.0001f || body->x - hw >= tile_left + 1.0f - 0.0001f)
                continue;
            if(body->vy >= 0.0f && old_y + hh <= (float)y + 0.001f && body->y + hh >= (float)y)
            {
                /* 내려오며 윗면을 통과한 경우 발을 윗면에 맞춘다. 여러 타일과
                 * 겹쳐도 fminf로 가장 위쪽의 착지면을 유지한다. */
                body->y = fminf(body->y, (float)y - hh);
                body->grounded = true;
                hit.y = true;
            }
            else if(tile == 3 && body->vy < 0.0f && old_y - hh >= (float)(y + 1) - 0.001f)
            {
                /* 올라갈 때 머리를 막는 것은 단단한 벽/천장(3)뿐이다. */
                body->y = fmaxf(body->y, (float)(y + 1) + hh);
                hit.y = true;
            }
        }
    }
    if(hit.y)
        body->vy = 0.0f;
    /* 바닥 아래로 완전히 내려간 뒤에만 위쪽으로 순환시킨다. */
    if(wrap && body->y > (float)BB_MAP_HEIGHT + 1.0f)
        body->y -= (float)BB_MAP_HEIGHT + 2.0f;
    return hit;
}

/* 새 라운드/부활에서 공통으로 쓰는 배치다. 1P는 왼쪽, 2P는 오른쪽에서
 * 중앙을 바라본다. 생명/점수/마나는 건드리지 않아 게임 중 기록이 이어진다. */
static void place_player(BBPlayer *player, int number)
{
    player->body = (BBBody){.x = number == 0 ? 4.0f : 28.0f, .y = 24.0f};
    player->facing = number == 0 ? 1 : -1;
    player->state = BB_PLAYER_NORMAL;
    player->death_timer = 0.0f;
    player->falling = true;
    player->air_control = true;
    player->jump_start_y = 24.0f;
    player->attack_timer = 0.0f;
    player->fire_cooldown = 0.0f;
}

/* 고정 크기 enemies 배열의 지정 칸을 등장 대기 상태로 덮어쓴다.
 * 지정 초기화에 쓰지 않은 속도/타이머 등은 0이 되어 이전 적의 값이 남지 않는다. */
static void spawn_enemy(BBGame *game, int index, BBEnemyType type, float x, float y, float delay)
{
    game->enemies[index] = (BBEnemy){
        .body.x = x, .type = type, .state = BB_ENEMY_SPAWNING,
        .active = true, .facing = random_unit(game) < 0.5f ? -1 : 1,
        .spawn_delay = delay, .spawn_y = y
    };
}

/* 유효한 라운드 번호(0..4)로 들어가는 내부 함수다. 라운드 전용 시간과
 * 발판 이동을 초기화한 뒤 참가자를 재배치하고 라운드 수만큼 적을 만든다. */
static void enter_level(BBGame *game, int level)
{
    game->level = level;
    game->level_time = 0.0f;
    game->platform_offset = 0.0f;
    game->platform_step = 0.0f;
    game->platform_direction = 1;
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
        /* 이미 탈락한 플레이어는 다음 라운드에서도 되살리지 않는다. */
        if(game->players[i].active && game->players[i].lives > 0)
        {
            place_player(&game->players[i], i);
            game->players[i].invulnerable = 3.0f;
        }
    }
    /* 1라운드의 한 마리부터 라운드마다 한 마리씩 늘린다. */
    int count = level + 1;
    for(int i = 0; i < count; ++i)
    {
        /* 종류를 순환하고 중앙을 기준으로 좌우에 펼친다. 등장 시간도
         * 0.45초씩 늦춰 여러 적이 한 위치에서 한꺼번에 떨어지지 않게 한다. */
        BBEnemyType type = (BBEnemyType)((i + level) % BB_ENEMY_TYPE_COUNT);
        float x = 16.0f + ((float)i - (float)(count - 1) * 0.5f) * 3.0f;
        float y = i % 2 == 0 ? 8.0f : 4.0f;
        spawn_enemy(game, i, type, x, y, (float)i * 0.45f);
    }
}

/* 음식 20종의 등장 순서를 한 묶음으로 만든다. 커서는 다음에 사용할 위치다.
 * 한 묶음을 모두 소비한 뒤에만 다시 섞으므로 묶음 안에서는 중복되지 않는다. */
static void shuffle_pickups(BBGame *game)
{
    for(int i = 0; i < BB_PICKUP_TYPE_COUNT; ++i)
        game->pickup_order[i] = (BBPickupType)i;
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
        return;
    memset(game, 0, sizeof *game);
    if(tiles != NULL)
        memcpy(game->maps, tiles, sizeof game->maps);
    game->rng = seed;
    game->player_count = 1;
    game->state = BB_STATE_MENU;
}

/* 메뉴에서 새 게임을 선택했을 때 호출한다. 맵/난수 상태를 보존하면서 이전
 * 플레이의 객체/기록/마나를 초기화한다. 인원은 배열 용량인 1..2명으로 제한한다. */
void bb_game_start(BBGame *game, int player_count)
{
    if(game == NULL)
        return;
    if(player_count < 1) player_count = 1;
    if(player_count > BB_MAX_PLAYERS) player_count = BB_MAX_PLAYERS;
    game->player_count = player_count;
    memset(game->players, 0, sizeof game->players);
    memset(game->enemies, 0, sizeof game->enemies);
    memset(game->bubbles, 0, sizeof game->bubbles);
    memset(game->boulders, 0, sizeof game->boulders);
    memset(game->pickups, 0, sizeof game->pickups);
    game->state = BB_STATE_INTRO;
    game->state_time = 0.0f;
    game->level_time = 0.0f;
    game->platform_offset = 0.0f;
    game->platform_step = 0.0f;
    game->platform_direction = 1;
    game->ticks = 0;
    memset(&game->events, 0, sizeof game->events);
    game->level = 0;
    game->won = false;
    shuffle_pickups(game);
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        BBPlayer *player = &game->players[i];
        /* 두 칸 모두 위치는 준비하지만, 참가 인원만 활성화하고 생명을 준다. */
        place_player(player, i);
        player->active = i < player_count;
        if(player->active) player->lives = BB_STARTING_LIVES;
    }
}

/* 플레이를 멈추고 메뉴 상태로 바꾼다. 기존 객체/맵을 지울 필요는 없으며,
 * 다시 시작할 때 bb_game_start가 정리한다. 지난 효과음 이벤트는 즉시 비운다. */
void bb_game_menu(BBGame *game)
{
    if(game == NULL)
        return;
    game->state = BB_STATE_MENU;
    game->state_time = 0.0f;
    memset(&game->events, 0, sizeof game->events);
}

/* Enter 또는 스모크 테스트에서 인트로만 건너뛴다.
 * 이미 플레이 중일 때 호출해도 라운드가 초기화되지 않도록 상태를 제한한다. */
void bb_game_skip_intro(BBGame *game)
{
    if(game != NULL && game->state == BB_STATE_INTRO)
        enter_level(game, 0);
}

/* 아직 처치하지 않은 적 수를 센다. 등장 대기와 포획 중인 적도 포함하며,
 * 죽어서 보상으로 변하는 연출 중인 적은 제외해 클리어 대기를 시작할 수 있다. */
int bb_game_enemies_left(const BBGame *game)
{
    int count = 0;
    if(game == NULL)
        return 0;
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
        if(game->enemies[i].active && game->enemies[i].state != BB_ENEMY_DEAD)
            ++count;
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
        return 0;
    return scores[type];
}

/* 실제 피격이 가능한 참가자만 생명을 한 번 차감한다. 즉시 DEAD가 되므로
 * 같은 틱에 적/바위가 여러 번 겹쳐도 추가 차감하지 않는다. 부활은 update_player가 맡는다. */
static void damage_player(BBGame *game, BBPlayer *player)
{
    if(!player->active || player->state != BB_PLAYER_NORMAL || player->invulnerable > 0.0f)
        return;
    --player->lives;
    player->state = BB_PLAYER_DEAD;
    player->death_timer = 1.5f;
    player->body.vx = 0.0f;
    player->body.vy = 0.0f;
    game->events.death = true;
}

/* 발사 대기시간이 끝났으면 공유 버블 배열의 첫 빈 칸을 사용한다.
 * 슬롯이 꽉 찼을 때는 대기시간/이벤트를 소비하지 않아 빈 칸이 생기면 다시 쏠 수 있다. */
static void fire_bubble(BBGame *game, BBPlayer *player, int owner)
{
    if(player->fire_cooldown > 0.0f)
        return;
    for(int i = 0; i < BB_MAX_BUBBLES; ++i)
    {
        BBBubble *bubble = &game->bubbles[i];
        if(bubble->active)
            continue;
        float x = player->body.x + (float)player->facing * 2.4f;
        float y = player->body.y;
        float speed = 20.0f * (float)player->facing;
        /* 원본의 짧은 벽 레이캐스트를 따른다. 유한한 크기의 버블이
         * 벽 타일 내부에서 생성되지 않도록 벽 바깥에 배치한다. */
        for(float distance = 0.1f; distance <= 3.15f; distance += 0.1f)
        {
            float cast_x = player->body.x + (float)player->facing * distance;
            int row = (int)floorf(y);
            int column = (int)floorf(cast_x);
            int tile = tile_at(game, column, row);
            float offset = 0.0f;
            /* 벽은 원래 좌표, 일방 통과 발판은 이동한 좌표로 찾는다.
             * 발사 위치 검사도 화면에 보이는 발판 위치와 일치해야 한다. */
            if(tile != 3)
            {
                offset = row < BB_MAP_HEIGHT - 2 ? game->platform_offset : 0.0f;
                column = (int)floorf(cast_x - offset);
                tile = tile_at(game, column, row) == 2 ? 2 : 0;
            }
            if(tile == 2 || tile == 3)
            {
                float boundary = (float)column + offset + (player->facing > 0 ? 0.0f : 1.0f);
                x = boundary - (float)player->facing * 0.751f;
                speed = 0.0f;
                break;
            }
        }
        *bubble = (BBBubble){
            .body = {.x = clamp(x, 0.75f, 31.25f), .y = y, .vx = speed},
            .active = true, .owner = owner, .captured_enemy = -1
        };
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
    float duration = player->boosting ? BB_ENERGY_BOOST_SECONDS : BB_ENERGY_CHARGE_SECONDS;
    player->energy_elapsed += (double)dt;
    if(player->energy_elapsed >= duration) {
        player->energy_elapsed -= duration;
        player->boosting = !player->boosting;
        player->body.vx *= player->boosting ? 2.0f : 0.5f;
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
    if(!player->active || player->state == BB_PLAYER_OUT)
        return;
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
                place_player(player, index);
                player->invulnerable = 3.0f;
            }
            else
                player->state = BB_PLAYER_OUT;
        }
        return;
    }
    update_mana(player, dt);
    float speed_multiplier = player->boosting ? 2.0f : 1.0f;
    /* NaN/무한대 입력이 좌표까지 오염시키지 않게 중립으로 바꾸고,
     * 게임패드 아날로그 입력도 -1..1 범위 안에서만 속도로 변환한다. */
    float move = isfinite(input.move) ? clamp(input.move, -1.0f, 1.0f) : 0.0f;
    if(move < 0.0f) player->facing = -1;
    if(move > 0.0f) player->facing = 1;
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
            player->falling = true;
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
                player->air_control = true;
            if(player->air_control)
                player->body.vx = move * BB_PLAYER_AIR_SPEED * speed_multiplier;
            player->body.vy += 30.0f * dt;
        }
    }
    move_body(game, &player->body, 0.9f, 0.975f, dt, true, true);
    if(input.fire)
        fire_bubble(game, player, index);
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
            continue;
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
 * 공중 발판은 현재 이동량을 빼 원본 열을 찾으므로 빈 과거 위치로 뛰지 않는다. */
static bool platform_above(const BBGame *game, const BBBody *body)
{
    int start = (int)floorf(body->y - 1.0f);
    for(int y = start; y > start - 5; --y)
    {
        int column = (int)floorf(body->x);
        float offset = y < BB_MAP_HEIGHT - 2 ? game->platform_offset : 0.0f;
        int platform_column = (int)floorf(body->x - offset);
        if(tile_at(game, column, y) == 3 || tile_at(game, platform_column, y) == 2)
            return true;
    }
    return false;
}

/* 마이타의 바위를 첫 빈 슬롯에 생성한다. 정면을 중심으로 각도를 조금
 * 흩뜨리며, 배열이 가득 차 있으면 발사 타이머를 유지해 다음 틱에 재시도한다. */
static void throw_boulder(BBGame *game, BBEnemy *enemy)
{
    if(enemy->throw_timer < 4.0f)
        return;
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
    {
        BBBoulder *boulder = &game->boulders[i];
        if(boulder->active)
            continue;
        float angle = random_unit(game) - 0.5f;
        *boulder = (BBBoulder){
            .body = {.x = enemy->body.x, .y = enemy->body.y,
                     .vx = cosf(angle) * 15.0f * (float)enemy->facing,
                     .vy = sinf(angle) * 15.0f},
            .active = true
        };
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
            continue;
        if(game->pickup_cursor >= BB_PICKUP_TYPE_COUNT)
            shuffle_pickups(game);
        *pickup = (BBPickup){
            .body = enemy->body, .active = true,
            .type = game->pickup_order[game->pickup_cursor++]
        };
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
        return;
    enemy->age += dt;
    if(enemy->state == BB_ENEMY_SPAWNING)
    {
        if(enemy->age <= enemy->spawn_delay)
            return;
        float progress = clamp((enemy->age - enemy->spawn_delay) / 2.0f, 0.0f, 1.0f);
        /* 대기가 끝난 뒤 2초 동안 화면 위에서 지정 높이까지 내려온다. */
        enemy->body.y = enemy->spawn_y * progress;
        if(progress >= 1.0f)
            enemy->state = BB_ENEMY_FALLING;
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
            enemy->body.vx = -vx;
        if(hit.y)
            enemy->body.vy = -vy;
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
            enemy->facing = dx < 0.0f ? -1 : 1;
            enemy->turn_timer = 0.0f;
        }
        jump = target->body.y < enemy->body.y - 1.0f && dx * (float)enemy->facing >= 0.0f &&
               enemy->jump_timer > 1.5f && platform_above(game, &enemy->body);
    }
    /* 몬스타는 발판 유무와 무관하게 짧게 도약하며 공중에서도 전진한다. */
    if(enemy->type == BB_ENEMY_MONSTA && enemy->jump_timer >= 1.1f)
        jump = true;
    float move = (float)enemy->facing;
    if(enemy->type == BB_ENEMY_MAITA && enemy->throw_timer >= 5.0f)
        throw_boulder(game, enemy);
    float multiplier = enemy->angry && enemy->type == BB_ENEMY_ZENCHAN ? 1.8f : 1.0f;
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
        enemy->body.vy += 9.81f * dt;
    else
    {
        enemy->state = BB_ENEMY_FALLING;
        enemy->body.vx = enemy->type == BB_ENEMY_MONSTA ? move * speed : 0.0f;
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
        return;
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
                enemy->angry = true;
        }
        else
        {
            float angle = (50.0f + 20.0f * random_unit(game)) * 0.01745329252f;
            /* 50..70도를 라디안으로 바꾸고 좌우 방향을 추첨해 위로 날려 보낸다. */
            /* Julgen의 밀도 0 동적 픽스처는 Box2D에서 질량 1로 동작한다. */
            float speed = 15.0f + 5.0f * random_unit(game);
            enemy->state = BB_ENEMY_DEAD;
            enemy->body.vx = cosf(angle) * speed * (random_unit(game) < 0.5f ? -1.0f : 1.0f);
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
        return;
    if(bubble->popping)
    {
        bubble->pop_timer -= dt;
        if(bubble->pop_timer <= 0.0f)
            finish_pop(game, bubble);
        return;
    }
    bubble->age += dt;
    /* age는 발사부터의 시간이다. 포획 시 age를 되돌리지 않고 수명만
     * 8초에서 13초로 늘리면 늦게 포획해도 기존 남은 시간에 정확히 5초가
     * 더해진다. 빈 거품의 수명, 부딪쳐 터뜨리기, 파열 연출은 그대로다. */
    float lifetime = bubble->captured_enemy >= 0 ? 13.0f : 8.0f;
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
        if(bubble->body.y > 11.0f)
            bubble->body.vy -= 3.0f * dt;
    }
    float vx = bubble->body.vx;
    float vy = bubble->body.vy;
    /* 거품은 일방 통과 발판을 양쪽 방향으로 지나며 단단한 벽에만 반사된다. */
    BBHits hit = move_body(game, &bubble->body, 0.75f, 0.75f, dt, false, false);
    if(hit.x)
        bubble->body.vx = -vx * 0.8f;
    if(hit.y)
        bubble->body.vy = -vy * 0.8f;
    if(bubble->body.y < 0.75f)
    {
        /* 맵 위에는 타일 벽이 없으므로 반지름을 기준으로 별도 상단 경계를 둔다. */
        bubble->body.y = 0.75f;
        bubble->body.vy = fabsf(bubble->body.vy) * 0.8f;
    }
    if(bubble->body.y > 29.0f)
        bubble->body.y -= 30.0f;
}

/* 바위는 3초 동안 중력으로 움직이고 벽/발판에서 속도의 90%로 튕긴다.
 * 플레이어 피해는 모든 이동이 끝난 뒤 접촉 검사에서 따로 처리한다. */
static void update_boulder(BBGame *game, BBBoulder *boulder, float dt)
{
    if(!boulder->active)
        return;
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
        boulder->body.vx = -vx * 0.9f;
    if(hit.y)
        boulder->body.vy = -vy * 0.9f;
}

/* 음식은 10초 후 사라진다. 자체 수평 속도는 0이지만 발판 위에 착지했다면
 * move_body가 발판 이동량을 더해 함께 이동시킨다. 바닥 통로로도 순환한다. */
static void update_pickup(BBGame *game, BBPickup *pickup, float dt)
{
    if(!pickup->active)
        return;
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

/* 거품별로 적 포획을 먼저 검사하고, 이어 플레이어의 터뜨리기/올라타기를 검사한다.
 * 파열 중인 거품은 제외하므로 한 번 시작한 파열의 이유나 적이 바뀌지 않는다. */
static void check_bubble_bump(BBGame *game)
{
    for(int b = 0; b < BB_MAX_BUBBLES; ++b)
    {
        BBBubble *bubble = &game->bubbles[b];
        if(!bubble->active || bubble->popping)
            continue;
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
            continue;
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
            continue;
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            if(circles_overlap(&boulder->body, 0.75f, &game->players[p].body, 0.95f))
                damage_player(game, &game->players[p]);
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
            continue;
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

/* 이동이 끝난 좌표로 포획 -> 적 피해 -> 바위 피해 -> 음식 -> 상태 전환 순서로
 * 검사한다. 검사 순서 자체가 같은 틱에 여러 사건이 일어났을 때의 게임 규칙이다. */
static void check_collisions_and_progress(BBGame *game)
{
    check_bubble_bump(game);
    /* 버블 포획을 먼저 처리하면 같은 틱에 적이 플레이어를 맞히지 않는다. */
    for(int e = 0; e < BB_MAX_ENEMIES; ++e)
    {
        BBEnemy *enemy = &game->enemies[e];
        if(!enemy->active || enemy->state == BB_ENEMY_SPAWNING || enemy->state == BB_ENEMY_CAPTURED || enemy->state == BB_ENEMY_DEAD)
            continue;
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            if(rects_overlap(&enemy->body, 0.9f, 0.95f,
                             &game->players[p].body, 0.9f, 0.975f))
                damage_player(game, &game->players[p]);
    }
    check_boulder_bump(game);
    check_pickup_bump(game);

    bool living_player = false;
    bool has_lives = false;
    /* DEAD는 아직 사망 연출 중이므로 OUT과 구분한다. 생명이 0이어도
     * 연출이 끝나기 전에는 결과 화면으로 넘어가지 않는다. */
    for(int i = 0; i < game->player_count; ++i) {
        if(!game->players[i].active)
            continue;
        if(game->players[i].state != BB_PLAYER_OUT)
            living_player = true;
        if(game->players[i].lives > 0)
            has_lives = true;
    }
    if(!living_player)
    {
        game->state = BB_STATE_SCORE;
        game->state_time = 0.0f;
        game->won = false;
        return;
    }
    /* 마지막 피격의 사망 그림이 끝날 때까지 기다린다.
     * 이때 다음 라운드나 승리로 넘어가면 목숨 0인 플레이어가 되살아난다. */
    if(!has_lives)
        return;
    /* 클리어 대기 중에도 아이템을 먹을 수 있도록 전환은 충돌 처리 뒤에 한다. */
    if(game->state == BB_STATE_PLAY && bb_game_enemies_left(game) == 0)
    {
        game->state = BB_STATE_CLEAR;
        game->state_time = 0.0f;
    }
    else if(game->state == BB_STATE_CLEAR && game->state_time >= 7.0f)
    {
        if(game->level + 1 < BB_LEVEL_COUNT)
            enter_level(game, game->level + 1);
        else
        {
            game->state = BB_STATE_SCORE;
            game->state_time = 0.0f;
            game->won = true;
        }
    }
}

/* 외부에서 호출하는 한 틱의 유일한 진입점이다. 보통 BB_FIXED_DT(1/120초)를
 * 전달하며 입력 NULL은 두 플레이어 모두 중립 입력을 뜻한다.
 * 큰 dt를 여러 물리 이동에 바로 쓰면 벽을 통과할 수 있어 1/30초까지만 허용한다. */
void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt)
{
    BBInput neutral = {0};
    if(game == NULL)
        return;
    /* 입력 -> 이동 -> 충돌 -> 라운드 진행을 한 번에 처리한다. */
    memset(&game->events, 0, sizeof game->events);
    if(!isfinite(dt) || dt <= 0.0f || dt > (1.0f / 30.0f))
        return;
    if(game->state == BB_STATE_MENU || game->state == BB_STATE_SCORE)
        return;
    ++game->ticks;
    game->state_time += dt;
    if(game->state == BB_STATE_INTRO)
    {
        /* 인트로 중에는 시간만 진행하고 7초 뒤 첫 라운드를 초기 배치한다. */
        if(game->state_time >= 7.0f)
            enter_level(game, 0);
        return;
    }
    game->level_time += dt;
    /* 모든 물체가 같은 새 발판 위치를 보게 이동/충돌보다 먼저 갱신한다.
     * 메뉴, 인트로, 결과 상태는 위에서 반환하므로 발판도 멈춘다. */
    update_platforms(game, dt);
    for(int i = 0; i < game->player_count; ++i)
        update_player(game, &game->players[i], i, inputs != NULL ? inputs[i] : neutral, dt);
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
        update_enemy(game, &game->enemies[i], i, dt);
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
        update_bubble(game, &game->bubbles[i], dt, center_x, center_y);
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
        update_boulder(game, &game->boulders[i], dt);
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        update_pickup(game, &game->pickups[i], dt);
    /* 각 객체를 모두 움직인 뒤에 접촉을 검사해야 배열 순서에 따라
     * 어떤 객체만 이전 좌표로 충돌 판정을 받는 일이 생기지 않는다. */
    check_collisions_and_progress(game);
}
