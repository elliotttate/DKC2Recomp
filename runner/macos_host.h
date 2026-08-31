#ifndef DKC2_MACOS_HOST_H
#define DKC2_MACOS_HOST_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum {
  kDkc2MacCommandToggleOverlay = 1u << 0,
  kDkc2MacCommandQuickSave = 1u << 1,
  kDkc2MacCommandQuickLoad = 1u << 2,
  kDkc2MacCommandToggleFullscreen = 1u << 3,
  kDkc2MacCommandFilterNearest = 1u << 4,
  kDkc2MacCommandFilterBilinear = 1u << 5,
  kDkc2MacCommandAspectNative = 1u << 6,
  kDkc2MacCommandAspect16x10 = 1u << 7,
  kDkc2MacCommandAspect16x9 = 1u << 8,
  kDkc2MacCommandQuit = 1u << 9,
};

/* Select a writable per-user directory and return the absolute launcher asset
 * path. DKC2_PORTABLE=1 keeps the executable-adjacent development behavior;
 * DKC2_USER_DIR provides an explicit isolated directory for automation. */
bool Dkc2MacPrepareRuntimeDirectory(char *assets_path,
                                    size_t assets_capacity,
                                    char *error,
                                    size_t error_capacity);

void Dkc2MacInstallMenu(void);
uint32_t Dkc2MacTakeCommands(void);
void Dkc2MacUpdateMenu(bool fullscreen, bool linear_filter, int aspect);

/* Wait one relative interval on an absolute Mach target. The final 1.5 ms is
 * a bounded CPU spin so scheduler coalescing cannot turn a stable deadline
 * into alternating early/late presentation intervals. */
void Dkc2MacWaitSeconds(double seconds);

#endif
