#ifndef BB_GAME_H
#define BB_GAME_H

#include <stdbool.h>
#include <stdint.h>

#define BB_MAP_WIDTH 32
#define BB_MAP_HEIGHT 28
#define BB_MAX_PLAYERS 1
#define BB_MAX_BUBBLES 64
#define BB_FIXED_DT (1.0f / 120.0f)
#define BB_PLAYER_GROUND_SPEED 6.0f
#define BB_PLAYER_AIR_SPEED 3.0f
#define BB_ENERGY_CHARGE_SECONDS 15.0f
#define BB_ENERGY_BOOST_SECONDS 5.0f

typedef enum BBState { BB_STATE_MENU, BB_STATE_INTRO, BB_STATE_PLAY } BBState;
/* 한 번의 update/check_bump 쌍에서 발생한 이벤트 비트. 다음 update가 지운다. */
enum BBEvent {
    BB_EVENT_FIRE = 1u << 0, BB_EVENT_JUMP = 1u << 1,
    BB_EVENT_POP = 1u << 2
};

/* 위치는 타일 단위의 중심 좌표이며, 원점은 왼쪽 위이고 y축은 아래로 증가한다. */
typedef struct BBBody { float x, y, vx, vy; bool grounded; } BBBody;
typedef struct BBInput { float move; bool jump, fire; } BBInput;
typedef bool (*BBCircleCollisionFn)(float ax, float ay, float ar,
                                    float bx, float by, float br);
typedef struct BBCollisionBackend { BBCircleCollisionFn circles; } BBCollisionBackend;
typedef struct BBPlayer {
    BBBody body;
    bool active, air_control, falling, boosting;
    int facing;
    float fire_cooldown, attack_timer, jump_start_y;
    /* 에너지는 0..1. 플레이 중에만 진행하며 새 게임에서 초기화한다. */
    float energy;
    double energy_elapsed;
} BBPlayer;
typedef struct BBBubble {
    BBBody body;
    bool active, popping;
    float age, pop_timer;
} BBBubble;
typedef struct BBGame {
    uint8_t maps[BB_MAP_HEIGHT][BB_MAP_WIDTH];
    BBPlayer players[BB_MAX_PLAYERS];
    BBBubble bubbles[BB_MAX_BUBBLES];
    BBState state;
    float state_time, level_time;
    uint64_t ticks;
    uint32_t rng, events;
    bool bump_pending; /* 유효한 update 뒤 충돌 처리가 남았음을 뜻한다. */
    BBCollisionBackend collision;
} BBGame;

/* tiles는 BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 가리킨다.
 * 값은 0=빈칸, 1=장식, 2=일방 통과 발판, 3=단단한 벽이다.
 * NULL은 빈 맵을 사용한다. 이 함수는 메모리를 할당하거나 파일을 읽지 않는다. */
void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed);
/* 그래픽 백엔드의 원 충돌 함수를 연결한다. NULL 함수는 C 기본 판정을 사용한다. */
void bb_game_set_collision_backend(BBGame *game, BBCollisionBackend backend);
/* 새 게임을 시작한다. 맵과 충돌 백엔드는 유지하며 플레이어와 버블을 초기화한다. */
void bb_game_start(BBGame *game);
/* 메뉴로 돌아가되 플레이어와 맵 데이터는 그대로 둔다. */
void bb_game_menu(BBGame *game);
/* 인트로 상태에서만 레벨 1 플레이로 진입한다. */
void bb_game_skip_intro(BBGame *game);
/* 입력, 이동, 시간과 버블 상태를 갱신한다.
 * 점프와 발사는 누른 순간만 true여야 하며, 잘못된 dt는 무시한다.
 * 각 유효한 update 다음에는 check_bump를 호출해야 충돌이 반영된다. */
void bb_game_update(BBGame *game, const BBInput inputs[BB_MAX_PLAYERS], float dt);
/* 직전 update의 버블/플레이어 충돌을 한 번만 처리한다. */
void bb_game_check_bump(BBGame *game);

#endif
