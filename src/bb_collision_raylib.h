#ifndef BB_COLLISION_RAYLIB_H
#define BB_COLLISION_RAYLIB_H

#include "bb_game.h"

/* 실행 게임의 충돌 함수를 raylib 구현으로 연결한다. 코어 테스트는 기본 판정을 쓸 수 있다. */
BBCollisionBackend bb_raylib_collision_backend(void);

#endif
