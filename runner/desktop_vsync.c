#include "desktop_vsync.h"

Dkc2DesktopVsyncStatus Dkc2DesktopEnableVsync(
    Dkc2DesktopSwapIntervalSetter setter, void *user) {
  if (!setter) return kDkc2DesktopVsyncUnsupported;
  return setter(user, 1) ? kDkc2DesktopVsyncEnabled
                         : kDkc2DesktopVsyncRequestFailed;
}

const char *Dkc2DesktopVsyncStatusName(Dkc2DesktopVsyncStatus status) {
  switch (status) {
    case kDkc2DesktopVsyncDisabled:
      return "off";
    case kDkc2DesktopVsyncEnabled:
      return "on";
    case kDkc2DesktopVsyncRequestFailed:
      return "request-failed";
    case kDkc2DesktopVsyncUnsupported:
    default:
      return "unsupported";
  }
}
