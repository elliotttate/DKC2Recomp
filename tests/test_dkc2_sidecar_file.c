#include "dkc2_sidecar_file.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef _WIN32
#include <unistd.h>
#endif

static int failures;

#define CHECK(condition)                                                  \
  do {                                                                    \
    if (!(condition)) {                                                   \
      fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,    \
              #condition);                                                \
      failures++;                                                         \
    }                                                                     \
  } while (0)

/* Fixtures are removed first and then created exclusively, so the setup
 * itself never follows a symlink some earlier run left at the name. */
static void WriteFixture(const char *name, const char *text) {
  remove(name);
  FILE *stream = fopen(name, "wbx");
  CHECK(stream != NULL);
  if (!stream) return;
  CHECK(fwrite(text, 1, strlen(text), stream) == strlen(text));
  CHECK(fclose(stream) == 0);
}

/* The whole file when it is shorter than `capacity`, or "" when missing. */
static void ReadAll(const char *name, char *out, size_t capacity) {
  memset(out, 0, capacity);
  FILE *stream = fopen(name, "rb");
  if (!stream) return;
  (void)fread(out, 1, capacity - 1, stream);
  fclose(stream);
}

static void TestWriteReplaces(void) {
  const char *path = "test_dkc2_sidecar.dat";
  remove(path);
  char text[32];
  CHECK(Dkc2SidecarWriteFile(path, "first", 5));
  ReadAll(path, text, sizeof text);
  CHECK(strcmp(text, "first") == 0);
  CHECK(Dkc2SidecarWriteFile(path, "second", 6)); /* replaces the file */
  ReadAll(path, text, sizeof text);
  CHECK(strcmp(text, "second") == 0);
  remove(path);
}

/* Earlier versions wrote and probed <path>.tmp with "wb". Whatever is
 * there now (a stale temporary, or a link to something else) is never
 * opened: writes and probes use fresh names created exclusively. */
static void TestLeavesExistingNamesAlone(void) {
  const char *path = "test_dkc2_sidecar_legacy.dat";
  const char *legacy = "test_dkc2_sidecar_legacy.dat.tmp";
  remove(path);
  WriteFixture(legacy, "keep");
  char text[32];
  CHECK(Dkc2SidecarCanCreate(path));
  CHECK(Dkc2SidecarWriteFile(path, "coins", 5));
  ReadAll(legacy, text, sizeof text);
  CHECK(strcmp(text, "keep") == 0);
  ReadAll(path, text, sizeof text);
  CHECK(strcmp(text, "coins") == 0);
  remove(legacy);
  remove(path);

#ifndef _WIN32
  /* A symlink planted at the old temporary name keeps its target intact. */
  const char *target = "test_dkc2_sidecar_target.dat";
  WriteFixture(target, "unrelated");
  remove(legacy);
  CHECK(symlink(target, legacy) == 0);
  CHECK(Dkc2SidecarCanCreate(path));
  CHECK(Dkc2SidecarWriteFile(path, "coins", 5));
  ReadAll(target, text, sizeof text);
  CHECK(strcmp(text, "unrelated") == 0);
  remove(legacy);
  remove(target);
  remove(path);
#endif
}

static void TestProbe(void) {
  const char *path = "test_dkc2_sidecar_probe.dat";
  remove(path);
  CHECK(Dkc2SidecarCanCreate(path));
  FILE *stream = fopen(path, "rb"); /* the probe made no sidecar */
  CHECK(stream == NULL);
  if (stream) fclose(stream);

  /* A parent that is a regular file can never hold the sidecar, whatever
   * an earlier run left behind. */
  const char *not_a_dir = "test_dkc2_sidecar_not_a_dir";
  WriteFixture(not_a_dir, "");
  CHECK(!Dkc2SidecarCanCreate("test_dkc2_sidecar_not_a_dir/side.dat"));
  CHECK(!Dkc2SidecarWriteFile("test_dkc2_sidecar_not_a_dir/side.dat", "x",
                              1));
  remove(not_a_dir);

  CHECK(!Dkc2SidecarCanCreate(""));
  CHECK(!Dkc2SidecarCanCreate(NULL));
  CHECK(!Dkc2SidecarWriteFile("", "x", 1));
  CHECK(!Dkc2SidecarWriteFile(path, NULL, 1));
}

int main(void) {
  TestWriteReplaces();
  TestLeavesExistingNamesAlone();
  TestProbe();
  if (failures) {
    fprintf(stderr, "%d check(s) failed\n", failures);
    return EXIT_FAILURE;
  }
  puts("dkc2_sidecar_file: all checks passed");
  return EXIT_SUCCESS;
}
