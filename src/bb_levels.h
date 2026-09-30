#ifndef BB_LEVELS_H
#define BB_LEVELS_H

#include "bb_game.h"

#define BB_LEVEL_PLATFORM_COUNT 5

/* BB_MAP_HEIGHT * BB_MAP_WIDTH 바이트를 채운다.
 * 레벨 1은 일방 통과 발판 다섯 개, 측면 벽과 바닥 순환 구멍을 가진다.
 * map이 NULL이면 아무 작업도 하지 않는다. */
void bb_levels_build(uint8_t *map);

#endif
