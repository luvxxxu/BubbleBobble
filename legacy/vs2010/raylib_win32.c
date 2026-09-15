#define WIN32_LEAN_AND_MEAN
/* Win32/GDI+의 전역 이름이 raylib 호환 API의 이름과 겹친다. */
#define Rectangle BBWin32Rectangle
#define CloseWindow BBWin32CloseWindow
#define LoadImage BBWin32LoadImage
#define DrawText BBWin32DrawText
#define DrawTextEx BBWin32DrawTextEx
#define Color BBGdiPlusColor
#include <windows.h>
#include <propidl.h>
#include <gdiplus.h>
#undef Color
#undef DrawTextEx
#undef DrawText
#undef LoadImage
#undef CloseWindow
#undef Rectangle

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raylib.h"

#define BB_RESOURCE_COUNT 128

typedef struct BBBitmap {
    HBITMAP bitmap;
    HDC dc;
    int width;
    int height;
    bool render_target;
} BBBitmap;

typedef struct BBImage {
    HBITMAP bitmap;
    int width;
    int height;
} BBImage;

const Color WHITE = {255, 255, 255, 255};
const Color BLACK = {0, 0, 0, 255};
const Color GRAY = {130, 130, 130, 255};
const Color YELLOW = {253, 249, 0, 255};
const Color RED = {230, 41, 55, 255};

static HWND bb_window;
static BBBitmap bb_screen;
static BBBitmap bb_resources[BB_RESOURCE_COUNT];
static HDC bb_current_dc;
static bool bb_should_close;
static bool bb_key_now[256];
static bool bb_key_before[256];
static int bb_key_queue[64];
static int bb_key_queue_count;
static LARGE_INTEGER bb_frequency;
static LARGE_INTEGER bb_last_tick;
static float bb_frame_time = 1.0f / 60.0f;
static int bb_target_fps = 60;
static ULONG_PTR bb_gdiplus_token;
static LoadFileDataCallback bb_file_loader;
static unsigned int bb_config_flags;

static bool bb_asset_exists(const char *path)
{
    FILE *file;
    if (bb_file_loader != NULL) {
        int size = 0;
        unsigned char *data = bb_file_loader(path, &size);
        if (data == NULL || size < 0) return false;
        MemFree(data);
        return true;
    }
    file = fopen(path, "rb");
    if (file == NULL) return false;
    fclose(file);
    return true;
}

static DWORD bb_colorref(Color color)
{
    return RGB(color.r, color.g, color.b);
}

static void bb_pump_messages(void)
{
    MSG message;
    int key;
    while (PeekMessage(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) bb_should_close = true;
        TranslateMessage(&message);
        DispatchMessage(&message);
    }
    bb_key_queue_count = 0;
    for (key = 0; key < 256; ++key) {
        bb_key_before[key] = bb_key_now[key];
        bb_key_now[key] = (GetAsyncKeyState(key) & 0x8000) != 0;
        if (bb_key_now[key] && !bb_key_before[key] && bb_key_queue_count < 64)
            bb_key_queue[bb_key_queue_count++] = key;
    }
}

static LRESULT CALLBACK bb_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    (void)lparam;
    if (message == WM_CLOSE) {
        bb_should_close = true;
        DestroyWindow(window);
        return 0;
    }
    if (message == WM_DESTROY) {
        bb_should_close = true;
        PostQuitMessage(0);
        return 0;
    }
    if (message == WM_KEYDOWN && wparam == VK_ESCAPE) return 0;
    return DefWindowProc(window, message, wparam, lparam);
}

static BBBitmap bb_make_bitmap(int width, int height)
{
    BBBitmap result;
    BITMAPINFO info;
    void *bits;
    memset(&result, 0, sizeof result);
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    result.bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (result.bitmap == NULL) return result;
    result.dc = CreateCompatibleDC(NULL);
    if (result.dc == NULL) {
        DeleteObject(result.bitmap);
        memset(&result, 0, sizeof result);
        return result;
    }
    SelectObject(result.dc, result.bitmap);
    result.width = width;
    result.height = height;
    return result;
}

static void bb_release_bitmap(BBBitmap *bitmap)
{
    if (bitmap->dc != NULL) DeleteDC(bitmap->dc);
    if (bitmap->bitmap != NULL) DeleteObject(bitmap->bitmap);
    memset(bitmap, 0, sizeof *bitmap);
}

