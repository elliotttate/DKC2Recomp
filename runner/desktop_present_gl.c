#include "desktop_present_gl.h"

#include "desktop_present.h"
#include "desktop_viewport.h"

#include <GL/gl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef GL_CLAMP_TO_EDGE
#define GL_CLAMP_TO_EDGE 0x812F
#endif
#ifndef GL_BGRA
#define GL_BGRA 0x80E1
#endif

typedef struct Dkc2DesktopGlState {
  HWND window;
  HDC dc;
  HGLRC context;
  GLuint texture;
  int texture_width;
  int texture_height;
  char version[64];
} Dkc2DesktopGlState;

static void SetError(char *error, size_t capacity, const char *message) {
  if (!error || capacity == 0) return;
  (void)snprintf(error, capacity, "%s", message ? message : "OpenGL error");
}

bool Dkc2DesktopGlPresenterInit(Dkc2DesktopGlPresenter *presenter, HWND window,
                                char *error, size_t error_capacity) {
  if (!presenter || !window) {
    SetError(error, error_capacity, "OpenGL presenter received no window");
    return false;
  }
  Dkc2DesktopGlState *state =
      (Dkc2DesktopGlState *)calloc(1, sizeof *state);
  if (!state) {
    SetError(error, error_capacity, "OpenGL presenter allocation failed");
    return false;
  }
  presenter->state = state;
  state->window = window;
  state->dc = GetDC(window);
  if (!state->dc) {
    SetError(error, error_capacity, "OpenGL could not acquire the window DC");
    Dkc2DesktopGlPresenterDestroy(presenter);
    return false;
  }

  PIXELFORMATDESCRIPTOR format;
  memset(&format, 0, sizeof format);
  format.nSize = sizeof format;
  format.nVersion = 1;
  format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
  format.iPixelType = PFD_TYPE_RGBA;
  format.cColorBits = 32;
  format.cAlphaBits = 8;
  int chosen = ChoosePixelFormat(state->dc, &format);
  if (chosen == 0 || !SetPixelFormat(state->dc, chosen, &format)) {
    SetError(error, error_capacity, "OpenGL pixel format is unavailable");
    Dkc2DesktopGlPresenterDestroy(presenter);
    return false;
  }
  state->context = wglCreateContext(state->dc);
  if (!state->context || !wglMakeCurrent(state->dc, state->context)) {
    SetError(error, error_capacity, "OpenGL context creation failed");
    Dkc2DesktopGlPresenterDestroy(presenter);
    return false;
  }

  const GLubyte *version = glGetString(GL_VERSION);
  (void)snprintf(state->version, sizeof state->version, "%s",
                 version ? (const char *)version : "unknown");
  glGenTextures(1, &state->texture);
  if (!state->texture) {
    SetError(error, error_capacity, "OpenGL texture creation failed");
    Dkc2DesktopGlPresenterDestroy(presenter);
    return false;
  }
  glBindTexture(GL_TEXTURE_2D, state->texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  return true;
}

void Dkc2DesktopGlPresenterDestroy(Dkc2DesktopGlPresenter *presenter) {
  if (!presenter || !presenter->state) return;
  Dkc2DesktopGlState *state = (Dkc2DesktopGlState *)presenter->state;
  if (state->context && state->dc)
    (void)wglMakeCurrent(state->dc, state->context);
  if (state->texture) glDeleteTextures(1, &state->texture);
  if (state->context) {
    (void)wglMakeCurrent(NULL, NULL);
    (void)wglDeleteContext(state->context);
  }
  if (state->window && state->dc) ReleaseDC(state->window, state->dc);
  free(state);
  presenter->state = NULL;
}

bool Dkc2DesktopGlPresent(Dkc2DesktopGlPresenter *presenter,
                          const RECT *client, const uint8_t *pixels,
                          int source_width, int source_height,
                          bool linear_filter,
                          Dkc2DesktopGlOverlayDraw overlay_draw,
                          void *overlay_user) {
  if (!presenter || !presenter->state || !client || !pixels ||
      source_width <= 0 || source_height <= 0)
    return false;
  Dkc2DesktopGlState *state = (Dkc2DesktopGlState *)presenter->state;
  if (!wglMakeCurrent(state->dc, state->context)) return false;
  int client_width = client->right - client->left;
  int client_height = client->bottom - client->top;
  if (client_width <= 0 || client_height <= 0) return true;
  Dkc2DesktopViewport viewport;
  if (!Dkc2DesktopComputeViewport(client_width, client_height,
                                  source_width, source_height, &viewport))
    return false;

  glViewport(0, 0, client_width, client_height);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  glViewport(viewport.x, client_height - viewport.y - viewport.height,
             viewport.width, viewport.height);
  glDisable(GL_DEPTH_TEST);
  glDisable(GL_BLEND);
  glEnable(GL_TEXTURE_2D);

  glBindTexture(GL_TEXTURE_2D, state->texture);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
  if (state->texture_width != source_width ||
      state->texture_height != source_height) {
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, source_width, source_height, 0,
                 GL_BGRA, GL_UNSIGNED_BYTE, pixels);
    state->texture_width = source_width;
    state->texture_height = source_height;
  } else {
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, source_width, source_height,
                    GL_BGRA, GL_UNSIGNED_BYTE, pixels);
  }
  GLint sampling = linear_filter ? GL_LINEAR : GL_NEAREST;
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
    glViewport(0, 0, client_width, client_height);
    overlay_draw(overlay_user, client_width, client_height);
  }
  glFlush();
  return SwapBuffers(state->dc) != FALSE;
}

const char *Dkc2DesktopGlVersion(const Dkc2DesktopGlPresenter *presenter) {
  if (!presenter || !presenter->state) return "unavailable";
  const Dkc2DesktopGlState *state =
      (const Dkc2DesktopGlState *)presenter->state;
  return state->version[0] ? state->version : "unknown";
}
