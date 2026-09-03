#ifndef DKC2_DESKTOP_PACER_H
#define DKC2_DESKTOP_PACER_H

#include <stdbool.h>

/* Decides whether a display's refresh ticks can carry the emulated frame
 * cadence. A display lock runs one emulated frame every ticks_per_frame
 * ticks, so a 60-Hz display shows every frame once and a 120-Hz display
 * shows every frame twice, each frame landing on the same phase of the
 * refresh. The small difference between that cadence and the cartridge's
 * 60.0988 Hz is absorbed by audio rate control. A tick rate that would
 * leave the frame rate further than the tolerance from native cannot be
 * locked, and the caller keeps pacing on its own clock. */
typedef struct Dkc2DesktopPacer {
  double native_hz;
  double tolerance;
  double period_average; /* seconds between ticks, exponential average */
  unsigned observations;
  unsigned ticks_per_frame; /* 0 while unlocked */
  double frame_hz;          /* the frame rate the lock produces */
} Dkc2DesktopPacer;

void Dkc2DesktopPacerInit(Dkc2DesktopPacer *pacer, double native_hz,
                          double tolerance);

/* The number of ticks per frame that keeps tick_hz / n within the tolerance
 * of native_hz, trying n = 1..4, or 0 when none does. */
unsigned Dkc2DesktopPacerTicksPerFrame(double tick_hz, double native_hz,
                                       double tolerance, double *frame_hz);

/* Feed the interval between two consecutive ticks. Returns the lock state
 * after this observation; a lock needs several consistent intervals. */
bool Dkc2DesktopPacerObserve(Dkc2DesktopPacer *pacer, double period_seconds);

/* Forget the measurements, for example when ticks stopped arriving. */
void Dkc2DesktopPacerReset(Dkc2DesktopPacer *pacer);

bool Dkc2DesktopPacerLocked(const Dkc2DesktopPacer *pacer);

#endif