static unsigned int bb_store_bitmap(BBBitmap bitmap)
{
    unsigned int index;
    for (index = 1; index < BB_RESOURCE_COUNT; ++index) {
        if (bb_resources[index].bitmap == NULL) {
            bb_resources[index] = bitmap;
            return index;
        }
    }
    bb_release_bitmap(&bitmap);
    return 0;
}

static BBBitmap *bb_bitmap_for_texture(Texture2D texture)
{
    if (texture.id == 0 || texture.id >= BB_RESOURCE_COUNT) return NULL;
    if (bb_resources[texture.id].bitmap == NULL) return NULL;
    return &bb_resources[texture.id];
}

static wchar_t *bb_utf8_to_wide(const char *value)
{
    int length;
    wchar_t *wide;
    if (value == NULL) return NULL;
    length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, NULL, 0);
    if (length <= 0) return NULL;
    wide = (wchar_t *)malloc((size_t)length * sizeof *wide);
    if (wide == NULL) return NULL;
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value, -1, wide, length) <= 0) {
        free(wide);
        return NULL;
    }
    return wide;
}

static HBITMAP bb_load_hbitmap(const char *path, int *width, int *height)
{
    GpImage *image;
    HBITMAP bitmap;
    UINT image_width;
    UINT image_height;
    wchar_t *wide = bb_utf8_to_wide(path);
    if (wide == NULL) return NULL;
    image = NULL;
    bitmap = NULL;
    if (GdipLoadImageFromFile(wide, &image) == Ok && image != NULL &&
        GdipGetImageWidth(image, &image_width) == Ok &&
        GdipGetImageHeight(image, &image_height) == Ok &&
        GdipCreateHBITMAPFromBitmap((GpBitmap *)image, &bitmap, 0) == Ok) {
        *width = (int)image_width;
        *height = (int)image_height;
    }
    else bitmap = NULL;
    if (image != NULL) GdipDisposeImage(image);
    free(wide);
    return bitmap;
}

static void bb_draw_bitmap(BBBitmap *source, Rectangle src, Rectangle dst)
{
    BLENDFUNCTION blend;
    BBBitmap flipped;
    BBBitmap *draw_source;
    bool flip_x;
    bool flip_y;
    int source_x;
    int source_y;
    int source_width;
    int source_height;
    if (bb_current_dc == NULL || source == NULL || source->dc == NULL) return;
    source_x = (int)src.x;
    source_y = (int)src.y;
    source_width = (int)src.width;
    source_height = (int)src.height;
    flip_x = source_width < 0;
    flip_y = source_height < 0;
    if (flip_x) source_width = -source_width;
    if (flip_y) source_height = -source_height;
    /* GDI의 렌더 대상은 raylib OpenGL 텍스처와 달리 이미 위쪽이 먼저다. */
    if (source->render_target) flip_y = false;
    if (source_width <= 0 || source_height <= 0 || dst.width == 0 || dst.height == 0) return;
    draw_source = source;
    memset(&flipped, 0, sizeof flipped);
    if (flip_x || flip_y) {
        flipped = bb_make_bitmap(source_width, source_height);
        if (flipped.dc == NULL) return;
        StretchBlt(flipped.dc, flip_x ? source_width : 0, flip_y ? source_height : 0,
                  flip_x ? -source_width : source_width, flip_y ? -source_height : source_height,
                  source->dc, source_x, source_y, source_width, source_height, SRCCOPY);
        draw_source = &flipped;
        source_x = 0;
        source_y = 0;
    }
    blend.BlendOp = AC_SRC_OVER;
    blend.BlendFlags = 0;
    blend.SourceConstantAlpha = 255;
    blend.AlphaFormat = AC_SRC_ALPHA;
    AlphaBlend(bb_current_dc, (int)dst.x, (int)dst.y, (int)dst.width, (int)dst.height,
               draw_source->dc, source_x, source_y, source_width, source_height, blend);
    bb_release_bitmap(&flipped);
}

void SetConfigFlags(unsigned int flags) { bb_config_flags = flags; }

