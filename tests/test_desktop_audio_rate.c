#include "desktop_audio_rate.h"

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

enum { kFrames = 534, kCapacity = 600 };

static void FillRamp(int16_t *frames, int count, int start) {
  for (int i = 0; i < count; i++) {
    frames[i * 2] = (int16_t)(start + i);
    frames[i * 2 + 1] = (int16_t)(-(start + i));
  }
}

static void TestUnityRatioIsTransparent(void) {
  Dkc2AudioStretch stretch;
  Dkc2AudioStretchReset(&stretch);
  int16_t in[kFrames * 2];
  int16_t out[kCapacity * 2];
  FillRamp(in, kFrames, 0);
  /* Interpolation keeps the final input frame for the next call, so the
   * first call is one frame short and the stream runs one frame late. */
  int written = Dkc2AudioStretchProcess(&stretch, 1.0, in, kFrames, out,
                                        kCapacity);
  EXPECT(written == kFrames - 1);
  for (int i = 0; i < written; i++) {
    EXPECT(out[i * 2] == in[i * 2]);
    EXPECT(out[i * 2 + 1] == in[i * 2 + 1]);
  }
  /* The second call continues exactly where the first ended. */
  FillRamp(in, kFrames, kFrames);
  written = Dkc2AudioStretchProcess(&stretch, 1.0, in, kFrames, out,
                                    kCapacity);
  EXPECT(written == kFrames);
  EXPECT(out[0] == kFrames - 1);
  EXPECT(out[(written - 1) * 2] == 2 * kFrames - 2);
}

static void TestStretchChangesCountAndStaysContinuous(void) {
  Dkc2AudioStretch stretch;
  Dkc2AudioStretchReset(&stretch);
  int16_t in[kFrames * 2];
  int16_t out[kCapacity * 2];
  int total = 0;
  int16_t previous = -1;
  /* Forty calls keep the ramp inside 16 bits. */
  for (int call = 0; call < 40; call++) {
    FillRamp(in, kFrames, call * kFrames);
    int written = Dkc2AudioStretchProcess(&stretch, 1.005, in, kFrames, out,
                                          kCapacity);
    EXPECT(written >= kFrames + 1 && written <= kFrames + 3);
    for (int i = 0; i < written; i++) {
      /* A ramp stays monotonic through interpolation and across calls, and
       * each channel keeps its own sign. */
      EXPECT(out[i * 2] >= previous);
      EXPECT(out[i * 2 + 1] == -out[i * 2]);
      previous = out[i * 2];
    }
    total += written;
  }
  EXPECT(fabs((double)total / (40.0 * kFrames) - 1.005) < 0.0005);

  Dkc2AudioStretchReset(&stretch);
  total = 0;
  for (int call = 0; call < 40; call++) {
    FillRamp(in, kFrames, call * kFrames);
    int written = Dkc2AudioStretchProcess(&stretch, 0.995, in, kFrames, out,
                                          kCapacity);
    EXPECT(written >= kFrames - 4 && written <= kFrames - 2);
    total += written;
  }
  EXPECT(fabs((double)total / (40.0 * kFrames) - 0.995) < 0.0005);
}

static void TestStretchHonoursCapacityAndBadInput(void) {
  Dkc2AudioStretch stretch;
  Dkc2AudioStretchReset(&stretch);
  int16_t in[kFrames * 2];
  int16_t out[kCapacity * 2];
  FillRamp(in, kFrames, 0);
  EXPECT(Dkc2AudioStretchProcess(&stretch, 1.0, in, kFrames, out, 10) == 10);
  EXPECT(Dkc2AudioStretchProcess(&stretch, 1.0, in, 0, out, kCapacity) == 0);
  EXPECT(Dkc2AudioStretchProcess(&stretch, 1.0, in, kFrames, out, 0) == 0);
  EXPECT(Dkc2AudioStretchProcess(NULL, 1.0, in, kFrames, out, kCapacity) == 0);
  /* A non-positive ratio is treated as unity rather than looping forever. */
  Dkc2AudioStretchReset(&stretch);
  EXPECT(Dkc2AudioStretchProcess(&stretch, 0.0, in, kFrames, out, kCapacity) ==
         kFrames - 1);
}

static void TestRateRatio(void) {
  EXPECT(fabs(Dkc2AudioRateRatio(2000.0, 2000.0, 0.005, 4.0) - 1.0) < 1e-12);
  EXPECT(fabs(Dkc2AudioRateRatio(0.0, 2000.0, 0.005, 4.0) - 1.005) < 1e-12);
  EXPECT(fabs(Dkc2AudioRateRatio(4000.0, 2000.0, 0.005, 4.0) - 0.995) < 1e-12);
  /* The gain sets where the full deviation is reached: a quarter short of
   * the target with gain 4. */
  EXPECT(fabs(Dkc2AudioRateRatio(1500.0, 2000.0, 0.005, 4.0) - 1.005) < 1e-12);
  EXPECT(fabs(Dkc2AudioRateRatio(1750.0, 2000.0, 0.005, 4.0) - 1.0025) < 1e-12);
  EXPECT(Dkc2AudioRateRatio(100.0, 0.0, 0.005, 4.0) == 1.0);
  EXPECT(Dkc2AudioRateRatio(100.0, 2000.0, 0.0, 4.0) == 1.0);
}

static void TestFillAverage(void) {
  EXPECT(Dkc2AudioFillAverage(-1.0, 1234.0, 0.1) == 1234.0);
  EXPECT(fabs(Dkc2AudioFillAverage(1000.0, 2000.0, 0.1) - 1100.0) < 1e-9);
  EXPECT(Dkc2AudioFillAverage(1000.0, 2000.0, 0.0) == 1000.0);
  EXPECT(Dkc2AudioFillAverage(1000.0, 2000.0, 1.0) == 2000.0);
}

int main(void) {
  TestUnityRatioIsTransparent();
  TestStretchChangesCountAndStaysContinuous();
  TestStretchHonoursCapacityAndBadInput();
  TestRateRatio();
  TestFillAverage();
  if (g_failures) {
    fprintf(stderr, "%d failure(s)\n", g_failures);
    return 1;
  }
  printf("desktop audio rate tests passed\n");
  return 0;
}
