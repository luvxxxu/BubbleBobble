/* 게임 상태를 화면과 소리로 표현하는 파일이다. 게임 규칙을 바꾸지 않고
 * 코어가 가진 위치/상태를 읽어 알맞은 그림과 UI를 선택한다. */
#include "bb_render.h"
#include "bb_levels.h"
#include "bb_raylib_types.h"
#include "rlgl.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* 1P의 녹색과 2P의 파란색을 메뉴, 이름 입력, 점수, 마나 표시에서 함께 쓴다. */
static const Color player_colors[BB_MAX_PLAYERS] = {
    {92, 230, 52, 255}, {80, 184, 255, 255}
};

/* 검증할 그림의 파일명과 원본 픽셀 크기를 한 항목으로 묶는다. */
typedef struct BBImageSize {
    const char *name;
    int width;
    int height;
} BBImageSize;

/* 스프라이트 시트에서 그림 한 칸의 가로·세로 번호다. */
typedef struct BBSpriteCell {
    int column;
    int row;
} BBSpriteCell;

/* raylib 파일 로더 콜백의 반환 버퍼는 raylib이 MemFree로 해제한다. */
static unsigned char *read_asset(const char *path, int *size)
{
    *size = 0; /* 실패 시 호출자가 읽지 못한 데이터를 사용하지 않도록 먼저 비운다. */
    FILE *file = bb_platform_fopen(path, "rb");
    if (!file) {
        return NULL;
    }
    /* 전체 크기를 확인한 뒤 처음으로 되돌아와 한 번에 읽는다. */
    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return NULL;
    }
    long length = ftell(file);
    /* 빈 파일, 64 MiB를 넘는 파일, 되감기에 실패한 파일은 에셋으로 받지 않는다.
     * 크기 제한 덕분에 아래 unsigned int/int 변환도 표현 범위 안에 들어온다. */
    if (length <= 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    /* raylib에 소유권을 넘길 메모리이므로 malloc 대신 MemAlloc으로 확보한다. */
    unsigned char *data = MemAlloc((unsigned int)length);
    if (!data) {
        fclose(file);
        return NULL;
    }
    size_t received = fread(data, 1, (size_t)length, file);
    int closed = fclose(file);
    /* 짧게 읽힌 파일이나 닫기 오류는 성공으로 취급하지 않고 버퍼도 즉시 돌려준다. */
    if (received != (size_t)length || closed != 0) {
        MemFree(data);
        return NULL;
    }
    *size = (int)length;
    return data;
}

/* 이미지/소리/폰트의 내부 읽기도 한글 경로를 지원하는 플랫폼 로더를 거치게 한다. */
void bb_assets_install_file_loader(void)
{
    SetLoadFileDataCallback(read_asset);
}

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
    static const BBImageSize images[] = {
        {"Levels.png", 32, 84}, {"BubbleCharacter.png", 96, 64},
        {"BobbleCharacter.png", 96, 64},
        {"Enemys.png", 256, 192}, {"Items.png", 576, 64},
        {"LevelTiles.png", 40, 200}, {"AttackBubble.png", 112, 16},
        {"BubbleLarge.png", 288, 32}, {"Logo.png", 143, 112}
    };
    const char *sounds[] = {"SFX/Bubble Bobble SFX (2).wav", "SFX/Bubble Bobble SFX (3).wav", "SFX/Jump.wav", "SFX/The Quest Begins.ogg"};
    char path[BB_PATH_CAP];
    for (size_t i = 0; i < sizeof images / sizeof images[0]; ++i) {
        if (!path_for(path, directory, images[i].name)) {
            return false;
        }
        Image image = LoadImage(path);
        if (!image.data || image.width != images[i].width || image.height != images[i].height) {
            fprintf(stderr, "Missing or invalid asset: %s (expected %dx%d)\n", path, images[i].width, images[i].height);
            if (image.data) {
                UnloadImage(image);
            }
            return false;
        }
        /* 검사용 디코딩 결과는 검사가 끝나면 즉시 해제하고 화면용 로드는 따로 한다. */
        UnloadImage(image);
    }
    /* 확장자만 믿지 않고 WAV/OGG를 실제 디코딩해 읽을 수 있는 소리인지 확인한다. */
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; ++i) {
        if (!path_for(path, directory, sounds[i])) {
            return false;
        }
        Wave wave = LoadWave(path);
        if (!IsWaveValid(wave)) {
            fprintf(stderr, "Invalid sound: %s\n", path);
            return false;
        }
        UnloadWave(wave);
    }
    /* 여기서는 폰트 파일이 최소 헤더 길이를 갖는지 확인한다.
     * 실제 폰트 생성 성공 여부는 그래픽 장치를 만든 뒤 bb_assets_load에서 검사한다. */
    if (!path_for(path, directory, "NES_Font.ttf")) {
        return false;
    }
    int size = 0;
    unsigned char *font = read_asset(path, &size);
    if (!font || size < 12) {
        MemFree(font);
        fprintf(stderr, "Missing font: %s\n", path);
        return false;
    }
    MemFree(font);
    /* Levels.png는 원본 에셋으로 검증하지만 실제 다섯 라운드는 코드의 배치표로 만든다. */
    if (maps) {
        bb_levels_build(maps);
    }
    return true;
}

/* 파일을 GPU 텍스처로 읽고 최근접 필터를 지정해 픽셀 아트의 경계를 보존한다.
 * 경로 생성 실패는 id가 0인 빈 텍스처로 표현한다. */
static Texture2D texture(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    if (!path_for(path, directory, name)) {
        return bb_empty_texture();
    }
    Texture2D result = LoadTexture(path);
    if (result.id) {
        SetTextureFilter(result, TEXTURE_FILTER_POINT);
    }
    return result;
}

/* 원본 포획 그림에서 버블 테두리에 쓰는 녹색인지 검사한다.
 * 적의 몸에도 같은 색이 있으므로 색 검사만으로 전체 그림을 바꾸지는 않는다. */
static bool bubble_green(Color color)
{
    return color.a != 0 && color.r == 92 && color.g == 230 && color.b == 52;
}

