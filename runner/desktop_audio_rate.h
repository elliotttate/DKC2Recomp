#ifndef DKC2_DESKTOP_AUDIO_RATE_H
#define DKC2_DESKTOP_AUDIO_RATE_H

#include <stdbool.h>
#include <stdint.h>

/* Dynamic audio rate control. When the emulated frame cadence follows the
 * display instead of the cartridge's own 60.0988 Hz, or the audio device's
 * clock runs slightly off the host clock, the audio queue would drain or
 * grow without bound. Stretching each frame's samples by a ratio within a
 * fraction of a percent, chosen from how full the queue is, keeps the queue
 * near a target without any audible pitch change. */

typedef struct Dkc2AudioStretch {
  double position; /* fractional read position past the kept frame */
  int16_t last[2]; /* the previous call's final stereo frame */
  bool primed;
} Dkc2AudioStretch;

void Dkc2AudioStretchReset(Dkc2AudioStretch *stretch);

/* Resample stereo 16-bit frames by ratio (output frames per input frame)
 * with linear interpolation, continuous across calls. Returns the number of
 * output frames written, at most out_capacity. A ratio of exactly 1.0
 * reproduces the input one frame late. */
int Dkc2AudioStretchProcess(Dkc2AudioStretch *stretch, double ratio,
                            const int16_t *in, int in_frames, int16_t *out,
                            int out_capacity);

/* The stretch ratio for a queue holding fill_average frames against a
 * target: 1 at the target, up to 1 + max_deviation when the queue is empty,
 * down to 1 - max_deviation when it holds gain times the target too much.
 * The gain sets how far from the target the full deviation is reached. */
double Dkc2AudioRateRatio(double fill_average, double target,
                          double max_deviation, double gain);

/* Exponential average of the queue fill; a negative previous value starts
 * the average at the new fill. */
double Dkc2AudioFillAverage(double previous, double fill, double weight);

#endif
