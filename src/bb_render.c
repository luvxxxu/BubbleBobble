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
    *size = 0;
    FILE *file = bb_platform_fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length <= 0 || length > 64 * 1024 * 1024 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return NULL;
    }
    unsigned char *data = MemAlloc((unsigned int)length);
    if (!data) { fclose(file); return NULL; }
    size_t received = fread(data, 1, (size_t)length, file);
    int closed = fclose(file);
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
    char path[BB_PATH_CAP];
    for (size_t i = 0; i < sizeof images / sizeof images[0]; ++i) {
        if (!path_for(path, directory, images[i].name)) return false;
        Image image = LoadImage(path);
        if (!image.data || image.width != images[i].width || image.height != images[i].height) {
            fprintf(stderr, "Missing or invalid asset: %s (expected %dx%d)\n", path, images[i].width, images[i].height);
            if (image.data) UnloadImage(image);
            return false;
        }
        if (i == 0 && maps) {
            Color *pixels = LoadImageColors(image);
            if (!pixels) { UnloadImage(image); return false; }
            for (int j = 0; j < BB_LEVEL_COUNT * BB_MAP_WIDTH * BB_MAP_HEIGHT; ++j) {
                Color p = pixels[j];
                maps[j] = p.r == 255 ? 1 : p.g == 255 ? 2 : p.b == 255 ? 3 : 0;
            }
            UnloadImageColors(pixels);
        }
        UnloadImage(image);
    }
    const char *sounds[] = {"SFX/Bubble Bobble SFX (2).wav", "SFX/Bubble Bobble SFX (3).wav", "SFX/Jump.wav", "SFX/The Quest Begins.ogg"};
    for (size_t i = 0; i < sizeof sounds / sizeof sounds[0]; ++i) {
        if (!path_for(path, directory, sounds[i])) return false;
        Wave wave = LoadWave(path);
        if (!IsWaveValid(wave)) { fprintf(stderr, "Invalid sound: %s\n", path); return false; }
        UnloadWave(wave);
    }
    if (!path_for(path, directory, "NES_Font.ttf")) return false;
    int size = 0;
    unsigned char *font = read_asset(path, &size);
    if (!font || size < 12) { MemFree(font); fprintf(stderr, "Missing font: %s\n", path); return false; }
    MemFree(font);
    return true;
}

static Texture2D texture(const char *directory, const char *name)
{
    char path[BB_PATH_CAP];
    if (!path_for(path, directory, name)) return bb_empty_texture();
    Texture2D result = LoadTexture(path);
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
        int size = 0;
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
    Texture2D textures[] = {a->player[0], a->player[1], a->enemy, a->tiles, a->bubble, a->large_bubble, a->items, a->logo};
    for (size_t i = 0; i < sizeof textures / sizeof textures[0]; ++i) if (textures[i].id) UnloadTexture(textures[i]);
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
    Rectangle source = {(float)(column * size), (float)(row * size), (float)(flip ? -size : size), (float)size};
    Rectangle destination = {roundf(x * 8 - (float)size / 2), roundf(y * 8 - (float)size / 2), (float)size, (float)size};
    DrawTexturePro(sheet, source, destination, bb_vector2(0, 0), 0, WHITE);
}

static void leaderboard(const BBAssets *a, const BBUI *ui, float top, size_t maximum)
{
    text(a, "SCORE   ROUND  NAME", 44, top, YELLOW);
    size_t limit = ui->score_count < maximum ? ui->score_count : maximum;
    for (size_t i = 0; i < limit; ++i) {
        char line[64];
        snprintf(line, sizeof line, "%2u %6u    %d    %.3s", (unsigned)i + 1, ui->scores[i].score, ui->scores[i].round, ui->scores[i].name);
        centered(a, line, top + 14 + (float)i * 12, WHITE);
    }
    if (!limit) centered(a, "NO SCORES YET", top + 20, WHITE);
}

