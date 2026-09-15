#ifndef BB_RENDER_H
#define BB_RENDER_H

#include "bb_game.h"
#include "bb_platform.h"
#include "raylib.h"

enum { BB_SCREEN_WIDTH = 256, BB_SCREEN_HEIGHT = 224 };
typedef struct BBAssets {
    Texture2D player[2], enemy, tiles, bubble, large_bubble, items, logo;
    Font font;
    Sound fire, jump, death;
    Music music;
    unsigned char *music_data;
    bool audio;
} BBAssets;
typedef struct BBUI {
    int menu_selection;
    bool muted, paused, save_failed;
    BbScore scores[BB_SCORE_COUNT];
    size_t score_count;
    int score_player, initial_cursor;
    char initials[4];
} BBUI;

void bb_assets_install_file_loader(void);
bool bb_assets_validate(const char *directory, uint8_t *maps);
bool bb_assets_load(BBAssets *assets, const char *directory, bool audio);
void bb_assets_unload(BBAssets *assets);
/* 순서도의 DrawGame 단계: 맵, 객체, UI를 이 순서로 그린다. */
void bb_draw_game(const BBAssets *assets, const BBGame *game, const BBUI *ui);
void bb_audio_events(const BBAssets *assets, uint32_t events);

#endif
