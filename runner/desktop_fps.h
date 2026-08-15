#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct Dkc2DesktopFpsCounter {
  uint64_t interval_start;
  uint32_t presented_frames;
} Dkc2DesktopFpsCounter;

void Dkc2DesktopFpsInit(Dkc2DesktopFpsCounter *counter, uint64_t now);

/* Records one host presentation when presented is true. Returns true once a
 * wall-clock interval of at least one second has completed and writes the
 * rounded presentation rate to fps. */
bool Dkc2DesktopFpsUpdate(Dkc2DesktopFpsCounter *counter, bool presented,
                          uint64_t now, uint64_t ticks_per_second,
                          unsigned *fps);
