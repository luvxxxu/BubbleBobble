#define WIN32_LEAN_AND_MEAN
/* Win32의 함수 이름이 raylib 호환 API의 이름과 겹친다. */
#define Rectangle BBWin32Rectangle
#define CloseWindow BBWin32CloseWindow
#include <windows.h>
#undef CloseWindow
#undef Rectangle

/* Win32 exposes these as ANSI/Unicode selection macros. */
#ifdef LoadImage
#undef LoadImage
#endif
#ifdef DrawText
#undef DrawText
#endif
#ifdef DrawTextEx
#undef DrawTextEx
#endif

#include "bb_gdiplus_flat.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "raylib.h"
#include "bb_platform.h"

/* Texture ID zero means invalid. Remaining IDs own one HBITMAP/DC pair in
 * this fixed table, including off-screen render targets. */
#define BB_RESOURCE_COUNT 128
#define BB_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 ((HANDLE)(LONG_PTR)-4)
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif
#ifndef WM_UNICHAR
#define WM_UNICHAR 0x0109
#endif
#ifndef UNICODE_NOCHAR
#define UNICODE_NOCHAR 0xFFFF
#endif

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

typedef struct BBFontResource {
    wchar_t *path;
    HFONT handle;
    int pixel_size;
} BBFontResource;

typedef BOOL (WINAPI *BBSetProcessDpiAwarenessContextFn)(HANDLE context);
typedef BOOL (WINAPI *BBSetProcessDPIAwareFn)(void);

typedef union BBDpiFunction {
    FARPROC procedure;
    BBSetProcessDpiAwarenessContextFn set_context;
    BBSetProcessDPIAwareFn set_legacy;
} BBDpiFunction;

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
static bool bb_has_focus;
static bool bb_key_now[256];
static bool bb_key_before[256];
static int bb_key_queue[64];
static int bb_key_queue_count;
static int bb_char_queue[64];
static int bb_char_queue_count;
static unsigned int bb_high_surrogate;
static int bb_pending_client_width;
static int bb_pending_client_height;
static int bb_minimum_client_width;
static int bb_minimum_client_height;
static bool bb_fullscreen;
static LONG bb_windowed_style;
static WINDOWPLACEMENT bb_windowed_placement;
static LARGE_INTEGER bb_frequency;
static LARGE_INTEGER bb_last_tick;
static float bb_frame_time = 1.0f / 60.0f;
static int bb_target_fps = 60;
static ULONG_PTR bb_gdiplus_token;
static bool bb_gdiplus_startup_failed;
static bool bb_gdiplus_atexit_registered;
static LoadFileDataCallback bb_file_loader;
static unsigned int bb_config_flags;

static BBBitmap bb_make_bitmap(int width, int height);
static bool bb_apply_pending_resize(void);

/*
 * VS2010's Windows SDK predates per-monitor-v2 DPI declarations.  Resolve the
 * newer entry point at run time so the same x86/x64 executable remains
 * loadable on older Windows versions, then fall back to Vista's system-aware
 * API.  This must run before the first HWND is created to prevent Windows from
 * bitmap-scaling the completed pixel-art frame.
 */
