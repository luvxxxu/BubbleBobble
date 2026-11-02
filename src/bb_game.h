#ifndef BB_GAME_H
#define BB_GAME_H

/* 게임에서 쓰는 이름과 데이터 모양을 모은 파일이다.
 * #define은 숫자에 이름을 붙이고, enum은 상태 이름들을 묶는다.
 * struct는 한 물체의 여러 정보를 묶는다. bool은 true/false,
 * float/double은 소수이며 double은 시간을 더 정밀하게 누적할 때 쓴다.
 * uint32_t/uint64_t는 크기가 정해진 양의 정수 형식이다. */
#include <stdbool.h>
#include <stdint.h>

/* 원본 맵 크기와 화면 확장 한도. 단위는 픽셀이 아니라 타일이다. */
#define BB_MAP_WIDTH 32
#define BB_MAP_HEIGHT 28
#define BB_WORLD_MAX_WIDTH 256
#define BB_WORLD_MAX_HEIGHT 256
/* 라운드/목숨/참가자와 고정 배열의 최대 크기다. 게임 중 배열을 늘리지 않는다. */
#define BB_LEVEL_COUNT 5
#define BB_STARTING_LIVES 5
#define BB_MAX_PLAYERS 2
/* 최대 화면 폭에서 마지막 라운드의 몬스터 수를 올림한 값이다. */
#define BB_MAX_ENEMIES ((BB_LEVEL_COUNT * BB_WORLD_MAX_WIDTH + BB_MAP_WIDTH - 1) / BB_MAP_WIDTH)
#define BB_MAX_BOULDERS 32
#define BB_MAX_PICKUPS BB_MAX_ENEMIES
#define BB_MAX_BUBBLES 64
/* 마지막 목숨을 잃었을 때 생기는 큰 부활 버블의 크기와 구조 제한 시간. */
#define BB_RESCUE_BUBBLE_RADIUS 1.75f
#define BB_RESCUE_BUBBLE_SECONDS 10.0
/* 물리는 초당 120번 갱신한다. 이동 속도는 타일/초, 나머지 시간은 초다. */
#define BB_FIXED_DT (1.0f / 120.0f)
#define BB_PLAYER_GROUND_SPEED 6.0f
#define BB_PLAYER_AIR_SPEED 3.0f
/* 마나를 15초 채운 다음 5초 동안 가로 이동 속도가 두 배가 된다. */
#define BB_ENERGY_CHARGE_SECONDS 15.0f
#define BB_ENERGY_BOOST_SECONDS 5.0f
/* 발판은 서로 다른 속도로 이동하며, 뒤 라운드일수록 더 자주 사라진다. */
#define BB_PLATFORM_TRAVEL 1.0f
#define BB_PLATFORM_MIN_SPEED 1.5f
#define BB_PLATFORM_MAX_SPEED 3.0f
#define BB_PLATFORM_FIRST_SOLID_SECONDS 20.0f
#define BB_PLATFORM_ROUND_ACCELERATION 3.5f
#define BB_PLATFORM_SOLID_VARIATION 2.0f
#define BB_PLATFORM_MIN_SOLID_SECONDS 6.0f
#define BB_PLATFORM_MAX_SOLID_SECONDS 22.0f
#define BB_PLATFORM_FIRST_WARNING_GAP_SECONDS 8.0
#define BB_PLATFORM_WARNING_GAP_REDUCTION 1.5
/* 한 번의 깜빡임은 표시 0.2초 + 숨김 0.2초다. 이를 세 번 반복한다. */
#define BB_PLATFORM_BLINK_SECONDS 0.2
#define BB_PLATFORM_BLINK_COUNT 3
#define BB_PLATFORM_WARNING_SECONDS (BB_PLATFORM_BLINK_SECONDS * BB_PLATFORM_BLINK_COUNT * 2)
#define BB_PLATFORM_ABSENT_SECONDS 3.0
/* 한 줄에서 타일과 빈칸이 번갈아 나타날 때의 최대 공중 발판 수다. */
#define BB_MAX_PLATFORMS (((BB_MAP_WIDTH + 1) / 2) * (BB_MAP_HEIGHT - 2))

