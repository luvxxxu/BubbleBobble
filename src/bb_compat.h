#ifndef BB_COMPAT_H
#define BB_COMPAT_H

/* VS2010의 C 모드는 C99 헤더와 일부 수학·형식화 함수를 제공하지 않는다. */
#if defined(_MSC_VER) && _MSC_VER <= 1600 && !defined(__cplusplus)
#include <math.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
typedef unsigned char bool;
typedef unsigned char uint8_t;
typedef unsigned int uint32_t;
typedef unsigned __int64 uint64_t;
#define true 1
#define false 0
#define UINT32_C(value) value##U
#define BB_UINT64_PRINTF "%I64u"
#define BB_UINT64_CAST(value) ((unsigned __int64)(value))
#ifndef isfinite
#define isfinite _finite
#endif

#if defined(__clang__) || defined(__GNUC__)
#define BB_COMPAT_UNUSED __attribute__((unused))
#else
#define BB_COMPAT_UNUSED
#endif

/* VS2010 CRT에는 C99 float 수학 함수가 완전하게 제공되지 않는다.
 * 게임 좌표와 시간 값은 모두 float 범위에 충분히 작으므로, 오래된 CRT의
 * double 함수를 호출한 뒤 float로 변환해 같은 의미를 유지한다. */
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

static float BB_COMPAT_UNUSED bb_floorf(float value)
{
    return (float)floor((double)value);
}

static float BB_COMPAT_UNUSED bb_fabsf(float value)
{
    return (float)fabs((double)value);
}

static float BB_COMPAT_UNUSED bb_sqrtf(float value)
{
    return (float)sqrt((double)value);
}

static float BB_COMPAT_UNUSED bb_sinf(float value)
{
    return (float)sin((double)value);
}

static float BB_COMPAT_UNUSED bb_cosf(float value)
{
    return (float)cos((double)value);
}

#define fminf bb_fminf
#define fmaxf bb_fmaxf
#define roundf bb_roundf
#define floorf bb_floorf
#define fabsf bb_fabsf
#define sqrtf bb_sqrtf
#define sinf bb_sinf
#define cosf bb_cosf

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
#define BB_UINT64_PRINTF "%llu"
#define BB_UINT64_CAST(value) ((unsigned long long)(value))
#endif

#endif
