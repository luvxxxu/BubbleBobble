#include "bb_render.h"
#include "bb_levels.h"
#include "bb_raylib_types.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static const Color player_colors[BB_MAX_PLAYERS] = {
    {92, 230, 52, 255}, {80, 184, 255, 255}
};

/* raylib 파일 로더 콜백의 반환 버퍼는 raylib이 MemFree로 해제한다. */
static unsigned char *read_asset(const char *path, int *size)
{
    FILE *file;
    long length;
    unsigned char *data;
    size_t received;
    int closed;

    *size = 0;
    file = bb_platform_fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    length = ftell(file);
    if (length <= 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    data = MemAlloc((unsigned int)length);
    if (!data) { fclose(file); return NULL; }
    received = fread(data, 1, (size_t)length, file);
    closed = fclose(file);
    if (received != (size_t)length || closed != 0) { MemFree(data); return NULL; }
    *size = (int)length;
    return data;
}

void bb_assets_install_file_loader(void) { SetLoadFileDataCallback(read_asset); }

static bool path_for(char path[BB_PATH_CAP], const char *directory, const char *name)
{
    return bb_path_join(path, BB_PATH_CAP, directory, name);
}

bool bb_assets_validate(const char *directory, uint8_t *maps)
{
    /* 창과 오디오 장치를 열기 전에 이미지·오디오를 디코딩하고 폰트 파일을 확인한다. */
    static const struct { const char *name; int width, height; } images[] = {
        {"Levels.png", 32, 84}, {"BubbleCharacter.png", 96, 64},
        {"BobbleCharacter.png", 96, 64},
        {"Enemys.png", 256, 192}, {"Items.png", 576, 64},
        {"LevelTiles.png", 40, 200}, {"AttackBubble.png", 112, 16},
        {"BubbleLarge.png", 288, 32}, {"Logo.png", 143, 112}
    };
    const char *sounds[] = {"SFX/Bubble Bobble SFX (2).wav", "SFX/Bubble Bobble SFX (3).wav", "SFX/Jump.wav", "SFX/The Quest Begins.ogg"};
    char path[BB_PATH_CAP];
    size_t i;
    Image image;
    Wave wave;
    int size;
    unsigned char *font;

    for (i = 0; i < sizeof images / sizeof images[0]; ++i) {
        if (!path_for(path, directory, images[i].name)) return false;
        image = LoadImage(path);
        if (!image.data || image.width != images[i].width || image.height != images[i].height) {
            fprintf(stderr, "Missing or invalid asset: %s (expected %dx%d)\n", path, images[i].width, images[i].height);
            if (image.data) UnloadImage(image);
            return false;
        }
        UnloadImage(image);
    }
    for (i = 0; i < sizeof sounds / sizeof sounds[0]; ++i) {
        if (!path_for(path, directory, sounds[i])) return false;
        wave = LoadWave(path);
        if (!IsWaveValid(wave)) { fprintf(stderr, "Invalid sound: %s\n", path); return false; }
        UnloadWave(wave);
    }
    if (!path_for(path, directory, "NES_Font.ttf")) return false;
    size = 0;
    font = read_asset(path, &size);
    if (!font || size < 12) { MemFree(font); fprintf(stderr, "Missing font: %s\n", path); return false; }
    MemFree(font);
    if (maps) bb_levels_build(maps);
    return true;
}

static Texture2D texture(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    Texture2D result;

    if (!path_for(path, directory, name)) return bb_empty_texture();
    result = LoadTexture(path);
    if (result.id) SetTextureFilter(result, TEXTURE_FILTER_POINT);
    return result;
}

static Sound sound(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    if (!path_for(path, directory, name)) return bb_empty_sound();
    return LoadSound(path);
}

bool bb_assets_load(BBAssets *a, const char *directory, bool audio)
{
    char path[BB_PATH_CAP];
    int size;

    /* 중간에 실패해도 호출자가 bb_assets_unload로 로드된 자원만 정리할 수 있다. */
    memset(a, 0, sizeof *a);
    a->players[0] = texture(directory, "BubbleCharacter.png");
    a->players[1] = texture(directory, "BobbleCharacter.png");
    a->enemy = texture(directory, "Enemys.png");
    a->items = texture(directory, "Items.png");
    a->tiles = texture(directory, "LevelTiles.png");
    a->bubble = texture(directory, "AttackBubble.png");
    a->large_bubble = texture(directory, "BubbleLarge.png");
    a->logo = texture(directory, "Logo.png");
    if (!path_for(path, directory, "NES_Font.ttf")) return false;
    a->font = LoadFontEx(path, 8, NULL, 95);
    if (!a->players[0].id || !a->players[1].id || !a->enemy.id || !a->items.id || !a->tiles.id ||
        !a->bubble.id || !a->large_bubble.id || !a->logo.id || !IsFontValid(a->font) ||
        a->font.texture.id == GetFontDefault().texture.id) return false;
    SetTextureFilter(a->font.texture, TEXTURE_FILTER_POINT);
    a->audio = audio && IsAudioDeviceReady();
    if (a->audio) {
        a->fire = sound(directory, "SFX/Bubble Bobble SFX (2).wav");
        a->death = sound(directory, "SFX/Bubble Bobble SFX (3).wav");
        a->jump = sound(directory, "SFX/Jump.wav");
        size = 0;
        if (!path_for(path, directory, "SFX/The Quest Begins.ogg")) return false;
        /* 메모리에서 연 음악 스트림보다 원본 버퍼를 오래 유지한다. */
        a->music_data = read_asset(path, &size);
        if (a->music_data) a->music = LoadMusicStreamFromMemory(".ogg", a->music_data, size);
        if (!IsSoundValid(a->fire) || !IsSoundValid(a->death) || !IsSoundValid(a->jump) || !IsMusicValid(a->music)) return false;
        a->music.looping = true;
        SetMusicVolume(a->music, 0.45f);
        PlayMusicStream(a->music);
    }
    return true;
}

void bb_assets_unload(BBAssets *a)
{
    Texture2D textures[8];
    size_t i;

    textures[0] = a->players[0];
    textures[1] = a->players[1];
    textures[2] = a->tiles;
    textures[3] = a->bubble;
    textures[4] = a->large_bubble;
    textures[5] = a->logo;
    textures[6] = a->enemy;
    textures[7] = a->items;
    for (i = 0; i < sizeof textures / sizeof textures[0]; ++i) if (textures[i].id) UnloadTexture(textures[i]);
    if (a->font.texture.id && a->font.texture.id != GetFontDefault().texture.id) UnloadFont(a->font);
    if (IsSoundValid(a->fire)) UnloadSound(a->fire);
    if (IsSoundValid(a->jump)) UnloadSound(a->jump);
    if (IsSoundValid(a->death)) UnloadSound(a->death);
    if (IsMusicValid(a->music)) UnloadMusicStream(a->music);
    MemFree(a->music_data);
    memset(a, 0, sizeof *a);
}

void bb_audio_events(const BBAssets *a, BBEvents events)
{
    if (!a->audio) return;
    if (events.fire) PlaySound(a->fire);
    if (events.jump) PlaySound(a->jump);
    if (events.death) PlaySound(a->death);
}

static void text(const BBAssets *a, const char *value, float x, float y, Color color)
{
    DrawTextEx(a->font, value, bb_vector2(x, y), 8, 0, color);
}

static void centered(const BBAssets *a, const char *value, float y, Color color)
{
    float width = MeasureTextEx(a->font, value, 8, 0).x;
    text(a, value, floorf((BB_SCREEN_WIDTH - width) * 0.5f), y, color);
}

static void sprite(Texture2D sheet, int column, int row, int size, float x, float y, bool flip)
{
    Rectangle source;
    Rectangle destination;

    /* 게임 좌표는 8픽셀 타일 단위의 중심점이고, 음수 너비는 좌우 반전이다. */
    source.x = (float)(column * size);
    source.y = (float)(row * size);
    source.width = (float)(flip ? -size : size);
    source.height = (float)size;
    destination.x = roundf(x * 8 - (float)size / 2);
    destination.y = roundf(y * 8 - (float)size / 2);
    destination.width = (float)size;
    destination.height = (float)size;
    DrawTexturePro(sheet, source, destination, bb_vector2(0, 0), 0, WHITE);
}

static int enemy_row(BBEnemyType type)
{
    /* Atlas row 2 contains Maita's boulder, not an enemy animation. */
    switch (type) {
        case BB_ENEMY_MAITA: return 1;
        case BB_ENEMY_MONSTA: return 3;
        default: return 0;
    }
}

static void pickup_sprite(const BBAssets *a, const BBPickup *pickup)
{
    /* Coordinates follow BBPickupType; every type has its own existing food sprite. */
    static const struct { int column, row; } cells[BB_PICKUP_TYPE_COUNT] = {
        {19, 0}, /* Watermelon */
        {25, 0}, /* Fries */
        {30, 1}, /* Cherry */
        {32, 1}, /* Strawberry */
        {16, 0}, /* Peach */
        {13, 0}, /* Orange */
        {34, 1}, /* Grapes */
        {17, 0}, /* Banana */
        {35, 1}, /* Pineapple */
        {14, 0}, /* Lemon */
        {4, 0},  /* Apple */
        {18, 0}, /* Pear */
        {5, 0},  /* Radish */
        {10, 0}, /* Corn */
        {2, 0},  /* Carrot */
        {1, 0},  /* Eggplant */
        {3, 1},  /* Ice cream */
        {28, 0}, /* Cake */
        {24, 0}, /* Donut */
        {27, 0}  /* Burger */
    };
    unsigned int type;

    type = (unsigned int)pickup->type;
    if (type >= BB_PICKUP_TYPE_COUNT) return;
    sprite(a->items, cells[type].column, cells[type].row, 16,
           pickup->body.x, pickup->body.y, false);
}

static void player_hud(const BBAssets *a, const BBPlayer *player, int index)
{
    float energy = fmaxf(0, fminf(1, player->energy));
    int x = 8 + index * 128;
    int fill = (int)(energy * 34 + 0.5f);
    Color color = player_colors[index];
    char label[32];
    char value[16];

    snprintf(label, sizeof label, "P%d", index + 1);
    text(a, label, (float)x, 2, color);
    snprintf(value, sizeof value, "%06d", player->score < 0 ? 0 : player->score);
    text(a, value, (float)(x + 112) - MeasureTextEx(a->font, value, 8, 0).x, 2, color);
    if (player->state == BB_PLAYER_OUT) color = GRAY;
    else if (player->boosting) color = YELLOW;
    text(a, "MANA", (float)x, 13, color);
    DrawRectangle(x + 34, 14, 36, 7, color);
    DrawRectangle(x + 35, 15, 34, 5, bb_color(32, 32, 32, 255));
    if (fill > 0) DrawRectangle(x + 35, 15, fill, 5, color);
    if (player->state == BB_PLAYER_OUT) snprintf(value, sizeof value, "OUT");
    else if (player->boosting) snprintf(value, sizeof value, "X2 %dS", (int)ceil((double)energy * BB_ENERGY_BOOST_SECONDS));
    else snprintf(value, sizeof value, "%d", (int)(energy * 100));
    text(a, value, (float)(x + 112) - MeasureTextEx(a->font, value, 8, 0).x, 13, color);
    if (player->state == BB_PLAYER_OUT) snprintf(label, sizeof label, "P%d OUT", index + 1);
    else snprintf(label, sizeof label, "P%d LIFE %d", index + 1, player->lives < 0 ? 0 : player->lives);
    if (index == 0) text(a, label, 8, 214, player_colors[index]);
    else text(a, label, 248 - MeasureTextEx(a->font, label, 8, 0).x, 214, player_colors[index]);
}

static void leaderboard(const BBAssets *a, const BBUI *ui, float top, size_t maximum)
{
    size_t limit = ui->score_count < maximum ? ui->score_count : maximum;
    size_t i;
    char value[24];
    text(a, "NO", 8, top, YELLOW);
    text(a, "NAME", 32, top, YELLOW);
    text(a, "SCORE", 132, top, YELLOW);
    text(a, "ROUND", 196, top, YELLOW);
    for (i = 0; i < limit; ++i) {
        float y = top + 14 + (float)i * 12;
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

static void score_screen(const BBAssets *a, const BBGame *g, const BBUI *ui)
{
    int i;
    char label[48];
    float initial_x;
    if (g->player_count == 0) centered(a, "LEADERBOARD", 16, YELLOW);
    else centered(a, g->won ? "ALL ROUNDS CLEAR!" : "GAME OVER", 16, YELLOW);
    for (i = 0; i < g->player_count; ++i) {
        snprintf(label, sizeof label, "P%d SCORE %d", i + 1, g->players[i].score < 0 ? 0 : g->players[i].score);
        centered(a, label, 34.0f + (float)i * 12, player_colors[i]);
    }
    if (ui->entering_initials) {
        snprintf(label, sizeof label, "P%d ENTER INITIALS", ui->score_player + 1);
        centered(a, label, 66, player_colors[ui->score_player]);
        centered(a, ui->initials, 80, WHITE);
        initial_x = floorf((BB_SCREEN_WIDTH - MeasureTextEx(a->font, ui->initials, 8, 0).x) * 0.5f);
        DrawRectangle((int)initial_x + ui->initial_cursor * 8, 89, 7, 1, player_colors[ui->score_player]);
        leaderboard(a, ui, 104, 6);
        centered(a, ui->save_failed ? "ENTER RETRY OR ESC CANCEL" : "ARROWS LETTER  ENTER NEXT", 198, GRAY);
        centered(a, "ESC CANCEL ENTRY", 214, GRAY);
    } else {
        leaderboard(a, ui, g->player_count == 0 ? 42.0f : 66.0f, g->player_count == 0 ? 10 : 9);
        centered(a, "ENTER TO RETURN", 202, GRAY);
    }
    if (ui->save_failed) {
        DrawRectangle(0, 212, BB_SCREEN_WIDTH, 12, BLACK);
        centered(a, ui->entering_initials ? "SAVE FAILED  ENTER RETRY" : "SCORES UNAVAILABLE", 214, RED);
    }
}

static void menu(const BBAssets *a, const BBUI *ui)
{
    const char *entries[BB_MENU_COUNT] = {"1 PLAYER", "2 PLAYERS", "LEADERBOARD"};
    int i;
    Color color;
    DrawTexture(a->logo, (BB_SCREEN_WIDTH - a->logo.width) / 2, 0, WHITE);
    for (i = 0; i < BB_MENU_COUNT; ++i) {
        color = GRAY;
        if (i == ui->menu_selection) {
            color = YELLOW;
            if (i < 2) color = player_colors[i];
            DrawRectangle(68, 119 + i * 13, 4, 4, color);
        }
        text(a, entries[i], 80, (float)(117 + i * 13), color);
    }
    centered(a, "UP DOWN ENTER  OR 1 2", 160, WHITE);
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

void bb_draw_game(const BBAssets *a, const BBGame *g, const BBUI *ui)
{
    int enemy_frame;
    int i, x, y, frame, column, row;
    const BBEnemy *enemy;
    const BBBubble *bubble;
    const BBPlayer *player;
    char label[32];

    ClearBackground(BLACK);
    if (g->state == BB_STATE_MENU) { menu(a, ui); return; }
    if (g->state == BB_STATE_SCORE) { score_screen(a, g, ui); return; }
    if (g->state != BB_STATE_INTRO) {
        for (y = 0; y < BB_MAP_HEIGHT; ++y)
            for (x = 0; x < BB_MAP_WIDTH; ++x)
                if (g->maps[g->level][y][x])
                    DrawTextureRec(a->tiles, bb_rectangle(0, (float)(g->level * 8), 8, 8),
                                   bb_vector2((float)(x * 8), (float)(y * 8)), WHITE);
    }
    enemy_frame = (int)(g->level_time * 4) % 2;
    for (i = 0; i < BB_MAX_ENEMIES; ++i) {
        enemy = &g->enemies[i];
        if (!enemy->active || enemy->state == BB_ENEMY_CAPTURED) continue;
        if (enemy->state == BB_ENEMY_SPAWNING && enemy->age <= enemy->spawn_delay) continue;
        if (enemy->state == BB_ENEMY_DEAD) frame = 12 + (int)(enemy->age * 4) % 4;
        else {
            frame = enemy_frame;
            if (enemy->angry) frame += 2;
        }
        sprite(a->enemy, frame, enemy_row(enemy->type), 16, enemy->body.x, enemy->body.y, enemy->facing > 0);
    }
    for (i = 0; i < BB_MAX_BUBBLES; ++i) {
        bubble = &g->bubbles[i];
        if (!bubble->active) continue;
        if (bubble->popping) frame = 5 + (int)(fmaxf(0, 0.25f - bubble->pop_timer) * 8) % 2;
        else if (bubble->age < 0.333f) frame = (int)(bubble->age * 12);
        else frame = 3 + (int)(bubble->age * 12) % 2;
        if (bubble->captured_enemy < 0 || bubble->popping)
            sprite(a->bubble, frame, 0, 16, bubble->body.x, bubble->body.y, false);
        else if (bubble->captured_enemy < BB_MAX_ENEMIES)
            sprite(a->enemy, 6 + enemy_frame, enemy_row(g->enemies[bubble->captured_enemy].type),
                   16, bubble->body.x, bubble->body.y, false);
    }
    for (i = 0; i < BB_MAX_PICKUPS; ++i)
        if (g->pickups[i].active) pickup_sprite(a, &g->pickups[i]);
    for (i = 0; i < BB_MAX_BOULDERS; ++i)
        if (g->boulders[i].active)
            sprite(a->enemy, 5 - (int)(g->boulders[i].age * 4) % 6, 2, 16,
                   g->boulders[i].body.x, g->boulders[i].body.y, false);
    for (i = 0; i < g->player_count; ++i) {
        player = &g->players[i];
        if (!player->active || player->state == BB_PLAYER_OUT) continue;
        if (player->invulnerable > 0 && player->state != BB_PLAYER_DEAD && (int)(g->level_time * 12) % 2) continue;
        if (fabsf(player->body.vx) > 0.1f) column = (int)(g->level_time * 7) % 4;
        else column = (int)(g->level_time * 7) % 2;
        row = 0;
        if (!player->body.grounded) { row = 1; column = 2 + (int)(g->level_time * 4) % 2; }
        if (player->attack_timer > 0) { row = 2; column = 0; }
        if (player->state == BB_PLAYER_DEAD) { row = 3; column = (int)(g->level_time * 10) % 6; }
        if (g->state == BB_STATE_INTRO) { row = 2; column = 1 + (int)(g->state_time * 4) % 2; }
        sprite(a->players[i], column, row, 16, player->body.x, player->body.y, player->facing < 0);
        if (g->state == BB_STATE_INTRO)
            sprite(a->large_bubble, 3 + (int)(g->state_time * 4) % 2, 0, 32, player->body.x, player->body.y, false);
    }
    DrawRectangle(0, 0, BB_SCREEN_WIDTH, 24, BLACK);
    DrawRectangle(0, 212, BB_SCREEN_WIDTH, 12, BLACK);
    for (i = 0; i < g->player_count; ++i)
        if (g->players[i].active) player_hud(a, &g->players[i], i);
    snprintf(label, sizeof label, "ROUND %d", g->level + 1);
    centered(a, label, 214, WHITE);
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
