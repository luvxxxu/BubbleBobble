/* 게임 상태를 화면과 소리로 표현하는 파일이다. 게임 규칙을 바꾸지 않고
 * 코어가 가진 위치/상태를 읽어 알맞은 그림과 UI를 선택한다. */
#include "bb_render.h"
#include "bb_levels.h"
#include "bb_raylib_types.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 1P의 녹색과 2P의 파란색을 메뉴, 이름 입력, 점수, 마나 표시에서 함께 쓴다. */
static const Color player_colors[BB_MAX_PLAYERS] = {
    {92, 230, 52, 255}, {80, 184, 255, 255}
};

/* raylib 파일 로더 콜백의 반환 버퍼는 raylib이 MemFree로 해제한다. */
static unsigned char *read_asset(const char *path, int *size)
{
    *size = 0; /* 실패 시 호출자가 읽지 못한 데이터를 사용하지 않도록 먼저 비운다. */
    FILE *file = bb_platform_fopen(path, "rb");
    if (!file) return NULL;
    /* 전체 크기를 확인한 뒤 처음으로 되돌아와 한 번에 읽는다. */
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    /* 빈 파일, 64 MiB를 넘는 파일, 되감기에 실패한 파일은 에셋으로 받지 않는다.
     * 크기 제한 덕분에 아래 unsigned int/int 변환도 표현 범위 안에 들어온다. */
    if (length <= 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    /* raylib에 소유권을 넘길 메모리이므로 malloc 대신 MemAlloc으로 확보한다. */
    unsigned char *data = MemAlloc((unsigned int)length);
    if (!data) { fclose(file); return NULL; }
    size_t received = fread(data, 1, (size_t)length, file);
    int closed = fclose(file);
    /* 짧게 읽힌 파일이나 닫기 오류는 성공으로 취급하지 않고 버퍼도 즉시 돌려준다. */
    if (received != (size_t)length || closed != 0) { MemFree(data); return NULL; }
    *size = (int)length;
    return data;
}

/* 이미지/소리/폰트의 내부 읽기도 한글 경로를 지원하는 플랫폼 로더를 거치게 한다. */
void bb_assets_install_file_loader(void) { SetLoadFileDataCallback(read_asset); }

/* 에셋 폴더와 상대 파일명을 합친다. 고정 버퍼에 들어가지 않으면 실패를 전달한다. */
static bool path_for(char path[BB_PATH_CAP], const char *directory, const char *name)
{
    return bb_path_join(path, BB_PATH_CAP, directory, name);
}

/* 에셋을 CPU 메모리로 읽어 기본 형식을 검사하고, 요청한 경우 플레이용 맵도 만든다.
 * GPU 텍스처나 재생 장치를 만들지 않으므로 --validate-assets에서도 호출할 수 있다. */
bool bb_assets_validate(const char *directory, uint8_t *maps)
{
    /* 창과 오디오 장치를 열기 전에 이미지·오디오를 디코딩하고 폰트 파일을 확인한다. */
    /* 스프라이트를 고정 좌표로 잘라 쓰므로 파일 존재뿐 아니라 원본 크기도 확인한다. */
    static const struct { const char *name; int width, height; } images[] = {
        {"Levels.png", 32, 84}, {"BubbleCharacter.png", 96, 64},
        {"BobbleCharacter.png", 96, 64},
        {"Enemys.png", 256, 192}, {"Items.png", 576, 64},
        {"LevelTiles.png", 40, 200}, {"AttackBubble.png", 112, 16},
        {"BubbleLarge.png", 288, 32}, {"Logo.png", 143, 112}
    };
    const char *sounds[] = {"SFX/Bubble Bobble SFX (2).wav", "SFX/Bubble Bobble SFX (3).wav", "SFX/Jump.wav", "SFX/The Quest Begins.ogg"};
    char path[BB_PATH_CAP];
    for (size_t i = 0; i < sizeof images / sizeof images[0]; ++i) {
        if (!path_for(path, directory, images[i].name)) return false;
        Image image = LoadImage(path);
        if (!image.data || image.width != images[i].width || image.height != images[i].height) {
            fprintf(stderr, "Missing or invalid asset: %s (expected %dx%d)\n", path, images[i].width, images[i].height);
            if (image.data) UnloadImage(image);
            return false;
        }
        /* 검사용 디코딩 결과는 검사가 끝나면 즉시 해제하고 화면용 로드는 따로 한다. */
        UnloadImage(image);
    }
    /* 확장자만 믿지 않고 WAV/OGG를 실제 디코딩해 읽을 수 있는 소리인지 확인한다. */
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; ++i) {
        if (!path_for(path, directory, sounds[i])) return false;
        Wave wave = LoadWave(path);
        if (!IsWaveValid(wave)) { fprintf(stderr, "Invalid sound: %s\n", path); return false; }
        UnloadWave(wave);
    }
    /* 여기서는 폰트 파일이 최소 헤더 길이를 갖는지 확인한다.
     * 실제 폰트 생성 성공 여부는 그래픽 장치를 만든 뒤 bb_assets_load에서 검사한다. */
    if (!path_for(path, directory, "NES_Font.ttf")) return false;
    int size = 0;
    unsigned char *font = read_asset(path, &size);
    if (!font || size < 12) { MemFree(font); fprintf(stderr, "Missing font: %s\n", path); return false; }
    MemFree(font);
    /* Levels.png는 원본 에셋으로 검증하지만 실제 다섯 라운드는 코드의 배치표로 만든다. */
    if (maps) bb_levels_build(maps);
    return true;
}

