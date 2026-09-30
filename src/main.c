#include "bb_game.h"
#include "bb_collision_raylib.h"
#include "bb_platform.h"
#include "bb_render.h"
#include "bb_raylib_types.h"

#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* 한 번의 이벤트 수집 안에서 시작과 종료가 모두 일어난 짧은 입력도 보존한다. */
static bool pressed_keys[512];

static void poll_key_edges(void)
{
    int key;
    memset(pressed_keys, 0, sizeof pressed_keys);
    while ((key = GetKeyPressed()) != 0)
        if (key > 0 && key < (int)(sizeof pressed_keys / sizeof pressed_keys[0])) pressed_keys[key] = true;
}

static bool key_pressed(int key)
{
    return (key >= 0 && key < (int)(sizeof pressed_keys / sizeof pressed_keys[0]) && pressed_keys[key]) || IsKeyPressed(key);
}

static bool positive_number(const char *value, int *out, int maximum)
{
    char *end;
    long number;
    errno = 0;
    number = strtol(value, &end, 10);
    if (errno || end == value || *end || number < 1 || number > maximum) return false;
    *out = (int)number;
    return true;
}

static bool pad_pressed(int index, int button)
{
    return IsGamepadAvailable(index) && IsGamepadButtonPressed(index, button);
}

static bool pad_down(int index, int button)
{
    return IsGamepadAvailable(index) && IsGamepadButtonDown(index, button);
}

