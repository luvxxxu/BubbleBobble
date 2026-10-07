#ifndef BB_RAYLIB_TYPES_H
#define BB_RAYLIB_TYPES_H

#include <string.h>

#include "raylib.h"

/* raylib 값 타입을 생성하는 공통 함수. */
Vector2 bb_vector2(float x, float y);
Rectangle bb_rectangle(float x, float y, float width, float height);
Color bb_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
/* 경로 구성 실패를 영으로 초기화한 값으로 표현해 공통 정리 경로에서 다룬다. */
Texture2D bb_empty_texture(void);
Sound bb_empty_sound(void);

#endif
