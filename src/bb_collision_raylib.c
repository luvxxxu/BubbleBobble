#include "bb_collision_raylib.h"

#include "raylib.h"

static bool raylib_recs(float ax, float ay, float aw, float ah,
                        float bx, float by, float bw, float bh)
{
    return CheckCollisionRecs((Rectangle){ax - aw, ay - ah, aw * 2.0f, ah * 2.0f},
                              (Rectangle){bx - bw, by - bh, bw * 2.0f, bh * 2.0f});
}

static bool raylib_circles(float ax, float ay, float ar,
                           float bx, float by, float br)
{
    return CheckCollisionCircles((Vector2){ax, ay}, ar, (Vector2){bx, by}, br);
}

BBCollisionBackend bb_raylib_collision_backend(void)
{
    return (BBCollisionBackend){ .recs = raylib_recs, .circles = raylib_circles };
}
