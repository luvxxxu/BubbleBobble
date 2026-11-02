/* 화면 크기를 게임의 타일 수와 그리기 배율로 바꾸는 계산을 검사한다.
 * 실제 창을 열지 않고 여러 픽셀 크기를 함수에 넣어 캐릭터 비율과 맵 확장이
 * 유지되는지 확인한다. assert는 잘못된 결과를 만나면 즉시 시험을 멈춘다. */
#include "bb_viewport.h"
#include "bb_game.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

/* 실제 픽셀 수에서 계산한 표시 범위가 화면 전체를 덮고, 바깥 벽의
 * 반 타일 미만만 잘라내는지 확인한다. 이동 가능한 내부 영역은 보존한다. */
static void test_resolutions(void)
{
    const int resolutions[][2] = {
        {256, 224}, {768, 672}, {1000, 700}, {1920, 1080},
        {2560, 1440}, {3840, 2160}, {3420, 1920}, {3440, 1440},
        {1080, 1920}, {700, 1000}, {160, 120}
    };
    for(unsigned i = 0; i < sizeof resolutions / sizeof resolutions[0]; ++i)
    {
        int width = resolutions[i][0], height = resolutions[i][1];
        BBViewport view = bb_viewport_fit(width, height, 1.0f);
        float tile = view.scale * 8.0f;
        assert(isfinite(view.scale) && view.scale > 0.0f);
        if(width >= 256 && height >= 224)
            assert(view.scale == floorf(view.scale));
        assert(view.columns >= BB_MAP_WIDTH && view.columns <= BB_WORLD_MAX_WIDTH);
        assert(view.rows >= BB_MAP_HEIGHT && view.rows <= BB_WORLD_MAX_HEIGHT);
        assert(view.offset_x <= 0.001f && view.offset_y <= 0.001f);
        assert(view.offset_x > -tile * 0.5f - 0.001f);
        assert(view.offset_y > -tile * 0.5f - 0.001f);
        assert(view.offset_x + (float)view.columns * tile >= (float)width - 0.001f);
        assert(view.offset_y + (float)view.rows * tile >= (float)height - 0.001f);
        assert(view.offset_x + 2.0f * tile > 0.0f);
        assert(view.offset_x + ((float)view.columns - 2.0f) * tile < (float)width);
    }
}

/* 16:9에서는 기존 32열을 늘리는 대신 이동 가능한 열 수가 늘어나야 한다.
 * 같은 물리 크기를 두 배로 그려도 배율 외의 월드 배치는 같아야 한다. */
static void test_expansion_and_retina(void)
{
    BBViewport base = bb_viewport_fit(768, 672, 1.0f);
    assert(base.columns == 32 && base.rows == 28 && base.scale == 3.0f);
    BBViewport wide = bb_viewport_fit(1920, 1080, 1.0f);
    BBViewport retina = bb_viewport_fit(3840, 2160, 2.0f);
    assert(wide.columns == 60 && wide.rows == 34 && wide.scale == 4.0f);
    assert(retina.columns == wide.columns && retina.rows == wide.rows);
    assert(retina.scale == wide.scale * 2.0f);
    assert(retina.offset_x == wide.offset_x * 2.0f);
    assert(retina.offset_y == wide.offset_y * 2.0f);
    BBViewport portrait = bb_viewport_fit(700, 1000, 1.0f);
    assert(portrait.rows > BB_MAP_HEIGHT);
    BBViewport invalid = bb_viewport_fit(0, -1, 1.0f);
    assert(invalid.columns == BB_MAP_WIDTH && invalid.rows == BB_MAP_HEIGHT);
    assert(invalid.scale == 1.0f);
    BBViewport fraction = bb_viewport_fit(2400, 1350, 1.25f);
    assert(fraction.columns == wide.columns && fraction.rows == wide.rows);
    assert(fraction.scale == wide.scale * 1.25f);
    BBViewport bad_dpi = bb_viewport_fit(1920, 1080, NAN);
    assert(bad_dpi.columns == wide.columns && bad_dpi.scale == wide.scale);
}

/* 매우 가늘거나 긴 창에서는 맵 최대 크기를 넘지 않으면서도 기존 플레이
 * 영역을 확대해 잘라내지 않는다. 남는 공간은 화면 쪽에서 외벽을 연장한다. */
static void test_extreme_aspect_ratios(void)
{
    BBViewport wide = bb_viewport_fit(4096, 224, 1.0f);
    assert(wide.columns == BB_WORLD_MAX_WIDTH && wide.rows == BB_MAP_HEIGHT);
    assert(wide.scale == 1.0f && wide.offset_x > 0.0f && wide.offset_y == 0.0f);
    BBViewport tall = bb_viewport_fit(256, 4096, 1.0f);
    assert(tall.rows == BB_WORLD_MAX_HEIGHT && tall.columns == BB_MAP_WIDTH);
    assert(tall.scale == 1.0f && tall.offset_y > 0.0f && tall.offset_x == 0.0f);
}

/* 일반 해상도, 고밀도 화면, 극단적인 화면비를 모두 통과해야 성공으로 끝낸다. */
int main(void)
{
    test_resolutions();
    test_expansion_and_retina();
    test_extreme_aspect_ratios();
    puts("Native-resolution viewport, world expansion, square pixels, and Retina scaling passed.");
    return 0;
}