void InitWindow(int width, int height, const char *title)
{
    WNDCLASSA window_class;
    GdiplusStartupInput startup_input;
    RECT rect;
    QueryPerformanceFrequency(&bb_frequency);
    QueryPerformanceCounter(&bb_last_tick);
    memset(&startup_input, 0, sizeof startup_input);
    startup_input.GdiplusVersion = 1;
    GdiplusStartup(&bb_gdiplus_token, &startup_input, NULL);
    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = bb_window_proc;
    window_class.hInstance = GetModuleHandle(NULL);
    window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
    window_class.lpszClassName = "BubbleBobbleVS2010";
    RegisterClassA(&window_class);
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    bb_window = CreateWindowA(window_class.lpszClassName, title,
                              (bb_config_flags & FLAG_WINDOW_RESIZABLE ? WS_OVERLAPPEDWINDOW : WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX) | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                              NULL, NULL, window_class.hInstance, NULL);
    bb_screen = bb_make_bitmap(width, height);
    bb_current_dc = bb_screen.dc;
}

void CloseWindow(void)
{
    unsigned int index;
    for (index = 1; index < BB_RESOURCE_COUNT; ++index) bb_release_bitmap(&bb_resources[index]);
    bb_release_bitmap(&bb_screen);
    if (bb_window != NULL) DestroyWindow(bb_window);
    bb_window = NULL;
    if (bb_gdiplus_token != 0) GdiplusShutdown(bb_gdiplus_token);
    bb_gdiplus_token = 0;
}

bool WindowShouldClose(void)
{
    bb_pump_messages();
    return bb_should_close;
}

bool IsWindowReady(void) { return bb_window != NULL && bb_screen.bitmap != NULL; }
bool IsWindowFocused(void) { return bb_window != NULL && GetForegroundWindow() == bb_window; }
void ToggleFullscreen(void) { }
int GetScreenWidth(void) { return bb_screen.width; }
int GetScreenHeight(void) { return bb_screen.height; }
void SetWindowMinSize(int width, int height) { (void)width; (void)height; }
void SetExitKey(int key) { (void)key; }
void SetTargetFPS(int fps) { bb_target_fps = fps > 0 ? fps : 60; }
float GetFrameTime(void) { return bb_frame_time; }
void SetTraceLogLevel(int logLevel) { (void)logLevel; }

void BeginDrawing(void) { bb_current_dc = bb_screen.dc; }

void EndDrawing(void)
{
    HDC window_dc;
    LARGE_INTEGER now;
    double elapsed;
    DWORD target_milliseconds;
    if (bb_window != NULL && bb_screen.dc != NULL) {
        window_dc = GetDC(bb_window);
        BitBlt(window_dc, 0, 0, bb_screen.width, bb_screen.height, bb_screen.dc, 0, 0, SRCCOPY);
        ReleaseDC(bb_window, window_dc);
    }
    QueryPerformanceCounter(&now);
    elapsed = (double)(now.QuadPart - bb_last_tick.QuadPart) / (double)bb_frequency.QuadPart;
    target_milliseconds = bb_target_fps > 0 ? (DWORD)(1000 / bb_target_fps) : 0;
    if (target_milliseconds > 0 && elapsed * 1000.0 < target_milliseconds) Sleep(target_milliseconds - (DWORD)(elapsed * 1000.0));
    QueryPerformanceCounter(&now);
    bb_frame_time = (float)((double)(now.QuadPart - bb_last_tick.QuadPart) / (double)bb_frequency.QuadPart);
    bb_last_tick = now;
    bb_pump_messages();
}

void BeginTextureMode(RenderTexture2D target)
{
    BBBitmap *bitmap = bb_bitmap_for_texture(target.texture);
    if (bitmap != NULL) bb_current_dc = bitmap->dc;
}

void EndTextureMode(void) { bb_current_dc = bb_screen.dc; }

void ClearBackground(Color color)
{
    HBRUSH brush;
    RECT rect;
    if (bb_current_dc == NULL) return;
    rect.left = 0;
    rect.top = 0;
    rect.right = bb_screen.width;
    rect.bottom = bb_screen.height;
    brush = CreateSolidBrush(bb_colorref(color));
    FillRect(bb_current_dc, &rect, brush);
    DeleteObject(brush);
}

bool IsKeyDown(int key) { return key >= 0 && key < 256 && bb_key_now[key]; }
bool IsKeyPressed(int key) { return key >= 0 && key < 256 && bb_key_now[key] && !bb_key_before[key]; }

