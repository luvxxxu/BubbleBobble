#include "bb_game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 원본 PlayerState, ZenChan, Maita, CaptureBubble, LevelState의 값을 사용한다.
 * 충돌 반응은 C++ Box2D 대신 C 타일 판정기로 처리한다. */
enum { HIT_X = 1, HIT_Y = 2 };

static float minimum(float a, float b) { return a < b ? a : b; }
static float maximum(float a, float b) { return a > b ? a : b; }
static float clamp(float a, float lo, float hi) { return minimum(maximum(a, lo), hi); }
static float decrease(float value, float dt) { return maximum(0.0f, value - dt); }

static float random_unit(BBGame *game)
{
    uint32_t value = game->rng;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    game->rng = value;
    return (float)(value >> 8) / 16777216.0f;
}

static int tile_at(const BBGame *game, int x, int y)
{
    if(x < 0 || x >= BB_MAP_WIDTH || y < 0 || y >= BB_MAP_HEIGHT)
        return 0;
    return game->maps[game->level][y][x];
}

static bool rects_overlap(const BBGame *game, const BBBody *a, float aw, float ah,
                          const BBBody *b, float bw, float bh)
{
    if(game->collision.recs != NULL)
        return game->collision.recs(a->x, a->y, aw, ah, b->x, b->y, bw, bh);
    return fabsf(a->x - b->x) < aw + bw && fabsf(a->y - b->y) < ah + bh;
}

static bool circles_overlap(const BBGame *game, const BBBody *a, float ar,
                            const BBBody *b, float br)
{
    if(game->collision.circles != NULL)
        return game->collision.circles(a->x, a->y, ar, b->x, b->y, br);
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float radius = ar + br;
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
    body->x += body->vx * dt;
    for(int y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh - 0.0001f); ++y)
    {
        for(int x = (int)floorf(body->x - hw + 0.0001f); x <= (int)floorf(body->x + hw - 0.0001f); ++x)
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
    for(int y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh + 0.0001f); ++y)
    {
        for(int x = (int)floorf(body->x - hw + 0.0001f); x <= (int)floorf(body->x + hw - 0.0001f); ++x)
        {
            int tile = tile_at(game, x, y);
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
    if(wrap && body->y > (float)BB_MAP_HEIGHT + 1.0f)
        body->y -= (float)BB_MAP_HEIGHT + 2.0f;
    return hit;
}

static void place_player(BBPlayer *player, int index)
{
    memset(&player->body, 0, sizeof player->body);
    player->body.x = index == 0 ? 4.0f : 28.0f;
    player->body.y = 24.0f;
    player->state = BB_PLAYER_NORMAL;
    player->facing = index == 0 ? 1 : -1;
    player->falling = true;
    player->air_control = true;
    player->jump_start_y = 24.0f;
    player->attack_timer = 0.0f;
    player->fire_cooldown = 0.0f;
}

static void spawn_enemy(BBGame *game, int index, BBEnemyType type, float x, float y, float delay, bool controlled)
{
    BBEnemy *enemy = &game->enemies[index];
    memset(enemy, 0, sizeof *enemy);
    enemy->body.x = x;
    enemy->type = type;
    enemy->state = BB_ENEMY_SPAWNING;
    enemy->active = true;
    enemy->controlled = controlled;
    enemy->facing = random_unit(game) < 0.5f ? -1 : 1;
    enemy->spawn_delay = delay;
    enemy->spawn_y = y;
}

static void enter_level(BBGame *game, int level)
{
    game->level = level;
    game->level_time = 0.0f;
    game->state_time = 0.0f;
    game->state = BB_STATE_PLAY;
    game->events |= BB_EVENT_LEVEL;
    memset(game->enemies, 0, sizeof game->enemies);
    memset(game->bubbles, 0, sizeof game->bubbles);
    memset(game->boulders, 0, sizeof game->boulders);
    memset(game->pickups, 0, sizeof game->pickups);
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        if(game->players[i].active && game->players[i].state != BB_PLAYER_OUT)
        {
            place_player(&game->players[i], i);
            game->players[i].invulnerable = 3.0f;
        }
    }
    if(game->mode == BB_MODE_VERSUS)
    {
        BBEnemyType type = random_unit(game) < 0.5f ? BB_ENEMY_ZENCHAN : BB_ENEMY_MAITA;
        spawn_enemy(game, 0, type, 16.0f, 0.0f, 0.0f, true);
        game->enemies[0].state = BB_ENEMY_FALLING;
        return;
    }
    if(level == 0)
    {
        spawn_enemy(game, 0, BB_ENEMY_ZENCHAN, 14.0f, 8.0f, 0.0f, false);
        spawn_enemy(game, 1, BB_ENEMY_MAITA, 16.0f, 8.0f, 1.0f, false);
        spawn_enemy(game, 2, BB_ENEMY_ZENCHAN, 18.0f, 8.0f, 2.0f, false);
    }
    else if(level == 1)
    {
        spawn_enemy(game, 0, BB_ENEMY_ZENCHAN, 12.0f, 10.0f, 0.0f, false);
        spawn_enemy(game, 1, BB_ENEMY_ZENCHAN, 15.0f, 4.0f, 0.0f, false);
        spawn_enemy(game, 2, BB_ENEMY_ZENCHAN, 17.0f, 4.0f, 0.0f, false);
        spawn_enemy(game, 3, BB_ENEMY_ZENCHAN, 20.0f, 10.0f, 0.0f, false);
    }
    else
    {
        spawn_enemy(game, 0, BB_ENEMY_ZENCHAN, 10.0f, 10.0f, 2.0f, false);
        spawn_enemy(game, 1, BB_ENEMY_ZENCHAN, 12.0f, 6.0f, 0.0f, false);
        spawn_enemy(game, 2, BB_ENEMY_ZENCHAN, 20.0f, 6.0f, 2.0f, false);
        spawn_enemy(game, 3, BB_ENEMY_ZENCHAN, 22.0f, 10.0f, 0.0f, false);
    }
}

