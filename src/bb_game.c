#include "bb_game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 충돌 반응은 C++ Box2D 대신 C 타일 판정기로 처리한다. */
enum { HIT_X = 1, HIT_Y = 2 };

static float minimum(float a, float b) { return a < b ? a : b; }
static float maximum(float a, float b) { return a > b ? a : b; }
static float clamp(float a, float lo, float hi) { return minimum(maximum(a, lo), hi); }
static float decrease(float value, float dt) { return maximum(0.0f, value - dt); }

static float random_unit(BBGame *game)
{
    /* 게임 상태에 보관한 xorshift32 시드를 사용해 같은 입력을 재현 가능하게 한다. */
    uint32_t value = game->rng;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->rng = value;
    return (float)(value >> 8) / 16777216.0f;
}

static int tile_at(const BBGame *game, int x, int y)
{
    /* 맵 밖은 빈 공간이다. 아래쪽 순환 통로와 맵 위 진입에 벽을 만들지 않는다. */
    if(x < 0 || x >= BB_MAP_WIDTH || y < 0 || y >= BB_MAP_HEIGHT)
        return 0;
    return game->maps[y][x];
}

static bool circles_overlap(const BBGame *game, const BBBody *a, float ar,
                            const BBBody *b, float br)
{
    float dx;
    float dy;
    float radius;
    if(game->collision.circles != NULL)
        return game->collision.circles(a->x, a->y, ar, b->x, b->y, br);
    dx = a->x - b->x;
    dy = a->y - b->y;
    radius = ar + br;
    return dx * dx + dy * dy < radius * radius;
}

/* 타일 값 2는 내려오는 물체의 윗면만 받는다.
 * 축을 따로 판정하므로 일방 통과 발판을 아래에서 지나갈 수 있다. */
static unsigned move_body(const BBGame *game, BBBody *body, float hw, float hh,
                          float dt, bool one_way, bool wrap)
{
    unsigned hit = 0;
    float old_x = body->x;
    float old_y = body->y;
    int x;
    int y;
    int tile;
    body->x += body->vx * dt;
    for(y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh - 0.0001f); ++y)
    {
        for(x = (int)floorf(body->x - hw + 0.0001f); x <= (int)floorf(body->x + hw - 0.0001f); ++x)
        {
            if(tile_at(game, x, y) != 3)
                continue;
            if(body->vx > 0.0f && old_x + hw <= (float)x + 0.001f)
            {
                body->x = minimum(body->x, (float)x - hw);
                hit |= HIT_X;
            }
            else if(body->vx < 0.0f && old_x - hw >= (float)(x + 1) - 0.001f)
            {
                body->x = maximum(body->x, (float)(x + 1) + hw);
                hit |= HIT_X;
            }
        }
    }
    if(body->x < hw || body->x > (float)BB_MAP_WIDTH - hw)
    {
        body->x = clamp(body->x, hw, (float)BB_MAP_WIDTH - hw);
        hit |= HIT_X;
    }
    if(hit & HIT_X)
        body->vx = 0.0f;

    body->y += body->vy * dt;
    body->grounded = false;
    for(y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh + 0.0001f); ++y)
    {
        for(x = (int)floorf(body->x - hw + 0.0001f); x <= (int)floorf(body->x + hw - 0.0001f); ++x)
        {
            tile = tile_at(game, x, y);
            if(tile != 3 && !(one_way && tile == 2))
                continue;
            if(body->vy >= 0.0f && old_y + hh <= (float)y + 0.001f && body->y + hh >= (float)y)
            {
                body->y = minimum(body->y, (float)y - hh);
                body->grounded = true;
                hit |= HIT_Y;
            }
            else if(tile == 3 && body->vy < 0.0f && old_y - hh >= (float)(y + 1) - 0.001f)
            {
                body->y = maximum(body->y, (float)(y + 1) + hh);
                hit |= HIT_Y;
            }
        }
    }
    if(hit & HIT_Y)
        body->vy = 0.0f;
    /* 바닥 아래로 완전히 내려간 뒤에만 위쪽으로 순환시킨다. */
    if(wrap && body->y > (float)BB_MAP_HEIGHT + 1.0f)
        body->y -= (float)BB_MAP_HEIGHT + 2.0f;
    return hit;
}

static void place_player(BBPlayer *player)
{
    memset(&player->body, 0, sizeof player->body);
    player->body.x = 4.0f;
    player->body.y = 24.0f;
    player->facing = 1;
    player->falling = true;
    player->air_control = true;
    player->jump_start_y = 24.0f;
    player->attack_timer = 0.0f;
    player->fire_cooldown = 0.0f;
}

static void enter_play(BBGame *game)
{
    int i;
    game->level_time = 0.0f;
    game->state_time = 0.0f;
    game->state = BB_STATE_PLAY;
    memset(game->bubbles, 0, sizeof game->bubbles);
    for(i = 0; i < BB_MAX_PLAYERS; ++i)
        if(game->players[i].active)
            place_player(&game->players[i]);
}