int GetKeyPressed(void)
{
    int key;
    if (bb_key_queue_count == 0) return KEY_NULL;
    key = bb_key_queue[0];
    memmove(bb_key_queue, bb_key_queue + 1, (size_t)(bb_key_queue_count - 1) * sizeof bb_key_queue[0]);
    --bb_key_queue_count;
    return key;
}

int GetCharPressed(void) { return 0; }

bool IsGamepadAvailable(int gamepad) { (void)gamepad; return false; }
float GetGamepadAxisMovement(int gamepad, int axis) { (void)gamepad; (void)axis; return 0.0f; }
bool IsGamepadButtonDown(int gamepad, int button) { (void)gamepad; (void)button; return false; }
bool IsGamepadButtonPressed(int gamepad, int button) { (void)gamepad; (void)button; return false; }
void *MemAlloc(unsigned int size) { return malloc(size); }
void MemFree(void *memory) { free(memory); }
void SetLoadFileDataCallback(LoadFileDataCallback callback) { bb_file_loader = callback; }

Image LoadImage(const char *fileName)
{
    Image image;
    BBImage *stored;
    memset(&image, 0, sizeof image);
    if (!bb_asset_exists(fileName)) return image;
    stored = (BBImage *)malloc(sizeof *stored);
    if (stored == NULL) return image;
    stored->bitmap = bb_load_hbitmap(fileName, &stored->width, &stored->height);
    if (stored->bitmap == NULL) {
        free(stored);
        return image;
    }
    image.data = stored;
    image.width = stored->width;
    image.height = stored->height;
    image.mipmaps = 1;
    return image;
}

Image LoadImageFromTexture(Texture2D texture)
{
    Image image;
    BBBitmap *source = bb_bitmap_for_texture(texture);
    BBImage *stored;
    memset(&image, 0, sizeof image);
    if (source == NULL) return image;
    stored = (BBImage *)malloc(sizeof *stored);
    if (stored == NULL) return image;
    stored->bitmap = (HBITMAP)CopyImage(source->bitmap, IMAGE_BITMAP, 0, 0, LR_CREATEDIBSECTION);
    if (stored->bitmap == NULL) {
        free(stored);
        return image;
    }
    stored->width = source->width;
    stored->height = source->height;
    image.data = stored;
    image.width = stored->width;
    image.height = stored->height;
    image.mipmaps = 1;
    return image;
}

bool IsImageValid(Image image) { return image.data != NULL && image.width > 0 && image.height > 0; }
void ImageFlipVertical(Image *image) { (void)image; }

Color *LoadImageColors(Image image)
{
    BBImage *stored = (BBImage *)image.data;
    BITMAPINFO info;
    unsigned char *pixels;
    Color *colors;
    HDC dc;
    int x;
    int y;
    if (stored == NULL || stored->bitmap == NULL) return NULL;
    colors = (Color *)malloc((size_t)stored->width * (size_t)stored->height * sizeof *colors);
    pixels = (unsigned char *)malloc((size_t)stored->width * (size_t)stored->height * 4);
    if (colors == NULL || pixels == NULL) {
        free(colors);
        free(pixels);
        return NULL;
    }
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = stored->width;
    info.bmiHeader.biHeight = -stored->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc = CreateCompatibleDC(NULL);
    if (dc == NULL || GetDIBits(dc, stored->bitmap, 0, (UINT)stored->height, pixels, &info, DIB_RGB_COLORS) == 0) {
        if (dc != NULL) DeleteDC(dc);
        free(colors);
        free(pixels);
        return NULL;
    }
    DeleteDC(dc);
    for (y = 0; y < stored->height; ++y) {
        for (x = 0; x < stored->width; ++x) {
            unsigned char *pixel = pixels + ((size_t)y * (size_t)stored->width + (size_t)x) * 4;
            colors[(size_t)y * (size_t)stored->width + (size_t)x].r = pixel[2];
            colors[(size_t)y * (size_t)stored->width + (size_t)x].g = pixel[1];
            colors[(size_t)y * (size_t)stored->width + (size_t)x].b = pixel[0];
            colors[(size_t)y * (size_t)stored->width + (size_t)x].a = pixel[3];
        }
    }
    free(pixels);
    return colors;
}