/* 파일을 GPU 텍스처로 읽고 최근접 필터를 지정해 픽셀 아트의 경계를 보존한다.
 * 경로 생성 실패는 id가 0인 빈 텍스처로 표현한다. */
static Texture2D texture(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    if (!path_for(path, directory, name)) return bb_empty_texture();
    Texture2D result = LoadTexture(path);
    if (result.id) SetTextureFilter(result, TEXTURE_FILTER_POINT);
    return result;
}

/* 짧은 효과음을 메모리에 올린다. 성공 여부는 호출자가 IsSoundValid로 검사한다. */
static Sound sound(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    if (!path_for(path, directory, name)) return bb_empty_sound();
    return LoadSound(path);
}

/* 창 생성 후 화면 자원과 선택적인 소리 자원을 준비한다.
 * false 반환 시에도 일부 자원은 만들어졌을 수 있으므로 호출자가 반드시 정리해야 한다. */
bool bb_assets_load(BBAssets *a, const char *directory, bool audio)
{
    char path[BB_PATH_CAP];

    /* 중간에 실패해도 호출자가 bb_assets_unload로 로드된 자원만 정리할 수 있다. */
    memset(a, 0, sizeof *a);
    /* 각 텍스처는 여러 동작/물체 그림이 격자로 담긴 스프라이트 시트다. */
    a->players[0] = texture(directory, "BubbleCharacter.png");
    a->players[1] = texture(directory, "BobbleCharacter.png");
    a->enemy = texture(directory, "Enemys.png");
    a->items = texture(directory, "Items.png");
    a->tiles = texture(directory, "LevelTiles.png");
    a->bubble = texture(directory, "AttackBubble.png");
    a->large_bubble = texture(directory, "BubbleLarge.png");
    a->logo = texture(directory, "Logo.png");
    if (!path_for(path, directory, "NES_Font.ttf")) return false;
    a->font = LoadFontEx(path, 8, NULL, 95); /* 8픽셀 크기의 기본 ASCII 95글자를 준비한다. */
    /* 폰트 로드 실패 시 raylib이 돌려주는 기본 폰트도 정상 에셋으로 오인하지 않는다. */
    if (!a->players[0].id || !a->players[1].id || !a->enemy.id || !a->items.id || !a->tiles.id ||
        !a->bubble.id || !a->large_bubble.id || !a->logo.id || !IsFontValid(a->font) ||
        a->font.texture.id == GetFontDefault().texture.id) return false;
    SetTextureFilter(a->font.texture, TEXTURE_FILTER_POINT);
    /* 소리 장치가 없으면 화면 자원만으로 실행을 계속할 수 있다. */
    a->audio = audio && IsAudioDeviceReady();
    if (a->audio) {
        a->fire = sound(directory, "SFX/Bubble Bobble SFX (2).wav");
        a->death = sound(directory, "SFX/Bubble Bobble SFX (3).wav");
        a->jump = sound(directory, "SFX/Jump.wav");
        int size = 0;
        if (!path_for(path, directory, "SFX/The Quest Begins.ogg")) return false;
        /* 메모리에서 연 음악 스트림보다 원본 버퍼를 오래 유지한다. */
        a->music_data = read_asset(path, &size);
        if (a->music_data) a->music = LoadMusicStreamFromMemory(".ogg", a->music_data, size);
        if (!IsSoundValid(a->fire) || !IsSoundValid(a->death) || !IsSoundValid(a->jump) || !IsMusicValid(a->music)) return false;
        /* 배경음은 반복 재생하고 효과음보다 작게 조절한다.
         * 스트림 버퍼 보충은 main.c의 UpdateMusicStream에서 매 프레임 수행한다. */
        a->music.looping = true;
        SetMusicVolume(a->music, 0.45f);
        PlayMusicStream(a->music);
    }
    return true;
}

