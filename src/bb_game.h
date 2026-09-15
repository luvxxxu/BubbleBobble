#ifndef BB_GAME_H
#define BB_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define BB_MAP_WIDTH 32
#define BB_MAP_HEIGHT 28
#define BB_LEVEL_COUNT 3
#define BB_MAX_PLAYERS 2
#define BB_MAX_ENEMIES 16
#define BB_MAX_BUBBLES 64
#define BB_MAX_BOULDERS 32
#define BB_MAX_PICKUPS 32
#define BB_FIXED_DT (1.0f / 120.0f)

typedef enum BBMode { BB_MODE_SOLO, BB_MODE_COOP, BB_MODE_VERSUS } BBMode;
typedef enum BBState { BB_STATE_MENU, BB_STATE_INTRO, BB_STATE_PLAY, BB_STATE_CLEAR, BB_STATE_SCORE } BBState;
typedef enum BBPlayerState { BB_PLAYER_NORMAL, BB_PLAYER_DEAD, BB_PLAYER_OUT } BBPlayerState;
typedef enum BBEnemyType { BB_ENEMY_ZENCHAN, BB_ENEMY_MAITA } BBEnemyType;
typedef enum BBEnemyState {
    BB_ENEMY_SPAWNING, BB_ENEMY_WALKING, BB_ENEMY_FALLING,
    BB_ENEMY_JUMPING, BB_ENEMY_CAPTURED, BB_ENEMY_DEAD
} BBEnemyState;
typedef enum BBPickupType { BB_PICKUP_WATERMELON, BB_PICKUP_FRIES } BBPickupType;
enum BBEvent {
    BB_EVENT_FIRE = 1u << 0, BB_EVENT_JUMP = 1u << 1,
    BB_EVENT_DEATH = 1u << 2, BB_EVENT_PICKUP = 1u << 3,
    BB_EVENT_POP = 1u << 4, BB_EVENT_LEVEL = 1u << 5
};

/* 위치는 타일 단위의 중심 좌표이며, 원점은 왼쪽 위이고 y축은 아래로 증가한다. */
typedef struct BBBody { float x, y, vx, vy; bool grounded; } BBBody;
typedef struct BBInput { float move; bool jump, fire; } BBInput;
typedef bool (*BBRectCollisionFn)(float ax, float ay, float aw, float ah,
                                  float bx, float by, float bw, float bh);
typedef bool (*BBCircleCollisionFn)(float ax, float ay, float ar,
                                    float bx, float by, float br);
typedef struct BBCollisionBackend {
    BBRectCollisionFn recs;
    BBCircleCollisionFn circles;
} BBCollisionBackend;
typedef struct BBPlayer {
    BBBody body;
    BBPlayerState state;
    bool active, air_control, falling;
    int facing, lives, score;
    float invulnerable, death_timer, fire_cooldown, attack_timer, jump_start_y;
} BBPlayer;
typedef struct BBEnemy {
    BBBody body;
    BBEnemyType type;
    BBEnemyState state;
    bool active, angry, controlled;
    int facing, bounces;
    float spawn_delay, spawn_y, age, jump_timer, turn_timer, throw_timer, bounce_timer;
} BBEnemy;
typedef struct BBBubble {
    BBBody body;
    bool active, popping, releasing;
    int owner, captured_enemy;
    float age, pop_timer;
} BBBubble;
typedef struct BBBoulder { BBBody body; bool active; float age; } BBBoulder;
typedef struct BBPickup { BBBody body; BBPickupType type; bool active; float age; } BBPickup;
typedef struct BBGame {
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBPlayer players[BB_MAX_PLAYERS];
    BBEnemy enemies[BB_MAX_ENEMIES];
    BBBubble bubbles[BB_MAX_BUBBLES];
    BBBoulder boulders[BB_MAX_BOULDERS];
    BBPickup pickups[BB_MAX_PICKUPS];
    BBState state;
    BBMode mode;
    int level; /* 인트로를 포함한 0부터 시작하는 플레이 스테이지 번호. */
    float state_time, level_time;
    uint64_t ticks;
    uint32_t rng, events;
    bool won, bump_pending;
    BBCollisionBackend collision;
} BBGame;

/* tiles는 BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 가리킨다.
 * 값은 0=빈칸, 1=장식, 2=일방 통과 발판, 3=단단한 벽이다.
 * NULL은 빈 맵을 사용한다. 이 함수는 메모리를 할당하거나 파일을 읽지 않는다. */
void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed);
/* 그래픽 백엔드의 사각형·원 충돌 함수를 연결한다. NULL 함수는 C 기본 판정을 사용한다. */
void bb_game_set_collision_backend(BBGame *game, BBCollisionBackend backend);
void bb_game_start(BBGame *game, BBMode mode);
void bb_game_menu(BBGame *game);
void bb_game_skip_intro(BBGame *game);
/* 순서도상의 UpdateGame 단계: 입력, 이동, 시간과 개별 객체 상태를 갱신한다.
 * 점프와 발사는 누른 순간만 true여야 하며, 잘못된 dt는 무시한다. */
void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt);
/* 순서도상의 CheckBump 단계: 충돌과 그에 따른 게임 이벤트를 처리한다. */
void bb_game_check_bump(BBGame *game);
int bb_game_enemies_left(const BBGame *game);

#endif
