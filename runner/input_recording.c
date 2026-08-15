#include "input_recording.h"

#include <stdio.h>
#include <string.h>

static void SetError(char *error, size_t error_size, const char *message,
                     const char *path) {
  if (!error || error_size == 0) return;
  if (path && *path)
    (void)snprintf(error, error_size, "%s: %s", message, path);
  else
    (void)snprintf(error, error_size, "%s", message);
}

bool Dkc2InputRecorderOpen(Dkc2InputRecorder *recorder, const char *path,
                           char *error, size_t error_size) {
  if (!recorder || !path || !*path) {
    SetError(error, error_size, "missing input recording path", NULL);
    return false;
  }
  memset(recorder, 0, sizeof *recorder);
  /* Binary mode preserves the documented LF-only recording format on
   * Windows, making the same controller stream byte-identical cross-platform. */
  recorder->stream = fopen(path, "wb");
  if (!recorder->stream) {
    SetError(error, error_size, "unable to create input recording", path);
    return false;
  }
  return true;
}

bool Dkc2InputRecorderWrite(Dkc2InputRecorder *recorder, uint32_t controller,
                            char *error, size_t error_size) {
  if (!recorder || !recorder->stream) {
    SetError(error, error_size, "input recorder is not open", NULL);
    return false;
  }
  if (fprintf(recorder->stream, "%06x\n",
              (unsigned)(controller & 0xffffffu)) < 0 ||
      fflush(recorder->stream) != 0) {
    SetError(error, error_size, "unable to write input recording", NULL);
    return false;
  }
  recorder->frames++;
  return true;
}

bool Dkc2InputRecorderClose(Dkc2InputRecorder *recorder,
                            char *error, size_t error_size) {
  if (!recorder) {
    SetError(error, error_size, "missing input recorder", NULL);
    return false;
  }
  if (!recorder->stream) return true;
  FILE *stream = recorder->stream;
  recorder->stream = NULL;
  if (fclose(stream) != 0) {
    SetError(error, error_size, "unable to close input recording", NULL);
    return false;
  }
  return true;
}

bool Dkc2InputRecorderIsOpen(const Dkc2InputRecorder *recorder) {
  return recorder && recorder->stream;
}