/* 로드 도중 실패한 객체도 안전하게 정리하도록 실제 생성된 자원만 해제한다.
 * 그래픽/소리 장치가 아직 살아 있는 동안 호출해야 한다. */
void bb_assets_unload(BBAssets *a)
{
    Texture2D textures[] = {
        a->players[0], a->players[1], a->tiles, a->bubble,
        a->large_bubble, a->logo, a->enemy, a->items
    };
    for (size_t i = 0; i < sizeof textures / sizeof textures[0]; ++i)
        if (textures[i].id) UnloadTexture(textures[i]);
    /* raylib의 공유 기본 폰트는 이 객체가 소유하지 않으므로 해제하지 않는다. */
    if (a->font.texture.id && a->font.texture.id != GetFontDefault().texture.id) UnloadFont(a->font);
    if (IsSoundValid(a->fire)) UnloadSound(a->fire);
    if (IsSoundValid(a->jump)) UnloadSound(a->jump);
    if (IsSoundValid(a->death)) UnloadSound(a->death);
    /* 음악 디코더가 원본 버퍼를 참조하므로 스트림을 먼저 닫고 버퍼를 나중에 해제한다. */
    if (IsMusicValid(a->music)) UnloadMusicStream(a->music);
    MemFree(a->music_data);
    memset(a, 0, sizeof *a); /* 해제한 핸들을 다시 유효한 자원으로 보지 않게 한다. */
}

/* 이번 코어 갱신에서 발생한 발사/점프/사망 사건을 준비된 효과음에 연결한다.
 * 사건 플래그는 같은 갱신 안의 여러 발생을 하나로 합친 값이다. */
void bb_audio_events(const BBAssets *a, BBEvents events)
{
    if (!a->audio) return;
    if (events.fire) PlaySound(a->fire);
    if (events.jump) PlaySound(a->jump);
    if (events.death) PlaySound(a->death);
}

/* UI 좌표는 타일 단위가 아닌 256x224 캔버스의 픽셀 단위다.
 * 모든 글자는 동일한 8픽셀 폰트와 추가 자간 0으로 그린다. */
static void text(const BBAssets *a, const char *value, float x, float y, Color color)
{
    DrawTextEx(a->font, value, bb_vector2(x, y), 8, 0, color);
}

/* 실제 글자 너비를 측정해 화면 중앙에 놓고 시작 위치를 정수 픽셀로 맞춘다. */
static void centered(const BBAssets *a, const char *value, float y, Color color)
{
    float width = MeasureTextEx(a->font, value, 8, 0).x;
    text(a, value, floorf((BB_SCREEN_WIDTH - width) * 0.5f), y, color);
}

