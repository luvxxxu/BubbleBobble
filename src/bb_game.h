#ifndef BB_GAME_H
#define BB_GAME_H

#include <stdbool.h>

#define BB_WORLD_WIDTH 25
#define BB_WORLD_HEIGHT 18
#define BB_TILE_SIZE 32
#define BB_STAGE_COUNT 3
#define BB_PLATFORM_COUNT 4
#define BB_ENEMY_COUNT 3
#define BB_MAX_BUBBLES 8
#define BB_PLAYER_SIZE 0.8f
#define BB_ENEMY_SIZE 0.8f

typedef struct BBPlatform {
    float x, y, width;
} BBPlatform;

extern const BBPlatform bb_stage_platforms[BB_STAGE_COUNT][BB_PLATFORM_COUNT];

typedef enum BBState {
    BB_STATE_MENU,
    BB_STATE_PLAY,
    BB_STATE_CLEAR,
    BB_STATE_WON,
    BB_STATE_LOST
} BBState;

typedef struct BBInput {
    float move; /* -1=왼쪽, 0=정지, 1=오른쪽 */
    bool jump;
    bool fire;
} BBInput;

typedef struct BBPlayer {
    float x, y, vy; /* 왼쪽 위 좌표와 세로 속도 */
    int facing, lives;
    bool grounded;
    float invincible, fire_cooldown;
} BBPlayer;

typedef struct BBEnemy {
    float x, y;
    int direction, platform;
    bool alive;
} BBEnemy;

typedef struct BBBubble {
    float x, y, vx, age; /* x,y는 원의 중심 */
    bool active;
} BBBubble;

typedef struct BBGame {
    BBPlayer player;
    BBEnemy enemies[BB_ENEMY_COUNT];
    BBBubble bubbles[BB_MAX_BUBBLES];
    int stage; /* 0, 1, 2는 화면에 1, 2, 3으로 표시한다. */
    BBState state;
} BBGame;

void bb_game_init(BBGame *game);
void bb_game_start(BBGame *game);
void bb_game_next_stage(BBGame *game);
void bb_game_menu(BBGame *game);
void bb_game_update(BBGame *game, BBInput input, float dt);
int bb_game_enemies_left(const BBGame *game);

#endif
