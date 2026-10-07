#include "bb_game.h"
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
    errno = 0;
    long number = strtol(value, &end, 10);
    if (errno || end == value || *end || number < 1 || number > maximum) return false;
    *out = (int)number;
    return true;
}

static bool pad_pressed(int index, int button)
{
    return IsGamepadAvailable(index) && IsGamepadButtonPressed(index, button);
}

static BBInput player_input(int player, int player_count)
{
    BBInput input = {0};
    if (player == 0) {
        input.move = (float)((int)IsKeyDown(KEY_D) - (int)IsKeyDown(KEY_A));
        input.jump = key_pressed(KEY_W) || key_pressed(KEY_SPACE);
        input.fire = key_pressed(KEY_E) || key_pressed(KEY_Z);
    }
    /* 2인 모드에서는 방향키를 2P에게만 보낸다. */
    if (player == 1 || player_count == 1) {
        input.move += (float)((int)IsKeyDown(KEY_RIGHT) - (int)IsKeyDown(KEY_LEFT));
        input.jump = input.jump || key_pressed(KEY_UP);
    }
    if (player == 1) input.fire = key_pressed(KEY_RIGHT_SHIFT);
    if (player_count == 1) {
        input.jump = input.jump || key_pressed(KEY_X);
        input.fire = input.fire || key_pressed(KEY_SLASH) || key_pressed(KEY_RIGHT_CONTROL);
    }
    if (IsGamepadAvailable(player)) {
        float stick = GetGamepadAxisMovement(player, GAMEPAD_AXIS_LEFT_X);
        if (fabsf(stick) > 0.2f) input.move += stick;
        input.move += (float)((int)IsGamepadButtonDown(player, GAMEPAD_BUTTON_LEFT_FACE_RIGHT) - (int)IsGamepadButtonDown(player, GAMEPAD_BUTTON_LEFT_FACE_LEFT));
        input.jump = input.jump || pad_pressed(player, GAMEPAD_BUTTON_RIGHT_FACE_RIGHT) || pad_pressed(player, GAMEPAD_BUTTON_LEFT_FACE_UP);
        input.fire = input.fire || pad_pressed(player, GAMEPAD_BUTTON_RIGHT_FACE_DOWN);
    }
    /* 키보드, 패드 방향키, 아날로그 스틱을 합쳐도 코어 입력 범위를 넘지 않게 한다. */
    if (input.move < -1) input.move = -1;
    if (input.move > 1) input.move = 1;
    return input;
}

static void score_entry_next(BBUI *ui, const BBGame *game)
{
    ui->entering_initials = false;
    for (int i = 0; i < game->player_count; ++i) {
        if (game->players[i].active && !ui->score_recorded[i]) {
            ui->score_player = i;
            ui->entering_initials = true;
            break;
        }
    }
    ui->initial_cursor = 0;
    memcpy(ui->initials, "AAA", sizeof ui->initials);
}

static void score_entry_begin(BBUI *ui, const BBGame *game)
{
    memset(ui->score_recorded, 0, sizeof ui->score_recorded);
    score_entry_next(ui, game);
    /* 플레이 중 누른 글자가 이름의 첫 글자로 넘어오지 않게 한다. */
    while (GetCharPressed() != 0) { }
}

static void score_submit(BBUI *ui, const BBGame *game, const char *path, bool smoke)
{
    BbScore record = {0};
    BbScore updated[BB_SCORE_COUNT];
    size_t count = ui->score_count;
    int player = ui->score_player;
    if (!ui->entering_initials || player < 0 || player >= game->player_count || ui->score_recorded[player]) return;
    record.score = (unsigned)(game->players[player].score < 0 ? 0 : game->players[player].score);
    record.round = game->level + 1;
    memcpy(record.name, ui->initials, sizeof record.name);
    /* 저장 성공 전에는 현재 표를 바꾸지 않아 재시도해도 중복되지 않는다. */
    memcpy(updated, ui->scores, sizeof updated);
    bb_scores_insert(updated, &count, record);
    /* 자동 실행은 --score-file을 지정해도 사용자 기록에 쓰지 않는다. */
    if (!smoke && (!path[0] || !bb_scores_save(path, updated, count))) {
        ui->save_failed = true;
        ui->initial_cursor = 2;
        fprintf(stderr, "Could not save leaderboard to %s\n", path);
        return;
    }
    memcpy(ui->scores, updated, sizeof updated);
    ui->score_count = count;
    ui->score_recorded[player] = true;
    ui->save_failed = false;
    score_entry_next(ui, game);
}