/* 포획 그림 한 칸에서 맨 위의 녹색과 이어진 부분만 찾는다.
 * 작은 16×16 배열을 반복해서 확인하므로 재귀나 별도의 자료구조가 필요 없다.
 * 테두리와 떨어진 몬스타의 눈 등은 선택되지 않아 적의 원래 색을 유지한다. */
static void blue_capture_ring(Color *pixels, int image_width, int column, int row)
{
    bool ring[16][16] = {{false}};
    int left = column * 16;
    int top = row * 16;
    for (int x = 0; x < 16; ++x) {
        ring[0][x] = bubble_green(pixels[top * image_width + left + x]);
    }

    bool changed = true;
    while (changed) {
        changed = false;
        for (int y = 0; y < 16; ++y) {
            for (int x = 0; x < 16; ++x) {
                int position = (top + y) * image_width + left + x;
                if (ring[y][x] || !bubble_green(pixels[position])) {
                    continue;
                }
                /* 한 픽셀 두께의 둥근 선은 모서리에서 대각선으로 이어진다. */
                bool connected = false;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        int nx = x + dx;
                        int ny = y + dy;
                        if (nx >= 0 && nx < 16 && ny >= 0 && ny < 16 && ring[ny][nx]) {
                            connected = true;
                        }
                    }
                }
                if (connected) {
                    ring[y][x] = true;
                    changed = true;
                }
            }
        }
    }
    for (int y = 0; y < 16; ++y) {
        for (int x = 0; x < 16; ++x) {
            if (ring[y][x]) {
                pixels[(top + y) * image_width + left + x] = player_colors[1];
            }
        }
    }
}

/* 녹색 그림에 파랑을 곱하기만 하면 초록 성분이 남으므로, 2P용 그림은
 * 로드할 때 픽셀 색을 바꾼 복사본으로 만든다. 원본 파일과 1P 그림은 그대로다. */
static Texture2D blue_texture(const char *directory, const char *name, bool captured)
{
    char path[BB_PATH_CAP];
    Texture2D result = bb_empty_texture();
    if (!path_for(path, directory, name)) {
        return result;
    }
    Image original = LoadImage(path);
    if (!original.data) {
        return result;
    }
    /* 알려진 시트 크기를 확인해 아래의 고정 좌표와 픽셀 개수 계산을 보장한다. */
    int expected_width = 112;
    int expected_height = 16;
    if (captured) {
        expected_width = 256;
        expected_height = 192;
    }
    if (original.width != expected_width || original.height != expected_height) {
        UnloadImage(original);
        return result;
    }
    Color *pixels = LoadImageColors(original);
    if (!pixels) {
        UnloadImage(original);
        return result;
    }
    if (captured) {
        /* 0: 젠짱, 1: 마이타, 3: 몬스타. 2번 행은 바위여서 제외한다. */
        for (int row = 0; row < 4; ++row) {
            if (row == 2) {
                continue;
            }
            blue_capture_ring(pixels, original.width, 6, row);
            blue_capture_ring(pixels, original.width, 7, row);
        }
    } else {
        for (int i = 0; i < original.width * original.height; ++i) {
            Color color = pixels[i];
            bool highlight = color.r >= 240 && color.g >= 240 && color.b >= 240;
            /* 투명 배경과 밝은 반사광은 유지하고 버블·파열 부분만 파랗게 한다. */
            if (color.a != 0 && !highlight) {
                pixels[i] = player_colors[1];
                pixels[i].a = color.a;
            }
        }
    }
    /* LoadImageColors가 만든 RGBA 배열을 읽는 임시 Image다. GPU 복사가 끝난
     * 뒤 배열과 원본 이미지를 각각 한 번씩 해제한다. */
    Image recolored = {pixels, original.width, original.height, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
    result = LoadTextureFromImage(recolored);
    if (result.id) {
        SetTextureFilter(result, TEXTURE_FILTER_POINT);
    }
    UnloadImageColors(pixels);
    UnloadImage(original);
    return result;
}

/* 짧은 효과음을 메모리에 올린다. 성공 여부는 호출자가 IsSoundValid로 검사한다. */
static Sound sound(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    if (!path_for(path, directory, name)) {
        return bb_empty_sound();
    }
    return LoadSound(path);
}

/* 창 생성 후 화면 자원과 선택적인 소리 자원을 준비한다.
 * false 반환 시에도 일부 자원은 만들어졌을 수 있으므로 호출자가 반드시 정리해야 한다. */
bool bb_assets_load(BBAssets *assets, const char *directory, bool audio)
{
    char path[BB_PATH_CAP];

    /* 중간에 실패해도 호출자가 bb_assets_unload로 로드된 자원만 정리할 수 있다. */
    memset(assets, 0, sizeof *assets);
    /* 각 텍스처는 여러 동작/물체 그림이 격자로 담긴 스프라이트 시트다. */
    assets->players[0] = texture(directory, "BubbleCharacter.png");
    assets->players[1] = texture(directory, "BobbleCharacter.png");
    assets->enemy = texture(directory, "Enemys.png");
    assets->items = texture(directory, "Items.png");
    assets->tiles = texture(directory, "LevelTiles.png");
    assets->bubble = texture(directory, "AttackBubble.png");
    assets->large_bubble = texture(directory, "BubbleLarge.png");
    assets->blue_bubble = blue_texture(directory, "AttackBubble.png", false);
    assets->blue_captured_enemy = blue_texture(directory, "Enemys.png", true);
    assets->logo = texture(directory, "Logo.png");
    if (!path_for(path, directory, "NES_Font.ttf")) {
        return false;
    }
    assets->font = LoadFontEx(path, 8, NULL, 95); /* 8픽셀 크기의 기본 ASCII 95글자를 준비한다. */
    /* 폰트 로드 실패 시 raylib이 돌려주는 기본 폰트도 정상 에셋으로 오인하지 않는다. */
    if (!assets->players[0].id || !assets->players[1].id || !assets->enemy.id || !assets->items.id || !assets->tiles.id ||
        !assets->bubble.id || !assets->large_bubble.id || !assets->blue_bubble.id ||
        !assets->blue_captured_enemy.id || !assets->logo.id || !IsFontValid(assets->font) ||
        assets->font.texture.id == GetFontDefault().texture.id) {
        return false;
    }
    SetTextureFilter(assets->font.texture, TEXTURE_FILTER_POINT);
    /* 소리 장치가 없으면 화면 자원만으로 실행을 계속할 수 있다. */
    assets->audio = audio && IsAudioDeviceReady();
    if (assets->audio) {
        assets->fire = sound(directory, "SFX/Bubble Bobble SFX (2).wav");
        assets->death = sound(directory, "SFX/Bubble Bobble SFX (3).wav");
        assets->jump = sound(directory, "SFX/Jump.wav");
        int size = 0;
        if (!path_for(path, directory, "SFX/The Quest Begins.ogg")) {
            return false;
        }
        /* 메모리에서 연 음악 스트림보다 원본 버퍼를 오래 유지한다. */
        assets->music_data = read_asset(path, &size);
        if (assets->music_data) {
            assets->music = LoadMusicStreamFromMemory(".ogg", assets->music_data, size);
        }
        if (!IsSoundValid(assets->fire) || !IsSoundValid(assets->death) || !IsSoundValid(assets->jump) || !IsMusicValid(assets->music)) {
            return false;
        }
        /* 배경음은 반복 재생하고 효과음보다 작게 조절한다.
         * 스트림 버퍼 보충은 main.c의 UpdateMusicStream에서 매 프레임 수행한다. */
        assets->music.looping = true;
        SetMusicVolume(assets->music, 0.45f);
        PlayMusicStream(assets->music);
    }
    return true;
}

/* 로드 도중 실패한 객체도 안전하게 정리하도록 실제 생성된 자원만 해제한다.
 * 그래픽/소리 장치가 아직 살아 있는 동안 호출해야 한다. */
void bb_assets_unload(BBAssets *assets)
{
    Texture2D textures[] = {
        assets->players[0], assets->players[1], assets->tiles, assets->bubble,
        assets->large_bubble, assets->logo, assets->enemy, assets->items,
        assets->blue_bubble, assets->blue_captured_enemy
    };
    for (size_t i = 0; i < sizeof textures / sizeof textures[0]; ++i) {
        if (textures[i].id) {
            UnloadTexture(textures[i]);
        }
    }
    /* raylib의 공유 기본 폰트는 이 객체가 소유하지 않으므로 해제하지 않는다. */
    if (assets->font.texture.id && assets->font.texture.id != GetFontDefault().texture.id) {
        UnloadFont(assets->font);
    }
    Sound sounds[] = {assets->fire, assets->jump, assets->death};
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; ++i) {
        if (IsSoundValid(sounds[i])) {
            UnloadSound(sounds[i]);
        }
    }
    /* 음악 디코더가 원본 버퍼를 참조하므로 스트림을 먼저 닫고 버퍼를 나중에 해제한다. */
    if (IsMusicValid(assets->music)) {
        UnloadMusicStream(assets->music);
    }
    MemFree(assets->music_data);
    memset(assets, 0, sizeof *assets); /* 해제한 핸들을 다시 유효한 자원으로 보지 않게 한다. */
}