void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed)
{
    if(game == NULL)
        return;
    memset(game, 0, sizeof *game);
    if(tiles != NULL)
        memcpy(game->maps, tiles, sizeof game->maps);
    game->rng = seed != 0 ? seed : 0xB0BB1EU;
    game->state = BB_STATE_MENU;
}

void bb_game_set_collision_backend(BBGame *game, BBCollisionBackend backend)
{
    if(game != NULL)
        game->collision = backend;
}

void bb_game_start(BBGame *game, BBMode mode)
{
    if(game == NULL || mode < BB_MODE_SOLO || mode > BB_MODE_VERSUS)
        return;
    memset(game->players, 0, sizeof game->players);
    memset(game->enemies, 0, sizeof game->enemies);
    memset(game->bubbles, 0, sizeof game->bubbles);
    memset(game->boulders, 0, sizeof game->boulders);
    memset(game->pickups, 0, sizeof game->pickups);
    game->mode = mode;
    game->state = BB_STATE_INTRO;
    game->state_time = 0.0f;
    game->level_time = 0.0f;
    game->ticks = 0;
    game->events = 0;
    game->level = 0;
    game->won = false;
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        BBPlayer *player = &game->players[i];
        place_player(player, i);
        player->active = i == 0 || mode == BB_MODE_COOP;
        player->lives = 3;
    }
}

void bb_game_menu(BBGame *game)
{
    if(game == NULL)
        return;
    game->state = BB_STATE_MENU;
    game->state_time = 0.0f;
    game->events = 0;
}

void bb_game_skip_intro(BBGame *game)
{
    if(game != NULL && game->state == BB_STATE_INTRO)
        enter_level(game, 0);
}

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

static void damage_player(BBGame *game, BBPlayer *player)
{
    if(!player->active || player->state != BB_PLAYER_NORMAL || player->invulnerable > 0.0f)
        return;
    --player->lives;
    player->state = BB_PLAYER_DEAD;
    player->death_timer = 1.5f;
    player->body.vx = 0.0f;
    player->body.vy = 0.0f;
    game->events |= BB_EVENT_DEATH;
}

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
            int tile = tile_at(game, (int)floorf(cast_x), (int)floorf(y));
            if(tile == 2 || tile == 3)
            {
                float boundary = player->facing > 0 ? floorf(cast_x) : floorf(cast_x) + 1.0f;
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
        bubble->owner = owner;
        bubble->captured_enemy = -1;
        player->fire_cooldown = 0.4f;
        player->attack_timer = 0.2f;
        game->events |= BB_EVENT_FIRE;
        break;
    }
}