/* 화면 전체의 진행 상태다. 개별 플레이어의 상태와 별도로 저장한다. */
typedef enum BBState {
    BB_STATE_MENU,   /* 메뉴에서 인원 수를 고르는 중 */
    BB_STATE_INTRO,  /* 시작 전 READY 안내 */
    BB_STATE_PLAY,   /* 적을 상대하는 중 */
    BB_STATE_CLEAR,  /* 라운드 완료 후 7초 동안 보상을 줍는 중 */
    BB_STATE_SCORE   /* 승리/패배 후 기록 입력 */
} BBState;
typedef enum BBPlayerState {
    BB_PLAYER_NORMAL,     /* 조작 가능한 상태 */
    BB_PLAYER_DEAD,       /* 1.5초 사망 연출 */
    BB_PLAYER_OUT,        /* 이번 게임에서 탈락 */
    BB_PLAYER_BUBBLE,     /* 10초 안에 동료가 구조할 수 있는 버블 */
    BB_PLAYER_WAIT_ROUND  /* 구조 시간이 끝나 다음 라운드를 기다림 */
} BBPlayerState;
/* 몬스터 종류에 따라 걷기·점프·바위 공격 규칙이 다르다. */
typedef enum BBEnemyType {
    BB_ENEMY_ZENCHAN,
    BB_ENEMY_MAITA,
    BB_ENEMY_MONSTA,
    BB_ENEMY_TYPE_COUNT /* 종류 개수이며 실제 몬스터 종류로 사용하지 않는다. */
} BBEnemyType;
typedef enum BBEnemyState {
    BB_ENEMY_SPAWNING, /* 지정된 위치에 등장하기를 기다림 */
    BB_ENEMY_WALKING,  /* 바닥/발판을 걸음 */
    BB_ENEMY_FALLING,  /* 아래로 떨어짐 */
    BB_ENEMY_JUMPING,  /* 점프 중 */
    BB_ENEMY_CAPTURED, /* 일반 버블 안에 잡힘 */
    BB_ENEMY_DEAD      /* 버블이 터져 처치된 뒤 날아감 */
} BBEnemyState;
/* 음식 그림과 점수표가 이 순서를 함께 사용한다. 마지막 값은 음식 개수다. */
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
    bool fire;   /* 버블 발사 성공 */
    bool jump;   /* 점프 시작 */
    bool pop;    /* 버블 파열 */
    bool death;  /* 플레이어 피격 */
    bool pickup; /* 음식 획득 */
    bool level;  /* 새 라운드 시작 */
} BBEvents;

/* 위치는 타일 단위의 중심 좌표이며, 원점은 왼쪽 위이고 y축은 아래로 증가한다. */
typedef struct BBBody {
    float x, y;      /* 현재 위치 */
    float vx, vy;    /* 초당 이동 속도 */
    bool grounded;  /* 바닥, 발판 또는 거품 위에 서 있는가 */
} BBBody;

typedef struct BBInput {
    float move;     /* 왼쪽 -1, 정지 0, 오른쪽 1 */
    bool jump;      /* 이번 입력에서 점프 키를 새로 눌렀는가 */
    bool fire;      /* 이번 입력에서 발사 키를 새로 눌렀는가 */
} BBInput;

