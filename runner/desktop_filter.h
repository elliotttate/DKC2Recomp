#ifndef DKC2_DESKTOP_FILTER_H
#define DKC2_DESKTOP_FILTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum Dkc2DesktopScreenFilter {
  kDkc2ScreenRaw = 0,
  kDkc2ScreenCrt = 1,
  kDkc2ScreenComposite = 2,
  kDkc2ScreenTrinitron = 3,
  kDkc2ScreenFilterCount = 4,
} Dkc2DesktopScreenFilter;

typedef struct Dkc2DesktopColorFilter {
  int screen_kind;
} Dkc2DesktopColorFilter;

bool Dkc2DesktopScreenFilterValid(int filter);
const char *Dkc2DesktopScreenFilterName(int filter);
bool Dkc2DesktopScreenFilterFromName(const char *name, int *filter);
bool Dkc2DesktopColorFilterInit(Dkc2DesktopColorFilter *filter,
                                int screen_kind);
void Dkc2DesktopColorFilterDestroy(Dkc2DesktopColorFilter *filter);

/* Returns source unchanged for Raw. For an opted-in screen model, writes a
 * present-only BGRX8888 frame to destination and returns destination. */
const uint8_t *Dkc2DesktopColorFilterApply(
    const Dkc2DesktopColorFilter *filter, const uint8_t *source,
    uint8_t *destination, size_t pixel_count);

#endif