static void tick_player(BBGame *game, BBPlayer *player, int index, BBInput input, float dt)
{
    if(!player->active || player->state == BB_PLAYER_OUT)
        return;
    player->invulnerable = decrease(player->invulnerable, dt);
    player->attack_timer = decrease(player->attack_timer, dt);
    player->fire_cooldown = decrease(player->fire_cooldown, dt);
    if(player->state == BB_PLAYER_DEAD)
    {
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
    float move = isfinite(input.move) ? clamp(input.move, -1.0f, 1.0f) : 0.0f;
    if(move != 0.0f)
        player->facing = move < 0.0f ? -1 : 1;
    if(player->body.grounded)
    {
        player->body.vx = move * 8.0f;
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
            player->body.vx = move * 4.0f;
            player->body.vy = 6.0f;
        }
        else
        {
            /* 플레이어가 반대 방향으로 움직일 때까지 관성을 유지한다. */
            if(player->body.vx == 0.0f || player->body.vx * move < 0.0f)
                player->air_control = true;
            if(player->air_control)
                player->body.vx = move * 4.0f;
            player->body.vy += 30.0f * dt;
        }
    }
    move_body(game, &player->body, 0.9f, 0.975f, dt, true, true);
    if(input.fire)
        fire_bubble(game, player, index);
}

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

static bool platform_above(const BBGame *game, const BBBody *body)
{
    int column = (int)floorf(body->x);
    int start = (int)floorf(body->y - 1.0f);
    for(int y = start; y > start - 5; --y)
        if(tile_at(game, column, y) >= 2)
            return true;
    return false;
}

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

static void spawn_pickup(BBGame *game, const BBEnemy *enemy)
{
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
    {
        if(game->pickups[i].active)
            continue;
        memset(&game->pickups[i], 0, sizeof game->pickups[i]);
        game->pickups[i].body = enemy->body;
        game->pickups[i].active = true;
        game->pickups[i].type = enemy->type == BB_ENEMY_ZENCHAN ? BB_PICKUP_WATERMELON : BB_PICKUP_FRIES;
        game->pickups[i].body.vx = 0.0f;
        game->pickups[i].body.vy = 9.0f;
        return;
    }
}

