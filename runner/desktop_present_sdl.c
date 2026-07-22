#include "desktop_present_sdl.h"

#include "desktop_viewport.h"

#include <SDL.h>

#include <stdio.h>
#include <string.h>

static void SetError(char *error, size_t capacity, const char *message) {
  if (!error || capacity == 0) return;
  (void)snprintf(error, capacity, "%s", message ? message : "SDL error");
}

bool Dkc2SdlPresenterInit(Dkc2SdlPresenter *presenter, int window_scale,
                          int fullscreen, bool hidden, bool linear_filter,
                          char *error, size_t error_capacity) {
  if (!presenter || window_scale < 1) {
    SetError(error, error_capacity, "invalid SDL presenter settings");
    return false;
  }
  memset(presenter, 0, sizeof *presenter);
  (void)SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY,
                    linear_filter ? "linear" : "nearest");
  Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
  flags |= hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN;
  if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
  SDL_Window *window = SDL_CreateWindow(
      "DKC2Recomp", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
      320 * window_scale, 240 * window_scale, flags);
  if (!window) {
    SetError(error, error_capacity, SDL_GetError());
    return false;
  }
  SDL_Renderer *renderer = SDL_CreateRenderer(
      window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer)
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
  if (!renderer) renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
  if (!renderer) {
    SetError(error, error_capacity, SDL_GetError());
    SDL_DestroyWindow(window);
    return false;
  }
  SDL_Texture *texture = SDL_CreateTexture(
      renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
      256, 224);
  if (!texture) {
    SetError(error, error_capacity, SDL_GetError());
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    return false;
  }
  (void)SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);
  SDL_RendererInfo info;
  memset(&info, 0, sizeof info);
  if (SDL_GetRendererInfo(renderer, &info) == 0)
    (void)snprintf(presenter->backend, sizeof presenter->backend,
                   "SDL2/%s", info.name ? info.name : "unknown");
  else
    (void)snprintf(presenter->backend, sizeof presenter->backend,
                   "SDL2/unknown");
  presenter->window = window;
  presenter->renderer = renderer;
  presenter->texture = texture;
  return true;
}

bool Dkc2SdlPresenterPresent(Dkc2SdlPresenter *presenter,
                             const uint8_t *pixels, int source_width,
                             int source_height) {
  if (!presenter || !presenter->renderer || !presenter->texture || !pixels ||
      source_width != 256 || source_height != 224)
    return false;
  SDL_Renderer *renderer = (SDL_Renderer *)presenter->renderer;
  SDL_Texture *texture = (SDL_Texture *)presenter->texture;
  if (SDL_UpdateTexture(texture, NULL, pixels, source_width * 4) != 0)
    return false;
  int output_width = 0;
  int output_height = 0;
  if (SDL_GetRendererOutputSize(renderer, &output_width, &output_height) != 0)
    return false;
  Dkc2DesktopViewport viewport;
  if (!Dkc2DesktopComputeViewport(output_width, output_height, &viewport))
    return true;
  SDL_Rect destination = {
      viewport.x, viewport.y, viewport.width, viewport.height};
  (void)SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
  if (SDL_RenderClear(renderer) != 0 ||
      SDL_RenderCopy(renderer, texture, NULL, &destination) != 0)
    return false;
  SDL_RenderPresent(renderer);
  return true;
}

void Dkc2SdlPresenterSetTitle(Dkc2SdlPresenter *presenter,
                              const char *title) {
  if (presenter && presenter->window && title)
    SDL_SetWindowTitle((SDL_Window *)presenter->window, title);
}

const char *Dkc2SdlPresenterBackend(const Dkc2SdlPresenter *presenter) {
  return presenter && presenter->backend[0] ? presenter->backend : "SDL2";
}

void Dkc2SdlPresenterDestroy(Dkc2SdlPresenter *presenter) {
  if (!presenter) return;
  if (presenter->texture)
    SDL_DestroyTexture((SDL_Texture *)presenter->texture);
  if (presenter->renderer)
    SDL_DestroyRenderer((SDL_Renderer *)presenter->renderer);
  if (presenter->window) SDL_DestroyWindow((SDL_Window *)presenter->window);
  memset(presenter, 0, sizeof *presenter);
}
