#pragma once

#include "cpu_state.h"

typedef enum Dkc2DeveloperResult {
  kDkc2DeveloperDisabled,
  kDkc2DeveloperUnknownCommand,
  kDkc2DeveloperUnavailable,
  kDkc2DeveloperExecuted
} Dkc2DeveloperResult;

/* Returns a function-key number (1..12), or zero when disabled. */
int Dkc2DeveloperBeatLevelKey(void);
/* Execute only between emulated frames. Never changes CPU registers/stack. */
Dkc2DeveloperResult Dkc2DeveloperCommand(CpuState *cpu, const char *command);