/* 이번 코어 갱신에서 발생한 발사/점프/사망 사건을 준비된 효과음에 연결한다.
 * 사건 플래그는 같은 갱신 안의 여러 발생을 하나로 합친 값이다. */
void bb_audio_events(const BBAssets *assets, BBEvents events)
{
    if (!assets->audio) {
        return;
    }
    if (events.fire) {
        PlaySound(assets->fire);
    }
    if (events.jump) {
        PlaySound(assets->jump);
    }
    if (events.death) {
        PlaySound(assets->death);
    }
}

/* UI는 8픽셀 폰트와 추가 자간 0을 사용한다. 카메라가 문자와 그림에
 * 같은 배율을 적용하므로 창 비율이 바뀌어도 글자가 가로로 늘어나지 않는다. */
static void text(const BBAssets *assets, const char *value, float x, float y, Color color)
{
    DrawTextEx(assets->font, value, bb_vector2(x, y), 8, 0, color);
}

/* 실제 글자 너비를 측정해 화면 중앙에 놓고 시작 위치를 정수 픽셀로 맞춘다. */
static void centered(const BBAssets *assets, const char *value, float y, Color color)
{
    float width = MeasureTextEx(assets->font, value, 8, 0).x;
    text(assets, value, floorf((BB_SCREEN_WIDTH - width) * 0.5f), y, color);
}

/* 시트의 column/row 칸에서 size 픽셀 정사각형 한 장을 잘라 그린다.
 * 물리 좌표의 중심과 그림 중심을 일치시키고 반올림해 반 픽셀에 걸치지 않게 한다. */
static void colored_sprite(Texture2D sheet, int column, int row, int size,
                           float x, float y, bool flip, Color color)
{
    /* 게임 좌표는 8픽셀 타일 단위의 중심점이고, 음수 너비는 좌우 반전이다. */
    Rectangle source = bb_rectangle((float)(column * size), (float)(row * size), (float)size, (float)size);
    if (flip) {
        source.width = -(float)size;
    }
    float left = roundf(x * 8 - (float)size / 2);
    float top = roundf(y * 8 - (float)size / 2);
    Rectangle destination = bb_rectangle(left, top, (float)size, (float)size);
    DrawTexturePro(sheet, source, destination, bb_vector2(0, 0), 0, color);
}

/* 흰색을 곱하면 원본 그림의 색을 그대로 표시한다. 일반 캐릭터와 물체에 쓴다. */
static void sprite(Texture2D sheet, int column, int row, int size, float x, float y, bool flip)
{
    colored_sprite(sheet, column, row, size, x, y, flip, WHITE);
}

/* 큰 버블은 흰색 원본이므로 2P일 때만 파란색을 곱하면 된다.
 * 인트로·구조 대기·구조 후 파열에서 같은 색 규칙을 사용한다. */
