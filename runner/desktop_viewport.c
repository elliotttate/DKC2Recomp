#include "desktop_viewport.h"

#include <stdint.h>

bool Dkc2DesktopComputeViewport(int output_width, int output_height,
                                int source_width, int source_height,
                                Dkc2DesktopViewport *viewport) {
  if (!viewport || output_width <= 0 || output_height <= 0 ||
      source_width <= 0 || source_height <= 0)
    return false;
  const int64_t aspect_numerator = (int64_t)source_width * 7;
  const int64_t aspect_denominator = (int64_t)source_height * 6;
  int draw_width = output_width;
  int draw_height =
      (int)((int64_t)draw_width * aspect_denominator / aspect_numerator);
  if (draw_height > output_height) {
    draw_height = output_height;
    draw_width =
        (int)((int64_t)draw_height * aspect_numerator / aspect_denominator);
  }
  viewport->x = (output_width - draw_width) / 2;
  viewport->y = (output_height - draw_height) / 2;
  viewport->width = draw_width;
  viewport->height = draw_height;
  return true;
}
