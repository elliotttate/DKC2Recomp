/* open, fdopen, close and getpid are POSIX, not C11: without this a strict
 * -std=c11 build leaves fdopen undeclared, and an implicit int return
 * would truncate the FILE pointer on 64-bit hosts. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "dkc2_sidecar_file.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

/* Names that are taken are skipped. Each name carries this process's id
 * and a counter that only grows, so stale files from other runs collide
 * only by chance; the limit just stops a directory that refuses every
 * name from looping forever. */
enum { kSidecarAttempts = 1024 };

static unsigned long SidecarProcessId(void) {
#ifdef _WIN32
  return (unsigned long)GetCurrentProcessId();
#else
  return (unsigned long)getpid();
#endif
}

/* Creates `name` only if nothing (file, directory or symlink) is there. */
static FILE *SidecarOpenExclusive(const char *name) {
#ifdef _WIN32
  return fopen(name, "wbx");
#else
  const int fd = open(name, O_WRONLY | O_CREAT | O_EXCL, 0666);
  if (fd < 0) return NULL;
  FILE *stream = fdopen(fd, "wb");
  if (!stream) {
    const int saved = errno;
    close(fd);
    remove(name);
    errno = saved;
  }
  return stream;
#endif
}

/* A new temporary beside path, created exclusively, or NULL. On success
 * *name_out holds its name, which the caller frees. */
static FILE *SidecarCreateTemporary(const char *path, char **name_out) {
  static uint32_t counter;
  *name_out = NULL;
  if (!path || !*path) return NULL;
  /* ".<pid>-<counter>.tmp": two 32-bit-plus values in hex, at most
   * 16 + 8 digits on any host, plus the punctuation. */
  const size_t capacity = strlen(path) + 40;
  char *name = (char *)malloc(capacity);
  if (!name) return NULL;
  const unsigned long pid = SidecarProcessId();
  for (unsigned attempt = 0; attempt < kSidecarAttempts; attempt++) {
    snprintf(name, capacity, "%s.%lx-%08lx.tmp", path, pid,
             (unsigned long)counter++);
    errno = 0;
    FILE *stream = SidecarOpenExclusive(name);
    if (stream) {
      *name_out = name;
      return stream;
    }
    if (errno != EEXIST) break;
  }
  free(name);
  return NULL;
}

/* Replace `path` with `temporary` in one step, so a failure leaves the old
 * file in place. POSIX rename() already replaces atomically; Windows
 * rename() refuses an existing target. */
static bool SidecarReplace(const char *temporary, const char *path) {
#ifdef _WIN32
  return MoveFileExA(temporary, path,
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
  return rename(temporary, path) == 0;
#endif
}

bool Dkc2SidecarWriteFile(const char *path, const void *data, size_t size) {
  if (!data && size) return false;
  char *temporary = NULL;
  FILE *stream = SidecarCreateTemporary(path, &temporary);
  if (!stream) return false;
  bool ok = size == 0 || fwrite(data, 1, size, stream) == size;
  if (fclose(stream) != 0) ok = false;
  if (ok) ok = SidecarReplace(temporary, path);
  if (!ok) remove(temporary);
  free(temporary);
  return ok;
}

bool Dkc2SidecarCanCreate(const char *path) {
  char *temporary = NULL;
  FILE *stream = SidecarCreateTemporary(path, &temporary);
  if (!stream) return false;
  const bool ok = fclose(stream) == 0;
  remove(temporary);
  free(temporary);
  return ok;
}
