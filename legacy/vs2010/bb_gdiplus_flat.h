#ifndef BB_VS2010_GDIPLUS_FLAT_H
#define BB_VS2010_GDIPLUS_FLAT_H

/*
 * Minimal C declaration set for the GDI+ flat ABI used by the legacy
 * renderer.  The Windows SDK's <gdiplus.h> is a C++ wrapper header, while
 * this project is deliberately compiled as C (/TC).  Keeping the small ABI
 * surface here avoids pulling C++ classes, namespaces, and constructors into
 * the VS2010 C translation unit.
 *
 * These declarations match the exported Gdiplus.dll functions.  The project
 * links gdiplus.lib, which supplies the corresponding import library.
 */

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int BBGdipStatus;

enum {
    BB_GDIP_OK = 0
};

typedef struct BBGdipImage BBGdipImage;
typedef struct BBGdipBitmap BBGdipBitmap;

/*
 * This is the C layout of GdiplusStartupInput.  The callback is unused and
 * remains NULL, so a void pointer intentionally avoids importing the C++
 * wrapper's callback typedef while preserving the ABI layout.
 */
typedef struct BBGdipStartupInput {
    UINT GdiplusVersion;
    void *DebugEventCallback;
    BOOL SuppressBackgroundThread;
    BOOL SuppressExternalCodecs;
} BBGdipStartupInput;

typedef struct BBGdipStartupOutput {
    BBGdipStatus (WINAPI *NotificationHook)(ULONG_PTR *token);
    void (WINAPI *NotificationUnhook)(ULONG_PTR token);
} BBGdipStartupOutput;

__declspec(dllimport) BBGdipStatus WINAPI GdiplusStartup(
    ULONG_PTR *token,
    const BBGdipStartupInput *input,
    BBGdipStartupOutput *output);
__declspec(dllimport) void WINAPI GdiplusShutdown(ULONG_PTR token);

__declspec(dllimport) BBGdipStatus WINAPI GdipLoadImageFromFile(
    const WCHAR *file_name,
    BBGdipImage **image);
__declspec(dllimport) BBGdipStatus WINAPI GdipGetImageWidth(
    BBGdipImage *image,
    UINT *width);
__declspec(dllimport) BBGdipStatus WINAPI GdipGetImageHeight(
    BBGdipImage *image,
    UINT *height);
/* The HBITMAP returned here is a separate GDI object: DeleteObject owns its
 * cleanup even after GdipDisposeImage releases the decoded GDI+ image. */
__declspec(dllimport) BBGdipStatus WINAPI GdipCreateHBITMAPFromBitmap(
    BBGdipBitmap *bitmap,
    HBITMAP *hbitmap,
    UINT background);
__declspec(dllimport) BBGdipStatus WINAPI GdipDisposeImage(BBGdipImage *image);

#ifdef __cplusplus
}
#endif

#endif
