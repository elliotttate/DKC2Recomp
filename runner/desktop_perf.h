#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum Dkc2DesktopPerfPhase {
  kDkc2PerfInput = 0,
  kDkc2PerfEmulation,
  kDkc2PerfRewind,
  kDkc2PerfPpu,
  kDkc2PerfAudio,
  kDkc2PerfPresent,
  kDkc2PerfPace,
  kDkc2PerfPhaseCount,
} Dkc2DesktopPerfPhase;

typedef struct Dkc2DesktopPerfCounter {
  uint64_t interval_start;
  uint64_t phase_ticks[kDkc2PerfPhaseCount];
  uint32_t presented_frames;
} Dkc2DesktopPerfCounter;

typedef struct Dkc2DesktopPerfSample {
  double elapsed_seconds;
  double presented_fps;
  double phase_ms[kDkc2PerfPhaseCount];
  double active_ms;
  double main_thread_busy_percent;
  double untracked_ms;
  uint32_t presented_frames;
} Dkc2DesktopPerfSample;

void Dkc2DesktopPerfInit(Dkc2DesktopPerfCounter *counter, uint64_t now);
void Dkc2DesktopPerfAdd(Dkc2DesktopPerfCounter *counter,
                        Dkc2DesktopPerfPhase phase, uint64_t ticks);

/* Completes one sampling interval after at least one second. Phase values in
 * the returned sample are averages per presented host frame. Pace is excluded
 * from active_ms and main_thread_busy_percent because it is intentional idle
 * time. GPU time is deliberately not represented: the current gameplay
 * backend is GDI and has no GPU timestamp API. */
bool Dkc2DesktopPerfUpdate(Dkc2DesktopPerfCounter *counter, bool presented,
                           uint64_t now, uint64_t ticks_per_second,
                           Dkc2DesktopPerfSample *sample);
