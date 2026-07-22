#include "desktop_viewport.h"

bool Dkc2DesktopComputeViewport(int output_width, int output_height,
                                Dkc2DesktopViewport *viewport) {
  if (!viewport || output_width <= 0 || output_height <= 0) return false;
  int draw_width = output_width;
  int draw_height = draw_width * 3 / 4;
  if (draw_height > output_height) {
    draw_height = output_height;
    draw_width = draw_height * 4 / 3;
  }
  viewport->x = (output_width - draw_width) / 2;
  viewport->y = (output_height - draw_height) / 2;
  viewport->width = draw_width;
  viewport->height = draw_height;
  return true;
}
