/* raylib의 작은 값 구조체를 만드는 공통 함수다.
 * 메모리를 할당하거나 자원을 생성하지 않고, 필드를 채운 구조체 값을 반환한다. */
#include "bb_raylib_types.h"

/* 화면 위치, 그리기 기준점 등에 쓰는 2차원 좌표를 만든다. 단위는 호출자가 정한다. */
Vector2 bb_vector2(float x, float y)
{
    return (Vector2){x, y};
}

/* 왼쪽 위 좌표와 크기로 사각형을 만든다.
 * 폭/높이의 부호는 그대로 보존해 raylib의 텍스처 좌우/상하 반전에도 사용할 수 있다. */
Rectangle bb_rectangle(float x, float y, float width, float height)
{
    return (Rectangle){x, y, width, height};
}

/* RGBA 각 채널을 0..255로 받는다. 알파 a는 0이면 투명, 255이면 불투명하다. */
Color bb_color(unsigned char r, unsigned char g, unsigned char b, unsigned char a)
{
    return (Color){r, g, b, a};
}

/* 텍스처 로드 실패를 나타내는 빈 값을 만든다. id가 0이므로 유효한 GPU 자원이 아니다. */
Texture2D bb_empty_texture(void)
{
    /* raylib 구조체의 모든 필드를 초기화해 리소스 유효성 검사에 넘긴다. */
    return (Texture2D){0};
}

/* 소리 로드 실패를 나타내는 빈 값을 만든다. 모든 필드를 0으로 초기화해
 * IsSoundValid가 실제로 생성된 소리와 구별할 수 있게 한다. */
Sound bb_empty_sound(void)
{
    return (Sound){0};
}
