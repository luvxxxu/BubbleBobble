#ifndef BB_GAME_H
#define BB_GAME_H

#include "bb_compat.h"

#define BB_MAP_WIDTH 32
#define BB_MAP_HEIGHT 28
#define BB_LEVEL_COUNT 5
#define BB_STARTING_LIVES 5
#define BB_MAX_PLAYERS 1
#define BB_MAX_ENEMIES 16
#define BB_MAX_BUBBLES 64
#define BB_MAX_BOULDERS 32
#define BB_MAX_PICKUPS 32
#define BB_FIXED_DT (1.0f / 120.0f)
#define BB_PLAYER_GROUND_SPEED 6.0f
#define BB_PLAYER_AIR_SPEED 3.0f
#define BB_ENERGY_CHARGE_SECONDS 15.0f
#define BB_ENERGY_BOOST_SECONDS 5.0f

typedef enum BBState { BB_STATE_MENU, BB_STATE_INTRO, BB_STATE_PLAY, BB_STATE_CLEAR, BB_STATE_SCORE } BBState;
typedef enum BBPlayerState { BB_PLAYER_NORMAL, BB_PLAYER_DEAD, BB_PLAYER_OUT } BBPlayerState;
typedef enum BBEnemyType {
    BB_ENEMY_ZENCHAN, BB_ENEMY_MAITA, BB_ENEMY_MONSTA, BB_ENEMY_TYPE_COUNT
} BBEnemyType;
typedef enum BBEnemyState {
    BB_ENEMY_SPAWNING, BB_ENEMY_WALKING, BB_ENEMY_FALLING,
    BB_ENEMY_JUMPING, BB_ENEMY_CAPTURED, BB_ENEMY_DEAD
} BBEnemyState;
typedef enum BBPickupType {
    BB_PICKUP_WATERMELON, BB_PICKUP_FRIES, BB_PICKUP_CHERRY,
    BB_PICKUP_STRAWBERRY, BB_PICKUP_PEACH, BB_PICKUP_ORANGE,
    BB_PICKUP_GRAPES, BB_PICKUP_BANANA, BB_PICKUP_PINEAPPLE,
    BB_PICKUP_LEMON, BB_PICKUP_APPLE, BB_PICKUP_PEAR,
    BB_PICKUP_RADISH, BB_PICKUP_CORN, BB_PICKUP_CARROT,
    BB_PICKUP_EGGPLANT, BB_PICKUP_ICE_CREAM, BB_PICKUP_CAKE,
    BB_PICKUP_DONUT, BB_PICKUP_BURGER, BB_PICKUP_TYPE_COUNT
} BBPickupType;
/* 한 번의 update/check_bump 쌍에서 발생한 이벤트 비트. 다음 update가 지운다. */
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
    bool active, air_control, falling, boosting;
    int facing, lives, score;
    float invulnerable, death_timer, fire_cooldown, attack_timer, jump_start_y;
    /* 에너지는 0..1. 생존 중인 PLAY/CLEAR 시간만 진행하고 라운드/부활 시 보존한다. */
    float energy;
    double energy_elapsed; /* 현재 충전 또는 가속 구간에서 지난 시간. */
} BBPlayer;
typedef struct BBEnemy {
    BBBody body;
    BBEnemyType type;
    BBEnemyState state;
    bool active, angry;
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
    BBPickupType pickup_order[BB_PICKUP_TYPE_COUNT];
    int pickup_cursor; /* 라운드 사이에도 유지하는 중복 없는 아이템 묶음의 다음 위치. */
    BBState state;
    int level; /* 인트로를 포함한 0부터 시작하는 플레이 스테이지 번호. */
    float state_time, level_time;
    uint64_t ticks;
    uint32_t rng, events;
    bool won, bump_pending; /* bump_pending은 유효한 update 뒤 충돌 처리가 남았음을 뜻한다. */
    BBCollisionBackend collision;
} BBGame;

/* tiles는 BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 가리킨다.
 * 값은 0=빈칸, 1=장식, 2=일방 통과 발판, 3=단단한 벽이다.
 * NULL은 빈 맵을 사용한다. 이 함수는 메모리를 할당하거나 파일을 읽지 않는다. */
void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed);
/* 그래픽 백엔드의 사각형·원 충돌 함수를 연결한다. NULL 함수는 C 기본 판정을 사용한다. */
void bb_game_set_collision_backend(BBGame *game, BBCollisionBackend backend);
/* 새 게임을 시작한다. 맵과 충돌 백엔드는 유지하며 플레이어와 객체를 초기화한다. */
void bb_game_start(BBGame *game);
/* 메뉴로 돌아가되 플레이어와 레벨 데이터는 그대로 둔다. */
void bb_game_menu(BBGame *game);
/* 인트로 상태에서만 첫 라운드로 진입한다. */
void bb_game_skip_intro(BBGame *game);
/* 순서도상의 UpdateGame 단계: 입력, 이동, 시간과 개별 객체 상태를 갱신한다.
 * 점프와 발사는 누른 순간만 true여야 하며, 잘못된 dt는 무시한다.
 * 각 유효한 update 다음에는 check_bump를 호출해야 충돌과 라운드 전환이 반영된다. */
void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt);
/* 순서도상의 CheckBump 단계: 직전 update의 충돌과 그 결과를 한 번만 처리한다. */
void bb_game_check_bump(BBGame *game);
/* 포획된 적도 세며, 죽은 적과 비활성 적은 제외한다. */
int bb_game_enemies_left(const BBGame *game);
/* 유효하지 않은 아이템 종류에는 0을 반환한다. */
int bb_game_pickup_score(BBPickupType type);

#endif