/* 한 플레이어의 현재 기록이다. players[0]은 초록 1P, players[1]은 파랑 2P다. */
typedef struct BBPlayer {
    BBBody body;         /* 위치, 속도, 착지 여부 */
    BBPlayerState state; /* 정상/사망/버블/대기/탈락 */
    int lives;           /* 남은 목숨 */
    int score;           /* 이 플레이어가 얻은 점수 */
    float invulnerable; /* 부활 후 피해를 받지 않는 남은 초 */
    float death_timer;  /* 사망 그림을 보여 줄 남은 초 */
    float rescue_pop_timer; /* 동료가 부활 버블을 터뜨린 뒤 남은 연출 시간 */
    double rescue_time_left; /* 부활 버블을 터뜨릴 수 있는 남은 초 */
    double rescue_turn_timer; /* 다음 무작위 방향을 정하기까지 남은 초 */
    float rescue_drift_vx, rescue_drift_vy; /* 무작위로 뽑은 목표 이동 속도 */
    uint32_t rescue_rng; /* 동료의 행동과 독립적인 부유 경로의 난수 상태 */
    bool active;      /* 이번 게임에 참가했는가 */
    bool air_control; /* 공중에서 좌우 입력을 받을 수 있는가 */
    bool falling;     /* 점프 후 느린 낙하 구간인가 */
    bool boosting;    /* 마나가 가득 차 두 배 가속 중인가 */
    int facing; /* 왼쪽 -1, 오른쪽 1 */
    float fire_cooldown; /* 다음 발사까지 남은 초 */
    float attack_timer;  /* 공격 그림을 표시할 남은 초 */
    float jump_start_y; /* 점프 출발 높이. 아래로 돌아오면 낙하 방식이 바뀐다. */
    /* 각 플레이어가 자기 마나와 충전 시간을 가진다. 마나는 0..1이다. */
    float energy;
    double energy_elapsed; /* 현재 충전 또는 가속 구간에서 지난 초 */
} BBPlayer;
/* 일반 발사 버블 한 개. 부활 버블은 BBPlayer에 저장하므로 이 배열을 차지하지 않는다. */
typedef struct BBBubble {
    BBBody body;   /* 버블 중심과 이동 속도 */
    bool active;  /* 배열의 이 칸을 현재 사용 중인가 */
    bool popping; /* 0.25초 파열 그림을 표시 중인가 */
    bool releasing; /* 파열 뒤 적을 풀어 줄 것인가. false이면 처치한다. */
    int owner;    /* 발사한 플레이어의 배열 번호 0 또는 1 */
    int captured_enemy; /* -1이면 빈 거품, 그 외에는 enemies 배열 번호 */
    float age;       /* 발사 후 지난 시간. 적을 잡아도 초기화하지 않는다. */
    float pop_timer; /* 파열 그림의 남은 시간 */
} BBBubble;
/* 적 한 마리의 AI와 물리 상태. 종류, 진행 상태, 타이머를 각각 구분한다. */
typedef struct BBEnemy {
    BBBody body;         /* 위치와 속도 */
    BBEnemyType type;     /* 몬스터 종류 */
    BBEnemyState state;   /* 등장/이동/포획/처치 상태 */
    bool active;         /* 이 배열 칸을 사용 중인가 */
    bool angry;          /* 버블에서 탈출해 화난 상태인가 */
    int facing;          /* 바라보는 쪽: 왼쪽 -1, 오른쪽 1 */
    int bounces;         /* 처치된 뒤 튕긴 횟수 */
    float spawn_delay;   /* 등장 전에 기다릴 시간. age가 이 값을 넘으면 내려온다. */
    float spawn_y;       /* 등장할 높이 */
    float age;           /* 생성/처치 연출을 시작한 뒤 지난 초 */
    float jump_timer;    /* 마지막 점프 뒤 지난 초 */
    float turn_timer;    /* 마지막 방향 판단 뒤 지난 초 */
    float throw_timer;   /* 마지막 바위 공격 뒤 지난 초 */
    float bounce_timer;  /* 처치 연출에서 마지막 튕김 뒤 지난 초 */
} BBEnemy;
/* Maita가 던진 바위. 최대 수명을 넘거나 벽에 맞으면 배열 칸을 비운다. */
typedef struct BBBoulder {
    BBBody body; /* 위치와 속도 */
    bool active; /* 현재 날아가고 있는가 */
    float age;   /* 발사 후 지난 초 */
} BBBoulder;
/* 처치한 적에게서 떨어진 음식. 먹거나 수명이 끝나면 active가 false가 된다. */
typedef struct BBPickup {
    BBBody body;        /* 위치와 낙하 속도 */
    BBPickupType type;  /* 음식 그림/점수 종류 */
    bool active;       /* 아직 화면에 남아 있는가 */
    float age;         /* 생성 후 지난 초 */
} BBPickup;

typedef enum BBPlatformState {
    BB_PLATFORM_SOLID,        /* 정상 표시, 밟을 수 있음 */
    BB_PLATFORM_DISAPPEARING, /* 소멸 예고 점멸, 아직 밟을 수 있음 */
    BB_PLATFORM_ABSENT,       /* 사라진 상태, 통과함 */
    BB_PLATFORM_APPEARING    /* 생성 예고 점멸, 아직 통과함 */
} BBPlatformState;

typedef struct BBMovingPlatform {
    int x, y, width;          /* 이동 전 기준 위치와 이어진 타일 수 */
    float offset;            /* 기준 x에서 현재까지 이동한 거리 */
    float step;              /* 이번 틱에 이동한 거리. 탑승자도 이만큼 옮긴다. */
    float speed;             /* 초당 이동 거리 */
    int direction;           /* 왼쪽 -1, 오른쪽 1 */
    BBPlatformState state;   /* 표시/점멸/부재 단계 */
    double state_time;       /* 현재 단계에 들어온 뒤 지난 초 */
    float solid_duration;    /* 정상 표시를 유지하는 최소 시간 */
    bool vanished;           /* 바로 이번 틱에 밟을 수 없게 되었는가 */
} BBMovingPlatform;

/* 게임 한 판의 모든 데이터. 외부 전역변수를 사용하지 않아 두 게임을
 * 따로 만들어 같은 입력을 넣고 결과를 비교할 수 있다. 배열은 모두 고정 크기다. */
