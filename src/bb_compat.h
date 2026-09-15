#ifndef BB_COMPAT_H
#define BB_COMPAT_H

/* MSVC의 C 모드는 일부 설치에서 C99 stdbool.h를 제공하지 않는다. */
#if defined(_MSC_VER) && !defined(__cplusplus)
typedef unsigned char bool;
#define true 1
#define false 0
#else
#include <stdbool.h>
#endif

#endif
