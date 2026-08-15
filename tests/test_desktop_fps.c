#include "desktop_fps.h"

#include <stdio.h>

#define CHECK(expression) do {                                             \
  if (!(expression)) {                                                     \
    (void)fprintf(stderr, "CHECK failed at line %d: %s\n", __LINE__,       \
                  #expression);                                            \
    return 1;                                                              \
  }                                                                        \
} while (0)

int main(void) {
  Dkc2DesktopFpsCounter counter;
  unsigned fps = 999;
  Dkc2DesktopFpsInit(&counter, 0);

  for (uint64_t frame = 1; frame <= 60; frame++) {
    bool updated = Dkc2DesktopFpsUpdate(
        &counter, true, frame * UINT64_C(1000) / UINT64_C(60), 1000, &fps);
    CHECK(updated == (frame == 60));
  }
  CHECK(fps == 60);

  for (uint64_t frame = 1; frame <= 30; frame++) {
    bool updated = Dkc2DesktopFpsUpdate(
        &counter, true, 1000 + frame * UINT64_C(1000) / UINT64_C(30),
        1000, &fps);
    CHECK(updated == (frame == 30));
  }
  CHECK(fps == 30);

  CHECK(Dkc2DesktopFpsUpdate(&counter, false, 3000, 1000, &fps));
  CHECK(fps == 0);
  CHECK(!Dkc2DesktopFpsUpdate(&counter, false, 2500, 1000, &fps));
  CHECK(!Dkc2DesktopFpsUpdate(NULL, true, 4000, 1000, &fps));
  CHECK(!Dkc2DesktopFpsUpdate(&counter, true, 4000, 0, &fps));

  puts("desktop FPS counter tests passed");
  return 0;
}