static void bb_enable_dpi_awareness(void)
{
    static bool attempted;
    HMODULE user32;
    BBDpiFunction function;
    if (attempted) return;
    attempted = true;
    user32 = GetModuleHandleA("user32.dll");
    if (user32 == NULL) return;
    function.procedure = GetProcAddress(user32, "SetProcessDpiAwarenessContext");
    if (function.procedure != NULL &&
        function.set_context(BB_DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    function.procedure = GetProcAddress(user32, "SetProcessDPIAware");
    if (function.procedure != NULL) (void)function.set_legacy();
}

static void bb_gdiplus_shutdown(void)
{
    if (bb_gdiplus_token != 0) GdiplusShutdown(bb_gdiplus_token);
    bb_gdiplus_token = 0;
}

/*
 * Asset validation runs before InitWindow().  GDI+ must therefore be ready
 * independently of window creation; this helper is intentionally idempotent
 * and also covers callers that load an image before installing a file loader.
 */
static bool bb_gdiplus_startup(void)
{
    BBGdipStartupInput startup_input;
    BBGdipStatus status;
    bb_enable_dpi_awareness();
    if (bb_gdiplus_token != 0) return true;
    if (bb_gdiplus_startup_failed) return false;
    memset(&startup_input, 0, sizeof startup_input);
    startup_input.GdiplusVersion = 1;
    status = GdiplusStartup(&bb_gdiplus_token, &startup_input, NULL);
    if (status != BB_GDIP_OK || bb_gdiplus_token == 0) {
        bb_gdiplus_token = 0;
        bb_gdiplus_startup_failed = true;
        fprintf(stderr, "Could not initialize GDI+ (status %d).\n", (int)status);
        return false;
    }
    if (!bb_gdiplus_atexit_registered) {
        if (atexit(bb_gdiplus_shutdown) == 0) bb_gdiplus_atexit_registered = true;
    }
    return true;
}

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
    file = bb_platform_fopen(path, "rb");
    if (file == NULL) return false;
    fclose(file);
    return true;
}

static DWORD bb_colorref(Color color)
{
    return RGB(color.r, color.g, color.b);
}

static void bb_set_minimum_track_size(HWND window, MINMAXINFO *limits)
{
    RECT client_rect;
    RECT window_rect;
    int client_width;
    int client_height;
    int frame_width;
    int frame_height;
    int minimum_width;
    int minimum_height;
    if (limits == NULL || bb_minimum_client_width <= 0 || bb_minimum_client_height <= 0) return;
    if (!GetClientRect(window, &client_rect) || !GetWindowRect(window, &window_rect)) return;
    client_width = (int)(client_rect.right - client_rect.left);
    client_height = (int)(client_rect.bottom - client_rect.top);
    frame_width = (int)(window_rect.right - window_rect.left) - client_width;
    frame_height = (int)(window_rect.bottom - window_rect.top) - client_height;
    if (frame_width < 0) frame_width = 0;
    if (frame_height < 0) frame_height = 0;
    minimum_width = bb_minimum_client_width > INT_MAX - frame_width ?
        INT_MAX : bb_minimum_client_width + frame_width;
    minimum_height = bb_minimum_client_height > INT_MAX - frame_height ?
        INT_MAX : bb_minimum_client_height + frame_height;
    if (limits->ptMinTrackSize.x < minimum_width) limits->ptMinTrackSize.x = minimum_width;
    if (limits->ptMinTrackSize.y < minimum_height) limits->ptMinTrackSize.y = minimum_height;
}

static void bb_set_window_client_size(HWND window, int width, int height)
{
    RECT client_rect;
    RECT window_rect;
    int client_width;
    int client_height;
    int window_width;
    int window_height;
    if (window == NULL || width <= 0 || height <= 0) return;
    if (!GetClientRect(window, &client_rect) || !GetWindowRect(window, &window_rect)) return;
    client_width = (int)(client_rect.right - client_rect.left);
    client_height = (int)(client_rect.bottom - client_rect.top);
    window_width = (int)(window_rect.right - window_rect.left);
    window_height = (int)(window_rect.bottom - window_rect.top);
    window_width -= client_width;
    window_height -= client_height;
    if (window_width < 0) window_width = 0;
    if (window_height < 0) window_height = 0;
    if (width > INT_MAX - window_width || height > INT_MAX - window_height) return;
    window_width += width;
    window_height += height;
    if (window_width <= 0 || window_height <= 0) return;
    (void)SetWindowPos(window, NULL, 0, 0, window_width, window_height,
                       SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

static bool bb_set_window_style(HWND window, LONG style)
{
    LONG previous;
    SetLastError(ERROR_SUCCESS);
    previous = SetWindowLongW(window, GWL_STYLE, style);
    return previous != 0 || GetLastError() == ERROR_SUCCESS;
}

/* Snapshot key state before dispatching this frame's messages. The key and
 * character queues retain discrete presses while IsKeyPressed uses the edge. */
static void bb_pump_messages(void)
{
    MSG message;
    int key;
    for (key = 0; key < 256; ++key) bb_key_before[key] = bb_key_now[key];
    bb_key_queue_count = 0;
    bb_char_queue_count = 0;
    while (PeekMessageW(&message, NULL, 0, 0, PM_REMOVE)) {
        if (message.message == WM_QUIT) bb_should_close = true;
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (!bb_should_close) (void)bb_apply_pending_resize();
}

static int bb_key_from_message(WPARAM wparam, LPARAM lparam)
{
    int key;
    key = (int)wparam;
    /* WM_KEY* reports both Control keys as VK_CONTROL.  The extended-key bit
     * is the documented way to preserve the right-control binding. */
    if (key == VK_CONTROL)
        key = (lparam & ((LPARAM)1 << 24)) != 0 ? VK_RCONTROL : VK_LCONTROL;
    return key;
}

static void bb_queue_character(unsigned int codepoint)
{
    if (codepoint > 0 && codepoint <= 0x10FFFFu &&
        !(codepoint >= 0xD800u && codepoint <= 0xDFFFu) &&
        bb_char_queue_count < 64)
        bb_char_queue[bb_char_queue_count++] = (int)codepoint;
}

/* WM_CHAR delivers UTF-16 code units. Pair surrogates before exposing Unicode
 * code points through GetCharPressed, replacing unmatched units. */
static void bb_queue_utf16_unit(unsigned int unit)
{
    if (unit >= 0xD800u && unit <= 0xDBFFu) {
        if (bb_high_surrogate != 0) bb_queue_character(0xFFFDu);
        bb_high_surrogate = unit;
        return;
    }
    if (unit >= 0xDC00u && unit <= 0xDFFFu) {
        if (bb_high_surrogate != 0) {
            bb_queue_character(0x10000u +
                               ((bb_high_surrogate - 0xD800u) << 10) +
                               (unit - 0xDC00u));
            bb_high_surrogate = 0;
        }
        else bb_queue_character(0xFFFDu);
        return;
    }
    if (bb_high_surrogate != 0) {
        bb_queue_character(0xFFFDu);
        bb_high_surrogate = 0;
    }
    bb_queue_character(unit);
}

static LRESULT CALLBACK bb_window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam)
{
    int key;
    if (message == WM_DPICHANGED) {
        const RECT *suggested;
        MONITORINFO monitor;
        if (bb_fullscreen) {
            memset(&monitor, 0, sizeof monitor);
            monitor.cbSize = sizeof monitor;
            if (GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor))
                (void)SetWindowPos(window, HWND_TOP,
                                   monitor.rcMonitor.left, monitor.rcMonitor.top,
                                   (int)(monitor.rcMonitor.right - monitor.rcMonitor.left),
                                   (int)(monitor.rcMonitor.bottom - monitor.rcMonitor.top),
                                   SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        }
        else {
            suggested = (const RECT *)lparam;
            if (suggested != NULL && suggested->right > suggested->left && suggested->bottom > suggested->top)
                (void)SetWindowPos(window, NULL, suggested->left, suggested->top,
                                   (int)(suggested->right - suggested->left),
                                   (int)(suggested->bottom - suggested->top),
                                   SWP_NOZORDER | SWP_NOACTIVATE);
        }
        return 0;
    }
    /* WM_SIZE can arrive during window creation and repeated drag updates.
     * Allocate the replacement backbuffer after the message pump drains. */
    if (message == WM_SIZE && wparam != SIZE_MINIMIZED) {
        int width;
        int height;
        width = (int)LOWORD(lparam);
        height = (int)HIWORD(lparam);
        if (width > 0 && height > 0) {
            bb_pending_client_width = width;
            bb_pending_client_height = height;
        }
    }
    if (message == WM_GETMINMAXINFO) {
        bb_set_minimum_track_size(window, (MINMAXINFO *)lparam);
        return 0;
    }
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
    if (message == WM_SETFOCUS) bb_has_focus = true;
    if (message == WM_KILLFOCUS) {
        bb_has_focus = false;
        memset(bb_key_now, 0, sizeof bb_key_now);
        bb_key_queue_count = 0;
        bb_char_queue_count = 0;
        bb_high_surrogate = 0;
    }
    if (bb_has_focus && (message == WM_KEYDOWN || message == WM_SYSKEYDOWN)) {
        key = bb_key_from_message(wparam, lparam);
        if (key >= 0 && key < 256) {
            /* Bit 30 is set for auto-repeat.  Queue only the physical edge. */
            if ((lparam & ((LPARAM)1 << 30)) == 0 && bb_key_queue_count < 64)
                bb_key_queue[bb_key_queue_count++] = key;
            bb_key_now[key] = true;
        }
        if (message == WM_KEYDOWN && wparam == VK_ESCAPE) return 0;
    }
    if (bb_has_focus && (message == WM_KEYUP || message == WM_SYSKEYUP)) {
        key = bb_key_from_message(wparam, lparam);
        if (key >= 0 && key < 256) bb_key_now[key] = false;
    }
    if (bb_has_focus && message == WM_CHAR) {
        bb_queue_utf16_unit((unsigned int)wparam);
        return 0;
    }
    if (message == WM_UNICHAR) {
        if (wparam == UNICODE_NOCHAR) return TRUE;
        if (bb_has_focus) {
            if (bb_high_surrogate != 0) bb_queue_character(0xFFFDu);
            bb_high_surrogate = 0;
            bb_queue_character((unsigned int)wparam);
        }
        return 0;
    }
    return DefWindowProcW(window, message, wparam, lparam);
}

static BBBitmap bb_make_bitmap(int width, int height)
{
    BBBitmap result;
    BITMAPINFO info;
    void *bits;
    HGDIOBJ previous;
    memset(&result, 0, sizeof result);
    if (width <= 0 || height <= 0 ||
        (size_t)width > (size_t)-1 / 4u / (size_t)height) return result;
    memset(&info, 0, sizeof info);
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = width;
    /* Negative DIB height puts the first row at the top, matching game-space
     * coordinates and avoiding a per-frame vertical copy. */
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    result.bitmap = CreateDIBSection(NULL, &info, DIB_RGB_COLORS, &bits, NULL, 0);
    if (result.bitmap == NULL || bits == NULL) {
        if (result.bitmap != NULL) DeleteObject(result.bitmap);
        memset(&result, 0, sizeof result);
        return result;
    }
    result.dc = CreateCompatibleDC(NULL);
    if (result.dc == NULL) {
        DeleteObject(result.bitmap);
        memset(&result, 0, sizeof result);
        return result;
    }
    previous = SelectObject(result.dc, result.bitmap);
    if (previous == NULL || previous == HGDI_ERROR || SetStretchBltMode(result.dc, COLORONCOLOR) == 0) {
        DeleteDC(result.dc);
        DeleteObject(result.bitmap);
        memset(&result, 0, sizeof result);
        return result;
    }
    memset(bits, 0, (size_t)width * (size_t)height * 4u);
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

static bool bb_apply_pending_resize(void)
{
    BBBitmap replacement;
    BBBitmap previous;
    bool current_is_screen;
    int width;
    int height;
    width = bb_pending_client_width;
    height = bb_pending_client_height;
    if (width <= 0 || height <= 0) return true;
    if (bb_screen.dc != NULL && bb_screen.width == width && bb_screen.height == height) {
        bb_pending_client_width = 0;
        bb_pending_client_height = 0;
        return true;
    }
    replacement = bb_make_bitmap(width, height);
    if (replacement.dc == NULL) {
        /* A large x86 window can exhaust the 32-bit address space.  Keep the
         * last complete frame, but wait for a new WM_SIZE before retrying. */
        bb_pending_client_width = 0;
        bb_pending_client_height = 0;
        return false;
    }
    current_is_screen = bb_current_dc == NULL || bb_current_dc == bb_screen.dc;
    previous = bb_screen;
    bb_screen = replacement;
    if (current_is_screen) bb_current_dc = bb_screen.dc;
    bb_pending_client_width = 0;
    bb_pending_client_height = 0;
    bb_release_bitmap(&previous);
    return true;
}

/* A successful insertion transfers both GDI handles to the resource table;
 * a full table releases them before returning the invalid ID. */
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

/* GDI+ decodes the asset, then the copied HBITMAP outlives the temporary
 * GDI+ image. The caller owns that HBITMAP and must DeleteObject it. */
static HBITMAP bb_load_hbitmap(const char *path, int *width, int *height)
{
    BBGdipImage *image;
    HBITMAP bitmap;
    UINT image_width;
    UINT image_height;
    wchar_t *wide = bb_utf8_to_wide(path);
    if (wide == NULL || !bb_gdiplus_startup()) {
        free(wide);
        return NULL;
    }
    image = NULL;
    bitmap = NULL;
    if (GdipLoadImageFromFile(wide, &image) == BB_GDIP_OK && image != NULL &&
        GdipGetImageWidth(image, &image_width) == BB_GDIP_OK &&
        GdipGetImageHeight(image, &image_height) == BB_GDIP_OK &&
        GdipCreateHBITMAPFromBitmap((BBGdipBitmap *)image, &bitmap, 0) == BB_GDIP_OK) {
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
    /* GDI TextOut/FillRect do not maintain alpha in our 32-bit render target.
     * The target is cleared to a complete RGB frame every draw, so its final
     * presentation must copy RGB directly instead of treating those alpha
     * bytes as transparency. PNG sprite textures still use AlphaBlend. */
    if (source->render_target) {
        StretchBlt(bb_current_dc, (int)dst.x, (int)dst.y, (int)dst.width, (int)dst.height,
                   draw_source->dc, source_x, source_y, source_width, source_height, SRCCOPY);
    }
    else {
        AlphaBlend(bb_current_dc, (int)dst.x, (int)dst.y, (int)dst.width, (int)dst.height,
                   draw_source->dc, source_x, source_y, source_width, source_height, blend);
    }
    bb_release_bitmap(&flipped);
}

void SetConfigFlags(unsigned int flags) { bb_config_flags = flags; }

void InitWindow(int width, int height, const char *title)
{
    WNDCLASSW window_class;
    RECT rect;
    RECT client_rect;
    DWORD window_style;
    wchar_t *wide_title;
    bb_enable_dpi_awareness();
    if (!bb_gdiplus_startup()) return;
    QueryPerformanceFrequency(&bb_frequency);
    QueryPerformanceCounter(&bb_last_tick);
    memset(&window_class, 0, sizeof window_class);
    window_class.lpfnWndProc = bb_window_proc;
    window_class.hInstance = GetModuleHandle(NULL);
    window_class.hCursor = LoadCursorA(NULL, IDC_ARROW);
    window_class.lpszClassName = L"BubbleBobbleVS2010";
    if (RegisterClassW(&window_class) == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return;
    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    window_style = (bb_config_flags & FLAG_WINDOW_RESIZABLE) ?
        WS_OVERLAPPEDWINDOW : WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    if (!AdjustWindowRect(&rect, window_style, FALSE)) return;
    wide_title = bb_utf8_to_wide(title != NULL ? title : "");
    if (wide_title == NULL) return;
    bb_window = CreateWindowW(window_class.lpszClassName, wide_title, window_style,
                              CW_USEDEFAULT, CW_USEDEFAULT,
                              (int)(rect.right - rect.left), (int)(rect.bottom - rect.top),
                              NULL, NULL, window_class.hInstance, NULL);
    free(wide_title);
    if (bb_window == NULL) return;
    /* AdjustWindowRect is not per-monitor-DPI aware in the VS2010 SDK.  Once
     * the HWND exists, measured non-client extents let us request the exact
     * physical client size without importing a newer SDK function. */
    bb_set_window_client_size(bb_window, width, height);
    if (!GetClientRect(bb_window, &client_rect)) {
        DestroyWindow(bb_window);
        bb_window = NULL;
        return;
    }
    bb_pending_client_width = (int)(client_rect.right - client_rect.left);
    bb_pending_client_height = (int)(client_rect.bottom - client_rect.top);
    if (!bb_apply_pending_resize() || bb_screen.dc == NULL) {
        DestroyWindow(bb_window);
        bb_window = NULL;
        return;
    }
    bb_current_dc = bb_screen.dc;
    ShowWindow(bb_window, SW_SHOW);
}

void CloseWindow(void)
{
    unsigned int index;
    for (index = 1; index < BB_RESOURCE_COUNT; ++index) bb_release_bitmap(&bb_resources[index]);
    bb_release_bitmap(&bb_screen);
    if (bb_window != NULL) DestroyWindow(bb_window);
    bb_window = NULL;
    bb_current_dc = NULL;
    bb_has_focus = false;
    bb_pending_client_width = 0;
    bb_pending_client_height = 0;
    bb_minimum_client_width = 0;
    bb_minimum_client_height = 0;
    bb_fullscreen = false;
    bb_windowed_style = 0;
    memset(&bb_windowed_placement, 0, sizeof bb_windowed_placement);
    bb_gdiplus_shutdown();
}

bool WindowShouldClose(void)
{
    bb_pump_messages();
    return bb_should_close;
}

bool IsWindowReady(void) { return bb_window != NULL && bb_screen.bitmap != NULL; }
bool IsWindowFocused(void) { return bb_window != NULL && GetForegroundWindow() == bb_window; }
void ToggleFullscreen(void)
{
    MONITORINFO monitor;
    LONG fullscreen_style;
    bool entering;
    if (bb_window == NULL) return;
    entering = !bb_fullscreen;
    if (entering) {
        memset(&bb_windowed_placement, 0, sizeof bb_windowed_placement);
        bb_windowed_placement.length = sizeof bb_windowed_placement;
        if (!GetWindowPlacement(bb_window, &bb_windowed_placement)) return;
        memset(&monitor, 0, sizeof monitor);
        monitor.cbSize = sizeof monitor;
        if (!GetMonitorInfoA(MonitorFromWindow(bb_window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        SetLastError(ERROR_SUCCESS);
        bb_windowed_style = GetWindowLongW(bb_window, GWL_STYLE);
        if (bb_windowed_style == 0 && GetLastError() != ERROR_SUCCESS) return;
        fullscreen_style = bb_windowed_style & ~WS_OVERLAPPEDWINDOW;
        if (!bb_set_window_style(bb_window, fullscreen_style)) return;
        bb_fullscreen = true;
        if (!SetWindowPos(bb_window, HWND_TOP,
                          monitor.rcMonitor.left, monitor.rcMonitor.top,
                          (int)(monitor.rcMonitor.right - monitor.rcMonitor.left),
                          (int)(monitor.rcMonitor.bottom - monitor.rcMonitor.top),
                          SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            bb_fullscreen = false;
            (void)bb_set_window_style(bb_window, bb_windowed_style);
            (void)SetWindowPlacement(bb_window, &bb_windowed_placement);
            (void)SetWindowPos(bb_window, NULL, 0, 0, 0, 0,
                               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            return;
        }
    }
    else {
        memset(&monitor, 0, sizeof monitor);
        monitor.cbSize = sizeof monitor;
        if (!GetMonitorInfoA(MonitorFromWindow(bb_window, MONITOR_DEFAULTTONEAREST), &monitor)) return;
        fullscreen_style = GetWindowLongW(bb_window, GWL_STYLE);
        bb_fullscreen = false;
        if (!bb_set_window_style(bb_window, bb_windowed_style)) {
            bb_fullscreen = true;
            return;
        }
        if (!SetWindowPlacement(bb_window, &bb_windowed_placement) ||
            !SetWindowPos(bb_window, NULL, 0, 0, 0, 0,
                          SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                          SWP_NOOWNERZORDER | SWP_FRAMECHANGED)) {
            bb_fullscreen = true;
            (void)bb_set_window_style(bb_window, fullscreen_style);
            (void)SetWindowPos(bb_window, HWND_TOP,
                               monitor.rcMonitor.left, monitor.rcMonitor.top,
                               (int)(monitor.rcMonitor.right - monitor.rcMonitor.left),
                               (int)(monitor.rcMonitor.bottom - monitor.rcMonitor.top),
                               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            return;
        }
    }
    if (!bb_apply_pending_resize()) {
        /* Do not commit a display mode whose client area has no matching
         * backbuffer, which is especially possible in a 32-bit process. */
        if (entering) {
            bb_fullscreen = false;
            (void)bb_set_window_style(bb_window, bb_windowed_style);
            (void)SetWindowPlacement(bb_window, &bb_windowed_placement);
            (void)SetWindowPos(bb_window, NULL, 0, 0, 0, 0,
                               SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        else {
            bb_fullscreen = true;
            (void)bb_set_window_style(bb_window, fullscreen_style);
            (void)SetWindowPos(bb_window, HWND_TOP,
                               monitor.rcMonitor.left, monitor.rcMonitor.top,
                               (int)(monitor.rcMonitor.right - monitor.rcMonitor.left),
                               (int)(monitor.rcMonitor.bottom - monitor.rcMonitor.top),
                               SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        (void)bb_apply_pending_resize();
    }
}
int GetScreenWidth(void) { return bb_screen.width; }
int GetScreenHeight(void) { return bb_screen.height; }
void SetWindowMinSize(int width, int height)
{
    RECT client_rect;
    int client_width;
    int client_height;
    int target_width;
    int target_height;
    bb_minimum_client_width = width > 0 ? width : 1;
    bb_minimum_client_height = height > 0 ? height : 1;
    if (bb_window == NULL || !GetClientRect(bb_window, &client_rect)) return;
    client_width = (int)(client_rect.right - client_rect.left);
    client_height = (int)(client_rect.bottom - client_rect.top);
    target_width = client_width < bb_minimum_client_width ? bb_minimum_client_width : client_width;
    target_height = client_height < bb_minimum_client_height ? bb_minimum_client_height : client_height;
    if (target_width != client_width || target_height != client_height)
        bb_set_window_client_size(bb_window, target_width, target_height);
}
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
    /* This software backend presents the backbuffer, then applies coarse
     * Sleep pacing. GetFrameTime includes both drawing and the sleep. */
    QueryPerformanceCounter(&now);
    elapsed = (double)(now.QuadPart - bb_last_tick.QuadPart) / (double)bb_frequency.QuadPart;
    target_milliseconds = bb_target_fps > 0 ? (DWORD)(1000 / bb_target_fps) : 0;
    if (target_milliseconds > 0 && elapsed * 1000.0 < target_milliseconds) Sleep(target_milliseconds - (DWORD)(elapsed * 1000.0));
    QueryPerformanceCounter(&now);
    bb_frame_time = (float)((double)(now.QuadPart - bb_last_tick.QuadPart) / (double)bb_frequency.QuadPart);
    bb_last_tick = now;
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

int GetCharPressed(void)
{
    int character;
    if (bb_char_queue_count == 0) return 0;
    character = bb_char_queue[0];
    memmove(bb_char_queue, bb_char_queue + 1, (size_t)(bb_char_queue_count - 1) * sizeof bb_char_queue[0]);
    --bb_char_queue_count;
    return character;
}

bool IsGamepadAvailable(int gamepad) { (void)gamepad; return false; }
float GetGamepadAxisMovement(int gamepad, int axis) { (void)gamepad; (void)axis; return 0.0f; }
bool IsGamepadButtonDown(int gamepad, int button) { (void)gamepad; (void)button; return false; }
bool IsGamepadButtonPressed(int gamepad, int button) { (void)gamepad; (void)button; return false; }
void *MemAlloc(unsigned int size) { return malloc(size); }
void MemFree(void *memory) { free(memory); }
void SetLoadFileDataCallback(LoadFileDataCallback callback)
{
    bb_file_loader = callback;
    /* main.c installs this immediately before validation of PNG assets. */
    (void)bb_gdiplus_startup();
}

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
    /* Image owns a copy, so UnloadImage and UnloadTexture can run separately. */
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
/* The only game caller is screenshot export, which this legacy backend does
 * not implement; no in-memory bitmap flip is needed for supported rendering. */
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

/* PNG screenshot export has no GDI encoder path in this compatibility shim. */
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
    HGDIOBJ previous;
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
    previous = SelectObject(bitmap.dc, bitmap.bitmap);
    if (previous == NULL || previous == HGDI_ERROR || SetStretchBltMode(bitmap.dc, COLORONCOLOR) == 0) {
        DeleteDC(bitmap.dc);
        DeleteObject(bitmap.bitmap);
        return texture;
    }
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
    /* Current game calls are axis-aligned with zero origin/rotation and WHITE
     * tint. This GDI subset handles source flipping and destination scaling. */
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

static HFONT bb_create_font(int pixel_size, const wchar_t *face_name)
{
    if (pixel_size < 1) pixel_size = 1;
    return CreateFontW(-pixel_size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                       NONANTIALIASED_QUALITY, FIXED_PITCH | FF_DONTCARE,
                       face_name);
}

static HFONT bb_font_handle(Font font, int pixel_size)
{
    BBFontResource *resource;
    HFONT replacement;
    resource = (BBFontResource *)font.glyphs;
    if (resource == NULL) return NULL;
    if (pixel_size < 1) pixel_size = 1;
    if (resource->handle != NULL && resource->pixel_size == pixel_size)
        return resource->handle;
    replacement = bb_create_font(pixel_size, L"Nintendo NES Font");
    if (replacement == NULL) return NULL;
    if (resource->handle != NULL) DeleteObject(resource->handle);
    resource->handle = replacement;
    resource->pixel_size = pixel_size;
    return resource->handle;
}

static bool bb_font_has_face(HFONT handle, const wchar_t *expected_face)
{
    HDC dc;
    HGDIOBJ previous;
    wchar_t actual_face[LF_FACESIZE];
    bool matches;
    if (handle == NULL || expected_face == NULL) return false;
    dc = CreateCompatibleDC(NULL);
    if (dc == NULL) return false;
    previous = SelectObject(dc, handle);
    if (previous == NULL || previous == HGDI_ERROR) {
        DeleteDC(dc);
        return false;
    }
    actual_face[0] = L'\0';
    matches = GetTextFaceW(dc, LF_FACESIZE, actual_face) > 0 &&
              lstrcmpiW(actual_face, expected_face) == 0;
    SelectObject(dc, previous);
    DeleteDC(dc);
    return matches;
}

static int bb_wide_codepoint_count(const wchar_t *text, int length)
{
    int index;
    int count;
    unsigned int unit;
    unsigned int next;
    index = 0;
    count = 0;
    while (index < length) {
        unit = (unsigned int)text[index++];
        if (unit >= 0xD800u && unit <= 0xDBFFu && index < length) {
            next = (unsigned int)text[index];
            if (next >= 0xDC00u && next <= 0xDFFFu) ++index;
        }
        ++count;
    }
    return count;
}

Font LoadFontEx(const char *fileName, int fontSize, int *codepoints, int codepointCount)
{
    Font font;
    BBFontResource *resource;
    wchar_t *wide_path;
    (void)codepoints;
    font = GetFontDefault();
    if (!bb_asset_exists(fileName)) return font;
    wide_path = bb_utf8_to_wide(fileName);
    if (wide_path == NULL) return font;
    resource = (BBFontResource *)malloc(sizeof *resource);
    if (resource == NULL) {
        free(wide_path);
        return font;
    }
    memset(resource, 0, sizeof *resource);
    if (AddFontResourceExW(wide_path, FR_PRIVATE, NULL) == 0) {
        free(resource);
        free(wide_path);
        return font;
    }
    resource->path = wide_path;
    font.baseSize = fontSize > 0 ? fontSize : 8;
    font.glyphCount = codepointCount > 0 ? codepointCount : 95;
    font.texture.id = 1;
    font.glyphs = resource;
    /* GDI silently substitutes unavailable faces. Reject substitution so
     * layout uses the intended private font or the explicit fallback. */
    if (bb_font_handle(font, font.baseSize) == NULL ||
        !bb_font_has_face(resource->handle, L"Nintendo NES Font")) {
        if (resource->handle != NULL) DeleteObject(resource->handle);
        (void)RemoveFontResourceExW(resource->path, FR_PRIVATE, NULL);
        free(resource->path);
        free(resource);
        return GetFontDefault();
    }
    return font;
}

bool IsFontValid(Font font)
{
    return font.glyphCount > 0 && font.texture.id != 0 && font.glyphs != NULL;
}

void UnloadFont(Font font)
{
    BBFontResource *resource;
    resource = (BBFontResource *)font.glyphs;
    if (resource == NULL) return;
    if (resource->handle != NULL) DeleteObject(resource->handle);
    if (resource->path != NULL) {
        (void)RemoveFontResourceExW(resource->path, FR_PRIVATE, NULL);
        free(resource->path);
    }
    free(resource);
}

void DrawText(const char *text, int posX, int posY, int fontSize, Color color)
{
    HFONT font;
    HGDIOBJ old_font;
    COLORREF old_color;
    int old_mode;
    wchar_t *wide_text;
    int length;
    if (bb_current_dc == NULL || text == NULL) return;
    wide_text = bb_utf8_to_wide(text);
    if (wide_text == NULL) return;
    length = (int)wcslen(wide_text);
    font = bb_create_font(fontSize, L"Terminal");
    if (font == NULL) {
        free(wide_text);
        return;
    }
    old_font = SelectObject(bb_current_dc, font);
    if (old_font == NULL || old_font == HGDI_ERROR) {
        DeleteObject(font);
        free(wide_text);
        return;
    }
    old_color = SetTextColor(bb_current_dc, bb_colorref(color));
    old_mode = SetBkMode(bb_current_dc, TRANSPARENT);
    (void)TextOutW(bb_current_dc, posX, posY, wide_text, length);
    SetBkMode(bb_current_dc, old_mode);
    SetTextColor(bb_current_dc, old_color);
    SelectObject(bb_current_dc, old_font);
    DeleteObject(font);
    free(wide_text);
}

void DrawTextEx(Font font, const char *text, Vector2 position, float fontSize, float spacing, Color tint)
{
    HFONT handle;
    HGDIOBJ old_font;
    COLORREF old_color;
    int old_mode;
    int length;
    int index;
    int units;
    float cursor;
    SIZE size;
    wchar_t *wide_text;
    unsigned int unit;
    unsigned int next;
    if (bb_current_dc == NULL || text == NULL) return;
    handle = bb_font_handle(font, (int)(fontSize + 0.5f));
    if (handle == NULL) {
        DrawText(text, (int)position.x, (int)position.y, (int)fontSize, tint);
        return;
    }
    wide_text = bb_utf8_to_wide(text);
    if (wide_text == NULL) return;
    old_font = SelectObject(bb_current_dc, handle);
    if (old_font == NULL || old_font == HGDI_ERROR) {
        free(wide_text);
        return;
    }
    old_color = SetTextColor(bb_current_dc, bb_colorref(tint));
    old_mode = SetBkMode(bb_current_dc, TRANSPARENT);
    length = (int)wcslen(wide_text);
    if (spacing == 0.0f) {
        (void)TextOutW(bb_current_dc, (int)position.x, (int)position.y, wide_text, length);
    }
    else {
        cursor = position.x;
        /* Spacing is per Unicode character, not per UTF-16 code unit. */
        for (index = 0; index < length; index += units) {
            units = 1;
            unit = (unsigned int)wide_text[index];
            if (unit >= 0xD800u && unit <= 0xDBFFu && index + 1 < length) {
                next = (unsigned int)wide_text[index + 1];
                if (next >= 0xDC00u && next <= 0xDFFFu) units = 2;
            }
            (void)TextOutW(bb_current_dc, (int)cursor, (int)position.y,
                           wide_text + index, units);
            size.cx = 0;
            size.cy = 0;
            if (GetTextExtentPoint32W(bb_current_dc, wide_text + index, units, &size))
                cursor += (float)size.cx + spacing;
            else cursor += fontSize + spacing;
        }
    }
    SetBkMode(bb_current_dc, old_mode);
    SetTextColor(bb_current_dc, old_color);
    SelectObject(bb_current_dc, old_font);
    free(wide_text);
}

Vector2 MeasureTextEx(Font font, const char *text, float fontSize, float spacing)
{
    Vector2 value;
    HFONT handle;
    HGDIOBJ old_font;
    HDC measurement_dc;
    SIZE size;
    int length;
    int characters;
    bool owns_dc;
    wchar_t *wide_text;
    value.x = 0.0f;
    value.y = fontSize;
    if (text == NULL) return value;
    wide_text = bb_utf8_to_wide(text);
    if (wide_text == NULL) return value;
    length = (int)wcslen(wide_text);
    characters = bb_wide_codepoint_count(wide_text, length);
    if (length == 0) {
        free(wide_text);
        return value;
    }
    handle = bb_font_handle(font, (int)(fontSize + 0.5f));
    if (handle == NULL) {
        value.x = (float)characters * (fontSize * 0.6f) +
                  (float)(characters - 1) * spacing;
        free(wide_text);
        return value;
    }
    measurement_dc = bb_current_dc;
    owns_dc = false;
    if (measurement_dc == NULL) {
        measurement_dc = CreateCompatibleDC(NULL);
        owns_dc = measurement_dc != NULL;
    }
    if (measurement_dc == NULL) {
        free(wide_text);
        return value;
    }
    old_font = SelectObject(measurement_dc, handle);
    if (old_font != NULL && old_font != HGDI_ERROR &&
        GetTextExtentPoint32W(measurement_dc, wide_text, length, &size)) {
        value.x = (float)size.cx + (float)(characters - 1) * spacing;
        value.y = (float)size.cy;
    }
    else value.x = (float)characters * (fontSize * 0.6f) +
                   (float)(characters - 1) * spacing;
    if (old_font != NULL && old_font != HGDI_ERROR) SelectObject(measurement_dc, old_font);
    if (owns_dc) DeleteDC(measurement_dc);
    free(wide_text);
    return value;
}