static void score_input(BBUI *ui, BBGame *game, bool up, bool down, bool confirm, const char *path, bool smoke)
{
    int key;
    if (!ui->entering_initials) {
        if (confirm) bb_game_menu(game);
        return;
    }
    if (key_pressed(KEY_LEFT) && ui->initial_cursor > 0) --ui->initial_cursor;
    if (key_pressed(KEY_RIGHT) && ui->initial_cursor < 2) ++ui->initial_cursor;
    char *letter = &ui->initials[ui->initial_cursor];
    if (up) *letter = *letter >= 'Z' ? 'A' : (char)(*letter + 1);
    if (down) *letter = *letter <= 'A' ? 'Z' : (char)(*letter - 1);
    while ((key = GetCharPressed()) != 0) {
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (key >= 'A' && key <= 'Z') *letter = (char)key;
    }
    if (confirm && ++ui->initial_cursor >= 3) score_submit(ui, game, path, smoke);
}

static void usage(const char *program)
{
    printf("Usage: %s [--assets DIR] [--mute] [--validate-assets] [--players 1|2]\n"
           "       [--smoke-test FRAMES] [--screenshot PATH.png] [--score-file PATH]\n"
           "P1: A/D move, W/Space jump, E/Z fire.\n"
           "P2: left/right move, up jump, Right Shift fire.\n"
           "1 player also accepts arrows, X jump, Slash/Right Ctrl fire.\n"
           "Gamepad 1/2: stick/D-pad move, right face button jump, bottom face button fire.\n"
           "Menu: 1/2 chooses players; up/down selects play or leaderboard; Enter confirms.\n"
           "P pause; M mute; Escape menu; F11 fullscreen.\n", program);
}

