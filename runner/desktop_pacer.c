#include "desktop_pacer.h"

#include <math.h>

enum {
  kDkc2PacerLockObservations = 8,
  kDkc2PacerMaximumTicksPerFrame = 4,
};

static const double kDkc2PacerAverageWeight = 0.1;
/* Intervals longer than this are a stalled link, not a refresh period. */
static const double kDkc2PacerLongestPeriod = 0.1;

void Dkc2DesktopPacerInit(Dkc2DesktopPacer *pacer, double native_hz,
                          double tolerance) {
  if (!pacer) return;
  pacer->native_hz = native_hz;
  pacer->tolerance = tolerance;
  Dkc2DesktopPacerReset(pacer);
}

unsigned Dkc2DesktopPacerTicksPerFrame(double tick_hz, double native_hz,
                                       double tolerance, double *frame_hz) {
  if (frame_hz) *frame_hz = 0.0;
  if (!(tick_hz > 0.0) || !(native_hz > 0.0)) return 0;
  for (unsigned n = 1; n <= kDkc2PacerMaximumTicksPerFrame; n++) {
    double candidate = tick_hz / (double)n;
    if (fabs(candidate / native_hz - 1.0) <= tolerance) {
      if (frame_hz) *frame_hz = candidate;
      return n;
    }
  }
  return 0;
}

bool Dkc2DesktopPacerObserve(Dkc2DesktopPacer *pacer, double period_seconds) {
  if (!pacer) return false;
  if (!(period_seconds > 0.0) || period_seconds > kDkc2PacerLongestPeriod) {
    Dkc2DesktopPacerReset(pacer);
    return false;
  }
  if (pacer->observations == 0)
    pacer->period_average = period_seconds;
  else
    pacer->period_average +=
        (period_seconds - pacer->period_average) * kDkc2PacerAverageWeight;
  if (pacer->observations < kDkc2PacerLockObservations) {
    pacer->observations++;
    if (pacer->observations < kDkc2PacerLockObservations) return false;
  }
  double frame_hz = 0.0;
  pacer->ticks_per_frame = Dkc2DesktopPacerTicksPerFrame(
      1.0 / pacer->period_average, pacer->native_hz, pacer->tolerance,
      &frame_hz);
  pacer->frame_hz = frame_hz;
  return pacer->ticks_per_frame != 0;
}

void Dkc2DesktopPacerReset(Dkc2DesktopPacer *pacer) {
  if (!pacer) return;
  pacer->period_average = 0.0;
  pacer->observations = 0;
  pacer->ticks_per_frame = 0;
  pacer->frame_hz = 0.0;
}

bool Dkc2DesktopPacerLocked(const Dkc2DesktopPacer *pacer) {
  return pacer && pacer->ticks_per_frame != 0;
}
