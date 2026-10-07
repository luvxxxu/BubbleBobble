#include "bb_raylib_types.h"

Vector2 bb_vector2(float x, float y)
{
    return (Vector2){x, y};
}

Rectangle bb_rectangle(float x, float y, float width, float height)
{
    return (Rectangle){x, y, width, height};
}

Color bb_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    return (Color){r, g, b, a};
}

Texture2D bb_empty_texture(void)
{
    /* raylib 구조체의 모든 필드를 초기화해 리소스 유효성 검사에 넘긴다. */
    return (Texture2D){0};
}

Sound bb_empty_sound(void)
{
    return (Sound){0};
}