void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed)
{
    if(game == NULL)
        return;
    memset(game, 0, sizeof *game);
    if(tiles != NULL)
        memcpy(game->maps, tiles, sizeof game->maps);
    /* xorshift32의 영 시드는 계속 영이므로 고정된 비영 시드로 대체한다. */
    game->rng = seed != 0 ? seed : 0xB0BB1EU;
    game->state = BB_STATE_MENU;
}

void bb_game_set_collision_backend(BBGame *game, BBCollisionBackend backend)
{
    if(game != NULL)
        game->collision = backend;
}

void bb_game_start(BBGame *game)
{
    int i;
    BBPlayer *player;
    if(game == NULL)
        return;
    memset(game->players, 0, sizeof game->players);
    memset(game->bubbles, 0, sizeof game->bubbles);
    game->state = BB_STATE_INTRO;
    game->state_time = 0.0f;
    game->level_time = 0.0f;
    game->ticks = 0;
    game->events = 0;
    game->bump_pending = false;
    for(i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        player = &game->players[i];
        place_player(player);
        player->active = true;
    }
}

void bb_game_menu(BBGame *game)
{
    if(game == NULL)
        return;
    game->state = BB_STATE_MENU;
    game->state_time = 0.0f;
    game->events = 0;
    game->bump_pending = false;
}

void bb_game_skip_intro(BBGame *game)
{
    if(game != NULL && game->state == BB_STATE_INTRO)
        enter_play(game);
}

static void fire_bubble(BBGame *game, BBPlayer *player)
{
    int i;
    int tile;
    BBBubble *bubble;
    float x;
    float y;
    float speed;
    float distance;
    float cast_x;
    float boundary;
    if(player->fire_cooldown > 0.0f)
        return;
    for(i = 0; i < BB_MAX_BUBBLES; ++i)
    {
        bubble = &game->bubbles[i];
        if(bubble->active)
            continue;
        x = player->body.x + (float)player->facing * 2.4f;
        y = player->body.y;
        speed = 20.0f * (float)player->facing;
        /* 원본의 짧은 벽 레이캐스트를 따른다. 유한한 크기의 버블이
         * 벽 타일 내부에서 생성되지 않도록 벽 바깥에 배치한다. */
        for(distance = 0.1f; distance <= 3.15f; distance += 0.1f)
        {
            cast_x = player->body.x + (float)player->facing * distance;
            tile = tile_at(game, (int)floorf(cast_x), (int)floorf(y));
            if(tile == 2 || tile == 3)
            {
                boundary = player->facing > 0 ? floorf(cast_x) : floorf(cast_x) + 1.0f;
                x = boundary - (float)player->facing * 0.751f;
                speed = 0.0f;
                break;
            }
        }
        memset(bubble, 0, sizeof *bubble);
        bubble->body.x = clamp(x, 0.75f, 31.25f);
        bubble->body.y = y;
        bubble->body.vx = speed;
        bubble->active = true;
        player->fire_cooldown = 0.4f;
        player->attack_timer = 0.2f;
        game->events |= BB_EVENT_FIRE;
        break;
    }
}

static void tick_player_energy(BBPlayer *player, float dt)
{
    bool was_boosting = player->boosting;
    double duration = player->boosting ? BB_ENERGY_BOOST_SECONDS : BB_ENERGY_CHARGE_SECONDS;
    player->energy_elapsed += (double)dt;
    if(player->energy_elapsed >= duration)
    {
        /* 남은 시간을 다음 구간으로 넘겨 프레임 속도에 따른 주기 오차를 없앤다. */
        player->energy_elapsed -= duration;
        player->boosting = !player->boosting;
    }
    if(player->boosting)
        player->energy = clamp(1.0f - (float)(player->energy_elapsed / BB_ENERGY_BOOST_SECONDS), 0.0f, 1.0f);
    else
        player->energy = clamp((float)(player->energy_elapsed / BB_ENERGY_CHARGE_SECONDS), 0.0f, 1.0f);
    /* 조작하지 않는 점프 관성도 가속 시작/종료 즉시 새 배율을 적용한다.
     * 수직 속도는 그대로 두어 점프 높이와 낙하 시간을 보존한다. */
    if(player->boosting != was_boosting)
        player->body.vx *= player->boosting ? 2.0f : 0.5f;
}

