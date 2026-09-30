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
/* stb_vorbis is isolated in this translation unit; keep its diagnostics local. */
#pragma warning(push, 0)
/* C4701 is emitted after flow analysis, so disable it explicitly as well. */
#pragma warning(disable : 4701)
#endif

/* Assets enter through the game's memory/file loading path. This is the one
 * translation unit that emits stb_vorbis's implementation and needs no stdio
 * entry points from the vendored decoder. */
#define STB_VORBIS_NO_STDIO
#include "third_party/stb_vorbis.c"

#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
