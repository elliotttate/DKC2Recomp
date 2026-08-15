#include "input_playback.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void SetError(char *error, size_t error_size, const char *message,
                     long line) {
  if (!error || error_size == 0) return;
  if (line > 0)
    (void)snprintf(error, error_size, "line %ld: %s", line, message);
  else
    (void)snprintf(error, error_size, "%s", message);
}

static char *Trim(char *text) {
  while (*text && isspace((unsigned char)*text)) text++;
  char *end = text + strlen(text);
  while (end > text && isspace((unsigned char)end[-1])) *--end = '\0';
  return text;
}

static int AppendFrames(Dkc2InputPlayback *playback, uint32_t mask,
                        unsigned long repeat, char *error,
                        size_t error_size, long line) {
  if (repeat == 0 || repeat > 1000000ul) {
    SetError(error, error_size, "repeat count must be 1..1000000", line);
    return 0;
  }
  if (playback->count > (SIZE_MAX / sizeof playback->frames[0]) - repeat) {
    SetError(error, error_size, "recording is too large", line);
    return 0;
  }
  size_t next_count = playback->count + (size_t)repeat;
  uint32_t *next = (uint32_t *)realloc(
      playback->frames, next_count * sizeof playback->frames[0]);
  if (!next) {
    SetError(error, error_size, "out of memory", line);
    return 0;
  }
  playback->frames = next;
  for (size_t i = playback->count; i < next_count; i++)
    playback->frames[i] = mask;
  playback->count = next_count;
  return 1;
}

int Dkc2InputPlaybackLoad(const char *path, Dkc2InputPlayback *playback,
                          char *error, size_t error_size) {
  if (!path || !*path || !playback) {
    SetError(error, error_size, "missing playback path", 0);
    return 0;
  }
  playback->frames = NULL;
  playback->count = 0;
  FILE *stream = fopen(path, "r");
  if (!stream) {
    SetError(error, error_size, "unable to open playback file", 0);
    return 0;
  }

  char line_text[256];
  long line = 0;
  while (fgets(line_text, sizeof line_text, stream)) {
    line++;
    char *comment = strpbrk(line_text, "#;");
    if (comment) *comment = '\0';
    char *line_start = Trim(line_text);
    if (!*line_start) continue;

    errno = 0;
    char *end = NULL;
    unsigned long mask = strtoul(line_start, &end, 16);
    if (errno != 0 || end == line_start || mask > 0xfffffful) {
      SetError(error, error_size, "expected 1-6 digit hex input mask", line);
      (void)fclose(stream);
      Dkc2InputPlaybackFree(playback);
      return 0;
    }
    end = Trim(end);

    unsigned long repeat = 1;
    if (*end) {
      if (*end == '*') end = Trim(end + 1);
      errno = 0;
      char *repeat_end = NULL;
      repeat = strtoul(end, &repeat_end, 10);
      repeat_end = Trim(repeat_end);
      if (errno != 0 || repeat_end == end || *repeat_end) {
        SetError(error, error_size,
                 "expected optional decimal repeat count", line);
        (void)fclose(stream);
        Dkc2InputPlaybackFree(playback);
        return 0;
      }
    }
    if (!AppendFrames(playback, (uint32_t)mask, repeat, error, error_size,
                      line)) {
      (void)fclose(stream);
      Dkc2InputPlaybackFree(playback);
      return 0;
    }
  }

  if (ferror(stream)) {
    SetError(error, error_size, "unable to read playback file", 0);
    (void)fclose(stream);
    Dkc2InputPlaybackFree(playback);
    return 0;
  }
  if (fclose(stream) != 0) {
    SetError(error, error_size, "unable to close playback file", 0);
    Dkc2InputPlaybackFree(playback);
    return 0;
  }
  return 1;
}

void Dkc2InputPlaybackFree(Dkc2InputPlayback *playback) {
  if (!playback) return;
  free(playback->frames);
  playback->frames = NULL;
  playback->count = 0;
}

uint32_t Dkc2InputPlaybackFrame(const Dkc2InputPlayback *playback,
                                size_t frame) {
  if (!playback || frame >= playback->count) return 0;
  return playback->frames[frame];
}
