#ifndef DKC2_DESKTOP_PRESENT_SDL_H
#define DKC2_DESKTOP_PRESENT_SDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "desktop_vsync.h"

/* How the finished SNES frame is scaled to the drawable. Nearest and
 * bilinear are the fixed-function samplers; Reconstruct is the experimental
 * single-pass GLSL upscaler (see Dkc2SdlPresenterSetUpscaler). */
enum {
  kDkc2UpscalerNearest = 0,
  kDkc2UpscalerBilinear = 1,
  kDkc2UpscalerReconstruct = 2,
  kDkc2UpscalerCount = 3,
};

typedef struct Dkc2SdlPresenter {
  void *window;
  void *gl_context;
  unsigned int texture;
  int texture_width;
  int texture_height;
  bool linear_filter;
  bool software_paced;
  Dkc2DesktopVsyncStatus vsync_status;
  char backend[96];
  /* Reconstruct upscaler state. program is 0 when the shader is
   * unavailable; the presenter then falls back to the fixed-function path
   * and reports why in shader_error. */
  int upscaler;
  int reconstruct_mode;        /* 0 sharp, 1 +dither, 2 +edges, 3 +lv2, 4 +lv3 */
  float reconstruct_strength;  /* 0..1 edge blend strength */
  float reconstruct_softness;  /* 0..1 transition band width, 1 = 3 pixels */
  float reconstruct_shading;   /* 0..1 gradient blend in shading bands */
  unsigned int program;
  int uniform_source;
  int uniform_source_size;
  int uniform_output_size;
  int uniform_mode;
  int uniform_strength;
  int uniform_softness;
  int uniform_shading;
  char shader_error[160];
  /* Optional one-shot drawable capture: the next presented frame's drawable
   * is read back into this caller-owned RGB buffer (top-down rows). */
  uint8_t *capture_rgb;
  int capture_width;
  int capture_height;
  bool capture_done;
} Dkc2SdlPresenter;

typedef void (*Dkc2SdlOverlayDraw)(void *user, int width, int height);

bool Dkc2SdlPresenterInit(Dkc2SdlPresenter *presenter, int window_scale,
                          int fullscreen, bool hidden, bool linear_filter,
                          int source_width, int source_height,
                          char *error, size_t error_capacity);
bool Dkc2SdlPresenterPresent(Dkc2SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height,
                             Dkc2SdlOverlayDraw overlay_draw,
                             void *overlay_user);
void Dkc2SdlPresenterSetTitle(Dkc2SdlPresenter *presenter,
                              const char *title);
/* Select the upscaler; Reconstruct silently falls back to the sampler
 * implied by linear_filter when its shader failed to build. mode and
 * strength tune the experiment (see the shader comment for what each mode
 * adds). Returns the upscaler actually in effect. */
int Dkc2SdlPresenterSetUpscaler(Dkc2SdlPresenter *presenter, int upscaler,
                                int mode, float strength, float softness,
                                float shading);
const char *Dkc2SdlPresenterUpscalerName(int upscaler);
bool Dkc2SdlPresenterUpscalerFromName(const char *name, int *upscaler);
/* Arm a one-shot readback of the next presented drawable (RGB, row 0 at the
 * top). width/height receive the drawable size; the buffer must hold
 * width*height*3 bytes for the current drawable, so callers pass a buffer
 * sized from Dkc2SdlPresenterDrawableSize. */
void Dkc2SdlPresenterDrawableSize(Dkc2SdlPresenter *presenter, int *width,
                                  int *height);
void Dkc2SdlPresenterArmCapture(Dkc2SdlPresenter *presenter, uint8_t *rgb,
                                int width, int height);
bool Dkc2SdlPresenterSetFullscreen(Dkc2SdlPresenter *presenter,
                                   bool fullscreen);
bool Dkc2SdlPresenterIsFullscreen(const Dkc2SdlPresenter *presenter);
const char *Dkc2SdlPresenterBackend(const Dkc2SdlPresenter *presenter);
Dkc2DesktopVsyncStatus Dkc2SdlPresenterVsyncStatus(
    const Dkc2SdlPresenter *presenter);
/* The platform window behind the SDL window (an NSWindow on macOS), or
 * NULL where none is exposed. */
void *Dkc2SdlPresenterNativeWindow(const Dkc2SdlPresenter *presenter);

bool Dkc2SdlPresenterUsesSoftwarePacing(
    const Dkc2SdlPresenter *presenter);
void Dkc2SdlPresenterDestroy(Dkc2SdlPresenter *presenter);

#endif
