#include "bb_collision_raylib.h"

#include "bb_raylib_types.h"

static bool raylib_recs(float ax, float ay, float aw, float ah,
                        float bx, float by, float bw, float bh)
{
    return CheckCollisionRecs(bb_rectangle(ax - aw, ay - ah, aw * 2.0f, ah * 2.0f),
                              bb_rectangle(bx - bw, by - bh, bw * 2.0f, bh * 2.0f));
}

static bool raylib_circles(float ax, float ay, float ar,
                           float bx, float by, float br)
{
    return CheckCollisionCircles(bb_vector2(ax, ay), ar, bb_vector2(bx, by), br);
}

BBCollisionBackend bb_raylib_collision_backend(void)
{
    BBCollisionBackend backend;
    backend.recs = raylib_recs;
    backend.circles = raylib_circles;
    return backend;
}
