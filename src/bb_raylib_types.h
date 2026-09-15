#ifndef BB_RAYLIB_TYPES_H
#define BB_RAYLIB_TYPES_H

#include <string.h>

#include "raylib.h"

/* C99 compound literal 없이 raylib 값 타입을 만든다. */
Vector2 bb_vector2(float x, float y);
Rectangle bb_rectangle(float x, float y, float width, float height);
Color bb_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
Texture2D bb_empty_texture(void);
Sound bb_empty_sound(void);

#endif
