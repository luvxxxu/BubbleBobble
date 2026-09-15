#include "bb_raylib_types.h"

Vector2 bb_vector2(float x, float y)
{
    Vector2 value;
    value.x = x;
    value.y = y;
    return value;
}

Rectangle bb_rectangle(float x, float y, float width, float height)
{
    Rectangle value;
    value.x = x;
    value.y = y;
    value.width = width;
    value.height = height;
    return value;
}

Color bb_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    Color value;
    value.r = r;
    value.g = g;
    value.b = b;
    value.a = a;
    return value;
}

Texture2D bb_empty_texture(void)
{
    Texture2D value;
    memset(&value, 0, sizeof value);
    return value;
}

Sound bb_empty_sound(void)
{
    Sound value;
    memset(&value, 0, sizeof value);
    return value;
}
