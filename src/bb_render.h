#ifndef BB_RENDER_H
#define BB_RENDER_H

#include "bb_game.h"
#include "bb_platform.h"
#include "bb_viewport.h"
#include "raylib.h"

/* 메뉴와 글자 배치의 기준 해상도다. 실제 창은 viewport의 공통 배율로 확대한다. */
enum { BB_SCREEN_WIDTH = 256, BB_SCREEN_HEIGHT = 224 };
/* 메뉴 항목의 배열 번호와 같은 순서다. COUNT는 항목의 총개수다. */
typedef enum BBMenu {
    BB_MENU_ONE_PLAYER, BB_MENU_TWO_PLAYERS, BB_MENU_LEADERBOARD, BB_MENU_COUNT
} BBMenu;
typedef struct BBAssets {
    Texture2D players[BB_MAX_PLAYERS]; /* 0번은 녹색, 1번은 파란색 캐릭터 시트 */
    Texture2D enemy, tiles, bubble, large_bubble, items, logo; /* 원본 그림 */
    Texture2D blue_bubble; /* 2P의 발사·부유·파열 그림 */
    Texture2D blue_captured_enemy; /* 적의 색은 유지하고 포획 버블만 파랗게 바꾼 그림 */
    Font font; /* 화면에 쓰는 8픽셀 영문 글꼴 */
    Sound fire, jump, death; /* 짧은 효과음 */
    Music music; /* 조금씩 읽으며 재생하는 배경음 */
    /* 메모리에서 연 음악 스트림을 내릴 때까지 유지하는 원본 OGG 데이터. */
    unsigned char *music_data;
    bool audio; /* 소리 장치와 필요한 소리 자원이 준비되었는가 */
} BBAssets;
/* 게임 규칙과 별개인 메뉴·일시 정지·순위표 편집 상태를 모은다. */
typedef struct BBUI {
    bool muted, paused; /* 음소거 / 일시 정지 */
    int selected_players; /* 다음 게임의 참가 인원: 1 또는 2 */
    int menu_selection; /* 현재 선택한 BBMenu 값 */
    bool save_failed, entering_initials; /* 저장 실패 / 이름 입력 중 */
    bool score_recorded[BB_MAX_PLAYERS]; /* 이번 게임의 점수를 이미 처리했는가 */
    int score_player, initial_cursor; /* 이름을 입력할 참가자 / 세 글자 중 현재 칸 */
    char initials[4]; /* 이름 세 글자와 문자열 끝 표시 '\0' */
    BbScore scores[BB_SCORE_COUNT]; /* 높은 점수부터 정렬된 순위표 */
    size_t score_count; /* 배열에서 실제로 사용하는 기록 개수 */
} BBUI;

/* raylib의 파일 읽기를 플랫폼별 UTF-8 경로 처리에 연결한다. */
void bb_assets_install_file_loader(void);
/* maps가 주어지면 검증 후 다섯 라운드의 타일 데이터를 채운다. */
bool bb_assets_validate(const char *directory, uint8_t *maps);
/* 로드 실패 시에도 bb_assets_unload를 호출해 부분적으로 만든 자원을 정리한다. */
bool bb_assets_load(BBAssets *assets, const char *directory, bool audio);
/* 장치를 닫기 전에 호출한다. 실패 도중 만들어진 일부 자원도 안전하게 정리한다. */
void bb_assets_unload(BBAssets *assets);
/* 순서도의 DrawGame 단계: 맵, 객체, UI를 이 순서로 그린다. */
void bb_draw_game(const BBAssets *assets, const BBGame *game, const BBUI *ui,
                  const BBViewport *viewport, int framebuffer_width, int framebuffer_height);
/* BeginDrawing/EndDrawing 사이의 마지막 작업으로 호출한다. 완성된 캔버스를
 * DPI 중복 확대 없이 실제 창 프레임버퍼의 모든 픽셀에 맞춰 출력한다. */
void bb_present_canvas(RenderTexture2D canvas, int framebuffer_width, int framebuffer_height);
/* 이번 갱신의 사건에 해당하는 효과음을 한 번씩 재생한다. */
void bb_audio_events(const BBAssets *assets, BBEvents events);

#endif
