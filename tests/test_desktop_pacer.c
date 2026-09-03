#include "desktop_pacer.h"

#include <math.h>
#include <stdio.h>

static int g_failures;

#define EXPECT(condition)                                                  \
  do {                                                                     \
    if (!(condition)) {                                                    \
      fprintf(stderr, "%s:%d: expectation failed: %s\n", __FILE__,         \
              __LINE__, #condition);                                       \
      g_failures++;                                                        \
    }                                                                      \
  } while (0)

static const double kNative = 60.098811862;

static void TestTicksPerFrame(void) {
  double frame_hz = 0.0;
  EXPECT(Dkc2DesktopPacerTicksPerFrame(60.0, kNative, 0.02, &frame_hz) == 1);
  EXPECT(fabs(frame_hz - 60.0) < 1e-9);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(59.94, kNative, 0.02, &frame_hz) == 1);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(120.0, kNative, 0.02, &frame_hz) == 2);
  EXPECT(fabs(frame_hz - 60.0) < 1e-9);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(119.88, kNative, 0.02, &frame_hz) == 2);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(240.0, kNative, 0.02, &frame_hz) == 4);
  /* Rates that would run the game visibly fast or slow cannot be locked. */
  EXPECT(Dkc2DesktopPacerTicksPerFrame(144.0, kNative, 0.02, &frame_hz) == 0);
  EXPECT(frame_hz == 0.0);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(50.0, kNative, 0.02, &frame_hz) == 0);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(75.0, kNative, 0.02, &frame_hz) == 0);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(90.0, kNative, 0.02, &frame_hz) == 0);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(0.0, kNative, 0.02, &frame_hz) == 0);
  EXPECT(Dkc2DesktopPacerTicksPerFrame(60.0, 0.0, 0.02, NULL) == 0);
}

static void TestObserveLocks(void) {
  Dkc2DesktopPacer pacer;
  Dkc2DesktopPacerInit(&pacer, kNative, 0.02);
  EXPECT(!Dkc2DesktopPacerLocked(&pacer));
  bool locked = false;
  for (int i = 0; i < 7; i++) {
    locked = Dkc2DesktopPacerObserve(&pacer, 1.0 / 120.0);
    EXPECT(!locked);
  }
  locked = Dkc2DesktopPacerObserve(&pacer, 1.0 / 120.0);
  EXPECT(locked);
  EXPECT(pacer.ticks_per_frame == 2);
  EXPECT(fabs(pacer.frame_hz - 60.0) < 0.01);
  /* A refresh change is followed once the average settles on it. */
  for (int i = 0; i < 60; i++) locked = Dkc2DesktopPacerObserve(&pacer, 1.0 / 60.0);
  EXPECT(locked);
  EXPECT(pacer.ticks_per_frame == 1);
  /* An unlockable rate releases the lock. */
  for (int i = 0; i < 60; i++) locked = Dkc2DesktopPacerObserve(&pacer, 1.0 / 144.0);
  EXPECT(!locked);
  EXPECT(pacer.ticks_per_frame == 0);
}

static void TestObserveResetsOnStall(void) {
  Dkc2DesktopPacer pacer;
  Dkc2DesktopPacerInit(&pacer, kNative, 0.02);
  for (int i = 0; i < 8; i++) (void)Dkc2DesktopPacerObserve(&pacer, 1.0 / 60.0);
  EXPECT(Dkc2DesktopPacerLocked(&pacer));
  EXPECT(!Dkc2DesktopPacerObserve(&pacer, 0.5));
  EXPECT(!Dkc2DesktopPacerLocked(&pacer));
  EXPECT(pacer.observations == 0);
  EXPECT(!Dkc2DesktopPacerObserve(&pacer, 0.0));
  EXPECT(!Dkc2DesktopPacerObserve(&pacer, -1.0));
  /* Null pacers are ignored. */
  EXPECT(!Dkc2DesktopPacerObserve(NULL, 1.0 / 60.0));
  EXPECT(!Dkc2DesktopPacerLocked(NULL));
}

int main(void) {
  TestTicksPerFrame();
  TestObserveLocks();
  TestObserveResetsOnStall();
  if (g_failures) {
    fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  printf("desktop pacer tests passed\n");
  return 0;
}
