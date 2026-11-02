#ifndef BB_RAYLIB_TYPES_H
#define BB_RAYLIB_TYPES_H

#include "raylib.h"

/* x, y 좌표를 묶는다. 화면에 그릴 때는 픽셀 단위를 사용한다. */
Vector2 bb_vector2(float x, float y);
/* 왼쪽 위 좌표와 가로·세로 길이를 묶는다. */
Rectangle bb_rectangle(float x, float y, float width, float height);
/* 빨강·초록·파랑·불투명도를 각각 0~255로 지정한다. */
Color bb_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a);
/* 경로 구성 실패를 영으로 초기화한 값으로 표현해 공통 정리 경로에서 다룬다. */
Texture2D bb_empty_texture(void);
Sound bb_empty_sound(void);

#endif
