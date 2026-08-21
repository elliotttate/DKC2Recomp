#ifndef DKC2_DESKTOP_PRESENT_GL_H
#define DKC2_DESKTOP_PRESENT_GL_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desktop_vsync.h"

/* Opaque so the Windows/OpenGL implementation does not leak GL headers into
 * the desktop host or its synthetic configuration tests. */
typedef struct Dkc2DesktopGlPresenter {
  void *state;
} Dkc2DesktopGlPresenter;

typedef void (*Dkc2DesktopGlOverlayDraw)(void *user, int width, int height);

bool Dkc2DesktopGlPresenterInit(Dkc2DesktopGlPresenter *presenter, HWND window,
                                bool enable_vsync,
                                char *error, size_t error_capacity);
void Dkc2DesktopGlPresenterDestroy(Dkc2DesktopGlPresenter *presenter);
bool Dkc2DesktopGlPresent(Dkc2DesktopGlPresenter *presenter,
                          const RECT *client, const uint8_t *pixels,
                          int source_width, int source_height,
                          bool linear_filter, int reserved_right_pixels,
                          Dkc2DesktopGlOverlayDraw overlay_draw,
                          void *overlay_user);
const char *Dkc2DesktopGlVersion(const Dkc2DesktopGlPresenter *presenter);
Dkc2DesktopVsyncStatus Dkc2DesktopGlVsyncStatus(
    const Dkc2DesktopGlPresenter *presenter);

#endif
