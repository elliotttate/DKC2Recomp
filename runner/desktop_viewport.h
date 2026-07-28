#ifndef DKC2_DESKTOP_VIEWPORT_H
#define DKC2_DESKTOP_VIEWPORT_H

#include <stdbool.h>

typedef struct Dkc2DesktopViewport {
  int x;
  int y;
  int width;
  int height;
} Dkc2DesktopViewport;

/*
 * Compute the centered presentation rectangle using the SNES 7:6 pixel
 * aspect ratio. This preserves authentic 4:3 for 256x224 and presents the
 * extended 342x224 framebuffer at approximately 16:9.
 */
bool Dkc2DesktopComputeViewport(int output_width, int output_height,
                                int source_width, int source_height,
                                Dkc2DesktopViewport *viewport);

#endif