/* 시트의 column/row 칸에서 size 픽셀 정사각형 한 장을 잘라 그린다.
 * 물리 좌표의 중심과 그림 중심을 일치시키고 반올림해 반 픽셀에 걸치지 않게 한다. */
static void sprite(Texture2D sheet, int column, int row, int size, float x, float y, bool flip)
{
    /* 게임 좌표는 8픽셀 타일 단위의 중심점이고, 음수 너비는 좌우 반전이다. */
    Rectangle source = {(float)(column * size), (float)(row * size), (float)(flip ? -size : size), (float)size};
    Rectangle destination = {roundf(x * 8 - (float)size / 2), roundf(y * 8 - (float)size / 2), (float)size, (float)size};
    DrawTexturePro(sheet, source, destination, bb_vector2(0, 0), 0, WHITE);
}

/* 적 종류를 스프라이트 시트의 행 번호로 바꾼다. 기본 행은 젠짱이다. */
static int enemy_row(BBEnemyType type)
{
    /* 0부터 센 2번 행은 마이타의 바위 그림이므로 몬스타는 3번 행을 사용한다. */
    switch (type) {
        case BB_ENEMY_MAITA: return 1;
        case BB_ENEMY_MONSTA: return 3;
        default: return 0;
    }
}

/* 아이템 종류마다 지정된 음식 그림을 선택해 현재 물리 위치에 그린다. */
static void pickup_sprite(const BBAssets *a, const BBPickup *pickup)
{
    /* 배열 순서는 BBPickupType과 같다. column/row는 Items.png의 16픽셀 격자 번호다. */
    static const struct { int column, row; } cells[BB_PICKUP_TYPE_COUNT] = {
        {19, 0}, /* 수박 */
        {25, 0}, /* 감자튀김 */
        {30, 1}, /* 체리 */
        {32, 1}, /* 딸기 */
        {16, 0}, /* 복숭아 */
        {13, 0}, /* 오렌지 */
        {34, 1}, /* 포도 */
        {17, 0}, /* 바나나 */
        {35, 1}, /* 파인애플 */
        {14, 0}, /* 레몬 */
        {4, 0},  /* 사과 */
        {18, 0}, /* 배 */
        {5, 0},  /* 무 */
        {10, 0}, /* 옥수수 */
        {2, 0},  /* 당근 */
        {1, 0},  /* 가지 */
        {3, 1},  /* 아이스크림 */
        {28, 0}, /* 케이크 */
        {24, 0}, /* 도넛 */
        {27, 0}  /* 햄버거 */
    };
    /* unsigned 변환으로 음수 종류도 큰 값이 되어 한 번의 범위 검사에 걸린다. */
    unsigned int type = (unsigned int)pickup->type;
    if (type >= BB_PICKUP_TYPE_COUNT) return;
    sprite(a->items, cells[type].column, cells[type].row, 16,
           pickup->body.x, pickup->body.y, false);
}

/* 플레이어 한 명의 점수, 마나, 남은 목숨을 고정된 화면 영역에 그린다.
 * 1P와 2P는 각각 폭 128픽셀을 사용해 서로의 표시를 덮지 않는다. */
