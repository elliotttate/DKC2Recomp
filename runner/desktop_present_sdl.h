#ifndef DKC2_DESKTOP_PRESENT_SDL_H
#define DKC2_DESKTOP_PRESENT_SDL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Dkc2SdlPresenter {
  void *window;
  void *gl_context;
  unsigned int texture;
  int texture_width;
  int texture_height;
  bool linear_filter;
  char backend[96];
} Dkc2SdlPresenter;

typedef void (*Dkc2SdlOverlayDraw)(void *user, int width, int height);

bool Dkc2SdlPresenterInit(Dkc2SdlPresenter *presenter, int window_scale,
                          int fullscreen, bool hidden, bool linear_filter,
                          char *error, size_t error_capacity);
bool Dkc2SdlPresenterPresent(Dkc2SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height,
                             Dkc2SdlOverlayDraw overlay_draw,
                             void *overlay_user);
void Dkc2SdlPresenterSetTitle(Dkc2SdlPresenter *presenter,
                              const char *title);
const char *Dkc2SdlPresenterBackend(const Dkc2SdlPresenter *presenter);
void Dkc2SdlPresenterDestroy(Dkc2SdlPresenter *presenter);

#endif
