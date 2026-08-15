#include "input_recording.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition) do { \
  if (!(condition)) { \
    fprintf(stderr, "check failed at line %d: %s\n", __LINE__, #condition); \
    return 1; \
  } \
} while (0)

int main(int argc, char **argv) {
  CHECK(argc == 3);
  const char *valid_path = argv[1];
  const char *missing_parent_path = argv[2];
  (void)remove(valid_path);

  Dkc2InputRecorder recorder = {0};
  char error[256] = {0};
  CHECK(!Dkc2InputRecorderOpen(
      &recorder, missing_parent_path, error, sizeof error));
  CHECK(strstr(error, "unable to create input recording") != NULL);
  CHECK(!Dkc2InputRecorderIsOpen(&recorder));

  error[0] = '\0';
  CHECK(Dkc2InputRecorderOpen(&recorder, valid_path, error, sizeof error));
  CHECK(Dkc2InputRecorderIsOpen(&recorder));
  CHECK(Dkc2InputRecorderWrite(&recorder, 0x001234u, error, sizeof error));
  CHECK(Dkc2InputRecorderWrite(&recorder, 0xabcdefu, error, sizeof error));
  CHECK(recorder.frames == 2);
  CHECK(Dkc2InputRecorderClose(&recorder, error, sizeof error));
  CHECK(!Dkc2InputRecorderIsOpen(&recorder));

  FILE *stream = fopen(valid_path, "rb");
  CHECK(stream != NULL);
  char contents[32] = {0};
  size_t size = fread(contents, 1, sizeof contents - 1, stream);
  CHECK(fclose(stream) == 0);
  CHECK(size == strlen("001234\nabcdef\n"));
  CHECK(strcmp(contents, "001234\nabcdef\n") == 0);
  CHECK(remove(valid_path) == 0);
  return 0;
}