static bool save_screenshot(RenderTexture2D canvas, const char *path)
{
    Image image = LoadImageFromTexture(canvas.texture);
    if (!image.data) return false;
    /* 렌더 텍스처의 읽기 방향을 화면과 맞춘 뒤 PNG로 인코딩한다. */
    ImageFlipVertical(&image);
    int size = 0;
    unsigned char *png = ExportImageToMemory(image, ".png", &size);
    UnloadImage(image);
    if (!png || size <= 0) { MemFree(png); return false; }
    FILE *file = bb_platform_fopen(path, "wb");
    bool ok = false;
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
    bool validate_only = false;
    char asset_directory[BB_PATH_CAP];
    char score_path[BB_PATH_CAP] = {0};
    uint8_t maps[BB_LEVEL_COUNT * BB_MAP_WIDTH * BB_MAP_HEIGHT];
    BBUI ui = { .selected_players = 1 };
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) asset_override = argv[++i];
        else if (strcmp(argv[i], "--score-file") == 0 && i + 1 < argc) score_override = argv[++i];
        else if (strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) screenshot = argv[++i];
        else if (strcmp(argv[i], "--mute") == 0) ui.muted = true;
        else if (strcmp(argv[i], "--validate-assets") == 0) validate_only = true;
        else if (strcmp(argv[i], "--players") == 0 && i + 1 < argc) {
            if (!positive_number(argv[++i], &ui.selected_players, BB_MAX_PLAYERS)) { usage(argv[0]); return EXIT_FAILURE; }
        }
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
    if (!bb_assets_validate(asset_directory, maps)) return EXIT_FAILURE;
    if (validate_only) {
        printf("Validated %d playable maps, both players, enemies, items, font, 3 WAV sounds and OGG music.\n", BB_LEVEL_COUNT);
        return EXIT_SUCCESS;
    }
    ui.menu_selection = ui.selected_players - 1;
    if (score_override) {
        if (strlen(score_override) >= sizeof score_path) return EXIT_FAILURE;
        memcpy(score_path, score_override, strlen(score_override) + 1);
    } else if (!smoke_frames && !bb_platform_score_path(score_path, sizeof score_path)) {
        fprintf(stderr, "User score directory unavailable; scores remain in memory.\n");
        ui.save_failed = true;
    }
    if (score_path[0] && !bb_scores_load(score_path, ui.scores, &ui.score_count)) {
        ui.save_failed = true;
        fprintf(stderr, "Cannot read scores: %s; persistent writes disabled.\n", score_path);
        score_path[0] = '\0';
    }
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(BB_SCREEN_WIDTH * 3, BB_SCREEN_HEIGHT * 3, "Bubble Bobble - C11");
    if (!IsWindowReady()) { fprintf(stderr, "Cannot create graphics window.\n"); return EXIT_FAILURE; }
    SetWindowMinSize(BB_SCREEN_WIDTH, BB_SCREEN_HEIGHT);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    /* 에셋 검증과 그래픽 의존성이 없는 코어 테스트에는 장치가 필요 없다. */
    InitAudioDevice();
    bool audio_ready = IsAudioDeviceReady();
    if (!audio_ready) fprintf(stderr, "Audio device unavailable; continuing without sound.\n");
    if (audio_ready) SetMasterVolume(ui.muted ? 0.0f : 1.0f);
    BBAssets assets;
    if (!bb_assets_load(&assets, asset_directory, audio_ready)) {
        fprintf(stderr, "Could not load game resources.\n");
        bb_assets_unload(&assets);
        if (audio_ready) CloseAudioDevice();
        CloseWindow();
        return EXIT_FAILURE;
    }
    RenderTexture2D canvas = LoadRenderTexture(BB_SCREEN_WIDTH, BB_SCREEN_HEIGHT);
    if (!IsRenderTextureValid(canvas)) {
        bb_assets_unload(&assets);
        if (audio_ready) CloseAudioDevice();
        CloseWindow();
        return EXIT_FAILURE;
    }
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT);
    BBGame game;
    bb_game_init(&game, maps, 0xBB1986u);
    if (smoke_frames) { bb_game_start(&game, ui.selected_players); bb_game_skip_intro(&game); }
    BBInput pending[BB_MAX_PLAYERS] = {0};
    double accumulator = 0;
    int frames = 0;
    bool screenshot_ok = true, was_simulating = false;
    while (!WindowShouldClose()) {
        poll_key_edges();
        BBState previous_state = game.state;
        bool focused = IsWindowFocused() || smoke_frames > 0;
        bool clear_actions = false;
        bool confirm = focused && (key_pressed(KEY_ENTER) || pad_pressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                                   pad_pressed(1, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
        bool up = focused && (key_pressed(KEY_UP) || key_pressed(KEY_W) ||
                              pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP) || pad_pressed(1, GAMEPAD_BUTTON_LEFT_FACE_UP));
        bool down = focused && (key_pressed(KEY_DOWN) || key_pressed(KEY_S) ||
                                pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || pad_pressed(1, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
        if (focused && key_pressed(KEY_F11)) ToggleFullscreen();
        if (focused && key_pressed(KEY_M)) { ui.muted = !ui.muted; if (audio_ready) SetMasterVolume(ui.muted ? 0.0f : 1.0f); }
        if (focused && key_pressed(KEY_ESCAPE)) {
            if (game.state == BB_STATE_MENU) break;
            ui.entering_initials = false;
            bb_game_menu(&game); ui.paused = false; accumulator = 0;
            memset(pending, 0, sizeof pending);
        } else if (focused && game.state == BB_STATE_MENU) {
            if (up) ui.menu_selection = (ui.menu_selection + BB_MENU_COUNT - 1) % BB_MENU_COUNT;
            if (down) ui.menu_selection = (ui.menu_selection + 1) % BB_MENU_COUNT;
            if (key_pressed(KEY_ONE)) ui.menu_selection = BB_MENU_ONE_PLAYER;
            if (key_pressed(KEY_TWO)) ui.menu_selection = BB_MENU_TWO_PLAYERS;
            if (ui.menu_selection != BB_MENU_LEADERBOARD) ui.selected_players = ui.menu_selection + 1;
            if (confirm) {
                if (ui.menu_selection == BB_MENU_LEADERBOARD) {
                    game.state = BB_STATE_SCORE;
                    game.player_count = 0;
                    memset(game.players, 0, sizeof game.players);
                    game.won = false;
                    ui.entering_initials = false;
                } else {
                    bb_game_start(&game, ui.selected_players);
                }
                accumulator = 0;
                clear_actions = true;
            }
        } else if (focused && game.state == BB_STATE_SCORE) {
            score_input(&ui, &game, up, down, confirm, score_path, smoke_frames > 0);
        } else if (focused) {
            if (game.state == BB_STATE_INTRO && confirm) {
                bb_game_skip_intro(&game);
                clear_actions = true;
            }
            if (key_pressed(KEY_P) || pad_pressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
                (game.player_count == 2 && pad_pressed(1, GAMEPAD_BUTTON_MIDDLE_RIGHT))) {
                ui.paused = !ui.paused;
                clear_actions = true;
                accumulator = 0;
            }
        }
        if (assets.audio) UpdateMusicStream(assets.music);
        bool simulate = !ui.paused && focused && game.state != BB_STATE_MENU && game.state != BB_STATE_SCORE;
        if (simulate) {
            /* 가변 렌더 프레임을 120Hz 고정 시뮬레이션 단계로 누적한다. */
            float elapsed = smoke_frames ? 1.0f / 60.0f : GetFrameTime();
            if (elapsed > 0.25f) elapsed = 0.25f;
            accumulator += elapsed;
            if (clear_actions || !was_simulating) memset(pending, 0, sizeof pending);
            for (int i = 0; i < game.player_count; ++i) {
                BBInput input = player_input(i, game.player_count);
                pending[i].move = input.move;
                /* 시작/재개 버튼이 발사로 이어지지 않게 전환 프레임의 동작을 비운다. */
                if (clear_actions || !was_simulating || (game.state != BB_STATE_PLAY && game.state != BB_STATE_CLEAR)) {
                    pending[i].jump = pending[i].fire = false;
                } else {
                    /* 짧게 눌렀다 뗀 동작도 다음 고정 단계까지 보존한다. */
                    pending[i].jump = pending[i].jump || input.jump;
                    pending[i].fire = pending[i].fire || input.fire;
                }
            }
            while (accumulator >= (double)BB_FIXED_DT) {
                if (smoke_frames) {
                    pending[0].move = (game.ticks / 240u) % 2u ? -1.0f : 1.0f;
                    pending[0].jump = game.ticks % 100u == 0;
                    pending[0].fire = game.ticks % 50u == 0;
                    if (game.player_count == 2) {
                        pending[1].move = (game.ticks / 300u) % 2u ? 1.0f : -1.0f;
                        pending[1].jump = game.ticks % 140u == 20u;
                        pending[1].fire = game.ticks % 70u == 10u;
                    }
                }
                bb_game_update(&game, pending, BB_FIXED_DT);
                bb_audio_events(&assets, game.events);
                for (int i = 0; i < game.player_count; ++i) pending[i].jump = pending[i].fire = false;
                accumulator -= BB_FIXED_DT;
                if (game.state == BB_STATE_SCORE) { accumulator = 0; break; }
            }
        } else { accumulator = 0; memset(pending, 0, sizeof pending); }
        was_simulating = simulate;
        if (game.state == BB_STATE_SCORE && previous_state != BB_STATE_SCORE && previous_state != BB_STATE_MENU)
            score_entry_begin(&ui, &game);
        BeginTextureMode(canvas);
        bb_draw_game(&assets, &game, &ui);
        EndTextureMode();
        /* 256x224 화면을 가능한 한 정수 배율로 중앙에 확대해 픽셀 경계를 유지한다. */
        float scale = fminf((float)GetScreenWidth() / BB_SCREEN_WIDTH, (float)GetScreenHeight() / BB_SCREEN_HEIGHT);
        if (scale >= 1) scale = floorf(scale);
        float width = BB_SCREEN_WIDTH * scale, height = BB_SCREEN_HEIGHT * scale;
        BeginDrawing();
        ClearBackground(bb_color(12, 12, 12, 255));
        /* raylib 렌더 텍스처는 표시할 때 세로 방향을 뒤집어야 한다. */
        DrawTexturePro(canvas.texture, bb_rectangle(0, 0, BB_SCREEN_WIDTH, -BB_SCREEN_HEIGHT),
                       bb_rectangle(((float)GetScreenWidth() - width) / 2, ((float)GetScreenHeight() - height) / 2, width, height),
                       bb_vector2(0, 0), 0, WHITE);
        EndDrawing();
        ++frames;
        if (screenshot && frames == (smoke_frames ? smoke_frames : 1)) screenshot_ok = save_screenshot(canvas, screenshot);
        if (smoke_frames && frames >= smoke_frames) break;
    }
    bool smoke_ok = !smoke_frames || frames >= smoke_frames;
    if (smoke_frames) {
        printf("Smoke %s: frames=%d ticks=%" PRIu64 " level=%d enemies=%d state=%d players=%d\n",
               smoke_ok ? "completed" : "interrupted", frames, game.ticks, game.level + 1,
               bb_game_enemies_left(&game), (int)game.state, game.player_count);
        for (int i = 0; i < game.player_count; ++i)
            printf("P%d: active=%d x=%.3f y=%.3f mana=%.3f boosting=%d lives=%d score=%d\n", i + 1,
                   (int)game.players[i].active, game.players[i].body.x, game.players[i].body.y,
                   game.players[i].energy, (int)game.players[i].boosting,
                   game.players[i].lives, game.players[i].score);
    }
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
    if (!bb_platform_arguments(argc, argv, &arguments)) {
        fprintf(stderr, "Cannot decode command-line arguments.\n");
        return EXIT_FAILURE;
    }
    int result = run_game(arguments.count, arguments.values);
    bb_platform_free_arguments(&arguments);
    return result;
}
