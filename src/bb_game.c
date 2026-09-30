#include "bb_game.h"

#include <math.h>
#include <string.h>

/* 바닥과 세 발판의 위치만 바꿔 세 스테이지를 만든다. */
const BBPlatform bb_stage_platforms[BB_STAGE_COUNT][BB_PLATFORM_COUNT] = {
    {{0.0f, 16.0f, 25.0f}, {3.0f, 12.0f, 8.0f},
     {11.0f, 9.0f, 8.0f}, {17.0f, 6.0f, 8.0f}},
    {{0.0f, 16.0f, 25.0f}, {16.0f, 12.0f, 7.0f},
     {9.0f, 9.0f, 7.0f}, {2.0f, 6.0f, 7.0f}},
    {{0.0f, 16.0f, 25.0f}, {4.0f, 12.0f, 6.0f},
     {12.0f, 8.0f, 6.0f}, {6.0f, 4.0f, 6.0f}}
};

static float clamp(float value, float low, float high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static bool overlaps(float ax, float ay, float aw, float ah,
                     float bx, float by, float bw, float bh)
{
    return ax < bx + bw && ax + aw > bx && ay < by + bh && ay + ah > by;
}

void bb_game_init(BBGame *game)
{
    if (!game) return;
    memset(game, 0, sizeof *game);
    game->state = BB_STATE_MENU;
}

static void enter_stage(BBGame *game)
{
    const BBPlatform *platforms = bb_stage_platforms[game->stage];
    int i;
    memset(game->enemies, 0, sizeof game->enemies);
    memset(game->bubbles, 0, sizeof game->bubbles);
    game->state = BB_STATE_PLAY;
    game->player.x = 1.5f;
    game->player.y = platforms[0].y - BB_PLAYER_SIZE;
    game->player.vy = 0.0f;
    game->player.facing = 1;
    game->player.grounded = true;
    game->player.invincible = 1.0f;
    game->player.fire_cooldown = 0.0f;
    for (i = 0; i < BB_ENEMY_COUNT; ++i) {
        BBEnemy *enemy = &game->enemies[i];
        enemy->platform = i + 1;
        enemy->x = platforms[enemy->platform].x + platforms[enemy->platform].width * 0.5f - BB_ENEMY_SIZE * 0.5f;
        enemy->y = platforms[enemy->platform].y - BB_ENEMY_SIZE;
        enemy->direction = i % 2 ? -1 : 1;
        enemy->alive = true;
    }
}

void bb_game_start(BBGame *game)
{
    if (!game) return;
    memset(game, 0, sizeof *game);
    game->player.lives = 3;
    enter_stage(game);
}

void bb_game_next_stage(BBGame *game)
{
    if (!game || game->state != BB_STATE_CLEAR || game->stage + 1 >= BB_STAGE_COUNT) return;
    ++game->stage;
    enter_stage(game);
}

void bb_game_menu(BBGame *game)
{
    if (game) game->state = BB_STATE_MENU;
}

int bb_game_enemies_left(const BBGame *game)
{
    int count = 0;
    int i;
    if (!game) return 0;
    for (i = 0; i < BB_ENEMY_COUNT; ++i)
        if (game->enemies[i].alive) ++count;
    return count;
}

static void fire_bubble(BBGame *game)
{
    BBPlayer *player = &game->player;
    int i;
    if (player->fire_cooldown > 0.0f) return;
    for (i = 0; i < BB_MAX_BUBBLES; ++i) {
        BBBubble *bubble = &game->bubbles[i];
        if (bubble->active) continue;
        bubble->x = player->x + (player->facing > 0 ? BB_PLAYER_SIZE + 0.2f : -0.2f);
        bubble->y = player->y + BB_PLAYER_SIZE * 0.5f;
        bubble->vx = player->facing * 10.0f;
        bubble->age = 0.0f;
        bubble->active = true;
        player->fire_cooldown = 0.3f;
        return;
    }
}

static void update_player(BBGame *game, BBInput input, float dt)
{
    BBPlayer *player = &game->player;
    float old_bottom;
    int i;

    player->invincible = fmaxf(0.0f, player->invincible - dt);
    player->fire_cooldown = fmaxf(0.0f, player->fire_cooldown - dt);
    if (!isfinite(input.move)) input.move = 0.0f;
    input.move = clamp(input.move, -1.0f, 1.0f);
    if (input.move != 0.0f) player->facing = input.move < 0.0f ? -1 : 1;
    player->x = clamp(player->x + input.move * 6.0f * dt,
                      0.0f, BB_WORLD_WIDTH - BB_PLAYER_SIZE);

    if (input.jump && player->grounded) {
        player->vy = -17.0f;
        player->grounded = false;
    }
    old_bottom = player->y + BB_PLAYER_SIZE;
    player->vy += 30.0f * dt;
    player->y += player->vy * dt;
    player->grounded = false;

    if (player->vy >= 0.0f) {
        for (i = 0; i < BB_PLATFORM_COUNT; ++i) {
            const BBPlatform *platform = &bb_stage_platforms[game->stage][i];
            if (player->x + BB_PLAYER_SIZE > platform->x &&
                player->x < platform->x + platform->width &&
                old_bottom <= platform->y + 0.001f &&
                player->y + BB_PLAYER_SIZE >= platform->y) {
                player->y = platform->y - BB_PLAYER_SIZE;
                player->vy = 0.0f;
                player->grounded = true;
            }
        }
    }
    if (player->y < 0.0f) {
        player->y = 0.0f;
        player->vy = 0.0f;
    }
    if (input.fire) fire_bubble(game);
}

static void update_enemies(BBGame *game, float dt)
{
    int i;
    for (i = 0; i < BB_ENEMY_COUNT; ++i) {
        BBEnemy *enemy = &game->enemies[i];
        const BBPlatform *platform;
        float left, right;
        if (!enemy->alive) continue;
        platform = &bb_stage_platforms[game->stage][enemy->platform];
        left = platform->x;
        right = platform->x + platform->width - BB_ENEMY_SIZE;
        enemy->x += enemy->direction * (1.2f + 0.3f * game->stage) * dt;
        if (enemy->x < left) {
            enemy->x = left;
            enemy->direction = 1;
        } else if (enemy->x > right) {
            enemy->x = right;
            enemy->direction = -1;
        }
    }
}

static void update_bubbles(BBGame *game, float dt)
{
    int i, j;
    for (i = 0; i < BB_MAX_BUBBLES; ++i) {
        BBBubble *bubble = &game->bubbles[i];
        if (!bubble->active) continue;
        bubble->x += bubble->vx * dt;
        bubble->age += dt;
        if (bubble->x < 0.0f || bubble->x > BB_WORLD_WIDTH || bubble->age > 1.5f) {
            bubble->active = false;
            continue;
        }
        for (j = 0; j < BB_ENEMY_COUNT; ++j) {
            BBEnemy *enemy = &game->enemies[j];
            if (enemy->alive && overlaps(bubble->x - 0.25f, bubble->y - 0.25f, 0.5f, 0.5f,
                                         enemy->x, enemy->y, BB_ENEMY_SIZE, BB_ENEMY_SIZE)) {
                enemy->alive = false;
                bubble->active = false;
                break;
            }
        }
    }
}

static void check_player_hit(BBGame *game)
{
    BBPlayer *player = &game->player;
    int i;
    if (player->invincible > 0.0f) return;
    for (i = 0; i < BB_ENEMY_COUNT; ++i) {
        const BBEnemy *enemy = &game->enemies[i];
        if (!enemy->alive || !overlaps(player->x, player->y, BB_PLAYER_SIZE, BB_PLAYER_SIZE,
                                       enemy->x, enemy->y, BB_ENEMY_SIZE, BB_ENEMY_SIZE)) continue;
        --player->lives;
        if (player->lives == 0) {
            game->state = BB_STATE_LOST;
        } else {
            player->x = 1.5f;
            player->y = bb_stage_platforms[game->stage][0].y - BB_PLAYER_SIZE;
            player->vy = 0.0f;
            player->grounded = true;
            player->invincible = 1.5f;
        }
        return;
    }
}

void bb_game_update(BBGame *game, BBInput input, float dt)
{
    if (!game || game->state != BB_STATE_PLAY || !isfinite(dt) || dt <= 0.0f) return;
    dt = fminf(dt, 1.0f / 30.0f);
    update_player(game, input, dt);
    update_enemies(game, dt);
    update_bubbles(game, dt);
    if (bb_game_enemies_left(game) == 0)
        game->state = game->stage == BB_STAGE_COUNT - 1 ? BB_STATE_WON : BB_STATE_CLEAR;
    else check_player_hit(game);
}
