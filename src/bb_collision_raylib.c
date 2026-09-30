#include "bb_collision_raylib.h"

#include "bb_raylib_types.h"

static bool raylib_circles(float ax, float ay, float ar,
                           float bx, float by, float br)
{
    return CheckCollisionCircles(bb_vector2(ax, ay), ar, bb_vector2(bx, by), br);
}

BBCollisionBackend bb_raylib_collision_backend(void)
{
    BBCollisionBackend backend;
    backend.circles = raylib_circles;
    return backend;
}
