/* 데스크톱 프로그램의 진입점이다. 이 파일은 입력, 창, 소리, 저장, 게임 반복을
 * 연결하고, 실제 게임 규칙과 충돌 계산은 bb_game.c에 맡긴다. */
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
static bool pressed_keys[512]; /* raylib 키 번호로 바로 조회하는 이번 프레임의 눌림 표 */

/* 한 화면 프레임에 한 번 키 이벤트 큐를 끝까지 읽는다.
 * 배열을 먼저 비우므로 이전 프레임의 눌림이 다음 프레임에 재사용되지 않는다. */
static void poll_key_edges(void)
{
    int key;
    memset(pressed_keys, 0, sizeof pressed_keys);
    /* 0은 큐가 비었다는 뜻이다. 범위 밖 키 번호는 배열에 기록하지 않는다. */
    while ((key = GetKeyPressed()) != 0)
        if (key > 0 && key < (int)(sizeof pressed_keys / sizeof pressed_keys[0])) pressed_keys[key] = true;
}

/* 큐에 남은 짧은 입력과 raylib의 일반 눌림 상태를 함께 확인한다.
 * 계속 누르고 있는지는 IsKeyDown으로 별도 확인하며, 여기서는 눌린 순간만 취급한다. */
static bool key_pressed(int key)
{
    return (key >= 0 && key < (int)(sizeof pressed_keys / sizeof pressed_keys[0]) && pressed_keys[key]) || IsKeyPressed(key);
}

/* 명령행의 양의 정수를 검증한 뒤에만 out에 기록한다.
 * 범위 초과, 빈 문자열, 뒤에 붙은 글자도 실패로 처리해 부분 변환을 허용하지 않는다. */
static bool positive_number(const char *value, int *out, int maximum)
{
    char *end;
    errno = 0; /* strtol의 오류와 이전 함수가 남긴 오류를 구별한다. */
    long number = strtol(value, &end, 10);
    if (errno || end == value || *end || number < 1 || number > maximum) return false;
    *out = (int)number;
    return true;
}

/* 해당 번호의 패드가 연결되었을 때만 버튼의 눌림 상태를 읽는다. */
static bool pad_pressed(int index, int button)
{
    return IsGamepadAvailable(index) && IsGamepadButtonPressed(index, button);
}

/* 한 플레이어의 여러 입력 장치를 코어가 이해하는 이동/점프/발사 값으로 합친다.
 * player는 0부터 시작하며, 존재하는 플레이어에 대해서만 호출한다. */
