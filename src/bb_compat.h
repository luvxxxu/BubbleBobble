#ifndef BB_COMPAT_H
#define BB_COMPAT_H

/* VS2010의 C 모드는 C99 헤더와 일부 수학·형식화 함수를 제공하지 않는다. */
#if defined(_MSC_VER) && _MSC_VER <= 1600 && !defined(__cplusplus)
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned char bool;
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;
#define true 1
#define false 0
#define UINT32_C(value) value##U
#ifndef isfinite
#define isfinite _finite
#endif

#if defined(__clang__) || defined(__GNUC__)
#define BB_COMPAT_UNUSED __attribute__((unused))
#else
#define BB_COMPAT_UNUSED
#endif

/* VS2010 CRT에는 C99 fminf/fmaxf/roundf가 없다. 게임에서 필요한 동작만 제공한다. */
static float BB_COMPAT_UNUSED bb_fminf(float left, float right)
{
    return left < right ? left : right;
}

static float BB_COMPAT_UNUSED bb_fmaxf(float left, float right)
{
    return left > right ? left : right;
}

static float BB_COMPAT_UNUSED bb_roundf(float value)
{
    return (float)(int)(value + (value >= 0.0f ? 0.5f : -0.5f));
}

#define fminf bb_fminf
#define fmaxf bb_fmaxf
#define roundf bb_roundf

static int BB_COMPAT_UNUSED bb_vsnprintf(char *buffer, size_t capacity, const char *format, va_list arguments)
{
    int result;
    if (capacity == 0) return -1;
    result = _vsnprintf(buffer, capacity, format, arguments);
    buffer[capacity - 1] = '\0';
    return result;
}

static int BB_COMPAT_UNUSED bb_snprintf(char *buffer, size_t capacity, const char *format, ...)
{
    int result;
    va_list arguments;
    va_start(arguments, format);
    result = bb_vsnprintf(buffer, capacity, format, arguments);
    va_end(arguments);
    return result;
}

#define snprintf bb_snprintf
#undef BB_COMPAT_UNUSED
#else
#include <stdbool.h>
#include <stdint.h>
#endif

#endif
