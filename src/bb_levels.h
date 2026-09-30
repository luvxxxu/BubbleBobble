#ifndef BB_LEVELS_H
#define BB_LEVELS_H

#include "bb_game.h"

#define BB_LEVEL_PLATFORM_COUNT 5

/* Write BB_LEVEL_COUNT * BB_MAP_HEIGHT * BB_MAP_WIDTH tile bytes.
 * Each round has five one-way platforms, side walls and a floor wrap opening. */
void bb_levels_build(uint8_t *maps);

#endif