static BBInput player_input(int player, int player_count)
{
    BBInput input = {0};
    /* 참/거짓을 1/0으로 바꾸어 오른쪽 - 왼쪽을 계산한다. 동시 입력은 상쇄된다. */
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
    /* 2P 발사는 오른쪽 Shift에만 연결해 1P의 발사 키와 겹치지 않게 한다. */
    if (player == 1) input.fire = key_pressed(KEY_RIGHT_SHIFT);
    /* 혼자 할 때는 이전 조작 방식도 계속 사용할 수 있게 보조 키를 허용한다. */
    if (player_count == 1) {
        input.jump = input.jump || key_pressed(KEY_X);
        input.fire = input.fire || key_pressed(KEY_SLASH) || key_pressed(KEY_RIGHT_CONTROL);
    }
    if (IsGamepadAvailable(player)) {
        /* 중앙 부근의 작은 스틱 흔들림은 무시하는 데드존을 둔다. */
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

/* 아직 점수를 제출하지 않은 활성 플레이어를 번호순으로 찾는다.
 * 모두 제출했으면 entering_initials가 false로 남아 순위표만 보여 준다. */
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
    /* 각 사람은 이전 사람의 이름을 물려받지 않고 AAA의 첫 칸부터 입력한다. */
    ui->initial_cursor = 0;
    memcpy(ui->initials, "AAA", sizeof ui->initials);
}

/* 게임 종료 시 한 번 호출해 이번 게임의 점수 입력 순서를 시작한다. */
static void score_entry_begin(BBUI *ui, const BBGame *game)
{
    memset(ui->score_recorded, 0, sizeof ui->score_recorded);
    score_entry_next(ui, game);
    /* 플레이 중 누른 글자가 이름의 첫 글자로 넘어오지 않게 한다. */
    while (GetCharPressed() != 0) { }
}

/* 현재 사람의 기록을 임시 순위표에 넣고, 디스크 저장까지 성공하면 반영한다.
 * 실패한 경우 같은 사람과 이름을 유지하므로 Enter로 다시 저장할 수 있다. */
static void score_submit(BBUI *ui, const BBGame *game, const char *path, bool smoke)
{
    BbScore record = {0};
    BbScore updated[BB_SCORE_COUNT];
    size_t count = ui->score_count;
    int player = ui->score_player;
    /* 입력 중이 아니거나 이미 제출한 사람은 기록을 중복 삽입하지 않는다. */
    if (!ui->entering_initials || player < 0 || player >= game->player_count || ui->score_recorded[player]) return;
    /* 파일 형식은 음수가 없는 점수와 사람이 읽는 1부터 시작하는 라운드를 쓴다. */
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
    /* 성공한 표와 제출 여부를 함께 확정한 뒤 다음 플레이어로 넘어간다. */
    memcpy(ui->scores, updated, sizeof updated);
    ui->score_count = count;
    ui->score_recorded[player] = true;
    ui->save_failed = false;
    score_entry_next(ui, game);
}

/* 이름은 영문 대문자 세 글자다. 방향키로 편집하거나 문자 키로 현재 칸을 바꾸며,
 * Enter는 다음 칸으로 이동하고 마지막 칸에서는 저장을 시도한다. */
static void score_input(BBUI *ui, BBGame *game, bool up, bool down, bool confirm, const char *path, bool smoke)
{
    int key;
    /* 이름 입력이 끝난 순위표에서 확인을 누르면 메뉴로 돌아간다. */
    if (!ui->entering_initials) {
        if (confirm) bb_game_menu(game);
        return;
    }
    if (key_pressed(KEY_LEFT) && ui->initial_cursor > 0) --ui->initial_cursor;
    if (key_pressed(KEY_RIGHT) && ui->initial_cursor < 2) ++ui->initial_cursor;
    /* 커서를 0..2로 유지하므로 문자열 끝의 종료 문자를 덮어쓰지 않는다. */
    char *letter = &ui->initials[ui->initial_cursor];
    if (up) *letter = *letter >= 'Z' ? 'A' : (char)(*letter + 1);
    if (down) *letter = *letter <= 'A' ? 'Z' : (char)(*letter - 1);
    /* 문자 이벤트는 키 코드와 다르다. 소문자는 대문자로 바꾸고 나머지는 버린다. */
    while ((key = GetCharPressed()) != 0) {
        if (key >= 'a' && key <= 'z') key -= 'a' - 'A';
        if (key >= 'A' && key <= 'Z') *letter = (char)key;
    }
    if (confirm && ++ui->initial_cursor >= 3) score_submit(ui, game, path, smoke);
}

/* 잘못된 옵션과 --help에서 같은 사용법을 출력해 실제 키 배치를 안내한다. */
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

/* 창의 여백을 제외한 원래 게임 캔버스를 PNG 파일로 저장한다.
 * GPU에서 읽은 이미지와 PNG 버퍼는 서로 별개이므로 각각 해제해야 한다. */
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
    /* 플랫폼 래퍼를 사용해 한글 경로에서도 이진 파일을 열 수 있게 한다. */
    FILE *file = bb_platform_fopen(path, "wb");
    bool ok = false;
    if (file) {
        /* 일부만 써졌거나 닫을 때 쓰기 오류가 발생해도 저장 실패로 보고한다. */
        ok = fwrite(png, 1, (size_t)size, file) == (size_t)size;
        if (fclose(file) != 0) ok = false;
    }
    MemFree(png);
    return ok;
}

/* UTF-8로 정리된 인자를 받아 자원을 준비하고 창이 닫힐 때까지 게임을 실행한다.
 * 초기화 도중 실패하면 그 시점까지 만든 자원을 역순으로 정리하고 실패 코드를 돌려준다. */
