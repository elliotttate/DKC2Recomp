#include "desktop_perf.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static void ExpectNear(const char *name, double actual, double expected) {
  if (fabs(actual - expected) > 0.0001) {
    (void)fprintf(stderr, "%s: expected %.6f, got %.6f\n",
                  name, expected, actual);
    exit(EXIT_FAILURE);
  }
}

int main(void) {
  Dkc2DesktopPerfCounter counter;
  Dkc2DesktopPerfSample sample;
  Dkc2DesktopPerfInit(&counter, 1000);
  for (int frame = 0; frame < 60; frame++) {
    Dkc2DesktopPerfAdd(&counter, kDkc2PerfEmulation, 3);
    Dkc2DesktopPerfAdd(&counter, kDkc2PerfPpu, 1);
    Dkc2DesktopPerfAdd(&counter, kDkc2PerfPace, 10);
    if (Dkc2DesktopPerfUpdate(&counter, true, 1000 + frame * 16,
                              1000, &sample)) {
      (void)fputs("sampling interval ended too early\n", stderr);
      return EXIT_FAILURE;
    }
  }
  if (!Dkc2DesktopPerfUpdate(&counter, false, 2000, 1000, &sample)) {
    (void)fputs("sampling interval did not complete\n", stderr);
    return EXIT_FAILURE;
  }
  if (sample.presented_frames != 60) {
    (void)fprintf(stderr, "expected 60 presentations, got %u\n",
                  sample.presented_frames);
    return EXIT_FAILURE;
  }
  ExpectNear("elapsed", sample.elapsed_seconds, 1.0);
  ExpectNear("fps", sample.presented_fps, 60.0);
  ExpectNear("emulation ms", sample.phase_ms[kDkc2PerfEmulation], 3.0);
  ExpectNear("PPU ms", sample.phase_ms[kDkc2PerfPpu], 1.0);
  ExpectNear("pace ms", sample.phase_ms[kDkc2PerfPace], 10.0);
  ExpectNear("active ms", sample.active_ms, 4.0);
  ExpectNear("busy percent", sample.main_thread_busy_percent, 24.0);
  ExpectNear("untracked ms", sample.untracked_ms, 8.0 / 3.0);

  Dkc2DesktopPerfInit(&counter, 5000);
  if (Dkc2DesktopPerfUpdate(&counter, false, 4000, 1000, &sample) ||
      counter.interval_start != 4000) {
    (void)fputs("clock reversal handling failed\n", stderr);
    return EXIT_FAILURE;
  }
  (void)puts("Desktop performance telemetry tests passed");
  return EXIT_SUCCESS;
}
