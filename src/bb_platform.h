#ifndef BB_PLATFORM_H
#define BB_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define BB_PATH_CAP 4096
/* Windows에서는 UTF-8로 변환한 values를 소유하고, 다른 플랫폼에서는 argv를 빌린다. */
typedef struct { int count; char **values; bool owned; } BBArguments;

/* Windows를 포함한 모든 경로는 UTF-8이며, 프로세스 작업 디렉터리는 바꾸지 않는다. */
bool bb_platform_asset_dir(char *out, size_t capacity);
bool bb_path_join(char *out, size_t capacity, const char *dir, const char *name);
FILE *bb_platform_fopen(const char *path, const char *mode);
bool bb_platform_arguments(int argc, char **argv, BBArguments *out);
/* owned일 때만 values를 해제하며, 어느 경우든 구조체를 초기화한다. */
void bb_platform_free_arguments(BBArguments *arguments);

#endif