static int run_game(int argc, char **argv)
{
    /* 옵션 문자열은 argv의 메모리를 빌려 쓴다. 여기서 별도로 해제하지 않는다. */
    const char *asset_override = NULL, *screenshot = NULL, *score_override = NULL;
    int smoke_frames = 0;
    bool validate_only = false;
    char asset_directory[BB_PATH_CAP];
    char score_path[BB_PATH_CAP] = {0};
    uint8_t maps[BB_LEVEL_COUNT * BB_MAP_WIDTH * BB_MAP_HEIGHT];
    BBUI ui = { .selected_players = 1 }; /* 지정하지 않은 UI 필드는 모두 0으로 시작한다. */
    /* 값이 필요한 옵션은 뒤 인자의 존재를 확인한 다음 소비한다.
     * 누락된 값이나 알 수 없는 옵션을 무시하지 않아 자동 실행의 실수를 드러낸다. */
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
    /* 직접 지정한 에셋 폴더를 우선하고, 없으면 실행 파일 위치를 기준으로 찾는다. */
    if (asset_override) {
        if (strlen(asset_override) >= sizeof asset_directory) return EXIT_FAILURE;
        memcpy(asset_directory, asset_override, strlen(asset_override) + 1);
    } else if (!bb_platform_asset_dir(asset_directory, sizeof asset_directory)) {
        fprintf(stderr, "Cannot locate executable assets; use --assets DIR.\n"); return EXIT_FAILURE;
    }
    /* 평상시 정보 로그는 줄이고, UTF-8 경로를 지원하는 파일 로더를 먼저 등록한다. */
    SetTraceLogLevel(LOG_WARNING);
    bb_assets_install_file_loader();
    /* 이미지와 사운드는 장치 없이 디코딩해 검증하므로 창 생성 전에 실패를 알릴 수 있다. */
    if (!bb_assets_validate(asset_directory, maps)) return EXIT_FAILURE;
    /* 검증 전용 실행은 그래픽/오디오 장치를 열거나 점수 파일을 수정하지 않는다. */
    if (validate_only) {
        printf("Validated %d playable maps, both players, enemies, items, font, 3 WAV sounds and OGG music.\n", BB_LEVEL_COUNT);
        return EXIT_SUCCESS;
    }
    /* 1인/2인 선택 번호를 0부터 시작하는 메뉴 배열 번호로 맞춘다. */
    ui.menu_selection = ui.selected_players - 1;
    /* 점수 경로는 명시한 파일 또는 운영체제의 사용자별 저장 폴더를 사용한다. */
    if (score_override) {
        if (strlen(score_override) >= sizeof score_path) return EXIT_FAILURE;
        memcpy(score_path, score_override, strlen(score_override) + 1);
    } else if (!smoke_frames && !bb_platform_score_path(score_path, sizeof score_path)) {
        fprintf(stderr, "User score directory unavailable; scores remain in memory.\n");
        ui.save_failed = true;
    }
    /* 기존 기록을 읽지 못하면 같은 경로에 덮어쓰지 않도록 쓰기도 막는다. */
    if (score_path[0] && !bb_scores_load(score_path, ui.scores, &ui.score_count)) {
        ui.save_failed = true;
        fprintf(stderr, "Cannot read scores: %s; persistent writes disabled.\n", score_path);
        score_path[0] = '\0';
    }
    /* 실제 창은 기본 캔버스의 세 배로 열되 크기 변경과 수직 동기화를 요청한다. */
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(BB_SCREEN_WIDTH * 3, BB_SCREEN_HEIGHT * 3, "Bubble Bobble - C11");
    if (!IsWindowReady()) { fprintf(stderr, "Cannot create graphics window.\n"); return EXIT_FAILURE; }
    SetWindowMinSize(BB_SCREEN_WIDTH, BB_SCREEN_HEIGHT);
    SetExitKey(KEY_NULL); /* Escape는 즉시 종료 대신 아래의 메뉴 처리에서 사용한다. */
    SetTargetFPS(60);
    /* 에셋 검증과 그래픽 의존성이 없는 코어 테스트에는 장치가 필요 없다. */
    InitAudioDevice();
    bool audio_ready = IsAudioDeviceReady();
    if (!audio_ready) fprintf(stderr, "Audio device unavailable; continuing without sound.\n");
    if (audio_ready) SetMasterVolume(ui.muted ? 0.0f : 1.0f);
    /* GPU 자원은 창을 만든 뒤, 소리 자원은 장치 상태를 확인한 뒤 올린다. */
    BBAssets assets;
    if (!bb_assets_load(&assets, asset_directory, audio_ready)) {
        fprintf(stderr, "Could not load game resources.\n");
        bb_assets_unload(&assets);
        if (audio_ready) CloseAudioDevice();
        CloseWindow();
        return EXIT_FAILURE;
    }
    /* 모든 화면을 작은 고정 해상도에 먼저 그려 창 크기와 게임 좌표를 분리한다. */
    RenderTexture2D canvas = LoadRenderTexture(BB_SCREEN_WIDTH, BB_SCREEN_HEIGHT);
    if (!IsRenderTextureValid(canvas)) {
        bb_assets_unload(&assets);
        if (audio_ready) CloseAudioDevice();
        CloseWindow();
        return EXIT_FAILURE;
    }
    SetTextureFilter(canvas.texture, TEXTURE_FILTER_POINT); /* 확대할 때 픽셀 사이를 흐리게 섞지 않는다. */
    BBGame game;
    bb_game_init(&game, maps, 0xBB1986u); /* 같은 시드로 시작해 자동 실행 결과를 재현한다. */
    if (smoke_frames) { bb_game_start(&game, ui.selected_players); bb_game_skip_intro(&game); }
    /* 화면 프레임과 물리 갱신 횟수가 달라도 눌림 동작을 한 번만 전달하기 위한 입력이다. */
    BBInput pending[BB_MAX_PLAYERS] = {0};
    double accumulator = 0; /* 아직 물리 계산에 사용하지 않은 실제 경과 시간(초) */
    int frames = 0;
    bool screenshot_ok = true, was_simulating = false;
    /* 한 번 반복할 때 입력을 수집하고, 필요한 만큼 코어를 갱신한 뒤 한 장을 그린다. */
    while (!WindowShouldClose()) {
        poll_key_edges();
        BBState previous_state = game.state; /* 이번 프레임에 점수 화면으로 바뀌었는지 비교한다. */
        /* 일반 실행은 창이 비활성일 때 멈추고, 자동 실행은 포커스에 의존하지 않는다. */
        bool focused = IsWindowFocused() || smoke_frames > 0;
        bool clear_actions = false;
        /* 메뉴 조작은 어느 패드에서도 가능하지만 포커스를 잃은 창은 입력받지 않는다. */
        bool confirm = focused && (key_pressed(KEY_ENTER) || pad_pressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                                   pad_pressed(1, GAMEPAD_BUTTON_RIGHT_FACE_DOWN));
        bool up = focused && (key_pressed(KEY_UP) || key_pressed(KEY_W) ||
                              pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_UP) || pad_pressed(1, GAMEPAD_BUTTON_LEFT_FACE_UP));
        bool down = focused && (key_pressed(KEY_DOWN) || key_pressed(KEY_S) ||
                                pad_pressed(0, GAMEPAD_BUTTON_LEFT_FACE_DOWN) || pad_pressed(1, GAMEPAD_BUTTON_LEFT_FACE_DOWN));
        /* 전체 화면과 음소거는 메뉴/플레이 상태와 무관한 공통 단축키다. */
        if (focused && key_pressed(KEY_F11)) ToggleFullscreen();
        if (focused && key_pressed(KEY_M)) { ui.muted = !ui.muted; if (audio_ready) SetMasterVolume(ui.muted ? 0.0f : 1.0f); }
        /* Escape: 메뉴에서는 종료, 그 외 화면에서는 누적 입력을 비우고 메뉴로 복귀한다. */
        if (focused && key_pressed(KEY_ESCAPE)) {
            if (game.state == BB_STATE_MENU) break;
            ui.entering_initials = false;
            bb_game_menu(&game); ui.paused = false; accumulator = 0;
            memset(pending, 0, sizeof pending);
        } else if (focused && game.state == BB_STATE_MENU) {
            /* 나머지 연산으로 위/아래 선택을 메뉴 끝에서 처음으로 순환시킨다. */
            if (up) ui.menu_selection = (ui.menu_selection + BB_MENU_COUNT - 1) % BB_MENU_COUNT;
            if (down) ui.menu_selection = (ui.menu_selection + 1) % BB_MENU_COUNT;
            if (key_pressed(KEY_ONE)) ui.menu_selection = BB_MENU_ONE_PLAYER;
            if (key_pressed(KEY_TWO)) ui.menu_selection = BB_MENU_TWO_PLAYERS;
            if (ui.menu_selection != BB_MENU_LEADERBOARD) ui.selected_players = ui.menu_selection + 1;
            if (confirm) {
                /* 메뉴에서 연 순위표는 방금 끝난 게임과 달리 새 기록을 입력하지 않는다. */
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
                clear_actions = true; /* 확인에 사용한 키를 게임 동작으로 다시 쓰지 않는다. */
            }
        } else if (focused && game.state == BB_STATE_SCORE) {
            score_input(&ui, &game, up, down, confirm, score_path, smoke_frames > 0);
        } else if (focused) {
            if (game.state == BB_STATE_INTRO && confirm) {
                bb_game_skip_intro(&game);
                clear_actions = true; /* 확인에 사용한 키를 게임 동작으로 다시 쓰지 않는다. */
            }
            /* 일시정지 전후의 시간을 모아 한꺼번에 시뮬레이션하지 않도록 누적량을 비운다. */
            if (key_pressed(KEY_P) || pad_pressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT) ||
                (game.player_count == 2 && pad_pressed(1, GAMEPAD_BUTTON_MIDDLE_RIGHT))) {
                ui.paused = !ui.paused;
                clear_actions = true; /* 확인에 사용한 키를 게임 동작으로 다시 쓰지 않는다. */
                accumulator = 0;
            }
        }
        /* 음악은 스트리밍이므로 게임이 일시정지되어도 매 프레임 버퍼를 보충한다. */
        if (assets.audio) UpdateMusicStream(assets.music);
        bool simulate = !ui.paused && focused && game.state != BB_STATE_MENU && game.state != BB_STATE_SCORE;
        if (simulate) {
            /* 가변 렌더 프레임을 120Hz 고정 시뮬레이션 단계로 누적한다. */
            float elapsed = smoke_frames ? 1.0f / 60.0f : GetFrameTime();
            /* 창 이동 등으로 긴 프레임이 생겨도 한 프레임의 따라잡기 계산을 제한한다. */
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
            /* 충분한 시간이 쌓인 횟수만큼 1/120초씩 처리한다. 남은 시간은 다음 프레임에 쓴다. */
            while (accumulator >= (double)BB_FIXED_DT) {
                /* 자동 실행은 실제 키 대신 틱 번호에 따른 이동/점프/발사를 반복한다.
                 * 2P는 주기를 다르게 해 두 플레이어가 항상 같은 행동을 하지 않게 한다. */
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
                /* 갱신이 만든 사건을 즉시 소리로 전달한다. 다음 갱신에서는 사건 표가 지워진다. */
                bb_game_update(&game, pending, BB_FIXED_DT);
                bb_audio_events(&assets, game.events);
                /* 이동은 유지하되 점프/발사는 소비해 같은 프레임의 다음 틱에서 반복하지 않는다. */
                for (int i = 0; i < game.player_count; ++i) pending[i].jump = pending[i].fire = false;
                accumulator -= BB_FIXED_DT;
                if (game.state == BB_STATE_SCORE) { accumulator = 0; break; }
            }
        } else { accumulator = 0; memset(pending, 0, sizeof pending); }
        was_simulating = simulate;
        /* 플레이에서 점수 화면으로 넘어온 순간에만 이름 입력을 시작한다. */
        if (game.state == BB_STATE_SCORE && previous_state != BB_STATE_SCORE && previous_state != BB_STATE_MENU)
            score_entry_begin(&ui, &game);
        /* 게임 전체를 오프스크린 캔버스에 먼저 완성한 뒤 실제 창으로 복사한다. */
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
        /* 스크린샷은 자동 실행의 마지막 장 또는 일반 실행의 첫 장을 한 번 저장한다. */
        ++frames;
        if (screenshot && frames == (smoke_frames ? smoke_frames : 1)) screenshot_ok = save_screenshot(canvas, screenshot);
        if (smoke_frames && frames >= smoke_frames) break;
    }
    /* 사용자가 창을 일찍 닫은 자동 실행은 정해진 프레임 수를 채우지 못했으므로 실패다. */
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
    /* 장치를 닫기 전에 해당 장치를 사용하는 텍스처와 소리를 먼저 해제한다. */
    UnloadRenderTexture(canvas);
    bb_assets_unload(&assets);
    if (audio_ready) CloseAudioDevice();
    CloseWindow();
    if (!screenshot_ok) fprintf(stderr, "Could not save screenshot: %s\n", screenshot);
    return screenshot_ok && smoke_ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

/* 운영체제별 명령행 인자 차이를 정리한 후 공통 실행 함수에 넘긴다.
 * 인자 변환이 할당한 메모리는 게임이 어떤 결과로 끝나든 마지막에 해제한다. */
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
