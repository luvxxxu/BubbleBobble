#include "bb_game.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* 벽과 천장/바닥 충돌을 각각 참/거짓으로 기록한다. */
typedef struct BBHits {
    bool x;
    bool y;
} BBHits;

static float clamp(float value, float low, float high)
{
    if(value < low) return low;
    if(value > high) return high;
    return value;
}

static float decrease(float value, float dt)
{
    value -= dt;
    return value < 0.0f ? 0.0f : value;
}

static float random_unit(BBGame *game)
{
    /* 곱셈과 덧셈으로 다음 난수를 만든다. 같은 시드면 테스트도 재현된다.
     * unsigned 32비트 정수의 넘침은 C에서 정의된 순환 연산이다. */
    game->rng = game->rng * UINT32_C(1664525) + UINT32_C(1013904223);
    return (float)(game->rng % 10000u) / 10000.0f;
}

static int tile_at(const BBGame *game, int x, int y)
{
    /* 맵 밖은 빈 공간이다. 아래쪽 순환 통로와 맵 위 진입에 벽을 만들지 않는다. */
    if(x < 0 || x >= BB_MAP_WIDTH || y < 0 || y >= BB_MAP_HEIGHT)
        return 0;
    return game->maps[game->level][y][x];
}

static bool rects_overlap(const BBBody *a, float aw, float ah,
                          const BBBody *b, float bw, float bh)
{
    return fabsf(a->x - b->x) < aw + bw && fabsf(a->y - b->y) < ah + bh;
}

static bool circles_overlap(const BBBody *a, float ar,
                            const BBBody *b, float br)
{
    float dx = a->x - b->x;
    float dy = a->y - b->y;
    float radius = ar + br;
    return dx * dx + dy * dy < radius * radius;
}

/* 타일 값 2는 내려오는 물체의 윗면만 받는다.
 * 축을 따로 판정하므로 일방 통과 발판을 아래에서 지나갈 수 있다. */
static BBHits move_body(const BBGame *game, BBBody *body, float hw, float hh,
                          float dt, bool one_way, bool wrap)
{
    BBHits hit = {false, false};
    float old_x = body->x;
    float old_y = body->y;
    /* 가로 이동 후 벽에 닿았는지 검사한다. */
    body->x += body->vx * dt;
    for(int y = (int)floorf(body->y - hh + 0.0001f); y <= (int)floorf(body->y + hh - 0.0001f); ++y)
    {
        for(int x = (int)floorf(body->x - hw + 0.0001f); x <= (int)floorf(body->x + hw - 0.0001f); ++x)
        {
            if(tile_at(game, x, y) != 3)
                continue;
            if(body->vx > 0.0f && old_x + hw <= (float)x + 0.001f)
            {
                body->x = fminf(body->x, (float)x - hw);
                hit.x = true;
            }
            else if(body->vx < 0.0f && old_x - hw >= (float)(x + 1) - 0.001f)
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
                body->y = fminf(body->y, (float)y - hh);
                body->grounded = true;
                hit.y = true;
            }
            else if(tile == 3 && body->vy < 0.0f && old_y - hh >= (float)(y + 1) - 0.001f)
            {
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

static void spawn_enemy(BBGame *game, int index, BBEnemyType type, float x, float y, float delay)
{
    game->enemies[index] = (BBEnemy){
        .body.x = x, .type = type, .state = BB_ENEMY_SPAWNING,
        .active = true, .facing = random_unit(game) < 0.5f ? -1 : 1,
        .spawn_delay = delay, .spawn_y = y
    };
}

static void enter_level(BBGame *game, int level)
{
    game->level = level;
    game->level_time = 0.0f;
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
        BBEnemyType type = (BBEnemyType)((i + level) % BB_ENEMY_TYPE_COUNT);
        float x = 16.0f + ((float)i - (float)(count - 1) * 0.5f) * 3.0f;
        float y = i % 2 == 0 ? 8.0f : 4.0f;
        spawn_enemy(game, i, type, x, y, (float)i * 0.45f);
    }
}

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
    game->ticks = 0;
    memset(&game->events, 0, sizeof game->events);
    game->level = 0;
    game->won = false;
    shuffle_pickups(game);
    for(int i = 0; i < BB_MAX_PLAYERS; ++i)
    {
        BBPlayer *player = &game->players[i];
        place_player(player, i);
        player->active = i < player_count;
        if(player->active) player->lives = BB_STARTING_LIVES;
    }
}

void bb_game_menu(BBGame *game)
{
    if(game == NULL)
        return;
    game->state = BB_STATE_MENU;
    game->state_time = 0.0f;
    memset(&game->events, 0, sizeof game->events);
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
        *bubble = (BBBubble){
            .body = {.x = clamp(x, 0.75f, 31.25f), .y = y, .vx = speed},
            .active = true, .owner = owner, .captured_enemy = -1
        };
        player->fire_cooldown = 0.4f;
        player->attack_timer = 0.2f;
        game->events.fire = true;
        break;
    }
}

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
    float move = isfinite(input.move) ? clamp(input.move, -1.0f, 1.0f) : 0.0f;
    if(move < 0.0f) player->facing = -1;
    if(move > 0.0f) player->facing = 1;
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
            game->events.jump = true;
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
        BBHits hit = move_body(game, &enemy->body, 0.95f, 0.95f, dt, true, true);
        if(hit.x)
            enemy->body.vx = -vx;
        if(hit.y)
            enemy->body.vy = -vy;
        if((hit.x || hit.y) && enemy->bounce_timer >= 0.5f)
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
    game->events.pop = true;
}

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
    BBHits hit = move_body(game, &bubble->body, 0.75f, 0.75f, dt, false, false);
    if(hit.x)
        bubble->body.vx = -vx * 0.8f;
    if(hit.y)
        bubble->body.vy = -vy * 0.8f;
    if(bubble->body.y < 0.75f)
    {
        bubble->body.y = 0.75f;
        bubble->body.vy = fabsf(bubble->body.vy) * 0.8f;
    }
    if(bubble->body.y > 29.0f)
        bubble->body.y -= 30.0f;
}

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
            if(circles_overlap(&boulder->body, 0.75f, &game->players[p].body, 0.95f))
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
        if(game->state_time >= 7.0f)
            enter_level(game, 0);
        return;
    }
    game->level_time += dt;
    for(int i = 0; i < game->player_count; ++i)
        update_player(game, &game->players[i], i, inputs != NULL ? inputs[i] : neutral, dt);
    for(int i = 0; i < BB_MAX_ENEMIES; ++i)
        update_enemy(game, &game->enemies[i], i, dt);
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
        update_bubble(game, &game->bubbles[i], dt, center_x, center_y);
    for(int i = 0; i < BB_MAX_BOULDERS; ++i)
        update_boulder(game, &game->boulders[i], dt);
    for(int i = 0; i < BB_MAX_PICKUPS; ++i)
        update_pickup(game, &game->pickups[i], dt);
    check_collisions_and_progress(game);
}