static void player_hud(const BBAssets *a, const BBPlayer *player, int index)
{
    float energy = fmaxf(0, fminf(1, player->energy)); /* 표시할 채움 비율을 0..1로 제한한다. */
    int x = 8 + index * 128;
    int fill = (int)(energy * 34 + 0.5f); /* 테두리를 뺀 34픽셀을 가장 가까운 정수로 채운다. */
    Color color = player_colors[index];
    char label[32];
    char value[16];

    snprintf(label, sizeof label, "P%d", index + 1);
    text(a, label, (float)x, 2, color);
    /* 점수는 최소 여섯 자리로 표시하되 실제 글자 폭을 재어 오른쪽에 정렬한다. */
    snprintf(value, sizeof value, "%06d", player->score < 0 ? 0 : player->score);
    text(a, value, (float)(x + 112) - MeasureTextEx(a->font, value, 8, 0).x, 2, color);
    /* 탈락은 회색, 가속 중은 노란색으로 일반 마나 충전 상태와 구별한다. */
    if (player->state == BB_PLAYER_OUT) color = GRAY;
    else if (player->boosting) color = YELLOW;
    text(a, "MANA", (float)x, 13, color);
    /* 바깥 사각형, 어두운 내부, 채워진 길이 순으로 마나 막대를 만든다. */
    DrawRectangle(x + 34, 14, 36, 7, color);
    DrawRectangle(x + 35, 15, 34, 5, bb_color(32, 32, 32, 255));
    if (fill > 0) DrawRectangle(x + 35, 15, fill, 5, color);
    /* 가속 중에는 남은 초를 올림해 0초처럼 보이는 상태를 줄이고, 평소에는 비율을 쓴다. */
    if (player->state == BB_PLAYER_OUT) snprintf(value, sizeof value, "OUT");
    else if (player->boosting) snprintf(value, sizeof value, "X2 %dS", (int)ceil((double)energy * BB_ENERGY_BOOST_SECONDS));
    else snprintf(value, sizeof value, "%d", (int)(energy * 100));
    text(a, value, (float)(x + 112) - MeasureTextEx(a->font, value, 8, 0).x, 13, color);
    if (player->state == BB_PLAYER_OUT) snprintf(label, sizeof label, "P%d OUT", index + 1);
    else snprintf(label, sizeof label, "P%d LIFE %d", index + 1, player->lives < 0 ? 0 : player->lives);
    /* 아래쪽 중앙의 라운드 표시를 비워 두고 목숨은 양 끝에 배치한다. */
    if (index == 0) text(a, label, 8, 214, player_colors[index]);
    else text(a, label, 248 - MeasureTextEx(a->font, label, 8, 0).x, 214, player_colors[index]);
}

/* 이미 정렬된 점수 배열에서 화면에 들어가는 개수만 표로 표시한다.
 * top과 maximum을 바꾸어 이름 입력 화면과 전체 순위표에서 같은 함수를 재사용한다. */
static void leaderboard(const BBAssets *a, const BBUI *ui, float top, size_t maximum)
{
    size_t limit = ui->score_count < maximum ? ui->score_count : maximum;
    char value[24];
    text(a, "NO", 8, top, YELLOW);
    text(a, "NAME", 32, top, YELLOW);
    text(a, "SCORE", 132, top, YELLOW);
    text(a, "ROUND", 196, top, YELLOW);
    for (size_t i = 0; i < limit; ++i) {
        float y = top + 14 + (float)i * 12; /* 머리글 아래 14픽셀부터 행마다 12픽셀 간격 */
        snprintf(value, sizeof value, "%u", (unsigned)i + 1);
        text(a, value, 8, y, WHITE);
        text(a, ui->scores[i].name, 32, y, WHITE);
        snprintf(value, sizeof value, "%u", ui->scores[i].score);
        text(a, value, 172 - MeasureTextEx(a->font, value, 8, 0).x, y, WHITE);
        snprintf(value, sizeof value, "%d", ui->scores[i].round);
        text(a, value, 216, y, WHITE);
    }
    if (!limit) centered(a, "NO SCORES YET", top + 20, WHITE);
}

