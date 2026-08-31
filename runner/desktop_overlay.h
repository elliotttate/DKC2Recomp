#ifndef DKC2_DESKTOP_OVERLAY_H
#define DKC2_DESKTOP_OVERLAY_H

#include "desktop_input.h"
#include "desktop_overlay_model.h"
#include "recomp_launcher.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Dkc2DesktopOverlay Dkc2DesktopOverlay;

Dkc2DesktopOverlay *Dkc2DesktopOverlayCreate(
    const RecompLauncherCSettings *settings);
bool Dkc2DesktopOverlayInitSdl(Dkc2DesktopOverlay *overlay, void *window,
                               void *gl_context);
bool Dkc2DesktopOverlayInitWin32(Dkc2DesktopOverlay *overlay, void *window);
void Dkc2DesktopOverlayDestroy(Dkc2DesktopOverlay *overlay);

bool Dkc2DesktopOverlayProcessSdlEvent(Dkc2DesktopOverlay *overlay,
                                       const void *event);
bool Dkc2DesktopOverlayProcessWin32Message(Dkc2DesktopOverlay *overlay,
                                           void *window, unsigned message,
                                           uintptr_t wparam,
                                           intptr_t lparam);
void Dkc2DesktopOverlaySetGamepad(Dkc2DesktopOverlay *overlay,
                                  const Dkc2GamepadState *gamepad);

void Dkc2DesktopOverlayToggle(Dkc2DesktopOverlay *overlay);
bool Dkc2DesktopOverlayIsOpen(const Dkc2DesktopOverlay *overlay);
bool Dkc2DesktopOverlayAssistTools(const Dkc2DesktopOverlay *overlay);
int Dkc2DesktopOverlaySelectedSlot(const Dkc2DesktopOverlay *overlay);
void Dkc2DesktopOverlayGetSettings(const Dkc2DesktopOverlay *overlay,
                                   RecompLauncherCSettings *settings);
void Dkc2DesktopOverlaySetSettings(Dkc2DesktopOverlay *overlay,
                                   const RecompLauncherCSettings *settings);
uint32_t Dkc2DesktopOverlayTakeActions(Dkc2DesktopOverlay *overlay);
void Dkc2DesktopOverlaySetStatus(Dkc2DesktopOverlay *overlay,
                                  const char *status, bool success);

/* Called by a presenter after drawing the game and before swapping buffers.
 * The presenter must have its OpenGL context current. */
void Dkc2DesktopOverlayRenderOpenGl(void *overlay, int width, int height);

#ifdef __cplusplus
}
#endif

#endif