static void large_bubble_sprite(const BBAssets *assets, int player, int frame, float x, float y)
{
    Color color = WHITE;
    if (player == 1) {
        color = player_colors[1];
    }
    colored_sprite(assets->large_bubble, frame, 0, 32, x, y, false, color);
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
static void pickup_sprite(const BBAssets *assets, const BBPickup *pickup)
{
    /* 배열 순서는 BBPickupType과 같다. column/row는 Items.png의 16픽셀 격자 번호다. */
    static const BBSpriteCell cells[BB_PICKUP_TYPE_COUNT] = {
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
    if (type >= BB_PICKUP_TYPE_COUNT) {
        return;
    }
    sprite(assets->items, cells[type].column, cells[type].row, 16,
           pickup->body.x, pickup->body.y, false);
}

/* 구조가 가능한 마지막 순간까지 1초로 표시하고, 초깃값은 10초를 넘기지 않는다. */
static int rescue_seconds(const BBPlayer *player)
{
    double seconds = ceil(player->rescue_time_left);
    seconds = fmin(BB_RESCUE_BUBBLE_SECONDS, seconds);
    seconds = fmax(1.0, seconds);
    return (int)seconds;
}

/* 플레이어 한 명의 점수, 마나, 남은 목숨을 화면 양 끝에 배치한다.
 * width/height는 HUD 배율을 적용하기 전의 실제 가시 영역 크기다. */
static void player_hud(const BBAssets *assets, const BBPlayer *player, int index, float width, float height)
{
    float energy = fmaxf(0, fminf(1, player->energy)); /* 표시할 채움 비율을 0..1로 제한한다. */
    float x = 8.0f;
    if (index != 0) {
        x = width - 120.0f;
    }
    int fill = (int)(energy * 34 + 0.5f); /* 테두리를 뺀 34픽셀을 가장 가까운 정수로 채운다. */
    Color color = player_colors[index];
    char label[32];
    char value[16];

    snprintf(label, sizeof label, "P%d", index + 1);
    text(assets, label, x, 2, color);
    /* 점수는 최소 여섯 자리로 표시하되 실제 글자 폭을 재어 오른쪽에 정렬한다. */
    int score = player->score;
    if (score < 0) {
        score = 0;
    }
    snprintf(value, sizeof value, "%06d", score);
    text(assets, value, x + 112 - MeasureTextEx(assets->font, value, 8, 0).x, 2, color);
    if (player->state == BB_PLAYER_BUBBLE) {
        /* 멈춘 마나 대신 구조 제한 시간을 보여 주며, 만료 뒤에는 다음 라운드를 안내한다. */
        snprintf(label, sizeof label, "HELP %dS", rescue_seconds(player));
        text(assets, label, x, 13, color);
    } else if (player->state == BB_PLAYER_WAIT_ROUND) {
        text(assets, "NEXT ROUND", x, 13, GRAY);
    } else {
        /* 탈락은 회색, 가속 중은 노란색으로 일반 마나 충전 상태와 구별한다. */
        /* 가속 중에는 남은 초를 올림해 0초처럼 보이는 상태를 줄이고, 평소에는 비율을 쓴다. */
        if (player->state == BB_PLAYER_OUT) {
            color = GRAY;
            snprintf(value, sizeof value, "OUT");
        }
        else if (player->boosting) {
            color = YELLOW;
            snprintf(value, sizeof value, "X2 %dS", (int)ceil((double)energy * BB_ENERGY_BOOST_SECONDS));
        }
        else {
            snprintf(value, sizeof value, "%d", (int)(energy * 100));
        }
        text(assets, "MANA", x, 13, color);
        /* 바깥 사각형, 어두운 내부, 채워진 길이 순으로 마나 막대를 만든다. */
        DrawRectangleRec(bb_rectangle(x + 34, 14, 36, 7), color);
        DrawRectangleRec(bb_rectangle(x + 35, 15, 34, 5), bb_color(32, 32, 32, 255));
        if (fill > 0) {
            DrawRectangleRec(bb_rectangle(x + 35, 15, (float)fill, 5), color);
        }
        text(assets, value, x + 112 - MeasureTextEx(assets->font, value, 8, 0).x, 13, color);
    }
    if (player->state == BB_PLAYER_BUBBLE) {
        snprintf(label, sizeof label, "P%d HELP", index + 1);
    }
    else if (player->state == BB_PLAYER_WAIT_ROUND) {
        snprintf(label, sizeof label, "P%d WAIT", index + 1);
    }
    else if (player->state == BB_PLAYER_OUT) {
        snprintf(label, sizeof label, "P%d OUT", index + 1);
    }
    else {
        int lives = player->lives;
        if (lives < 0) {
            lives = 0;
        }
        snprintf(label, sizeof label, "P%d LIFE %d", index + 1, lives);
    }
    /* 아래쪽 중앙의 라운드 표시를 비워 두고 목숨은 양 끝에 배치한다. */
    x = 8;
    if (index != 0) {
        x = width - 8 - MeasureTextEx(assets->font, label, 8, 0).x;
    }
    text(assets, label, x, height - 10, player_colors[index]);
}

/* 이미 정렬된 점수 배열에서 화면에 들어가는 개수만 표로 표시한다.
 * top과 maximum을 바꾸어 이름 입력 화면과 전체 순위표에서 같은 함수를 재사용한다. */
static void leaderboard(const BBAssets *assets, const BBUI *ui, float top, size_t maximum)
{
    size_t limit = ui->score_count;
    if (limit > maximum) {
        limit = maximum;
    }
    char value[24];
    text(assets, "NO", 8, top, YELLOW);
    text(assets, "NAME", 32, top, YELLOW);
    text(assets, "SCORE", 132, top, YELLOW);
    text(assets, "ROUND", 196, top, YELLOW);
    for (size_t i = 0; i < limit; ++i) {
        float y = top + 14 + (float)i * 12; /* 머리글 아래 14픽셀부터 행마다 12픽셀 간격 */
        snprintf(value, sizeof value, "%u", (unsigned)i + 1);
        text(assets, value, 8, y, WHITE);
        text(assets, ui->scores[i].name, 32, y, WHITE);
        snprintf(value, sizeof value, "%u", ui->scores[i].score);
        text(assets, value, 172 - MeasureTextEx(assets->font, value, 8, 0).x, y, WHITE);
        snprintf(value, sizeof value, "%d", ui->scores[i].round);
        text(assets, value, 216, y, WHITE);
    }
    if (!limit) {
        centered(assets, "NO SCORES YET", top + 20, WHITE);
    }
}

/* 메뉴에서 본 순위표, 게임 종료 결과, 이름 입력을 하나의 화면에서 구분한다. */
static void score_screen(const BBAssets *assets, const BBGame *game, const BBUI *ui)
{
    char label[48];
    /* player_count가 0이면 메뉴에서 순위표만 열었으므로 승패 제목을 표시하지 않는다. */
    const char *title = "GAME OVER";
    if (game->player_count == 0) {
        title = "LEADERBOARD";
    } else if (game->won) {
        title = "ALL ROUNDS CLEAR!";
    }
    centered(assets, title, 16, YELLOW);
    for (int i = 0; i < game->player_count; ++i) {
        int score = game->players[i].score;
        if (score < 0) {
            score = 0;
        }
        snprintf(label, sizeof label, "P%d SCORE %d", i + 1, score);
        centered(assets, label, 34.0f + (float)i * 12, player_colors[i]);
    }
    if (ui->entering_initials) {
        snprintf(label, sizeof label, "P%d ENTER INITIALS", ui->score_player + 1);
        centered(assets, label, 66, player_colors[ui->score_player]);
        centered(assets, ui->initials, 80, WHITE);
        /* 가운데 정렬된 세 글자의 시작점을 다시 구해 현재 편집 칸 밑에 선을 긋는다. */
        float initial_x = floorf((BB_SCREEN_WIDTH - MeasureTextEx(assets->font, ui->initials, 8, 0).x) * 0.5f);
        DrawRectangle((int)initial_x + ui->initial_cursor * 8, 89, 7, 1, player_colors[ui->score_player]);
        leaderboard(assets, ui, 104, 6); /* 이름 입력과 조작 안내 공간 때문에 여섯 기록만 보여 준다. */
        if (ui->save_failed) {
            centered(assets, "ENTER RETRY OR ESC CANCEL", 198, GRAY);
        } else {
            centered(assets, "ARROWS LETTER  ENTER NEXT", 198, GRAY);
        }
        centered(assets, "ESC CANCEL ENTRY", 214, GRAY);
    } else {
        if (game->player_count == 0) {
            leaderboard(assets, ui, 42.0f, 10);
        } else {
            leaderboard(assets, ui, 66.0f, 9);
        }
        centered(assets, "ENTER TO RETURN", 202, GRAY);
    }
    /* 저장 실패 문구는 화면 맨 아래를 다시 칠해 기존 안내와 겹치지 않게 한다. */
    if (ui->save_failed) {
        DrawRectangle(0, 212, BB_SCREEN_WIDTH, 12, BLACK);
        if (ui->entering_initials) {
            centered(assets, "SAVE FAILED  ENTER RETRY", 214, RED);
        } else {
            centered(assets, "SCORES UNAVAILABLE", 214, RED);
        }
    }
}

/* 시작 메뉴와 두 플레이어의 키 안내를 그린다. 선택 변경과 실행은 main.c가 처리한다. */
static void menu(const BBAssets *assets, const BBUI *ui)
{
    const char *entries[BB_MENU_COUNT] = {"1 PLAYER", "2 PLAYERS", "LEADERBOARD"};
    DrawTexture(assets->logo, (BB_SCREEN_WIDTH - assets->logo.width) / 2, 0, WHITE);
    for (int i = 0; i < BB_MENU_COUNT; ++i) {
        Color color = GRAY;
        /* 선택한 항목은 색과 작은 사각형 두 가지 단서로 강조한다. */
        if (i == ui->menu_selection) {
            color = YELLOW;
            if (i < 2) {
                color = player_colors[i];
            }
            DrawRectangle(68, 119 + i * 13, 4, 4, color);
        }
        text(assets, entries[i], 80, (float)(117 + i * 13), color);
    }
    centered(assets, "UP DOWN ENTER  OR 1 2", 160, WHITE);
    /* 같은 열에 행동 이름과 1P/2P의 실제 키를 놓아 두 조작 체계를 비교하기 쉽게 한다. */
    text(assets, "MOVE", 32, 174, GRAY);
    text(assets, "JUMP", 104, 174, GRAY);
    text(assets, "FIRE", 184, 174, GRAY);
    text(assets, "P1", 8, 187, player_colors[0]);
    text(assets, "A D", 32, 187, player_colors[0]);
    text(assets, "W SPACE", 104, 187, player_colors[0]);
    text(assets, "E Z", 184, 187, player_colors[0]);
    text(assets, "P2", 8, 201, player_colors[1]);
    text(assets, "ARROWS", 32, 201, player_colors[1]);
    text(assets, "UP", 104, 201, player_colors[1]);
    text(assets, "RSHIFT", 184, 201, player_colors[1]);
    centered(assets, "P PAUSE  M SOUND  F11 FULL", 215, GRAY);
}

/* 발판 한 덩어리의 경계를 2픽셀 선/2픽셀 간격의 점선으로 그린다.
 * 원본 픽셀 좌표에서 그려 확대 배율이 바뀌어도 선의 비율과 간격은 같다. */
static void platform_outline(float x, float y, int width)
{
    for (int offset = 0; offset < width; offset += 4) {
        int dash = width - offset;
        if (dash > 2) {
            dash = 2;
        }
        DrawRectangleRec(bb_rectangle(x + (float)offset, y, (float)dash, 1), WHITE);
        DrawRectangleRec(bb_rectangle(x + (float)offset, y + 7, (float)dash, 1), WHITE);
    }
    for (int offset = 0; offset < 8; offset += 4) {
        DrawRectangleRec(bb_rectangle(x, y + (float)offset, 1, 2), WHITE);
        DrawRectangleRec(bb_rectangle(x + (float)width - 1, y + (float)offset, 1, 2), WHITE);
    }
}

/* 고정 벽과 바닥을 그린 뒤, 공중 발판의 실제 상태와 점선 예고를 그린다. */
static void draw_tiles(const BBAssets *assets, const BBGame *game)
{
    /* 인트로에서는 맵을 숨긴다. 맵 데이터의 0은 빈칸이고 나머지는 타일을 그린다. */
    if (game->state != BB_STATE_INTRO) {
        for (int y = 0; y < game->world_height; ++y) {
            for (int x = 0; x < game->world_width; ++x) {
                int tile = bb_game_tile(game, x, y);
                if (!tile) {
                    continue;
                }
                /* 공중 발판은 아래에서 상태별로 한 번만 그린다. 고정 바닥과 벽,
                 * 장식은 생성/소멸 예고에 영향을 받지 않고 원래 질감을 유지한다. */
                if (tile == 2 && y < game->world_height - 2) {
                    continue;
                }
                DrawTextureRec(assets->tiles, bb_rectangle(0, (float)(game->level * 8), 8, 8),
                               bb_vector2((float)(x * 8), (float)(y * 8)), WHITE);
            }
        }
        for (int i = 0; i < game->platform_count; ++i) {
            const BBMovingPlatform *platform = &game->platforms[i];
            float x = roundf(((float)platform->x + platform->offset) * 8.0f);
            float y = (float)(platform->y * 8);
            if (platform->state == BB_PLATFORM_SOLID) {
                for (int tile = 0; tile < platform->width; ++tile) {
                    DrawTextureRec(assets->tiles, bb_rectangle(0, (float)(game->level * 8), 8, 8),
                                   bb_vector2(x + (float)(tile * 8), y), WHITE);
                }
            } else if (bb_game_platform_blink_visible(platform)) {
                /* 생성/소멸 예고는 같은 위상의 점선 테두리를 세 번 점멸한다.
                 * 없는 상태와 점멸의 꺼진 구간에는 어떤 타일도 남기지 않는다. */
                platform_outline(x, y, platform->width * 8);
            }
        }
    }
}

/* 포획되지 않은 적을 그린다. 포획된 적은 버블을 그릴 때 함께 표시한다. */
static void draw_enemies(const BBAssets *assets, const BBGame *game)
{
    /* 같은 라운드 시계를 사용해 적의 기본 두 장을 초당 네 장 속도로 번갈아 보여 준다. */
    int enemy_frame = (int)(game->level_time * 4) % 2;
    for (int i = 0; i < BB_MAX_ENEMIES; ++i) {
        const BBEnemy *enemy = &game->enemies[i];
        /* 포획된 적은 아래 버블 반복에서 버블 위치에 그리므로 여기서는 중복 표시하지 않는다. */
        if (!enemy->active || enemy->state == BB_ENEMY_CAPTURED ||
            (enemy->state == BB_ENEMY_SPAWNING && enemy->age <= enemy->spawn_delay)) {
            continue;
        }
        /* 분노 상태는 다음 두 칸, 사망 상태는 12번부터 네 칸의 별도 애니메이션을 쓴다. */
        int frame = enemy_frame;
        if (enemy->angry) {
            frame += 2;
        }
        if (enemy->state == BB_ENEMY_DEAD) {
            frame = 12 + (int)(enemy->age * 4) % 4;
        }
        sprite(assets->enemy, frame, enemy_row(enemy->type), 16, enemy->body.x, enemy->body.y, enemy->facing > 0);
    }
}

/* 각 버블의 주인을 확인해 1P 원본 또는 2P 파란 그림을 고른다. */
static void draw_bubbles(const BBAssets *assets, const BBGame *game)
{
    int enemy_frame = (int)(game->level_time * 4) % 2;
    for (int i = 0; i < BB_MAX_BUBBLES; ++i) {
        const BBBubble *bubble = &game->bubbles[i];
        if (!bubble->active) {
            continue;
        }
        int frame;
        /* 팽창(처음 약 1/3초), 부유, 터짐을 각각 다른 프레임 범위로 표현한다. */
        if (bubble->popping) {
            frame = 5 + (int)(fmaxf(0, 0.25f - bubble->pop_timer) * 8) % 2;
        }
        else if (bubble->age < 0.333f) {
            frame = (int)(bubble->age * 12);
        }
        else {
            frame = 3 + (int)(bubble->age * 12) % 2;
        }
        /* 적이 든 버블은 적 시트의 포획 그림을 사용하며, 터지는 동안은 일반 터짐 그림을 쓴다. */
        if (bubble->captured_enemy < 0 || bubble->popping) {
            Texture2D sheet = assets->bubble;
            if (bubble->owner == 1) {
                sheet = assets->blue_bubble;
            }
            sprite(sheet, frame, 0, 16, bubble->body.x, bubble->body.y, false);
        } else if (bubble->captured_enemy < BB_MAX_ENEMIES) {
            Texture2D sheet = assets->enemy;
            if (bubble->owner == 1) {
                sheet = assets->blue_captured_enemy;
            }
            int row = enemy_row(game->enemies[bubble->captured_enemy].type);
            sprite(sheet, 6 + enemy_frame, row, 16, bubble->body.x, bubble->body.y, false);
        }
    }
}

/* 활성 아이템을 먼저 그리고 그 위에 바위를 겹쳐 그린다. */
static void draw_pickups_and_boulders(const BBAssets *assets, const BBGame *game)
{
    /* 비활성 슬롯은 그리지 않는다. 아이템 뒤에 바위, 그 뒤에 플레이어를 겹쳐 그린다. */
    for (int i = 0; i < BB_MAX_PICKUPS; ++i) {
        if (game->pickups[i].active) {
            pickup_sprite(assets, &game->pickups[i]);
        }
    }
    for (int i = 0; i < BB_MAX_BOULDERS; ++i) {
        if (game->boulders[i].active) {
            sprite(assets->enemy, 5 - (int)(game->boulders[i].age * 4) % 6, 2, 16,
                   game->boulders[i].body.x, game->boulders[i].body.y, false);
        }
    }
}

/* 캐릭터의 상태에 맞는 그림과 부활 버블을 고른다. 숨김 상태는 먼저 제외한다. */
static void draw_players(const BBAssets *assets, const BBGame *game)
{
    for (int i = 0; i < game->player_count; ++i) {
        const BBPlayer *player = &game->players[i];
        if (!player->active || player->state == BB_PLAYER_OUT || player->state == BB_PLAYER_WAIT_ROUND) {
            continue;
        }
        if (player->state == BB_PLAYER_BUBBLE) {
            /* 자기 캐릭터와 색을 유지해 일반 공격 거품과 구조 대상을 구분한다.
             * 구조 대기 그림은 무적 깜빡임과 무관하게 계속 보여 준다. */
            sprite(assets->players[i], 2 + (int)(game->level_time * 4) % 2, 1, 16,
                   player->body.x, player->body.y, player->facing < 0);
            int frame = 3 + (int)(game->level_time * 4) % 2;
            large_bubble_sprite(assets, i, frame, player->body.x, player->body.y);
            continue;
        }
        /* 큰 버블 시트의 6번 팽창/7번 파열 프레임을 0.25초 동안 보여 준다.
         * 캐릭터가 부활 무적으로 깜빡이는 프레임에도 터지는 버블은 남긴다. */
        if (player->state == BB_PLAYER_NORMAL && player->rescue_pop_timer > 0.0f) {
            int frame = 7;
            if (player->rescue_pop_timer > 0.125f) {
                frame = 6;
            }
            large_bubble_sprite(assets, i, frame, player->body.x, player->body.y);
        }
        /* 부활 무적 시간에는 번갈아 건너뛰어 깜빡임을 만들되 사망 그림은 숨기지 않는다. */
        if (player->invulnerable > 0 && player->state != BB_PLAYER_DEAD && (int)(game->level_time * 12) % 2) {
            continue;
        }
        /* 기본은 정지 두 장/이동 네 장이다. 이후 조건이 참일수록 우선순위가 높다.
         * 공중 → 공격 → 사망 → 인트로 순으로 행과 열을 덮어쓴다. */
        int frame_count = 2;
        if (fabsf(player->body.vx) > 0.1f) {
            frame_count = 4;
        }
        int column = (int)(game->level_time * 7) % frame_count;
        int row = 0;
        if (!player->body.grounded) {
            row = 1;
            column = 2 + (int)(game->level_time * 4) % 2;
        }
        if (player->attack_timer > 0) {
            row = 2;
            column = 0;
        }
        if (player->state == BB_PLAYER_DEAD) {
            row = 3;
            column = (int)(game->level_time * 10) % 6;
        }
        if (game->state == BB_STATE_INTRO) {
            row = 2;
            column = 1 + (int)(game->state_time * 4) % 2;
        }
        sprite(assets->players[i], column, row, 16, player->body.x, player->body.y, player->facing < 0);
        /* 인트로에서는 캐릭터 위에 32픽셀짜리 큰 버블을 한 겹 더 그린다. */
        if (game->state == BB_STATE_INTRO) {
            int frame = 3 + (int)(game->state_time * 4) % 2;
            large_bubble_sprite(assets, i, frame, player->body.x, player->body.y);
        }
    }
}

/* 그림이 겹치는 순서도 화면 규칙이다. 맵 → 적 → 버블 → 아이템/바위 →
 * 플레이어 순서로 그려 기존 화면과 같은 앞뒤 관계를 유지한다. */
static void draw_world(const BBAssets *assets, const BBGame *game)
{
    draw_tiles(assets, game);
    draw_enemies(assets, game);
    draw_bubbles(assets, game);
    draw_pickups_and_boulders(assets, game);
    draw_players(assets, game);
}

/* 구조 대상의 이름표는 HUD와 화면 끝에 가려지지 않게 가시 영역에 맞춘다.
 * 위쪽에 공간이 없을 때만 버블 아래에 두며, 캐릭터와 같은 색으로 식별한다. */
static void rescue_labels(const BBAssets *assets, const BBGame *game, const BBViewport *viewport,
                          int framebuffer_width, int framebuffer_height, float hud_scale)
{
    float left = -viewport->offset_x / viewport->scale;
    float right = ((float)framebuffer_width - viewport->offset_x) / viewport->scale;
    float top = (24.0f * hud_scale - viewport->offset_y) / viewport->scale;
    float bottom = ((float)framebuffer_height - 12.0f * hud_scale - viewport->offset_y) / viewport->scale;
    for (int i = 0; i < game->player_count; ++i) {
        const BBPlayer *player = &game->players[i];
        if (!player->active || player->state != BB_PLAYER_BUBBLE) {
            continue;
        }
        char label[16];
        snprintf(label, sizeof label, "P%d HELP %dS", i + 1, rescue_seconds(player));
        float width = MeasureTextEx(assets->font, label, 8, 0).x;
        float x = roundf(player->body.x * 8.0f - width * 0.5f);
        float y = roundf(player->body.y * 8.0f - 27.0f);
        if (y < top + 2.0f) {
            y = roundf(player->body.y * 8.0f + 19.0f);
        }
        x = fmaxf(left + 2.0f, fminf(x, right - width - 2.0f));
        y = fmaxf(top + 2.0f, fminf(y, bottom - 10.0f));
        DrawRectangleRec(bb_rectangle(x - 1, y - 1, width + 2, 10), BLACK);
        text(assets, label, x, y, player_colors[i]);
    }
}

/* 완성된 캔버스를 창에 출력한다. raylib 5.5의 Windows 고해상도 창은
 * 크기 변경 뒤에도 BeginDrawing에서 DPI 확대 행렬을 적용할 수 있다.
 * 캔버스는 이미 실제 픽셀 크기이므로, 아래 세 좌표 설정을 모두 맞춘다.
 * 1. viewport: 실제로 그릴 화면의 픽셀 범위
 * 2. projection: 좌표 (0, 0)~(너비, 높이)를 그 범위에 대응시킴
 * 3. modelview: 추가 확대·이동을 하지 않음
 * macOS의 논리 좌표와 Retina 픽셀 차이도 같은 경로에서 처리한다. */
void bb_present_canvas(RenderTexture2D canvas, int framebuffer_width, int framebuffer_height)
{
    if(framebuffer_width <= 0 || framebuffer_height <= 0 || !IsRenderTextureValid(canvas))
    {
        return;
    }

    /* 이전 그림을 먼저 제출해야 행렬을 바꿔도 그 그림의 위치가 달라지지 않는다. */
    rlDrawRenderBatchActive();
    rlViewport(0, 0, framebuffer_width, framebuffer_height);
    rlMatrixMode(RL_PROJECTION);
    rlLoadIdentity();
    rlOrtho(0, framebuffer_width, framebuffer_height, 0, 0.0, 1.0);
    rlMatrixMode(RL_MODELVIEW);
    rlLoadIdentity();

    /* 렌더 텍스처의 위아래를 뒤집고 화면 전체에 한 번만 복사한다. */
    DrawTexturePro(canvas.texture,
                   bb_rectangle(0, 0, (float)canvas.texture.width, -(float)canvas.texture.height),
                   bb_rectangle(0, 0, (float)framebuffer_width, (float)framebuffer_height),
                   bb_vector2(0, 0), 0, WHITE);
    rlDrawRenderBatchActive();
}

/* 해상도에 맞는 월드와 화면 고정 HUD를 분리해 그린다. 모든 카메라는
 * 가로·세로 공통 배율을 사용하며, 가장자리 타일 일부만 화면 밖으로 나간다. */
void bb_draw_game(const BBAssets *assets, const BBGame *game, const BBUI *ui,
                  const BBViewport *viewport, int framebuffer_width, int framebuffer_height)
{
    ClearBackground(BLACK);
    if (!viewport || viewport->scale <= 0.0f || framebuffer_width <= 0 || framebuffer_height <= 0)
        return;
    float scale = viewport->scale;
    float panel_scale = fminf(scale, fminf((float)framebuffer_width / BB_SCREEN_WIDTH,
                                        (float)framebuffer_height / BB_SCREEN_HEIGHT));
    Camera2D panel = {0};
    panel.zoom = panel_scale;
    panel.offset = bb_vector2(((float)framebuffer_width - BB_SCREEN_WIDTH * panel_scale) * 0.5f,
                              ((float)framebuffer_height - BB_SCREEN_HEIGHT * panel_scale) * 0.5f);
    /* 메뉴와 순위표는 원래 간격을 유지하며 화면 중앙에 놓는다. */
    if (game->state == BB_STATE_MENU || game->state == BB_STATE_SCORE) {
        BeginMode2D(panel);
        if (game->state == BB_STATE_MENU) {
            menu(assets, ui);
        }
        else {
            score_screen(assets, game, ui);
        }
        EndMode2D();
        return;
    }
    Camera2D world = {0};
    world.zoom = scale;
    world.offset = bb_vector2(viewport->offset_x, viewport->offset_y);
    BeginMode2D(world);
    /* 최대 월드 크기를 넘는 극단적인 창 비율은 실제 영역을 자르지 않는다.
     * 남은 외곽만 같은 격자의 벽 타일로 채우고 월드 내부는 다시 비운다. */
    if (viewport->offset_x > 0.0f || viewport->offset_y > 0.0f) {
        float tile_pixels = 8.0f * scale;
        int first_x = (int)floorf(-viewport->offset_x / tile_pixels);
        int first_y = (int)floorf(-viewport->offset_y / tile_pixels);
        int last_x = (int)ceilf(((float)framebuffer_width - viewport->offset_x) / tile_pixels);
        int last_y = (int)ceilf(((float)framebuffer_height - viewport->offset_y) / tile_pixels);
        for (int y = first_y; y < last_y; ++y) {
            for (int x = first_x; x < last_x; ++x) {
                DrawTextureRec(assets->tiles, bb_rectangle(0, (float)(game->level * 8), 8, 8),
                               bb_vector2((float)x * 8.0f, (float)y * 8.0f), WHITE);
            }
        }
        DrawRectangle(0, 0, game->world_width * 8, game->world_height * 8, BLACK);
    }
    draw_world(assets, game);
    EndMode2D();

    /* HUD는 월드의 음수 테두리 오프셋을 적용하지 않고 실제 화면 끝을 따른다. */
    Camera2D hud = {0};
    hud.zoom = panel_scale;
    float width = (float)framebuffer_width / hud.zoom;
    float height = (float)framebuffer_height / hud.zoom;
    BeginMode2D(hud);
    DrawRectangleRec(bb_rectangle(0, 0, width, 24), BLACK);
    DrawRectangleRec(bb_rectangle(0, height - 12, width, 12), BLACK);
    for (int i = 0; i < game->player_count; ++i) {
        if (game->players[i].active) {
            player_hud(assets, &game->players[i], i, width, height);
        }
    }
    char label[32];
    snprintf(label, sizeof label, "ROUND %d", game->level + 1);
    text(assets, label, floorf((width - MeasureTextEx(assets->font, label, 8, 0).x) * 0.5f), height - 10, WHITE);
    EndMode2D();
    BeginMode2D(world);
    rescue_labels(assets, game, viewport, framebuffer_width, framebuffer_height, panel_scale);
    EndMode2D();

    /* 코어의 진행 상태에 맞는 안내를 마지막에 얹는다. 일시정지는 가장 위에 표시한다. */
    BeginMode2D(panel);
    if (game->state == BB_STATE_INTRO) {
        centered(assets, "READY!", 88, YELLOW);
        centered(assets, "ENTER TO START", 110, WHITE);
        centered(assets, "CHARGE 15S  BOOST 5S", 130, GRAY);
        centered(assets, "BOOST: MOVE SPEED X2", 142, GRAY);
    }
    if (game->state == BB_STATE_CLEAR) {
        centered(assets, "ROUND CLEAR!", 100, YELLOW);
    }
    if (ui->paused) {
        DrawRectangle(48, 86, 160, 42, BLACK);
        centered(assets, "PAUSED", 94, YELLOW);
        centered(assets, "P TO RESUME", 112, WHITE);
    }
    EndMode2D();
}
