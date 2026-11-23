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

/* Decides whether the desktop host should pause emulation, audio, and active
 * presentation while the window is backgrounded. Hidden automated test runs
 * never pause; minimized or hidden user windows always pause; unfocused user
 * windows pause when pause_on_focus_loss is enabled unless run_in_background
 * is explicitly requested. */
bool Dkc2DesktopShouldPauseInBackground(bool hidden_test,
                                        bool minimized_or_hidden,
                                        bool focused,
                                        bool pause_on_focus_loss,
                                        bool run_in_background);

/* Decides whether a background-paused window should perform a one-shot
 * repaint of the last rendered frame (for example after expose or resize).
 * Minimized, hidden, and fullscreen windows never repaint while paused so the
 * host does not submit OpenGL swaps behind the compositor during Alt+Tab. */
bool Dkc2DesktopShouldRepaintWhenPaused(bool needs_repaint,
                                        bool minimized_or_hidden,
                                        bool fullscreen);

#endif