static void tick_player(BBGame *game, BBPlayer *player, BBInput input, float dt)
{
    float move;
    float speed_multiplier;
    if(!player->active)
        return;
    player->attack_timer = decrease(player->attack_timer, dt);
    player->fire_cooldown = decrease(player->fire_cooldown, dt);
    tick_player_energy(player, dt);
    speed_multiplier = player->boosting ? 2.0f : 1.0f;
    move = isfinite(input.move) ? clamp(input.move, -1.0f, 1.0f) : 0.0f;
    if(move != 0.0f)
        player->facing = move < 0.0f ? -1 : 1;
    if(player->body.grounded)
    {
        player->body.vx = move * BB_PLAYER_GROUND_SPEED * speed_multiplier;
        player->body.vy = 30.0f * dt;
        player->falling = true;
        if(input.jump)
        {
            player->body.vy = -18.0f;
            player->body.grounded = false;
            player->jump_start_y = player->body.y;
            player->falling = false;
            player->air_control = false;
            game->events |= BB_EVENT_JUMP;
        }
    }
    if(!player->body.grounded)
    {
        if(player->body.y > player->jump_start_y && player->body.vy > 0.0f)
            player->falling = true;
        if(player->falling)
        {
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
        fire_bubble(game, player);
}

static void pop_bubble(BBGame *game, BBBubble *bubble)
{
    if(bubble->popping)
        return;
    bubble->popping = true;
    bubble->pop_timer = 0.25f;
    bubble->body.vx = 0.0f;
    bubble->body.vy = 0.0f;
    game->events |= BB_EVENT_POP;
}

static void tick_bubble(BBGame *game, BBBubble *bubble, float dt, float center_x, float center_y)
{
    float vx;
    float vy;
    unsigned hit;
    if(!bubble->active)
        return;
    if(bubble->popping)
    {
        bubble->pop_timer -= dt;
        if(bubble->pop_timer <= 0.0f)
            bubble->active = false;
        return;
    }
    bubble->age += dt;
    if(bubble->age > 8.0f)
    {
        pop_bubble(game, bubble);
        return;
    }
    /* 고정 시간 간격에서 안정적으로 항력, 난수 힘, 상승 힘, 무리 중심을 적용한다. */
    bubble->body.vx += ((random_unit(game) * 2.0f - 1.0f) * 20.0f - bubble->body.vx) * dt;
    bubble->body.vy += ((random_unit(game) * 2.0f - 1.0f) * 20.0f - bubble->body.vy) * dt;
    if(bubble->age > 1.0f)
    {
        bubble->body.vx += (center_x - bubble->body.x) * 0.3f * dt;
        bubble->body.vy += (center_y - bubble->body.y) * 0.3f * dt;
        if(bubble->body.y > 11.0f)
            bubble->body.vy -= 3.0f * dt;
    }
    vx = bubble->body.vx;
    vy = bubble->body.vy;
    hit = move_body(game, &bubble->body, 0.75f, 0.75f, dt, false, false);
    if(hit & HIT_X)
        bubble->body.vx = -vx * 0.8f;
    if(hit & HIT_Y)
        bubble->body.vy = -vy * 0.8f;
    if(bubble->body.y < 0.75f)
    {
        bubble->body.y = 0.75f;
        bubble->body.vy = fabsf(bubble->body.vy) * 0.8f;
    }
    if(bubble->body.y > 29.0f)
        bubble->body.y -= 30.0f;
}

void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt)
{
    BBInput neutral = {0};
    float center_x;
    float center_y;
    int bubbles;
    int i;
    if(game == NULL)
        return;
    /* 이벤트는 틱 단위이고, 이전 틱의 미처리 충돌은 새 update가 덮어쓴다. */
    game->events = 0;
    game->bump_pending = false;
    if(!isfinite(dt) || dt <= 0.0f || dt > (1.0f / 30.0f))
        return;
    if(game->state == BB_STATE_MENU)
        return;
    ++game->ticks;
    if(game->state == BB_STATE_INTRO)
    {
        game->state_time += dt;
        if(game->state_time >= 7.0f)
            enter_play(game);
        return;
    }
    game->level_time += dt;
    for(i = 0; i < BB_MAX_PLAYERS; ++i)
        tick_player(game, &game->players[i], inputs != NULL ? inputs[i] : neutral, dt);
    center_x = 0.0f;
    center_y = 0.0f;
    bubbles = 0;
    for(i = 0; i < BB_MAX_BUBBLES; ++i)
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
    for(i = 0; i < BB_MAX_BUBBLES; ++i)
        tick_bubble(game, &game->bubbles[i], dt, center_x, center_y);
    game->bump_pending = true;
}

static void check_bubble_bump(BBGame *game)
{
    int b;
    int p;
    BBBubble *bubble;
    BBPlayer *player;
    float dx;
    float dy;
    for(b = 0; b < BB_MAX_BUBBLES; ++b)
    {
        bubble = &game->bubbles[b];
        if(!bubble->active || bubble->popping || bubble->age < 1.0f)
            continue;
        for(p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            player = &game->players[p];
            if(!player->active ||
               !circles_overlap(game, &bubble->body, 0.75f, &player->body, 0.95f))
                continue;
            dx = player->body.vx - bubble->body.vx;
            dy = player->body.vy - bubble->body.vy;
            if(sqrtf(dx * dx + dy * dy) + bubble->age >= 15.0f)
            {
                pop_bubble(game, bubble);
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

void bb_game_check_bump(BBGame *game)
{
    if(game == NULL || !game->bump_pending)
        return;
    /* 같은 update의 충돌을 중복 적용하지 않는다. */
    game->bump_pending = false;
    check_bubble_bump(game);
}