static void menu(const BBAssets *a, const BBUI *ui)
{
    DrawTexture(a->logo, (BB_SCREEN_WIDTH - a->logo.width) / 2, 4, WHITE);
    const char *entries[] = {"1 PLAYER", "2 PLAYERS", "VERSUS", "LEADERBOARD"};
    for (int i = 0; i < 4; ++i) {
        float y = 124.0f + (float)i * 15;
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
        centered(a, g->won ? "CONGRATULATIONS!" : "BUBBLE BOBBLE SCORES", 16, p1_color);
        for (int i = 0; i < 2; ++i) {
            if (!g->players[i].active) continue;
            char line[48];
            snprintf(line, sizeof line, "P%d SCORE %d", i + 1, g->players[i].score < 0 ? 0 : g->players[i].score);
            centered(a, line, 36.0f + (float)i * 12, i == 0 ? p1_color : p2_color);
        }
        if (ui->score_player >= 0) {
            char title[48];
            snprintf(title, sizeof title, "ENTER P%d INITIALS", ui->score_player + 1);
            centered(a, title, 68, YELLOW);
            centered(a, ui->initials, 82, WHITE);
            text(a, "_", 116.0f + (float)ui->initial_cursor * 8, 88, p1_color);
        }
        bool browse = !g->players[0].active && !g->players[1].active;
        leaderboard(a, ui, browse ? 54.0f : 106.0f, browse ? 10 : 5);
        centered(a, ui->score_player >= 0 ? "UP/DOWN LETTER  ENTER NEXT" : "ENTER TO RETURN", 202, GRAY);
        if (ui->save_failed) centered(a, "SCORE SAVE FAILED", 214, RED);
        return;
    }
    if (g->state != BB_STATE_INTRO) {
        for (int y = 0; y < BB_MAP_HEIGHT; ++y)
            for (int x = 0; x < BB_MAP_WIDTH; ++x)
                if (g->maps[g->level][y][x])
                    DrawTextureRec(a->tiles, bb_rectangle(0, (float)(g->level * 8), 8, 8), bb_vector2((float)(x * 8), (float)(y * 8)), WHITE);
    }
    int enemy_frame = (int)(g->level_time * 4) % 2;
    for (int i = 0; i < BB_MAX_ENEMIES; ++i) {
        const BBEnemy *e = &g->enemies[i];
        if (!e->active || e->state == BB_ENEMY_CAPTURED ||
            (e->state == BB_ENEMY_SPAWNING && e->age <= e->spawn_delay)) continue;
        int frame = e->state == BB_ENEMY_DEAD ? 12 + (int)(e->age * 4) % 4 : enemy_frame + (e->angry && e->type == BB_ENEMY_ZENCHAN ? 2 : 0);
        sprite(a->enemy, frame, (int)e->type, 16, e->body.x, e->body.y, e->facing > 0);
        if (e->controlled) text(a, "2", e->body.x * 8 - 4, e->body.y * 8 - 18, p2_color);
    }
    for (int i = 0; i < BB_MAX_BUBBLES; ++i) {
        const BBBubble *b = &g->bubbles[i];
        if (!b->active) continue;
        int frame = b->popping ? 5 + ((int)(fmaxf(0, 0.25f - b->pop_timer) * 8) % 2) : b->age < 0.333f ? (int)(b->age * 12) : 3 + (int)(b->age * 12) % 2;
        if (b->captured_enemy < 0 || b->popping) sprite(a->bubble, frame, 0, 16, b->body.x, b->body.y, false);
        if (!b->popping && b->captured_enemy >= 0 && b->captured_enemy < BB_MAX_ENEMIES)
            sprite(a->enemy, 6 + enemy_frame, (int)g->enemies[b->captured_enemy].type, 16, b->body.x, b->body.y, false);
    }
    for (int i = 0; i < BB_MAX_PICKUPS; ++i) {
        const BBPickup *p = &g->pickups[i];
        if (p->active) sprite(a->items, p->type == BB_PICKUP_FRIES ? 25 : 19, 0, 16, p->body.x, p->body.y, false);
    }
    for (int i = 0; i < BB_MAX_BOULDERS; ++i) {
        const BBBoulder *b = &g->boulders[i];
        if (b->active) sprite(a->enemy, 5 - (int)(b->age * 4) % 6, 2, 16, b->body.x, b->body.y, false);
    }
    for (int i = 0; i < BB_MAX_PLAYERS; ++i) {
        const BBPlayer *p = &g->players[i];
        if (!p->active || p->state == BB_PLAYER_OUT) continue;
        if (p->invulnerable > 0 && p->state != BB_PLAYER_DEAD && (int)(g->level_time * 12) % 2) continue;
        int column = (int)(g->level_time * 7) % (fabsf(p->body.vx) > 0.1f ? 4 : 2), row = 0;
        if (!p->body.grounded) { row = 1; column = 2 + (int)(g->level_time * 4) % 2; }
        if (p->attack_timer > 0) { row = 2; column = 0; }
        if (p->state == BB_PLAYER_DEAD) { row = 3; column = (int)(g->level_time * 10) % 6; }
        if (g->state == BB_STATE_INTRO) { row = 2; column = 1 + (int)(g->state_time * 4) % 2; }
        sprite(a->player[i], column, row, 16, p->body.x, p->body.y, p->facing < 0);
        if (g->state == BB_STATE_INTRO) sprite(a->large_bubble, 3 + (int)(g->state_time * 4) % 2, 0, 32, p->body.x, p->body.y, false);
    }
    DrawRectangle(0, 0, BB_SCREEN_WIDTH, 17, BLACK);
    for (int i = 0; i < BB_MAX_PLAYERS; ++i) {
        if (!g->players[i].active) continue;
        char label[48];
        snprintf(label, sizeof label, "%dUP %06d", i + 1, g->players[i].score < 0 ? 0 : g->players[i].score);
        text(a, label, i == 0 ? 8 : 152, 2, i == 0 ? p1_color : p2_color);
        snprintf(label, sizeof label, "LIVES %d", g->players[i].lives < 0 ? 0 : g->players[i].lives);
        text(a, label, i == 0 ? 8 : 184, 214, i == 0 ? p1_color : p2_color);
    }
    char round[24];
    snprintf(round, sizeof round, "ROUND %d", g->level + 1);
    centered(a, round, 214, WHITE);
    if (g->state == BB_STATE_INTRO) { centered(a, "READY!", 88, YELLOW); centered(a, "ENTER TO START", 110, WHITE); }
    if (g->state == BB_STATE_CLEAR) centered(a, "ROUND CLEAR!", 100, YELLOW);
    if (ui->paused) { DrawRectangle(48, 86, 160, 42, BLACK); centered(a, "PAUSED", 94, YELLOW); centered(a, "P TO RESUME", 112, WHITE); }
}
