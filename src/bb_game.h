#ifndef BB_GAME_H
#define BB_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define BB_MAP_WIDTH 32
#define BB_MAP_HEIGHT 28
#define BB_LEVEL_COUNT 5
#define BB_STARTING_LIVES 5
#define BB_MAX_PLAYERS 2
#define BB_MAX_ENEMIES 16
#define BB_MAX_BOULDERS 32
#define BB_MAX_PICKUPS 32
#define BB_MAX_BUBBLES 64
#define BB_FIXED_DT (1.0f / 120.0f)
#define BB_PLAYER_GROUND_SPEED 6.0f
#define BB_PLAYER_AIR_SPEED 3.0f
#define BB_ENERGY_CHARGE_SECONDS 15.0f
#define BB_ENERGY_BOOST_SECONDS 5.0f

typedef enum BBState {
    BB_STATE_MENU,
    BB_STATE_INTRO,
    BB_STATE_PLAY,
    BB_STATE_CLEAR,
    BB_STATE_SCORE
} BBState;
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

/* 이번 갱신에서 일어난 일. 소리 재생에 사용하며 다음 갱신에서 지운다. */
typedef struct BBEvents {
    bool fire;
    bool jump;
    bool pop;
    bool death;
    bool pickup;
    bool level;
} BBEvents;

/* 위치는 타일 단위의 중심 좌표이며, 원점은 왼쪽 위이고 y축은 아래로 증가한다. */
typedef struct BBBody {
    float x, y;      /* 현재 위치 */
    float vx, vy;    /* 초당 이동 속도 */
    bool grounded;  /* 발판이나 거품 위에 서 있는가 */
} BBBody;

typedef struct BBInput {
    float move;     /* 왼쪽 -1, 정지 0, 오른쪽 1 */
    bool jump;
    bool fire;
} BBInput;

typedef struct BBPlayer {
    BBBody body;
    BBPlayerState state;
    int lives;
    int score;
    float invulnerable; /* 부활 후 피해를 받지 않는 남은 초 */
    float death_timer;  /* 사망 그림을 보여 줄 남은 초 */
    bool active;
    bool air_control;
    bool falling;
    bool boosting;
    int facing; /* 왼쪽 -1, 오른쪽 1 */
    float fire_cooldown; /* 다음 발사까지 남은 초 */
    float attack_timer;  /* 공격 그림을 표시할 남은 초 */
    float jump_start_y;
    /* 각 플레이어가 자기 마나와 충전 시간을 가진다. 마나는 0..1이다. */
    float energy;
    double energy_elapsed; /* 현재 충전 또는 가속 구간에서 지난 초 */
} BBPlayer;
typedef struct BBBubble {
    BBBody body;
    bool active;
    bool popping;
    bool releasing;
    int owner;
    int captured_enemy; /* -1이면 빈 거품, 그 외에는 enemies 배열 번호 */
    float age;
    float pop_timer;
} BBBubble;
typedef struct BBEnemy {
    BBBody body;
    BBEnemyType type;
    BBEnemyState state;
    bool active;
    bool angry;
    int facing;
    int bounces;
    float spawn_delay;
    float spawn_y;
    float age;
    float jump_timer;
    float turn_timer;
    float throw_timer;
    float bounce_timer;
} BBEnemy;
typedef struct BBBoulder {
    BBBody body;
    bool active;
    float age;
} BBBoulder;
typedef struct BBPickup {
    BBBody body;
    BBPickupType type;
    bool active;
    float age;
} BBPickup;

typedef struct BBGame {
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBPlayer players[BB_MAX_PLAYERS];
    BBBubble bubbles[BB_MAX_BUBBLES];
    BBEnemy enemies[BB_MAX_ENEMIES];
    BBBoulder boulders[BB_MAX_BOULDERS];
    BBPickup pickups[BB_MAX_PICKUPS];
    BBPickupType pickup_order[BB_PICKUP_TYPE_COUNT];
    int pickup_cursor; /* 20종을 모두 뽑은 뒤 다시 섞는다. */
    int level; /* 배열 번호 0..4, 화면에는 1..5로 표시 */
    bool won;
    int player_count; /* 1인 또는 2인. 배열 번호는 각각 0과 1이다. */
    BBState state;
    float state_time, level_time;
    uint64_t ticks;
    uint32_t rng;
    BBEvents events;
} BBGame;

/* tiles는 BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 가리킨다.
 * 값은 0=빈칸, 1=장식, 2=일방 통과 발판, 3=단단한 벽이다.
 * NULL은 빈 맵을 사용한다. 이 함수는 메모리를 할당하거나 파일을 읽지 않는다. */
void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed);
/* 새 게임: 맵은 유지하고 두 플레이어의 상태와 버블을 모두 초기화한다.
 * player_count는 1..2로 제한한다. 1인 모드의 두 번째 플레이어는 비활성이다. */
void bb_game_start(BBGame *game, int player_count);
/* 메뉴로 돌아가되 플레이어와 맵 데이터는 그대로 둔다. */
void bb_game_menu(BBGame *game);
/* 인트로 상태에서만 첫 라운드 플레이로 진입한다. */
void bb_game_skip_intro(BBGame *game);
/* 플레이어 -> 적과 거품/바위/아이템 -> 충돌 -> 라운드 순서로 한 번에 갱신한다.
 * inputs는 두 칸 배열 또는 NULL(입력 없음)이다.
 * 점프와 발사는 누른 순간만 true여야 하며, 잘못된 dt는 무시한다. */
void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt);

/* 포획 중인 적도 세지만, 죽었거나 비활성인 적은 제외한다. */
int bb_game_enemies_left(const BBGame *game);
int bb_game_pickup_score(BBPickupType type);

#endif