static BBInput player_input(void)
{
    BBInput input = {0};
    float stick;
    input.move = (float)((int)IsKeyDown(KEY_D) - (int)IsKeyDown(KEY_A));
    input.move += (float)((int)IsKeyDown(KEY_RIGHT) - (int)IsKeyDown(KEY_LEFT));
    input.jump = key_pressed(KEY_W) || key_pressed(KEY_SPACE) || key_pressed(KEY_X) || key_pressed(KEY_UP);
    input.fire = key_pressed(KEY_E) || key_pressed(KEY_Z) || key_pressed(KEY_SLASH) || key_pressed(KEY_RIGHT_CONTROL);
    if (IsGamepadAvailable(0)) {
        stick = GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X);
        if (fabsf(stick) > 0.2f) input.move += stick;
        input.move += (float)((int)pad_down(0, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) - (int)pad_down(0, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
        input.jump = input.jump || pad_pressed(0, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
        input.fire = input.fire || pad_pressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    }
    /* 키보드, 패드 방향키, 아날로그 스틱을 합쳐도 코어 입력 범위를 넘지 않게 한다. */
    if (input.move < -1) input.move = -1;
    if (input.move > 1) input.move = 1;
    return input;
}

static void usage(const char *program)
{
    printf("Usage: %s [--assets DIR] [--mute] [--validate-assets]\n"
           "       [--smoke-test FRAMES] [--screenshot PATH.png]\n"
           "Move: A/D or left/right. Jump: W/Space/X/up. Fire: E/Z/Slash/Right Ctrl.\n"
           "Menu: Enter; P pause; M mute; Escape menu; F11 fullscreen.\n", program);
}

static bool save_screenshot(RenderTexture2D canvas, const char *path)
{
    Image image;
    int size;
    unsigned char *png;
    FILE *file;
    bool ok;
    image = LoadImageFromTexture(canvas.texture);
    if (!image.data) return false;
    /* 렌더 텍스처의 읽기 방향을 화면과 맞춘 뒤 PNG로 인코딩한다. */
    ImageFlipVertical(&image);
    size = 0;
    png = ExportImageToMemory(image, ".png", &size);
    UnloadImage(image);
    if (!png || size <= 0) { MemFree(png); return false; }
    file = bb_platform_fopen(path, "wb");
    ok = false;
    if (file) {
        ok = fwrite(png, 1, (size_t)size, file) == (size_t)size;
        if (fclose(file) != 0) ok = false;
    }
    MemFree(png);
    return ok;
}

static int run_game(int argc, char **argv)
{
    const char *asset_override = NULL, *screenshot = NULL;
    int smoke_frames = 0;
    bool validate_only = false, muted = false;
    int i;
    char asset_directory[BB_PATH_CAP];
    uint8_t map[BB_MAP_WIDTH * BB_MAP_HEIGHT];
    BBUI ui = {0};
    bool audio_ready;
    BBAssets assets;
    RenderTexture2D canvas;
    BBGame game;
    BBInput pending = {0};
    double accumulator = 0;
    int frames = 0;
    bool screenshot_ok = true;
    bool confirm;
    bool simulate;
    float elapsed;
    BBInput input;
    float scale_x;
    float scale_y;
    float scale;
    float width;
    float height;
    bool smoke_ok;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) asset_override = argv[++i];
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if (strcmp(argv[i], "--mute") == 0) muted = true;
        else if (strcmp(argv[i], "--validate-assets") == 0) validate_only = true;
        else if (strcmp(argv[i], "--smoke-test") == 0 && i + 1 < argc) {
            if (!positive_number(argv[++i], &smoke_frames, 100000)) { usage(argv[0]); return EXIT_FAILURE; }
        } else if (strcmp(argv[i], "--help") == 0) { usage(argv[0]); return EXIT_SUCCESS; }
        else { fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]); usage(argv[0]); return EXIT_FAILURE; }
    }
    if (asset_override) {
        if (strlen(asset_override) >= sizeof asset_directory) return EXIT_FAILURE;
        memcpy(asset_directory, asset_override, strlen(asset_override) + 1);
    } else if (!bb_platform_asset_dir(asset_directory, sizeof asset_directory)) {
        fprintf(stderr, "Cannot locate executable assets; use --assets DIR.\n"); return EXIT_FAILURE;
    }
    SetTraceLogLevel(LOG_WARNING);
    bb_assets_install_file_loader();
    /* 이미지와 사운드는 장치 없이 디코딩해 검증하므로 창 생성 전에 실패를 알릴 수 있다. */
    if (!bb_assets_validate(asset_directory, map)) return EXIT_FAILURE;
    if (validate_only) {
        puts("Validated level 1 map, active sprite sheets/images, font, 2 WAV sounds and OGG music.");
        return EXIT_SUCCESS;
    }
    ui.muted = muted;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(BB_SCREEN_WIDTH * 3, BB_SCREEN_HEIGHT * 3, "Bubble Bobble - C11");
    if (!IsWindowReady()) { fprintf(stderr, "Cannot create graphics window.\n"); return EXIT_FAILURE; }
    SetWindowMinSize(BB_SCREEN_WIDTH, BB_SCREEN_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    /* 에셋 검증과 그래픽 의존성이 없는 코어 테스트에는 장치가 필요 없다. */
    InitAudioDevice();
    audio_ready = IsAudioDeviceReady();
    if (!audio_ready) fprintf(stderr, "Audio device unavailable; continuing without sound.\n");
    if (audio_ready) SetMasterVolume(muted ? 0.0f : 1.0f);
    if (!bb_assets_load(&assets, asset_directory, audio_ready)) {
        fprintf(stderr, "Could not load game resources.\n");
        bb_assets_unload(&assets);
        if (audio_ready) CloseAudioDevice();
        CloseWindow();
        return EXIT_FAILURE;
    }
    canvas = LoadRenderTexture(BB_SCREEN_WIDTH, BB_SCREEN_HEIGHT);
    if (!IsRenderTextureValid(canvas)) {
        bb_assets_unload(&assets);
        if (audio_ready) CloseAudioDevice();
        CloseWindow();
        return EXIT_FAILURE;
    }
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);
    bb_game_init(&game, map, 0xBB1986u);
    bb_game_set_collision_backend(&game, bb_raylib_collision_backend());
    if (smoke_frames) { bb_game_start(&game); bb_game_skip_intro(&game); }
    while (!WindowShouldClose()) {
        poll_key_edges();
        confirm = key_pressed(KEY_ENTER) || pad_pressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        if (key_pressed(KEY_F11)) ToggleFullscreen();
        if (key_pressed(KEY_M)) { ui.muted = !ui.muted; if (audio_ready) SetMasterVolume(ui.muted ? 0.0f : 1.0f); }
        if (key_pressed(KEY_ESCAPE)) {
            if (game.state == BB_STATE_MENU) break;
            bb_game_menu(&game); ui.paused = false; accumulator = 0;
            memset(&pending, 0, sizeof pending);
        } else if (game.state == BB_STATE_MENU) {
            if (confirm) {
                bb_game_start(&game);
                accumulator = 0;
            }
        } else {
            if (game.state == BB_STATE_INTRO && confirm) bb_game_skip_intro(&game);
            if (key_pressed(KEY_P) || pad_pressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) ui.paused = !ui.paused;
        }
        if (assets.audio) UpdateMusicStream(assets.music);
        simulate = !ui.paused && (IsWindowFocused() || smoke_frames > 0) &&
                   game.state != BB_STATE_MENU;
        if (simulate) {
            /* 가변 렌더 프레임을 120Hz 고정 시뮬레이션 단계로 누적한다. */
            elapsed = smoke_frames ? 1.0f / 60.0f : GetFrameTime();
            if (elapsed > 0.25f) elapsed = 0.25f;
            accumulator += elapsed;
            input = player_input();
            pending.move = input.move;
            /* 짧게 눌렀다 뗀 동작도 다음 고정 단계까지 보존한다. */
            pending.jump = pending.jump || input.jump;
            pending.fire = pending.fire || input.fire;
            while (accumulator >= (double)BB_FIXED_DT) {
                if (smoke_frames) {
                    pending.move = (game.ticks / 240u) % 2u ? -1.0f : 1.0f;
                    pending.jump = game.ticks % 100u == 0;
                    pending.fire = game.ticks % 50u == 0;
                }
                bb_game_update(&game, &pending, BB_FIXED_DT);
                /* 같은 틱의 이동 결과에 대해 버블 충돌을 한 번 처리한다. */
                bb_game_check_bump(&game);
                bb_audio_events(&assets, game.events);
                pending.jump = false;
                pending.fire = false;
                accumulator -= BB_FIXED_DT;
            }
        } else { accumulator = 0; memset(&pending, 0, sizeof pending); }
        BeginTextureMode(canvas);
        bb_draw_game(&assets, &game, &ui);
        EndTextureMode();
        /* 256x224 화면을 가능한 한 정수 배율로 중앙에 확대해 픽셀 경계를 유지한다. */
        scale_x = (float)GetScreenWidth() / BB_SCREEN_WIDTH;
        scale_y = (float)GetScreenHeight() / BB_SCREEN_HEIGHT;
        scale = fminf(scale_x, scale_y);
        if (scale >= 1) scale = floorf(scale);
        width = BB_SCREEN_WIDTH * scale;
        height = BB_SCREEN_HEIGHT * scale;
        BeginDrawing();
        ClearBackground(bb_color(12, 12, 12, 255));
        /* raylib 렌더 텍스처는 표시할 때 세로 방향을 뒤집어야 한다. */
        DrawTexturePro(canvas.texture, bb_rectangle(0, 0, BB_SCREEN_WIDTH, -BB_SCREEN_HEIGHT),
                       bb_rectangle(((float)GetScreenWidth() - width) / 2, ((float)GetScreenHeight() - height) / 2, width, height),
                       bb_vector2(0, 0), 0, WHITE);
        EndDrawing();
        ++frames;
        if (smoke_frames && frames >= smoke_frames) {
            if (screenshot) screenshot_ok = save_screenshot(canvas, screenshot);
            break;
        }
        if (!smoke_frames && screenshot && frames == 1) screenshot_ok = save_screenshot(canvas, screenshot);
    }
    smoke_ok = !smoke_frames || frames >= smoke_frames;
    if (smoke_frames) printf("Smoke %s: frames=%d ticks=%" PRIu64 " level=1 state=%d\n", smoke_ok ? "completed" : "interrupted", frames,
                             game.ticks, (int)game.state);
    UnloadRenderTexture(canvas);
    bb_assets_unload(&assets);
    if (audio_ready) CloseAudioDevice();
    CloseWindow();
    if (!screenshot_ok) fprintf(stderr, "Could not save screenshot: %s\n", screenshot);
    return screenshot_ok && smoke_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

int main(int argc, char **argv)
{
    BBArguments arguments;
    int result;
    if (!bb_platform_arguments(argc, argv, &arguments)) {
        fprintf(stderr, "Cannot decode command-line arguments.\n");
        return EXIT_FAILURE;
    }
    result = run_game(arguments.count, arguments.values);
    bb_platform_free_arguments(&arguments);
    return result;
}
