#include "desktop_present_sdl.h"

#include "desktop_launcher.h"
#include "desktop_viewport.h"

#include <SDL.h>
#include <SDL_opengl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

static void SetError(char *error, size_t capacity, const char *message) {
  if (!error || capacity == 0) return;
  (void)snprintf(error, capacity, "%s", message ? message : "SDL error");
}

static bool SetSdlSwapInterval(void *user, int interval) {
  (void)user;
  return SDL_GL_SetSwapInterval(interval) == 0;
}

static bool EnvironmentEnabled(const char *name) {
  const char *value = getenv(name);
  return value && *value && *value != '0';
}

bool Dkc2SdlPresenterInit(Dkc2SdlPresenter *presenter, int window_scale,
                          int fullscreen, bool hidden, bool linear_filter,
                          int source_width, int source_height,
                          char *error, size_t error_capacity) {
  if (!presenter || window_scale < 1 ||
      source_width <= 0 || source_height <= 0) {
    SetError(error, error_capacity, "invalid SDL presenter settings");
    return false;
  }
  memset(presenter, 0, sizeof *presenter);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
  (void)SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                            SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
  (void)SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
  Uint32 flags =
      SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL;
  flags |= hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  int base_height = 240;
  int base_width =
      source_width * 7 * base_height / (source_height * 6);
  SDL_Window *window = SDL_CreateWindow(
      DKC2_PRODUCT_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      base_width * window_scale, base_height * window_scale, flags);
  if (!window) {
    SetError(error, error_capacity, SDL_GetError());
    return false;
  }
  SDL_GLContext context = SDL_GL_CreateContext(window);
  if (!context || SDL_GL_MakeCurrent(window, context) != 0) {
    SetError(error, error_capacity, SDL_GetError());
    if (context) SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }
#ifdef __APPLE__
  /* The visible Mac host uses one exact Mach deadline as its presentation
   * authority. A second blocking OpenGL-vsync gate can quantize a timely
   * frame onto the following 60/120-Hz display callback and produce the
   * alternating micro-hitches seen during horizontal traversal. macOS still
   * composites the window atomically. Keep the old gate only as an explicit
   * diagnostic override. */
  presenter->software_paced = !hidden &&
      !EnvironmentEnabled("DKC2_KEEP_OPENGL_VSYNC");
#else
  presenter->software_paced = false;
#endif
  if (presenter->software_paced || hidden) {
    (void)SDL_GL_SetSwapInterval(0);
    presenter->vsync_status = kDkc2DesktopVsyncDisabled;
  } else {
    presenter->vsync_status =
        Dkc2DesktopEnableVsync(SetSdlSwapInterval, NULL);
    if (presenter->vsync_status != kDkc2DesktopVsyncEnabled)
      (void)SDL_GL_SetSwapInterval(0);
  }
  GLuint texture = 0;
  glGenTextures(1, &texture);
  if (!texture) {
    SetError(error, error_capacity, "OpenGL texture creation failed");
    SDL_GL_DeleteContext(context);
    SDL_DestroyWindow(window);
    return false;
  }
  glBindTexture(GL_TEXTURE_2D, texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  const GLubyte *version = glGetString(GL_VERSION);
  (void)snprintf(presenter->backend, sizeof presenter->backend,
                 "SDL2/OpenGL %s; vsync=%s; pacing=%s",
                 version ? (const char *)version : "unknown",
                 Dkc2DesktopVsyncStatusName(presenter->vsync_status),
                 presenter->software_paced ? "mach" : "swap");
  presenter->window = window;
  presenter->gl_context = context;
  presenter->texture = texture;
  presenter->linear_filter = linear_filter;
  return true;
}

bool Dkc2SdlPresenterPresent(Dkc2SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height,
                             Dkc2SdlOverlayDraw overlay_draw,
                             void *overlay_user) {
  if (!presenter || !presenter->window || !presenter->gl_context ||
      !presenter->texture || !pixels || source_width <= 0 ||
      source_height <= 0)
    return false;
  SDL_Window *window = (SDL_Window *)presenter->window;
  SDL_GLContext context = (SDL_GLContext)presenter->gl_context;
  if (SDL_GL_MakeCurrent(window, context) != 0) return false;
  int output_width = 0;
  int output_height = 0;
  SDL_GL_GetDrawableSize(window, &output_width, &output_height);
  Dkc2DesktopViewport viewport;
  if (!Dkc2DesktopComputeViewport(output_width, output_height,
                                  source_width, source_height, &viewport))
    return true;

  glViewport(0, 0, output_width, output_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(viewport.x, output_height - viewport.y - viewport.height,
             viewport.width, viewport.height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);
  glBindTexture(GL_TEXTURE_2D, presenter->texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (presenter->texture_width != source_width ||
      presenter->texture_height != source_height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    presenter->texture_width = source_width;
    presenter->texture_height = source_height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width, source_height,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
  }
  GLint sampling = presenter->linear_filter ? GL_LINEAR : GL_NEAREST;
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, sampling);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, sampling);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
  glColor4f(1.0f, 1.0f, 1.0f, 1.0f);
  glBegin(GL_QUADS);
  glTexCoord2f(0.0f, 1.0f);
  glVertex2f(-1.0f, -1.0f);
  glTexCoord2f(1.0f, 1.0f);
  glVertex2f(1.0f, -1.0f);
  glTexCoord2f(1.0f, 0.0f);
  glVertex2f(1.0f, 1.0f);
  glTexCoord2f(0.0f, 0.0f);
  glVertex2f(-1.0f, 1.0f);
  glEnd();
  glBindTexture(GL_TEXTURE_2D, 0);
  glDisable(GL_TEXTURE_2D);
  if (overlay_draw) {
    glViewport(0, 0, output_width, output_height);
    overlay_draw(overlay_user, output_width, output_height);
  }
  glFlush();
  SDL_GL_SwapWindow(window);
  return true;
}

void Dkc2SdlPresenterSetTitle(Dkc2SdlPresenter *presenter,
                              const char *title) {
  if (presenter && presenter->window && title)
    SDL_SetWindowTitle((SDL_Window *)presenter->window, title);
}

bool Dkc2SdlPresenterSetFullscreen(Dkc2SdlPresenter *presenter,
                                   bool fullscreen) {
  if (!presenter || !presenter->window)
    return false;
  Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0;
  return SDL_SetWindowFullscreen((SDL_Window *)presenter->window, flags) == 0;
}

bool Dkc2SdlPresenterIsFullscreen(const Dkc2SdlPresenter *presenter) {
  if (!presenter || !presenter->window)
    return false;
  return (SDL_GetWindowFlags((SDL_Window *)presenter->window) &
          (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_FULLSCREEN_DESKTOP)) != 0;
}

const char *Dkc2SdlPresenterBackend(const Dkc2SdlPresenter *presenter) {
  return presenter && presenter->backend[0] ? presenter->backend : "SDL2";
}

Dkc2DesktopVsyncStatus Dkc2SdlPresenterVsyncStatus(
    const Dkc2SdlPresenter *presenter) {
  return presenter ? presenter->vsync_status
                   : kDkc2DesktopVsyncUnsupported;
}

bool Dkc2SdlPresenterUsesSoftwarePacing(
    const Dkc2SdlPresenter *presenter) {
  return presenter && presenter->software_paced;
}

void Dkc2SdlPresenterDestroy(Dkc2SdlPresenter *presenter) {
  if (!presenter) return;
  if (presenter->window && presenter->gl_context)
    (void)SDL_GL_MakeCurrent((SDL_Window *)presenter->window,
                            (SDL_GLContext)presenter->gl_context);
  if (presenter->texture) glDeleteTextures(1, &presenter->texture);
  if (presenter->gl_context)
    SDL_GL_DeleteContext((SDL_GLContext)presenter->gl_context);
  if (presenter->window) SDL_DestroyWindow((SDL_Window *)presenter->window);
  memset(presenter, 0, sizeof *presenter);
}