/* 메뉴에서 본 순위표, 게임 종료 결과, 이름 입력을 하나의 화면에서 구분한다. */
static void score_screen(const BBAssets *a, const BBGame *g, const BBUI *ui)
{
    char label[48];
    /* player_count가 0이면 메뉴에서 순위표만 열었으므로 승패 제목을 표시하지 않는다. */
    if (g->player_count == 0) centered(a, "LEADERBOARD", 16, YELLOW);
    else centered(a, g->won ? "ALL ROUNDS CLEAR!" : "GAME OVER", 16, YELLOW);
    for (int i = 0; i < g->player_count; ++i) {
        snprintf(label, sizeof label, "P%d SCORE %d", i + 1, g->players[i].score < 0 ? 0 : g->players[i].score);
        centered(a, label, 34.0f + (float)i * 12, player_colors[i]);
    }
    if (ui->entering_initials) {
        snprintf(label, sizeof label, "P%d ENTER INITIALS", ui->score_player + 1);
        centered(a, label, 66, player_colors[ui->score_player]);
        centered(a, ui->initials, 80, WHITE);
        /* 가운데 정렬된 세 글자의 시작점을 다시 구해 현재 편집 칸 밑에 선을 긋는다. */
        float initial_x = floorf((BB_SCREEN_WIDTH - MeasureTextEx(a->font, ui->initials, 8, 0).x) * 0.5f);
        DrawRectangle((int)initial_x + ui->initial_cursor * 8, 89, 7, 1, player_colors[ui->score_player]);
        leaderboard(a, ui, 104, 6); /* 이름 입력과 조작 안내 공간 때문에 여섯 기록만 보여 준다. */
        centered(a, ui->save_failed ? "ENTER RETRY OR ESC CANCEL" : "ARROWS LETTER  ENTER NEXT", 198, GRAY);
        centered(a, "ESC CANCEL ENTRY", 214, GRAY);
    } else {
        leaderboard(a, ui, g->player_count == 0 ? 42.0f : 66.0f, g->player_count == 0 ? 10 : 9);
        centered(a, "ENTER TO RETURN", 202, GRAY);
    }
    /* 저장 실패 문구는 화면 맨 아래를 다시 칠해 기존 안내와 겹치지 않게 한다. */
    if (ui->save_failed) {
        DrawRectangle(0, 212, BB_SCREEN_WIDTH, 12, BLACK);
        centered(a, ui->entering_initials ? "SAVE FAILED  ENTER RETRY" : "SCORES UNAVAILABLE", 214, RED);
    }
}

/* 시작 메뉴와 두 플레이어의 키 안내를 그린다. 선택 변경과 실행은 main.c가 처리한다. */
static void menu(const BBAssets *a, const BBUI *ui)
{
    const char *entries[BB_MENU_COUNT] = {"1 PLAYER", "2 PLAYERS", "LEADERBOARD"};
    DrawTexture(a->logo, (BB_SCREEN_WIDTH - a->logo.width) / 2, 0, WHITE);
    for (int i = 0; i < BB_MENU_COUNT; ++i) {
        Color color = GRAY;
        /* 선택한 항목은 색과 작은 사각형 두 가지 단서로 강조한다. */
        if (i == ui->menu_selection) {
            color = i < 2 ? player_colors[i] : YELLOW;
            DrawRectangle(68, 119 + i * 13, 4, 4, color);
        }
        text(a, entries[i], 80, (float)(117 + i * 13), color);
    }
    centered(a, "UP DOWN ENTER  OR 1 2", 160, WHITE);
    /* 같은 열에 행동 이름과 1P/2P의 실제 키를 놓아 두 조작 체계를 비교하기 쉽게 한다. */
    text(a, "MOVE", 32, 174, GRAY);
    text(a, "JUMP", 104, 174, GRAY);
    text(a, "FIRE", 184, 174, GRAY);
    text(a, "P1", 8, 187, player_colors[0]);
    text(a, "A D", 32, 187, player_colors[0]);
    text(a, "W SPACE", 104, 187, player_colors[0]);
    text(a, "E Z", 184, 187, player_colors[0]);
    text(a, "P2", 8, 201, player_colors[1]);
    text(a, "ARROWS", 32, 201, player_colors[1]);
    text(a, "UP", 104, 201, player_colors[1]);
    text(a, "RSHIFT", 184, 201, player_colors[1]);
    centered(a, "P PAUSE  M SOUND  F11 FULL", 215, GRAY);
}

