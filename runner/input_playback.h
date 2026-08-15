#ifndef DKC2_INPUT_PLAYBACK_H
#define DKC2_INPUT_PLAYBACK_H

#include <stddef.h>
#include <stdint.h>

typedef struct Dkc2InputPlayback {
  uint32_t *frames;
  size_t count;
} Dkc2InputPlayback;

int Dkc2InputPlaybackLoad(const char *path, Dkc2InputPlayback *playback,
                          char *error, size_t error_size);
void Dkc2InputPlaybackFree(Dkc2InputPlayback *playback);
uint32_t Dkc2InputPlaybackFrame(const Dkc2InputPlayback *playback,
                                size_t frame);

#endif