static void tick_enemy(BBGame *game, BBEnemy *enemy, BBInput input, float dt)
{
    if(!enemy->active || enemy->state == BB_ENEMY_CAPTURED)
        return;
    enemy->age += dt;
    if(enemy->state == BB_ENEMY_SPAWNING)
    {
        if(enemy->age <= enemy->spawn_delay)
            return;
        float progress = clamp((enemy->age - enemy->spawn_delay) / 2.0f, 0.0f, 1.0f);
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
        enemy->bounce_timer += dt;
        unsigned hit = move_body(game, &enemy->body, 0.95f, 0.95f, dt, true, true);
        if(hit & HIT_X)
            enemy->body.vx = -vx;
        if(hit & HIT_Y)
            enemy->body.vy = -vy;
        if(hit != 0 && enemy->bounce_timer >= 0.5f)
        {
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
    enemy->jump_timer += dt;
    enemy->turn_timer += dt;
    enemy->throw_timer += dt;
    float move = (float)enemy->facing;
    bool jump = false;
    if(enemy->controlled)
    {
        move = isfinite(input.move) ? clamp(input.move, -1.0f, 1.0f) : 0.0f;
        if(move != 0.0f)
            enemy->facing = move < 0.0f ? -1 : 1;
        jump = input.jump;
        if(input.fire)
        {
            if(enemy->type == BB_ENEMY_ZENCHAN)
                enemy->angry = !enemy->angry;
            else
                throw_boulder(game, enemy);
        }
    }
    else
    {
        const BBPlayer *target = closest_player(game, &enemy->body);
        if(target != NULL)
        {
            float dx = target->body.x - enemy->body.x;
            if(enemy->turn_timer >= 2.0f + (float)((unsigned)(enemy - game->enemies) % 4) * 0.5f && fabsf(dx) > 5.0f)
            {
                enemy->facing = dx < 0.0f ? -1 : 1;
                enemy->turn_timer = 0.0f;
            }
            jump = target->body.y < enemy->body.y - 1.0f && dx * (float)enemy->facing >= 0.0f &&
                   enemy->jump_timer > 1.5f && platform_above(game, &enemy->body);
        }
        move = (float)enemy->facing;
        if(enemy->type == BB_ENEMY_MAITA && enemy->throw_timer >= 5.0f)
            throw_boulder(game, enemy);
    }
    float multiplier = enemy->angry && enemy->type == BB_ENEMY_ZENCHAN ? 1.8f : 1.0f;
    float speed = enemy->type == BB_ENEMY_ZENCHAN ? 8.0f : 6.0f;
    if(enemy->body.grounded)
    {
        enemy->state = BB_ENEMY_WALKING;
        enemy->body.vx = move * speed * multiplier;
        enemy->body.vy = 9.81f * dt;
        if(jump)
        {
            enemy->body.vx *= enemy->type == BB_ENEMY_ZENCHAN ? 0.2f : 0.5f;
            enemy->body.vy = enemy->type == BB_ENEMY_ZENCHAN ? -10.0f : -12.0f;
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
        enemy->body.vx = 0.0f;
        enemy->body.vy = (enemy->type == BB_ENEMY_ZENCHAN ? 5.0f : 8.0f) * multiplier;
    }
    unsigned hit = move_body(game, &enemy->body, 0.9f, 0.95f, dt, true, true);
    if((hit & HIT_X) && !enemy->controlled)
    {
        enemy->facing = -enemy->facing;
        enemy->turn_timer = 0.0f;
    }
}

static void pop_bubble(BBGame *game, BBBubble *bubble, bool release)
{
    if(bubble->popping)
        return;
    bubble->popping = true;
    bubble->releasing = release;
    bubble->pop_timer = 0.25f;
    bubble->body.vx = 0.0f;
    bubble->body.vy = 0.0f;
    game->events |= BB_EVENT_POP;
}

static void finish_pop(BBGame *game, BBBubble *bubble)
{
    if(bubble->captured_enemy >= 0 && bubble->captured_enemy < BB_MAX_ENEMIES)
    {
        BBEnemy *enemy = &game->enemies[bubble->captured_enemy];
        enemy->body = bubble->body;
        enemy->body.grounded = false;
        enemy->age = 0.0f;
        if(bubble->releasing)
        {
            enemy->state = BB_ENEMY_FALLING;
            if(enemy->type == BB_ENEMY_ZENCHAN)
                enemy->angry = true;
        }
        else
        {
            float angle = (50.0f + 20.0f * random_unit(game)) * 0.01745329252f;
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

static void tick_bubble(BBGame *game, BBBubble *bubble, float dt, float center_x, float center_y)
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
    if(bubble->age > 8.0f)
    {
        pop_bubble(game, bubble, true);
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
    float vx = bubble->body.vx;
    float vy = bubble->body.vy;
    unsigned hit = move_body(game, &bubble->body, 0.75f, 0.75f, dt, false, false);
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

static void tick_boulder(BBGame *game, BBBoulder *boulder, float dt)
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
    unsigned hit = move_body(game, &boulder->body, 0.75f, 0.75f, dt, true, true);
    if(hit & HIT_X)
        boulder->body.vx = -vx * 0.9f;
    if(hit & HIT_Y)
        boulder->body.vy = -vy * 0.9f;
}

static void tick_pickup(BBGame *game, BBPickup *pickup, float dt)
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

void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt)
{
    if(game == NULL)
        return;
    game->events = 0;
    game->bump_pending = false;
    if(!isfinite(dt) || dt <= 0.0f || dt > (1.0f / 30.0f))
        return;
    if(game->state == BB_STATE_MENU || game->state == BB_STATE_SCORE)
        return;
    ++game->ticks;
    game->state_time += dt;
    if(game->state == BB_STATE_INTRO)
    {
        if(game->state_time >= 7.0f)
            enter_level(game, 0);
        return;
    }
    game->level_time += dt;
    BBInput neutral = {0};
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
        tick_player(game, &game->players[i], i, inputs != NULL ? inputs[i] : neutral, dt);
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
        tick_enemy(game, &game->enemies[i], inputs != NULL ? inputs[1] : neutral, dt);
    float center_x = 0.0f;
    float center_y = 0.0f;
    int bubbles = 0;
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
        tick_bubble(game, &game->bubbles[i], dt, center_x, center_y);
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
        tick_boulder(game, &game->boulders[i], dt);
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        tick_pickup(game, &game->pickups[i], dt);
    game->bump_pending = true;
}

static void check_bubble_bump(BBGame *game)
{
    for(int b = 0; b < BB_MAX_BUBBLES; ++b)
    {
        BBBubble *bubble = &game->bubbles[b];
        if(!bubble->active || bubble->popping)
            continue;
        if(bubble->captured_enemy < 0)
        {
            for(int e = 0; e < BB_MAX_ENEMIES; ++e)
            {
                BBEnemy *enemy = &game->enemies[e];
                if(!enemy->active || enemy->state == BB_ENEMY_SPAWNING ||
                   enemy->state == BB_ENEMY_CAPTURED || enemy->state == BB_ENEMY_DEAD)
                    continue;
                if(circles_overlap(game, &bubble->body, 0.75f, &enemy->body, 0.95f))
                {
                    bubble->captured_enemy = e;
                    enemy->state = BB_ENEMY_CAPTURED;
                    break;
                }
            }
        }
        if(bubble->age < 1.0f)
            continue;
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
        {
            BBPlayer *player = &game->players[p];
            if(!player->active || player->state != BB_PLAYER_NORMAL ||
               !circles_overlap(game, &bubble->body, 0.75f, &player->body, 0.95f))
                continue;
            float dx = player->body.vx - bubble->body.vx;
            float dy = player->body.vy - bubble->body.vy;
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

static void check_boulder_bump(BBGame *game)
{
    for(int b = 0; b < BB_MAX_BOULDERS; ++b)
    {
        BBBoulder *boulder = &game->boulders[b];
        if(!boulder->active)
            continue;
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            if(circles_overlap(game, &boulder->body, 0.75f, &game->players[p].body, 0.95f))
                damage_player(game, &game->players[p]);
    }
}

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
               circles_overlap(game, &pickup->body, 0.95f, &player->body, 0.95f))
            {
                player->score += pickup->type == BB_PICKUP_WATERMELON ? 100 : 200;
                pickup->active = false;
                game->events |= BB_EVENT_PICKUP;
                break;
            }
        }
    }
}

void bb_game_check_bump(BBGame *game)
{
    if(game == NULL || !game->bump_pending)
        return;
    game->bump_pending = false;
    check_bubble_bump(game);
    /* 버블 포획을 먼저 처리하면 같은 틱에 적이 플레이어를 맞히지 않는다. */
    for(int e = 0; e < BB_MAX_ENEMIES; ++e)
    {
        BBEnemy *enemy = &game->enemies[e];
        if(!enemy->active || enemy->state == BB_ENEMY_SPAWNING || enemy->state == BB_ENEMY_CAPTURED || enemy->state == BB_ENEMY_DEAD)
            continue;
        for(int p = 0; p < BB_MAX_PLAYERS; ++p)
            if(rects_overlap(game, &enemy->body, 0.9f, 0.95f,
                             &game->players[p].body, 0.9f, 0.975f))
                damage_player(game, &game->players[p]);
    }
    check_boulder_bump(game);
    check_pickup_bump(game);

    bool living_player = false;
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
        if(game->players[i].active && game->players[i].state != BB_PLAYER_OUT)
            living_player = true;
    if(!living_player)
    {
        game->state = BB_STATE_SCORE;
        game->state_time = 0.0f;
        game->won = false;
        return;
    }
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
