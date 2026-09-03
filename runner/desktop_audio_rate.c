#include "desktop_audio_rate.h"

void Dkc2AudioStretchReset(Dkc2AudioStretch *stretch) {
  if (!stretch) return;
  stretch->position = 0.0;
  stretch->last[0] = 0;
  stretch->last[1] = 0;
  stretch->primed = false;
}

static int16_t Lerp(int16_t a, int16_t b, double fraction) {
  double value = (double)a + ((double)b - (double)a) * fraction;
  if (value > 32767.0) value = 32767.0;
  if (value < -32768.0) value = -32768.0;
  return (int16_t)value;
}

int Dkc2AudioStretchProcess(Dkc2AudioStretch *stretch, double ratio,
                            const int16_t *in, int in_frames, int16_t *out,
                            int out_capacity) {
  if (!stretch || !in || !out || in_frames <= 0 || out_capacity <= 0)
    return 0;
  if (!(ratio > 0.0)) ratio = 1.0;
  if (!stretch->primed) {
    stretch->last[0] = in[0];
    stretch->last[1] = in[1];
    stretch->position = 1.0;
    stretch->primed = true;
  }
  /* The stream this call sees is the kept frame followed by the input, so
   * position p interpolates between element floor(p) and the next one; the
   * last pair is available while p stays below in_frames. */
  const double step = 1.0 / ratio;
  double position = stretch->position;
  int written = 0;
  while (position < (double)in_frames && written < out_capacity) {
    int index = (int)position;
    double fraction = position - (double)index;
    const int16_t *a = index == 0 ? stretch->last : &in[(index - 1) * 2];
    const int16_t *b = &in[index * 2];
    out[written * 2] = Lerp(a[0], b[0], fraction);
    out[written * 2 + 1] = Lerp(a[1], b[1], fraction);
    written++;
    position += step;
  }
  stretch->last[0] = in[(in_frames - 1) * 2];
  stretch->last[1] = in[(in_frames - 1) * 2 + 1];
  stretch->position = position - (double)in_frames;
  return written;
}

double Dkc2AudioRateRatio(double fill_average, double target,
                          double max_deviation, double gain) {
  if (!(target > 0.0) || !(max_deviation > 0.0)) return 1.0;
  double deviation = gain * (target - fill_average) / target;
  if (deviation > 1.0) deviation = 1.0;
  if (deviation < -1.0) deviation = -1.0;
  return 1.0 + max_deviation * deviation;
}

double Dkc2AudioFillAverage(double previous, double fill, double weight) {
  if (previous < 0.0) return fill;
  if (weight <= 0.0) return previous;
  if (weight >= 1.0) return fill;
  return previous + (fill - previous) * weight;
}