/* 한 장의 게임 화면을 그리는 공개 함수다. 먼저 배경과 물체, 그다음 HUD와 안내를 그려
 * 나중에 그린 요소가 앞 요소를 덮는 순서를 명시적으로 유지한다. */
void bb_draw_game(const BBAssets *a, const BBGame *g, const BBUI *ui)
{
    char label[32];

    /* 이전 프레임을 지우고 독립 화면인 메뉴/점수표는 그린 직후 반환한다. */
    ClearBackground(BLACK);
    if (g->state == BB_STATE_MENU) { menu(a, ui); return; }
    if (g->state == BB_STATE_SCORE) { score_screen(a, g, ui); return; }
    /* 인트로에서는 맵을 숨긴다. 맵 데이터의 0은 빈칸이고 나머지는 타일을 그린다. */
    if (g->state != BB_STATE_INTRO) {
        for (int y = 0; y < BB_MAP_HEIGHT; ++y) {
            for (int x = 0; x < BB_MAP_WIDTH; ++x) {
                int tile = g->maps[g->level][y][x];
                if (!tile) continue;
                float draw_x = (float)x;
                /* 값 2인 공중 발판만 코어의 이동량을 더한다. 다섯 발판은 같은 만큼
                 * 움직여 간격을 유지하며, 맨 아래 바닥과 벽/장식은 제자리에 남는다. */
                if (tile == 2 && y < BB_MAP_HEIGHT - 2)
                    draw_x += g->platform_offset;
                /* 코어와 같은 타일 좌표를 8픽셀 단위로 바꾸고, 캐릭터와 마찬가지로
                 * 화면에서만 정수 픽셀에 맞춘다. 물리 좌표의 소수 부분은 그대로 둔다. */
                DrawTextureRec(a->tiles, bb_rectangle(0, (float)(g->level * 8), 8, 8),
                               bb_vector2(roundf(draw_x * 8.0f), (float)(y * 8)), WHITE);
            }
        }
    }
    /* 같은 라운드 시계를 사용해 적의 기본 두 장을 초당 네 장 속도로 번갈아 보여 준다. */
    int enemy_frame = (int)(g->level_time * 4) % 2;
    for (int i = 0; i < BB_MAX_ENEMIES; ++i) {
        const BBEnemy *enemy = &g->enemies[i];
        /* 포획된 적은 아래 버블 반복에서 버블 위치에 그리므로 여기서는 중복 표시하지 않는다. */
        if (!enemy->active || enemy->state == BB_ENEMY_CAPTURED) continue;
        if (enemy->state == BB_ENEMY_SPAWNING && enemy->age <= enemy->spawn_delay) continue;
        /* 분노 상태는 다음 두 칸, 사망 상태는 12번부터 네 칸의 별도 애니메이션을 쓴다. */
        int frame = enemy_frame + (enemy->angry ? 2 : 0);
        if (enemy->state == BB_ENEMY_DEAD) frame = 12 + (int)(enemy->age * 4) % 4;
        sprite(a->enemy, frame, enemy_row(enemy->type), 16, enemy->body.x, enemy->body.y, enemy->facing > 0);
    }
    for (int i = 0; i < BB_MAX_BUBBLES; ++i) {
        const BBBubble *bubble = &g->bubbles[i];
        if (!bubble->active) continue;
        int frame;
        /* 팽창(처음 약 1/3초), 부유, 터짐을 각각 다른 프레임 범위로 표현한다. */
        if (bubble->popping) frame = 5 + (int)(fmaxf(0, 0.25f - bubble->pop_timer) * 8) % 2;
        else if (bubble->age < 0.333f) frame = (int)(bubble->age * 12);
        else frame = 3 + (int)(bubble->age * 12) % 2;
        /* 적이 든 버블은 적 시트의 포획 그림을 사용하며, 터지는 동안은 일반 터짐 그림을 쓴다. */
        if (bubble->captured_enemy < 0 || bubble->popping)
            sprite(a->bubble, frame, 0, 16, bubble->body.x, bubble->body.y, false);
        else if (bubble->captured_enemy < BB_MAX_ENEMIES)
            sprite(a->enemy, 6 + enemy_frame, enemy_row(g->enemies[bubble->captured_enemy].type),
                   16, bubble->body.x, bubble->body.y, false);
    }
    /* 비활성 슬롯은 그리지 않는다. 아이템 뒤에 바위, 그 뒤에 플레이어를 겹쳐 그린다. */
    for (int i = 0; i < BB_MAX_PICKUPS; ++i)
        if (g->pickups[i].active) pickup_sprite(a, &g->pickups[i]);
    for (int i = 0; i < BB_MAX_BOULDERS; ++i)
        if (g->boulders[i].active)
            sprite(a->enemy, 5 - (int)(g->boulders[i].age * 4) % 6, 2, 16,
                   g->boulders[i].body.x, g->boulders[i].body.y, false);
    for (int i = 0; i < g->player_count; ++i) {
        const BBPlayer *player = &g->players[i];
        if (!player->active || player->state == BB_PLAYER_OUT) continue;
        /* 부활 무적 시간에는 번갈아 건너뛰어 깜빡임을 만들되 사망 그림은 숨기지 않는다. */
        if (player->invulnerable > 0 && player->state != BB_PLAYER_DEAD && (int)(g->level_time * 12) % 2) continue;
        /* 기본은 정지 두 장/이동 네 장이다. 이후 조건이 참일수록 우선순위가 높다.
         * 공중 → 공격 → 사망 → 인트로 순으로 행과 열을 덮어쓴다. */
        int column = (int)(g->level_time * 7) % (fabsf(player->body.vx) > 0.1f ? 4 : 2);
        int row = 0;
        if (!player->body.grounded) { row = 1; column = 2 + (int)(g->level_time * 4) % 2; }
        if (player->attack_timer > 0) { row = 2; column = 0; }
        if (player->state == BB_PLAYER_DEAD) { row = 3; column = (int)(g->level_time * 10) % 6; }
        if (g->state == BB_STATE_INTRO) { row = 2; column = 1 + (int)(g->state_time * 4) % 2; }
        sprite(a->players[i], column, row, 16, player->body.x, player->body.y, player->facing < 0);
        /* 인트로에서는 캐릭터 위에 32픽셀짜리 큰 버블을 한 겹 더 그린다. */
        if (g->state == BB_STATE_INTRO)
            sprite(a->large_bubble, 3 + (int)(g->state_time * 4) % 2, 0, 32, player->body.x, player->body.y, false);
    }
    /* 게임 물체 위에 상하 정보 영역을 다시 칠해 글자가 배경에 묻히지 않게 한다. */
    DrawRectangle(0, 0, BB_SCREEN_WIDTH, 24, BLACK);
    DrawRectangle(0, 212, BB_SCREEN_WIDTH, 12, BLACK);
    for (int i = 0; i < g->player_count; ++i)
        if (g->players[i].active) player_hud(a, &g->players[i], i);
    snprintf(label, sizeof label, "ROUND %d", g->level + 1);
    centered(a, label, 214, WHITE);
    /* 코어의 진행 상태에 맞는 안내를 마지막에 얹는다. 일시정지는 가장 위에 표시한다. */
    if (g->state == BB_STATE_INTRO) {
        centered(a, "READY!", 88, YELLOW);
        centered(a, "ENTER TO START", 110, WHITE);
        centered(a, "CHARGE 15S  BOOST 5S", 130, GRAY);
        centered(a, "BOOST: MOVE SPEED X2", 142, GRAY);
    }
    if (g->state == BB_STATE_CLEAR) centered(a, "ROUND CLEAR!", 100, YELLOW);
    if (ui->paused) {
        DrawRectangle(48, 86, 160, 42, BLACK);
        centered(a, "PAUSED", 94, YELLOW);
        centered(a, "P TO RESUME", 112, WHITE);
    }
}
