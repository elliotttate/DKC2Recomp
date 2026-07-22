#include "desktop_viewport.h"

#include <stdio.h>

static int CheckViewport(int output_width, int output_height, int x, int y,
                         int width, int height) {
  Dkc2DesktopViewport viewport;
  if (!Dkc2DesktopComputeViewport(output_width, output_height, &viewport) ||
      viewport.x != x || viewport.y != y || viewport.width != width ||
      viewport.height != height) {
    fprintf(stderr,
            "FAIL: viewport %dx%d produced (%d,%d %dx%d), expected "
            "(%d,%d %dx%d)\n",
            output_width, output_height, viewport.x, viewport.y,
            viewport.width, viewport.height, x, y, width, height);
    return 1;
  }
  return 0;
}

int main(void) {
  int failures = 0;
  failures += CheckViewport(1280, 720, 160, 0, 960, 720);
  failures += CheckViewport(800, 800, 0, 100, 800, 600);
  failures += CheckViewport(320, 240, 0, 0, 320, 240);
  Dkc2DesktopViewport viewport;
  if (Dkc2DesktopComputeViewport(0, 240, &viewport) ||
      Dkc2DesktopComputeViewport(320, -1, &viewport) ||
      Dkc2DesktopComputeViewport(320, 240, NULL)) {
    fprintf(stderr, "FAIL: invalid viewport dimensions were accepted\n");
    failures++;
  }
  if (failures) return 1;
  puts("desktop viewport tests passed");
  return 0;
}
