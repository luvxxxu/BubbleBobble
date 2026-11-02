#ifndef BB_PLATFORM_H
#define BB_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/* 문자열 배열의 크기는 마지막 '\0' 문자 자리까지 포함한다. */
#define BB_PATH_CAP 4096
#define BB_SCORE_COUNT 10
#define BB_SCORE_MAX_ROUND 5

/* 순위표 한 줄이다. name[3]에는 문자열의 끝을 나타내는 '\0'을 넣는다. */
typedef struct {
    unsigned score; /* 0 이상의 점수 */
    int round;      /* 사람이 읽는 라운드 번호: 1부터 5까지 */
    char name[4];   /* 이름 세 글자와 종료 문자 한 글자 */
} BbScore;

/* 명령행 문자열들을 한곳에 모은다. values[i]가 i번째 문자열이다.
 * Windows에서는 UTF-8 복사본을 만들고, 다른 환경에서는 원래 argv를 빌린다. */
typedef struct {
    int count;      /* 프로그램 이름을 포함한 문자열 개수 */
    char **values;  /* 문자열 주소를 차례대로 담은 배열 */
    bool owned;     /* true이면 bb_platform_free_arguments가 메모리를 해제함 */
} BBArguments;

/* Windows를 포함한 모든 경로는 UTF-8이며, 프로세스 작업 디렉터리는 바꾸지 않는다. */
bool bb_platform_asset_dir(char *out, size_t capacity);
/* 사용자별 저장 폴더를 준비하고 순위표 파일의 경로를 out에 쓴다. */
bool bb_platform_score_path(char *out, size_t capacity);
/* dir 뒤에 '/'와 name을 붙인다. 결과가 capacity를 넘으면 false를 반환한다. */
bool bb_path_join(char *out, size_t capacity, const char *dir, const char *name);
/* 파일 열기/삭제에서 Windows의 한글 경로도 처리한다. 열린 파일은 호출자가 닫는다. */
FILE *bb_platform_fopen(const char *path, const char *mode);
bool bb_platform_remove(const char *path);
/* 운영체제가 준 인자를 UTF-8 형식으로 준비한다. 실패하면 false를 반환한다. */
bool bb_platform_arguments(int argc, char **argv, BBArguments *out);
/* owned일 때만 values를 해제하며, 어느 경우든 구조체를 초기화한다. */
void bb_platform_free_arguments(BBArguments *arguments);
/* 이름은 대문자/숫자/점 3자, 라운드는 1..5다. 없는 파일은 빈 순위표다.
 * 손상된 행은 건너뛰고, 읽기 실패 또는 과도한 크기는 false와 count=0이다. */
bool bb_scores_load(const char *path, BbScore scores[BB_SCORE_COUNT], size_t *count);
/* 점수 내림차순, 동점이면 라운드 내림차순. 완전 동점은 기존 기록이 먼저다.
 * 잘못된 기록은 무시하며 상위 10개만 보관한다. */
void bb_scores_insert(BbScore scores[BB_SCORE_COUNT], size_t *count, BbScore score);
/* 임시 파일을 완전히 쓴 뒤 교체한다. 실패하면 기존 파일을 유지한다. */
bool bb_scores_save(const char *path, const BbScore *scores, size_t count);

#endif