void UnloadImageColors(Color *colors) { free(colors); }

void UnloadImage(Image image)
{
    BBImage *stored = (BBImage *)image.data;
    if (stored != NULL) {
        if (stored->bitmap != NULL) DeleteObject(stored->bitmap);
        free(stored);
    }
}

unsigned char *ExportImageToMemory(Image image, const char *fileType, int *fileSize)
{
    (void)image;
    (void)fileType;
    if (fileSize != NULL) *fileSize = 0;
    return NULL;
}

Texture2D LoadTexture(const char *fileName)
{
    Texture2D texture;
    BBBitmap bitmap;
    unsigned int id;
    memset(&texture, 0, sizeof texture);
    memset(&bitmap, 0, sizeof bitmap);
    if (!bb_asset_exists(fileName)) return texture;
    bitmap.bitmap = bb_load_hbitmap(fileName, &bitmap.width, &bitmap.height);
    if (bitmap.bitmap == NULL) return texture;
    bitmap.dc = CreateCompatibleDC(NULL);
    if (bitmap.dc == NULL) {
        DeleteObject(bitmap.bitmap);
        return texture;
    }
    SelectObject(bitmap.dc, bitmap.bitmap);
    id = bb_store_bitmap(bitmap);
    if (id == 0) return texture;
    texture.id = id;
    texture.width = bb_resources[id].width;
    texture.height = bb_resources[id].height;
    texture.mipmaps = 1;
    return texture;
}

void UnloadTexture(Texture2D texture)
{
    if (texture.id > 0 && texture.id < BB_RESOURCE_COUNT) bb_release_bitmap(&bb_resources[texture.id]);
}

RenderTexture2D LoadRenderTexture(int width, int height)
{
    RenderTexture2D target;
    BBBitmap bitmap = bb_make_bitmap(width, height);
    unsigned int id;
    memset(&target, 0, sizeof target);
    if (bitmap.bitmap == NULL) return target;
    bitmap.render_target = true;
    id = bb_store_bitmap(bitmap);
    if (id == 0) return target;
    target.id = id;
    target.texture.id = id;
    target.texture.width = width;
    target.texture.height = height;
    target.texture.mipmaps = 1;
    return target;
}

bool IsRenderTextureValid(RenderTexture2D target) { return bb_bitmap_for_texture(target.texture) != NULL; }
void UnloadRenderTexture(RenderTexture2D target) { UnloadTexture(target.texture); }
void SetTextureFilter(Texture2D texture, int filter) { (void)texture; (void)filter; }

void DrawTexture(Texture2D texture, int posX, int posY, Color tint)
{
    BBBitmap *source = bb_bitmap_for_texture(texture);
    Rectangle src;
    Rectangle dst;
    (void)tint;
    if (source == NULL) return;
    src.x = 0;
    src.y = 0;
    src.width = (float)source->width;
    src.height = (float)source->height;
    dst.x = (float)posX;
    dst.y = (float)posY;
    dst.width = src.width;
    dst.height = src.height;
    bb_draw_bitmap(source, src, dst);
}

void DrawTextureRec(Texture2D texture, Rectangle source, Vector2 position, Color tint)
{
    BBBitmap *bitmap = bb_bitmap_for_texture(texture);
    Rectangle destination;
    (void)tint;
    if (bitmap == NULL) return;
    destination.x = position.x;
    destination.y = position.y;
    destination.width = source.width;
    destination.height = source.height;
    bb_draw_bitmap(bitmap, source, destination);
}

void DrawTexturePro(Texture2D texture, Rectangle source, Rectangle destination, Vector2 origin, float rotation, Color tint)
{
    (void)origin;
    (void)rotation;
    (void)tint;
    bb_draw_bitmap(bb_bitmap_for_texture(texture), source, destination);
}

void DrawRectangle(int posX, int posY, int width, int height, Color color)
{
    RECT rect;
    HBRUSH brush;
    if (bb_current_dc == NULL) return;
    rect.left = posX;
    rect.top = posY;
    rect.right = posX + width;
    rect.bottom = posY + height;
    brush = CreateSolidBrush(bb_colorref(color));
    FillRect(bb_current_dc, &rect, brush);
    DeleteObject(brush);
}

