#include "bb_viewport.h"
#include "bb_game.h"

#include <math.h>

/* 원본 한 픽셀에 공통 배율을 적용하고, 남는 너비/높이는 실제 타일 수로
 * 바꾼다. 가로·세로를 별도로 늘리지 않아 타일과 캐릭터가 정사각형을 유지한다.
 * 양끝의 부분 타일은 바깥 벽 안에서만 잘라 소수 타일 맵을 피한다.
 * 최대 월드보다 긴 창의 남은 영역은 렌더러가 벽으로 이어 그린다. */
BBViewport bb_viewport_fit(int framebuffer_width, int framebuffer_height, float pixel_ratio)
{
    BBViewport view = {0};
    view.columns = BB_MAP_WIDTH;
    view.rows = BB_MAP_HEIGHT;
    view.scale = 1.0f;
    if(framebuffer_width <= 0 || framebuffer_height <= 0)
    {
        return view;
    }
    if(!isfinite(pixel_ratio) || pixel_ratio <= 0.0f)
    {
        pixel_ratio = 1.0f;
    }

    float width = (float)framebuffer_width;
    float height = (float)framebuffer_height;
    /* 논리 창에서 배율을 정한 뒤 DPI를 곱해야 같은 창을 Retina로 옮겼을
     * 때 맵 크기까지 바뀌지 않고 더 많은 픽셀로 같은 배치를 그릴 수 있다. */
    double width_scale = (double)framebuffer_width / (BB_MAP_WIDTH * 8.0);
    double height_scale = (double)framebuffer_height / (BB_MAP_HEIGHT * 8.0);
    double fit = fmin(width_scale, height_scale) / (double)pixel_ratio;
    double drawing_scale = fit;
    if(fit >= 1.0)
    {
        /* 정수 배율을 쓰면 픽셀 아트의 한 칸이 흐려지지 않는다. */
        drawing_scale = floor(fit);
    }
    view.scale = (float)(drawing_scale * (double)pixel_ratio);
    float tile_pixels = 8.0f * view.scale;
    /* 정수로 바꾸기 전에 제한해 매우 큰 입력도 int 변환 범위를 넘지 않는다. */
    view.columns = (int)fminf(ceilf(width / tile_pixels), BB_WORLD_MAX_WIDTH);
    view.rows = (int)fminf(ceilf(height / tile_pixels), BB_WORLD_MAX_HEIGHT);
    if(view.columns < BB_MAP_WIDTH)
    {
        view.columns = BB_MAP_WIDTH;
    }
    if(view.rows < BB_MAP_HEIGHT)
    {
        view.rows = BB_MAP_HEIGHT;
    }
    view.offset_x = (width - (float)view.columns * tile_pixels) * 0.5f;
    view.offset_y = (height - (float)view.rows * tile_pixels) * 0.5f;
    return view;
}
