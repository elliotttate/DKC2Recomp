#include "input_playback.h"

#include <stdio.h>
#include <stdlib.h>

static void Check(int condition, const char *message) {
  if (!condition) {
    (void)fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
  }
}

int main(int argc, char **argv) {
  Check(argc == 3, "usage: test_input_playback <valid> <invalid>");

  Dkc2InputPlayback playback = {0};
  char error[160];
  Check(Dkc2InputPlaybackLoad(argv[1], &playback, error, sizeof error),
        error);
  Check(playback.count == 7, "expanded frame count is wrong");
  Check(Dkc2InputPlaybackFrame(&playback, 0) == 0x000u,
        "first frame mismatch");
  Check(Dkc2InputPlaybackFrame(&playback, 1) == 0x108u,
        "repeat frame 1 mismatch");
  Check(Dkc2InputPlaybackFrame(&playback, 3) == 0x108u,
        "repeat frame 3 mismatch");
  Check(Dkc2InputPlaybackFrame(&playback, 4) == 0x100008u,
        "packed two-player frame mismatch");
  Check(Dkc2InputPlaybackFrame(&playback, 6) == 0x00fu,
        "space-repeat frame mismatch");
  Check(Dkc2InputPlaybackFrame(&playback, 7) == 0,
        "EOF should replay neutral input");
  Dkc2InputPlaybackFree(&playback);

  Check(!Dkc2InputPlaybackLoad(argv[2], &playback, error, sizeof error),
        "invalid recording unexpectedly parsed");
  Check(playback.frames == NULL && playback.count == 0,
        "failed load leaked playback storage");

  (void)puts("Input playback parser tests passed");
  return EXIT_SUCCESS;
}
