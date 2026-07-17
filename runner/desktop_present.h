#ifndef DKC2_DESKTOP_PRESENT_H
#define DKC2_DESKTOP_PRESENT_H

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc2DesktopPresenter {
  HDC back_dc;
  HBITMAP back_bitmap;
  HGDIOBJ original_bitmap;
  int width;
  int height;
} Dkc2DesktopPresenter;

void Dkc2DesktopPresenterDestroy(Dkc2DesktopPresenter *presenter);

/* Compose the complete letterboxed frame off screen, then update the target
 * DC with one BitBlt. This keeps the visible surface from observing the black
 * clear that precedes the stretched SNES image. */
bool Dkc2DesktopPresent(Dkc2DesktopPresenter *presenter, HDC target,
                        const RECT *client, const uint8_t *pixels,
                        const BITMAPINFO *bitmap_info, int source_width,
                        int source_height);

#endif
