#ifndef DKC2_SIDECAR_FILE_H
#define DKC2_SIDECAR_FILE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Small host-side files kept beside save.srm (the KONG letters, Banana
 * Coins) are written through a temporary file that is then moved over the
 * target in one step, so a failed write keeps the previous file.
 *
 * The temporary file is always new: its name carries the process id and a
 * per-process counter, and it is created exclusively (O_CREAT | O_EXCL, or
 * C11's "wbx" on Windows). Exclusive creation never opens a file that is
 * already there and never follows a symlink planted at the name, so
 * neither a write nor a probe can truncate anything else. A name that is
 * taken (left by a crashed run, say) is skipped for the next one. */

/* Writes data to a fresh temporary beside path, then replaces path with
 * it. Returns false, leaving path as it was, when any step fails. */
bool Dkc2SidecarWriteFile(const char *path, const void *data, size_t size);

/* Whether Dkc2SidecarWriteFile could create its temporary beside path now:
 * makes one the same way and removes it. A missing or read-only parent
 * directory fails. */
bool Dkc2SidecarCanCreate(const char *path);

#ifdef __cplusplus
}
#endif

#endif
