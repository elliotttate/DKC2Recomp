#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Dkc2RewindHistory {
  uint8_t *storage;
  size_t snapshot_size;
  size_t capacity;
  size_t count;
  size_t write_index;
} Dkc2RewindHistory;

bool Dkc2RewindHistoryInit(Dkc2RewindHistory *history,
                           size_t snapshot_size, size_t capacity);
void Dkc2RewindHistoryDestroy(Dkc2RewindHistory *history);
bool Dkc2RewindHistoryPush(Dkc2RewindHistory *history,
                           const void *snapshot);
bool Dkc2RewindHistoryPop(Dkc2RewindHistory *history, void *snapshot);
