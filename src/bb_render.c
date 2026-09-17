#include "bb_render.h"
#include "bb_raylib_types.h"

#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static const Color p1_color = {92, 230, 52, 255};
static const Color p2_color = {52, 168, 230, 255};

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
    static const struct { const char *name; int width, height; } images[] = {
        {"Levels.png", 32, 84}, {"BubbleCharacter.png", 96, 64},
        {"BobbleCharacter.png", 96, 64}, {"Enemys.png", 256, 192},
        {"LevelTiles.png", 40, 200}, {"AttackBubble.png", 112, 16},
        {"BubbleLarge.png", 288, 32}, {"Items.png", 576, 64}, {"Logo.png", 143, 112}
    };
    const char *sounds[] = {"SFX/Bubble Bobble SFX (2).wav", "SFX/Bubble Bobble SFX (3).wav", "SFX/Jump.wav", "SFX/The Quest Begins.ogg"};
    char path[BB_PATH_CAP];
    size_t i;
    int j;
    Image image;
    Color *pixels;
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
        if (i == 0 && maps) {
            pixels = LoadImageColors(image);
            if (!pixels) { UnloadImage(image); return false; }
            for (j = 0; j < BB_LEVEL_COUNT * BB_MAP_WIDTH * BB_MAP_HEIGHT; ++j) {
                Color p;
                p = pixels[j];
                maps[j] = p.r == 255 ? 1 : p.g == 255 ? 2 : p.b == 255 ? 3 : 0;
            }
            UnloadImageColors(pixels);
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

    memset(a, 0, sizeof *a);
    a->player[0] = texture(directory, "BubbleCharacter.png");
    a->player[1] = texture(directory, "BobbleCharacter.png");
    a->enemy = texture(directory, "Enemys.png");
    a->tiles = texture(directory, "LevelTiles.png");
    a->bubble = texture(directory, "AttackBubble.png");
    a->large_bubble = texture(directory, "BubbleLarge.png");
    a->items = texture(directory, "Items.png");
    a->logo = texture(directory, "Logo.png");
    if (!path_for(path, directory, "NES_Font.ttf")) return false;
    a->font = LoadFontEx(path, 8, NULL, 95);
    if (!a->player[0].id || !a->player[1].id || !a->enemy.id || !a->tiles.id ||
        !a->bubble.id || !a->large_bubble.id || !a->items.id || !a->logo.id || !IsFontValid(a->font) ||
        a->font.texture.id == GetFontDefault().texture.id) return false;
    SetTextureFilter(a->font.texture, TEXTURE_FILTER_POINT);
    a->audio = audio && IsAudioDeviceReady();
    if (a->audio) {
        a->fire = sound(directory, "SFX/Bubble Bobble SFX (2).wav");
        a->death = sound(directory, "SFX/Bubble Bobble SFX (3).wav");
        a->jump = sound(directory, "SFX/Jump.wav");
        size = 0;
        if (!path_for(path, directory, "SFX/The Quest Begins.ogg")) return false;
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

    textures[0] = a->player[0];
    textures[1] = a->player[1];
    textures[2] = a->enemy;
    textures[3] = a->tiles;
    textures[4] = a->bubble;
    textures[5] = a->large_bubble;
    textures[6] = a->items;
    textures[7] = a->logo;
    for (i = 0; i < sizeof textures / sizeof textures[0]; ++i) if (textures[i].id) UnloadTexture(textures[i]);
    if (a->font.texture.id && a->font.texture.id != GetFontDefault().texture.id) UnloadFont(a->font);
    if (IsSoundValid(a->fire)) UnloadSound(a->fire);
    if (IsSoundValid(a->jump)) UnloadSound(a->jump);
    if (IsSoundValid(a->death)) UnloadSound(a->death);
    if (IsMusicValid(a->music)) UnloadMusicStream(a->music);
    MemFree(a->music_data);
    memset(a, 0, sizeof *a);
}

void bb_audio_events(const BBAssets *a, uint32_t events)
{
    if (!a->audio) return;
    if (events & BB_EVENT_FIRE) PlaySound(a->fire);
    if (events & BB_EVENT_JUMP) PlaySound(a->jump);
    if (events & BB_EVENT_DEATH) PlaySound(a->death);
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

static void leaderboard(const BBAssets *a, const BBUI *ui, float top, size_t maximum)
{
    size_t limit;
    size_t i;
    char line[64];

    text(a, "SCORE   ROUND  NAME", 44, top, YELLOW);
    limit = ui->score_count < maximum ? ui->score_count : maximum;
    for (i = 0; i < limit; ++i) {
        snprintf(line, sizeof line, "%2u %6u    %d    %.3s", (unsigned)i + 1, ui->scores[i].score, ui->scores[i].round, ui->scores[i].name);
        centered(a, line, top + 14 + (float)i * 12, WHITE);
    }
    if (!limit) centered(a, "NO SCORES YET", top + 20, WHITE);
}

static void menu(const BBAssets *a, const BBUI *ui)
{
    const char *entries[] = {"1 PLAYER", "2 PLAYERS", "VERSUS", "LEADERBOARD"};
    int i;
    float y;

    DrawTexture(a->logo, (BB_SCREEN_WIDTH - a->logo.width) / 2, 4, WHITE);
    for (i = 0; i < 4; ++i) {
        y = 124.0f + (float)i * 15;
        centered(a, entries[i], y, i == ui->menu_selection ? p1_color : WHITE);
        if (i == ui->menu_selection) text(a, ">", 56, y, p1_color);
    }
    centered(a, "ARROWS / ENTER TO SELECT", 190, GRAY);
    centered(a, "P1 A/D W E  P2 ARROWS /", 202, GRAY);
    centered(a, "M SOUND  F11 FULLSCREEN", 214, GRAY);
}

void bb_draw_game(const BBAssets *a, const BBGame *g, const BBUI *ui)
{
    ClearBackground(BLACK);
    if (g->state == BB_STATE_MENU) { menu(a, ui); return; }
    if (g->state == BB_STATE_SCORE) {
        int i;
        char line[48];
        char title[48];
        bool browse;

        centered(a, g->won ? "CONGRATULATIONS!" : "BUBBLE BOBBLE SCORES", 16, p1_color);
        for (i = 0; i < 2; ++i) {
            if (!g->players[i].active) continue;
            snprintf(line, sizeof line, "P%d SCORE %d", i + 1, g->players[i].score < 0 ? 0 : g->players[i].score);
            centered(a, line, 36.0f + (float)i * 12, i == 0 ? p1_color : p2_color);
        }
        if (ui->score_player >= 0) {
            snprintf(title, sizeof title, "ENTER P%d INITIALS", ui->score_player + 1);
            centered(a, title, 68, YELLOW);
            centered(a, ui->initials, 82, WHITE);
            text(a, "_", 116.0f + (float)ui->initial_cursor * 8, 88, p1_color);
        }
        browse = !g->players[0].active && !g->players[1].active;
        leaderboard(a, ui, browse ? 54.0f : 106.0f, browse ? 10 : 5);
        centered(a, ui->score_player >= 0 ? "UP/DOWN LETTER  ENTER NEXT" : "ENTER TO RETURN", 202, GRAY);
        if (ui->save_failed) centered(a, "SCORE SAVE FAILED", 214, RED);
        return;
    }
    {
        int enemy_frame;
        int i;
        int x;
        int y;
        int frame;
        int column;
        int row;
        const BBEnemy *enemy;
        const BBBubble *bubble;
        const BBPickup *pickup;
        const BBBoulder *boulder;
        const BBPlayer *player;
        char label[48];
        char round[24];

        if (g->state != BB_STATE_INTRO) {
            for (y = 0; y < BB_MAP_HEIGHT; ++y)
                for (x = 0; x < BB_MAP_WIDTH; ++x)
                    if (g->maps[g->level][y][x])
                        DrawTextureRec(a->tiles, bb_rectangle(0, (float)(g->level * 8), 8, 8), bb_vector2((float)(x * 8), (float)(y * 8)), WHITE);
        }
        enemy_frame = (int)(g->level_time * 4) % 2;
        for (i = 0; i < BB_MAX_ENEMIES; ++i) {
            enemy = &g->enemies[i];
            if (!enemy->active || enemy->state == BB_ENEMY_CAPTURED ||
                (enemy->state == BB_ENEMY_SPAWNING && enemy->age <= enemy->spawn_delay)) continue;
            frame = enemy->state == BB_ENEMY_DEAD ? 12 + (int)(enemy->age * 4) % 4 : enemy_frame + (enemy->angry && enemy->type == BB_ENEMY_ZENCHAN ? 2 : 0);
            sprite(a->enemy, frame, (int)enemy->type, 16, enemy->body.x, enemy->body.y, enemy->facing > 0);
            if (enemy->controlled) text(a, "2", enemy->body.x * 8 - 4, enemy->body.y * 8 - 18, p2_color);
        }
        for (i = 0; i < BB_MAX_BUBBLES; ++i) {
            bubble = &g->bubbles[i];
            if (!bubble->active) continue;
            frame = bubble->popping ? 5 + ((int)(fmaxf(0, 0.25f - bubble->pop_timer) * 8) % 2) : bubble->age < 0.333f ? (int)(bubble->age * 12) : 3 + (int)(bubble->age * 12) % 2;
            if (bubble->captured_enemy < 0 || bubble->popping) sprite(a->bubble, frame, 0, 16, bubble->body.x, bubble->body.y, false);
            if (!bubble->popping && bubble->captured_enemy >= 0 && bubble->captured_enemy < BB_MAX_ENEMIES)
                sprite(a->enemy, 6 + enemy_frame, (int)g->enemies[bubble->captured_enemy].type, 16, bubble->body.x, bubble->body.y, false);
        }
        for (i = 0; i < BB_MAX_PICKUPS; ++i) {
            pickup = &g->pickups[i];
            if (pickup->active) sprite(a->items, pickup->type == BB_PICKUP_FRIES ? 25 : 19, 0, 16, pickup->body.x, pickup->body.y, false);
        }
        for (i = 0; i < BB_MAX_BOULDERS; ++i) {
            boulder = &g->boulders[i];
            if (boulder->active) sprite(a->enemy, 5 - (int)(boulder->age * 4) % 6, 2, 16, boulder->body.x, boulder->body.y, false);
        }
        for (i = 0; i < BB_MAX_PLAYERS; ++i) {
            player = &g->players[i];
            if (!player->active || player->state == BB_PLAYER_OUT) continue;
            if (player->invulnerable > 0 && player->state != BB_PLAYER_DEAD && (int)(g->level_time * 12) % 2) continue;
            column = (int)(g->level_time * 7) % (fabsf(player->body.vx) > 0.1f ? 4 : 2);
            row = 0;
            if (!player->body.grounded) { row = 1; column = 2 + (int)(g->level_time * 4) % 2; }
            if (player->attack_timer > 0) { row = 2; column = 0; }
            if (player->state == BB_PLAYER_DEAD) { row = 3; column = (int)(g->level_time * 10) % 6; }
            if (g->state == BB_STATE_INTRO) { row = 2; column = 1 + (int)(g->state_time * 4) % 2; }
            sprite(a->player[i], column, row, 16, player->body.x, player->body.y, player->facing < 0);
            if (g->state == BB_STATE_INTRO) sprite(a->large_bubble, 3 + (int)(g->state_time * 4) % 2, 0, 32, player->body.x, player->body.y, false);
        }
        DrawRectangle(0, 0, BB_SCREEN_WIDTH, 17, BLACK);
        for (i = 0; i < BB_MAX_PLAYERS; ++i) {
            if (!g->players[i].active) continue;
            snprintf(label, sizeof label, "%dUP %06d", i + 1, g->players[i].score < 0 ? 0 : g->players[i].score);
            text(a, label, i == 0 ? 8.0f : 152.0f, 2.0f, i == 0 ? p1_color : p2_color);
            snprintf(label, sizeof label, "LIVES %d", g->players[i].lives < 0 ? 0 : g->players[i].lives);
            text(a, label, i == 0 ? 8.0f : 184.0f, 214.0f, i == 0 ? p1_color : p2_color);
        }
        snprintf(round, sizeof round, "ROUND %d", g->level + 1);
        centered(a, round, 214, WHITE);
        if (g->state == BB_STATE_INTRO) { centered(a, "READY!", 88, YELLOW); centered(a, "ENTER TO START", 110, WHITE); }
        if (g->state == BB_STATE_CLEAR) centered(a, "ROUND CLEAR!", 100, YELLOW);
        if (ui->paused) { DrawRectangle(48, 86, 160, 42, BLACK); centered(a, "PAUSED", 94, YELLOW); centered(a, "P TO RESUME", 112, WHITE); }
    }
}