typedef struct BBGame {
    uint8_t maps[BB_LEVEL_COUNT][BB_MAP_HEIGHT][BB_MAP_WIDTH]; /* [라운드][행][열] 원본 타일 */
    int world_width, world_height; /* 현재 화면에 맞춰 확장된 맵의 타일 수 */
    BBPlayer players[BB_MAX_PLAYERS];   /* 참가자 */
    BBBubble bubbles[BB_MAX_BUBBLES];   /* 일반 발사 버블 */
    BBEnemy enemies[BB_MAX_ENEMIES];   /* 현재 라운드의 적 */
    BBBoulder boulders[BB_MAX_BOULDERS]; /* 날아가는 바위 */
    BBPickup pickups[BB_MAX_PICKUPS];  /* 떨어진 음식 */
    BBPickupType pickup_order[BB_PICKUP_TYPE_COUNT]; /* 중복을 막기 위해 섞어 둔 음식 순서 */
    int pickup_cursor; /* 20종을 모두 뽑은 뒤 다시 섞는다. */
    int level; /* 배열 번호 0..4, 화면에는 1..5로 표시 */
    int round_enemy_quota; /* 현재 라운드에서 한 번이라도 생성한 적 수. 축소/처치 시 줄이지 않는다. */
    bool won; /* 마지막 라운드까지 이겼는가 */
    int player_count; /* 1인 또는 2인. 배열 번호는 각각 0과 1이다. */
    BBState state;          /* 메뉴/인트로/플레이/클리어/결과 */
    float state_time;       /* 현재 화면 상태가 시작된 뒤 지난 초 */
    float level_time;       /* 현재 라운드에서 흐른 초. 그림 움직임에도 사용한다. */
    BBMovingPlatform platforms[BB_MAX_PLATFORMS]; /* 공중 발판 */
    int platform_count;    /* 위 배열에서 실제 사용하는 칸 수 */
    double platform_warning_cooldown; /* 다음 발판이 사라지기 시작할 때까지 남은 초 */
    int platform_last_choice; /* 마지막 무작위 선택 번호이며 다음 순서를 정하지 않는다. */
    uint64_t ticks;   /* 유효한 bb_game_update 호출 횟수 */
    uint32_t rng;     /* 다음 일반 난수를 만드는 데 필요한 현재 값 */
    BBEvents events;  /* 이번 호출에서 발생한 소리/화면 사건 */
} BBGame;

/* tiles는 BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 가리킨다.
 * 값은 0=빈칸, 1=장식, 2=일방 통과 발판, 3=단단한 벽이다.
 * NULL은 빈 맵을 사용한다. 이 함수는 메모리를 할당하거나 파일을 읽지 않는다. */
void bb_game_init(BBGame *game, const uint8_t *tiles, uint32_t seed);
/* 원본 맵을 중앙/바닥에 맞추며 실제 플레이 영역을 바꾼다. 진행 시간은 유지한다.
 * 플레이 중 폭이 늘면 플레이어 속도를 맞추고 해당 라운드의 추가 적을 등장 대기로 만든다.
 * 너비는 32..256, 높이는 28..256 타일로 제한하며 같은 크기는 아무 작업도 하지 않는다. */
void bb_game_resize(BBGame *game, int width, int height);
/* 현재 월드의 고정 타일 조회. 움직이는 발판의 기준 좌표이며 맵 밖은 0이다. */
int bb_game_tile(const BBGame *game, int x, int y);
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

/* 월드 기준 타일에 속한 발판의 이동량. 벽, 바닥, 빈칸 및 맵 밖은 0이다. */
float bb_game_tile_offset(const BBGame *game, int x, int y);
/* 기본 좌우 한 타일에 늘어난 월드 폭의 절반을 더한 최대 이동 거리다.
 * 실제 발판은 자신의 길이를 포함해 양옆 벽 안쪽으로 제한된다. */
float bb_game_platform_travel(const BBGame *game);
/* 정상 발판과 소멸 예고 발판처럼 지금 밟을 수 있는 상태인지 확인한다. */
bool bb_game_platform_solid(const BBMovingPlatform *platform);
/* 점멸 예고 중 이번 순간에 점선을 표시해야 하는지 돌려준다. */
bool bb_game_platform_blink_visible(const BBMovingPlatform *platform);

/* 포획 중인 적도 세지만, 죽었거나 비활성인 적은 제외한다. */
int bb_game_enemies_left(const BBGame *game);
/* 음식 종류 하나를 받아 획득 점수를 돌려준다. 잘못된 종류이면 0이다. */
int bb_game_pickup_score(BBPickupType type);

#endif
