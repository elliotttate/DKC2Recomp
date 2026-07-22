#ifndef DKC2_DESKTOP_VIEWPORT_H
#define DKC2_DESKTOP_VIEWPORT_H

#include <stdbool.h>

typedef struct Dkc2DesktopViewport {
  int x;
  int y;
  int width;
  int height;
} Dkc2DesktopViewport;

/* Compute the centered 4:3 presentation rectangle shared by every host. */
bool Dkc2DesktopComputeViewport(int output_width, int output_height,
                                Dkc2DesktopViewport *viewport);

#endif
