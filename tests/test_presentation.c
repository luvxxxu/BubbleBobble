/* 실제 GPU 출력에서 DPI가 두 번 적용되지 않는지 검사한다.
 * 창을 여는 별도 진단 프로그램이므로 일반 헤드리스 CTest에는 넣지 않는다.
 * Windows의 물리 픽셀 좌표, Retina의 논리 좌표, 크기 변경 직후 남은 행렬을
 * 같은 창에서 재현한다. 실제 Windows 장치에서 실행한 검증과는 구분한다. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "bb_render.h"
#include "rlgl.h"

#include <assert.h>
#include <stdio.h>

/* 네 모서리는 서로 다른 색이다. 1픽셀 격자와 가운데 표식으로 일부 잘림뿐
 * 아니라 확대, 세로 뒤집힘, 한 픽셀 어긋남도 전체 픽셀 비교에서 발견한다. */
static Color pattern_color(int x, int y, int width, int height)
{
    Color color = {230, 35, 45, 255};
    if(x >= width / 2 && y < height / 2)
        color = (Color){30, 210, 70, 255};
    if(x < width / 2 && y >= height / 2)
        color = (Color){40, 85, 235, 255};
    if(x >= width / 2 && y >= height / 2)
        color = (Color){235, 195, 30, 255};
    if(x % 31 == 0 || y % 29 == 0)
        color = (Color){20, 25, 30, 255};
    if(x == 0 || y == 0 || x == width - 1 || y == height - 1)
        color = WHITE;
    if(x >= width / 2 - 3 && x <= width / 2 + 3 &&
       y >= height / 2 - 3 && y <= height / 2 + 3)
        color = MAGENTA;
    return color;
}

/* 게임과 같은 RenderTexture2D에 원본 패턴을 그린다. 실제 출력 해상도와
 * 같은 크기이며 보간을 끄므로 올바른 복사는 모든 RGB 값이 정확히 같다. */
static RenderTexture2D make_canvas(int width, int height)
{
    Image pattern = GenImageColor(width, height, BLACK);
    assert(pattern.data != NULL);
    Color *pixels = pattern.data;
    for(int y = 0; y < height; ++y)
        for(int x = 0; x < width; ++x)
            pixels[y * width + x] = pattern_color(x, y, width, height);
    Texture2D texture = LoadTextureFromImage(pattern);
    assert(texture.id != 0);
    SetTextureFilter(texture, TEXTURE_FILTER_POINT);
    UnloadImage(pattern);

    RenderTexture2D canvas = LoadRenderTexture(width, height);
    assert(IsRenderTextureValid(canvas));
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);
    BeginTextureMode(canvas);
    ClearBackground(BLACK);
    DrawTexture(texture, 0, 0, WHITE);
    EndTextureMode();
    UnloadTexture(texture);
    return canvas;
}

/* BeginDrawing이 적용하는 DPI 행렬과 이전 창의 뷰포트를 재현한다.
 * logical_ratio=1이면 Windows, 2이면 Retina와 같은 논리 좌표 투영이다. */
static void simulate_window_state(int width, int height, float dpi_x, float dpi_y,
                                  float logical_ratio, bool stale_viewport)
{
    rlDrawRenderBatchActive();
    int viewport_width = width;
    int viewport_height = height;
    if(stale_viewport)
    {
        viewport_width /= 2;
        viewport_height /= 2;
    }
    rlViewport(0, 0, viewport_width, viewport_height);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, (double)width / logical_ratio, (double)height / logical_ratio, 0, 0, 1);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();
    rlScalef(dpi_x, dpi_y, 1.0f);
}

/* LoadImageFromScreen은 플랫폼 DPI를 다시 곱하므로 사용하지 않는다.
 * 이미 확인한 실제 프레임버퍼 크기로 읽는다. 반환 배열은 위에서 아래 순서다. */
