#ifndef DKC2_DESKTOP_VSYNC_H
#define DKC2_DESKTOP_VSYNC_H

#include <stdbool.h>

typedef enum Dkc2DesktopVsyncStatus {
  kDkc2DesktopVsyncUnsupported = 0,
  kDkc2DesktopVsyncDisabled,
  kDkc2DesktopVsyncEnabled,
  kDkc2DesktopVsyncRequestFailed,
} Dkc2DesktopVsyncStatus;

typedef bool (*Dkc2DesktopSwapIntervalSetter)(void *user, int interval);

/* GPU presenters request one swap per display refresh. The host's exact SNES
 * frame deadline remains the emulation clock; swap synchronization only makes
 * each completed presentation atomic with respect to monitor scanout. */
Dkc2DesktopVsyncStatus Dkc2DesktopEnableVsync(
    Dkc2DesktopSwapIntervalSetter setter, void *user);

const char *Dkc2DesktopVsyncStatusName(Dkc2DesktopVsyncStatus status);

#endif
