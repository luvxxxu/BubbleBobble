#include "bb_game.h"
#include "bb_collision_raylib.h"
#include "bb_platform.h"
#include "bb_render.h"
#include "bb_raylib_types.h"

#include <errno.h>
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

static BBInput player_input(int index, bool single)
{
    BBInput input = {0};
    float stick;
    if (index == 0) {
        input.move = (float)((int)IsKeyDown(KEY_D) - (int)IsKeyDown(KEY_A));
        input.jump = key_pressed(KEY_W) || key_pressed(KEY_SPACE) || key_pressed(KEY_X);
        input.fire = key_pressed(KEY_E) || key_pressed(KEY_Z);
    }
    if (index == 1 || single) {
        input.move += (float)((int)IsKeyDown(KEY_RIGHT) - (int)IsKeyDown(KEY_LEFT));
        input.jump = input.jump || key_pressed(KEY_UP);
        input.fire = input.fire || key_pressed(KEY_SLASH) || key_pressed(KEY_RIGHT_CONTROL);
    }
    if (IsGamepadAvailable(index)) {
        stick = GetGamepadAxisMovement(index, GAMEPAD_AXIS_LEFT_X);
        if (fabsf(stick) > 0.2f) input.move += stick;
        input.move += (float)((int)pad_down(index, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) - (int)pad_down(index, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
        input.jump = input.jump || pad_pressed(index, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || pad_pressed(index, GAMEPAD_BUTTON_LEFT_FACE_UP);
        input.fire = input.fire || pad_pressed(index, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    }
    if (input.move < -1) input.move = -1;
    if (input.move > 1) input.move = 1;
    return input;
}

static int next_score_player(const BBGame *game, int after)
{
    int i;
    for (i = after + 1; i < BB_MAX_PLAYERS; ++i)
        if (game->players[i].active && game->players[i].score >= 0) return i;
    return -1;
}

static void score_entry_begin(BBUI *ui, const BBGame *game)
{
    ui->score_player = next_score_player(game, -1);
    ui->initial_cursor = 0;
    memcpy(ui->initials, "AAA", 4);
}

static void score_input(BBUI *ui, BBGame *game, bool up, bool down, bool confirm, const char *score_path, bool smoke)
{
    char *letter;
    int key;
    BbScore record = {0};
    if (ui->score_player < 0) {
        if (confirm) bb_game_menu(game);
        return;
    }
    if (key_pressed(KEY_LEFT) && ui->initial_cursor > 0) --ui->initial_cursor;
    if (key_pressed(KEY_RIGHT) && ui->initial_cursor < 2) ++ui->initial_cursor;
    letter = &ui->initials[ui->initial_cursor];
    if (up) *letter = *letter >= 'Z' ? 'A' : (char)(*letter + 1);
    if (down) *letter = *letter <= 'A' ? 'Z' : (char)(*letter - 1);
    key = GetCharPressed();
    while (key) {
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (key >= 'A' && key <= 'Z') *letter = (char)key;
        key = GetCharPressed();
    }
    if (!confirm) return;
    if (++ui->initial_cursor < 3) return;
    record.score = (unsigned)game->players[ui->score_player].score;
    record.round = game->level + 1;
    memcpy(record.name, ui->initials, sizeof record.name);
    bb_scores_insert(ui->scores, &ui->score_count, record);
    if (!smoke && (!score_path[0] || !bb_scores_save(score_path, ui->scores, ui->score_count))) {
        ui->save_failed = true;
        fprintf(stderr, "Could not save leaderboard to %s\n", score_path);
    }
    ui->score_player = next_score_player(game, ui->score_player);
    ui->initial_cursor = 0;
    memcpy(ui->initials, "AAA", 4);
}

static void usage(const char *program)
{
    printf("Usage: %s [--assets DIR] [--mute] [--validate-assets]\n"
           "       [--smoke-test FRAMES] [--screenshot PATH.png] [--score-file PATH]\n"
           "P1: A/D, W/Space/X jump, E/Z fire. P2: arrows, / fire.\n"
           "Menu: arrows + Enter; P pause; M mute; Escape menu; F11 fullscreen.\n", program);
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
    const char *asset_override = NULL, *screenshot = NULL, *score_override = NULL;
    int smoke_frames = 0;
    bool validate_only = false, muted = false;
    int i;
    char asset_directory[BB_PATH_CAP];
    char score_path[BB_PATH_CAP] = {0};
    uint8_t maps[BB_LEVEL_COUNT * BB_MAP_WIDTH * BB_MAP_HEIGHT];
    BBUI ui = {0};
    bool audio_ready;
    BBAssets assets;
    RenderTexture2D canvas;
    BBGame game;
    BBInput pending[BB_MAX_PLAYERS] = {{0}};
    double accumulator = 0;
    int frames = 0;
    bool screenshot_ok = true;
    BBState previous_state;
    bool confirm;
    bool up;
    bool down;
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
        else if (strcmp(argv[i], "--score-file") == 0 && i + 1 < argc) score_override = argv[++i];
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
    if (!bb_assets_validate(asset_directory, maps)) return EXIT_FAILURE;
    if (validate_only) {
        printf("Validated all 3 maps, 8 sprite sheets/images, original font file, 3 WAV sounds and OGG music.\n");
        return EXIT_SUCCESS;
    }
    ui.muted = muted;
    ui.score_player = -1;
    if (score_override) {
        if (strlen(score_override) >= sizeof score_path) return EXIT_FAILURE;
        memcpy(score_path, score_override, strlen(score_override) + 1);
    } else if (!smoke_frames && !bb_platform_score_path(score_path, sizeof score_path)) {
        fprintf(stderr, "User score directory unavailable; scores remain in memory.\n");
        ui.save_failed = true;
    }
    if (score_path[0]) {
        if (!bb_scores_load(score_path, ui.scores, &ui.score_count)) {
            ui.save_failed = true;
            fprintf(stderr, "Cannot read scores: %s; persistent writes disabled.\n", score_path);
            score_path[0] = '\0';
        }
    }
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
    if (audio_ready) SetMasterVolume(muted ? 0 : 1);
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
    bb_game_init(&game, maps, 0xBB1986u);
    bb_game_set_collision_backend(&game, bb_raylib_collision_backend());
    if (smoke_frames) { bb_game_start(&game, BB_MODE_COOP); bb_game_skip_intro(&game); }
    while (!WindowShouldClose()) {
        poll_key_edges();
        previous_state = game.state;
        confirm = key_pressed(KEY_ENTER) || pad_pressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
        up = key_pressed(KEY_UP) || key_pressed(KEY_W) || pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP);
        down = key_pressed(KEY_DOWN) || key_pressed(KEY_S) || pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN);
        if (key_pressed(KEY_F11)) ToggleFullscreen();
        if (key_pressed(KEY_M)) { ui.muted = !ui.muted; if (audio_ready) SetMasterVolume(ui.muted ? 0 : 1); }
        if (key_pressed(KEY_ESCAPE)) {
            if (game.state == BB_STATE_MENU) break;
            bb_game_menu(&game); ui.paused = false; accumulator = 0;
            memset(pending, 0, sizeof pending);
        } else if (game.state == BB_STATE_MENU) {
            if (up) ui.menu_selection = (ui.menu_selection + 3) % 4;
            if (down) ui.menu_selection = (ui.menu_selection + 1) % 4;
            if (confirm) {
                if (ui.menu_selection == 3) {
                    game.state = BB_STATE_SCORE; ui.score_player = -1;
                    memset(game.players, 0, sizeof game.players);
                    game.won = false;
                }
                else bb_game_start(&game, (BBMode)ui.menu_selection);
                accumulator = 0;
            }
        } else if (game.state == BB_STATE_SCORE) {
            score_input(&ui, &game, up, down, confirm, score_path, smoke_frames > 0);
        } else {
            if (game.state == BB_STATE_INTRO && confirm) bb_game_skip_intro(&game);
            if (key_pressed(KEY_P) || pad_pressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)) ui.paused = !ui.paused;
        }
        if (assets.audio) UpdateMusicStream(assets.music);
        simulate = !ui.paused && (IsWindowFocused() || smoke_frames > 0) &&
                   game.state != BB_STATE_MENU && game.state != BB_STATE_SCORE;
        if (simulate) {
            elapsed = smoke_frames ? 1.0f / 60.0f : GetFrameTime();
            if (elapsed > 0.25f) elapsed = 0.25f;
            accumulator += elapsed;
            for (i = 0; i < BB_MAX_PLAYERS; ++i) {
                input = player_input(i, game.mode == BB_MODE_SOLO && i == 0);
                pending[i].move = input.move;
                pending[i].jump = pending[i].jump || input.jump;
                pending[i].fire = pending[i].fire || input.fire;
            }
            while (accumulator >= (double)BB_FIXED_DT) {
                if (smoke_frames) {
                    for (i = 0; i < BB_MAX_PLAYERS; ++i) {
                        pending[i].move = (game.ticks / 240u + (uint64_t)i) % 2u ? -1.0f : 1.0f;
                        pending[i].jump = game.ticks % 100u == 0;
                        pending[i].fire = game.ticks % 50u == 0;
                    }
                }
                bb_game_update(&game, pending, BB_FIXED_DT);
                bb_game_check_bump(&game);
                bb_audio_events(&assets, game.events);
                for (i = 0; i < BB_MAX_PLAYERS; ++i) { pending[i].jump = false; pending[i].fire = false; }
                accumulator -= BB_FIXED_DT;
                if (game.state == BB_STATE_SCORE) { accumulator = 0; break; }
            }
        } else { accumulator = 0; memset(pending, 0, sizeof pending); }
        if (game.state == BB_STATE_SCORE && previous_state != BB_STATE_SCORE && previous_state != BB_STATE_MENU)
            score_entry_begin(&ui, &game);
        BeginTextureMode(canvas);
        bb_draw_game(&assets, &game, &ui);
        EndTextureMode();
        scale_x = (float)GetScreenWidth() / BB_SCREEN_WIDTH;
        scale_y = (float)GetScreenHeight() / BB_SCREEN_HEIGHT;
        scale = fminf(scale_x, scale_y);
        if (scale >= 1) scale = floorf(scale);
        width = BB_SCREEN_WIDTH * scale;
        height = BB_SCREEN_HEIGHT * scale;
        BeginDrawing();
        ClearBackground(bb_color(12, 12, 12, 255));
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
    if (smoke_frames) printf("Smoke %s: frames=%d ticks=" BB_UINT64_PRINTF " level=%d enemies=%d state=%d\n", smoke_ok ? "completed" : "interrupted", frames,
                             BB_UINT64_CAST(game.ticks), game.level + 1, bb_game_enemies_left(&game), (int)game.state);
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