static size_t mismatched_pixels(int width, int height)
{
    rlDrawRenderBatchActive();
    unsigned char *pixels = rlReadScreenPixels(width, height);
    assert(pixels != NULL);
    size_t mismatches = 0;
    for(int y = 0; y < height; ++y)
    {
        for(int x = 0; x < width; ++x)
        {
            Color expected = pattern_color(x, y, width, height);
            size_t index = ((size_t)y * (size_t)width + (size_t)x) * 4;
            if(pixels[index] != expected.r || pixels[index + 1] != expected.g ||
               pixels[index + 2] != expected.b)
                ++mismatches;
        }
    }
    MemFree(pixels);
    return mismatches;
}

/* 같은 입력 상태에서 기존 복사는 확대/잘림을 일으키고 수정된 복사는
 * 패턴 전체를 보존하는지 검사한다. 픽셀 결과를 비교하므로 호출 순서만
 * 흉내 낸 모의 검사가 아니라 실제 그리기 결과를 확인한다. */
static void check_presentation(RenderTexture2D canvas, int width, int height,
                               float dpi_x, float dpi_y, float logical_ratio, bool stale_viewport)
{
    Rectangle source = {0, 0, (float)width, -(float)height};
    Rectangle old_destination = {0, 0, (float)width / logical_ratio,
                                      (float)height / logical_ratio};
    Vector2 origin = {0};
    BeginDrawing();
    ClearBackground(BLACK);
    simulate_window_state(width, height, dpi_x, dpi_y, logical_ratio, stale_viewport);
    DrawTexturePro(canvas.texture, source, old_destination, origin, 0, WHITE);
    size_t old_mismatches = mismatched_pixels(width, height);
    if(dpi_x > 1.0f || dpi_y > 1.0f || stale_viewport)
        assert(old_mismatches > 0);
    else
        assert(old_mismatches == 0);

    ClearBackground(BLACK);
    simulate_window_state(width, height, dpi_x, dpi_y, logical_ratio, stale_viewport);
    bb_present_canvas(canvas, width, height);
    size_t new_mismatches = mismatched_pixels(width, height);
    EndDrawing();
    printf("Presentation %dx%d dpi=%.2fx%.2f logical=%.1f stale=%d: old=%zu new=%zu\n",
           width, height, (double)dpi_x, (double)dpi_y, (double)logical_ratio, stale_viewport,
           old_mismatches, new_mismatches);
    assert(new_mismatches == 0);
}

/* 창 모드 크기를 바꾼 뒤 이벤트를 처리해 실제 출력 크기를 읽는다.
 * 960×540 창은 Retina에서 1920×1080 프레임버퍼를 만들어 FHD도 검사한다. */
int main(void)
{
    const int sizes[][2] = {{320, 240}, {960, 540}, {640, 360}};
    const float scales[] = {1.0f, 1.25f, 1.5f, 2.0f};
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_WINDOW_HIGHDPI);
    InitWindow(sizes[0][0], sizes[0][1], "Bubble Bobble presentation regression");
    assert(IsWindowReady());
    SetTargetFPS(60);
    SetExitKey(KEY_NULL);
    for(unsigned size = 0; size < sizeof sizes / sizeof sizes[0]; ++size)
    {
        SetWindowSize(sizes[size][0], sizes[size][1]);
        for(int frame = 0; frame < 3; ++frame)
        {
            BeginDrawing();
            ClearBackground(BLACK);
            EndDrawing();
        }
        int width = GetRenderWidth();
        int height = GetRenderHeight();
        assert(width > 0 && height > 0);
        RenderTexture2D canvas = make_canvas(width, height);
        for(unsigned scale = 0; scale < sizeof scales / sizeof scales[0]; ++scale)
        {
            check_presentation(canvas, width, height, scales[scale], scales[scale], 1.0f, false);
            check_presentation(canvas, width, height, scales[scale], scales[scale], 2.0f, false);
        }
        check_presentation(canvas, width, height, 1.5f, 1.5f, 1.0f, true);
        check_presentation(canvas, width, height, 2.0f, 2.0f, 2.0f, true);
        check_presentation(canvas, width, height, 1.5f, 1.25f, 1.0f, false);
        check_presentation(canvas, width, height, 1.25f, 1.5f, 2.0f, false);
        UnloadRenderTexture(canvas);
    }
    CloseWindow();
    puts("36 physical/logical DPI and resize presentation cases passed.");
    return 0;
}
