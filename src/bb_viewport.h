#ifndef BB_VIEWPORT_H
#define BB_VIEWPORT_H

typedef struct BBViewport {
    int columns, rows; /* 실제 게임 영역의 가로·세로 타일 수 */
    float scale; /* 원본 그림의 한 픽셀을 화면에서 몇 픽셀로 그리는가 */
    float offset_x, offset_y; /* 게임 영역을 가운데 놓기 위한 화면 좌표 */
} BBViewport;

/* 크기는 실제 framebuffer 픽셀 수, pixel_ratio는 논리 창 좌표 대비 픽셀 배율이다. */
BBViewport bb_viewport_fit(int framebuffer_width, int framebuffer_height, float pixel_ratio);

#endif
