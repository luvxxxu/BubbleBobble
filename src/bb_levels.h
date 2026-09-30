#ifndef BB_LEVELS_H
#define BB_LEVELS_H

#include "bb_game.h"

#define BB_LEVEL_PLATFORM_COUNT 5

/* 연속된 BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 채운다.
 * 각 라운드는 일방 통과 발판 다섯 개, 측면 벽과 바닥 순환 구멍을 가진다.
 * maps가 NULL이면 아무 작업도 하지 않는다. */
void bb_levels_build(uint8_t *maps);

#endif
