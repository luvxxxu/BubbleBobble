/* Keep third-party diagnostics local to the vendored decoder translation unit. */
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wcomment"
#pragma clang diagnostic ignored "-Wtautological-compare"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcomment"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(push, 0)
#endif

#define STB_VORBIS_NO_STDIO
#include "third_party/stb_vorbis.c"

#if defined(_MSC_VER) && !defined(__clang__)
#pragma warning(pop)
#endif

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
