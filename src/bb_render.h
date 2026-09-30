#ifndef BB_RENDER_H
#define BB_RENDER_H

#include "bb_game.h"
#include "bb_platform.h"
#include "raylib.h"

enum { BB_SCREEN_WIDTH = 256, BB_SCREEN_HEIGHT = 224 };
typedef enum BBMenu { BB_MENU_PLAY, BB_MENU_LEADERBOARD, BB_MENU_COUNT } BBMenu;
typedef struct BBAssets {
    Texture2D player, enemy, tiles, bubble, large_bubble, items, logo;
    Font font;
    Sound fire, jump, death;
    Music music;
    /* 메모리에서 연 음악 스트림을 내릴 때까지 유지하는 원본 OGG 데이터. */
    unsigned char *music_data;
    bool audio;
} BBAssets;
typedef struct BBUI {
    int menu_selection;
    bool muted, paused, save_failed, entering_initials;
    BbScore scores[BB_SCORE_COUNT];
    size_t score_count;
    int initial_cursor;
    char initials[4];
} BBUI;

/* raylib의 파일 읽기를 플랫폼별 UTF-8 경로 처리에 연결한다. */
void bb_assets_install_file_loader(void);
/* maps가 주어지면 검증 후 플레이 가능한 레벨 데이터를 채운다. */
bool bb_assets_validate(const char *directory, uint8_t *maps);
/* 로드 실패 시에도 bb_assets_unload를 호출해 부분적으로 만든 자원을 정리한다. */
bool bb_assets_load(BBAssets *assets, const char *directory, bool audio);
void bb_assets_unload(BBAssets *assets);
/* 순서도의 DrawGame 단계: 맵, 객체, UI를 이 순서로 그린다. */
void bb_draw_game(const BBAssets *assets, const BBGame *game, const BBUI *ui);
void bb_audio_events(const BBAssets *assets, uint32_t events);

#endif