void DrawCircle(int centerX, int centerY, float radius, Color color)
{
    HBRUSH brush;
    HGDIOBJ old_brush;
    if (bb_current_dc == NULL) return;
    brush = CreateSolidBrush(bb_colorref(color));
    old_brush = SelectObject(bb_current_dc, brush);
    Ellipse(bb_current_dc, (int)(centerX - radius), (int)(centerY - radius), (int)(centerX + radius), (int)(centerY + radius));
    SelectObject(bb_current_dc, old_brush);
    DeleteObject(brush);
}

bool CheckCollisionRecs(Rectangle a, Rectangle b)
{
    return a.x < b.x + b.width && a.x + a.width > b.x && a.y < b.y + b.height && a.y + a.height > b.y;
}

bool CheckCollisionCircles(Vector2 a, float ar, Vector2 b, float br)
{
    float x = a.x - b.x;
    float y = a.y - b.y;
    float radius = ar + br;
    return x * x + y * y < radius * radius;
}

Font GetFontDefault(void)
{
    Font font;
    memset(&font, 0, sizeof font);
    font.baseSize = 8;
    return font;
}

Font LoadFontEx(const char *fileName, int fontSize, int *codepoints, int codepointCount)
{
    Font font;
    FILE *file;
    (void)codepoints;
    (void)codepointCount;
    font = GetFontDefault();
    file = fopen(fileName, "rb");
    if (file == NULL) return font;
    fclose(file);
    font.baseSize = fontSize;
    font.glyphCount = 95;
    font.texture.id = 1;
    return font;
}

bool IsFontValid(Font font) { return font.glyphCount > 0 && font.texture.id != 0; }
void UnloadFont(Font font) { (void)font; }

void DrawText(const char *text, int posX, int posY, int fontSize, Color color)
{
    HFONT font;
    HGDIOBJ old_font;
    COLORREF old_color;
    int old_mode;
    if (bb_current_dc == NULL || text == NULL) return;
    font = CreateFontA(-fontSize, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH, "Terminal");
    old_font = SelectObject(bb_current_dc, font);
    old_color = SetTextColor(bb_current_dc, bb_colorref(color));
    old_mode = SetBkMode(bb_current_dc, TRANSPARENT);
    TextOutA(bb_current_dc, posX, posY, text, (int)strlen(text));
    SetBkMode(bb_current_dc, old_mode);
    SetTextColor(bb_current_dc, old_color);
    SelectObject(bb_current_dc, old_font);
    DeleteObject(font);
}

void DrawTextEx(Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint)
{
    (void)font;
    (void)spacing;
    DrawText(text, (int)position.x, (int)position.y, (int)fontSize, tint);
}

Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing)
{
    Vector2 value;
    (void)font;
    value.x = (float)strlen(text) * (fontSize * 0.6f + spacing);
    value.y = fontSize;
    return value;
}

void InitAudioDevice(void) { }
void CloseAudioDevice(void) { }
bool IsAudioDeviceReady(void) { return false; }
void SetMasterVolume(float volume) { (void)volume; }

Wave LoadWave(const char *fileName)
{
    Wave wave;
    FILE *file;
    memset(&wave, 0, sizeof wave);
    file = fopen(fileName, "rb");
    if (file == NULL) return wave;
    fclose(file);
    wave.data = malloc(1);
    return wave;
}

bool IsWaveValid(Wave wave) { return wave.data != NULL; }
void UnloadWave(Wave wave) { free(wave.data); }
Sound LoadSound(const char *fileName) { Sound sound; (void)fileName; sound.frameCount = 0; return sound; }
bool IsSoundValid(Sound sound) { return sound.frameCount != 0; }
void UnloadSound(Sound sound) { (void)sound; }
void PlaySound(Sound sound) { (void)sound; }
Music LoadMusicStreamFromMemory(const char *fileType, const unsigned char *data, int dataSize) { Music music; (void)fileType; (void)data; (void)dataSize; music.looping = false; return music; }
bool IsMusicValid(Music music) { (void)music; return false; }
void UnloadMusicStream(Music music) { (void)music; }
void PlayMusicStream(Music music) { (void)music; }
void UpdateMusicStream(Music music) { (void)music; }
void SetMusicVolume(Music music, float volume) { (void)music; (void)volume; }
