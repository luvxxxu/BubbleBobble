#ifndef BB_PLATFORM_H
#define BB_PLATFORM_H

#include "bb_compat.h"
#include <stddef.h>
#include <stdio.h>

#define BB_PATH_CAP 4096
#define BB_SCORE_COUNT 10
typedef struct { unsigned score; int round; char name[4]; } BbScore;
/* Windows에서는 UTF-8로 변환한 values를 소유하고, 다른 플랫폼에서는 argv를 빌린다. */
typedef struct { int count; char **values; bool owned; } BBArguments;

/* Windows를 포함한 모든 경로는 UTF-8이며, 프로세스 작업 디렉터리는 바꾸지 않는다. */
bool bb_platform_asset_dir(char *out, size_t capacity);
bool bb_platform_score_path(char *out, size_t capacity);
bool bb_path_join(char *out, size_t capacity, const char *dir, const char *name);
FILE *bb_platform_fopen(const char *path, const char *mode);
bool bb_platform_remove(const char *path);
bool bb_platform_arguments(int argc, char **argv, BBArguments *out);
/* owned일 때만 values를 해제하며, 어느 경우든 구조체를 초기화한다. */
void bb_platform_free_arguments(BBArguments *arguments);
/* 없는 파일은 빈 순위표로 처리하고, 읽기 실패는 false를 반환한다. */
bool bb_scores_load(const char *path, BbScore scores[BB_SCORE_COUNT], size_t *count);
void bb_scores_insert(BbScore scores[BB_SCORE_COUNT], size_t *count, BbScore score);
/* 임시 파일 작성 후 대상 경로를 교체한다. */
bool bb_scores_save(const char *path, const BbScore *scores, size_t count);

#endif
