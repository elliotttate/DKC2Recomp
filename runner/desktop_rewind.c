#include "desktop_rewind.h"

#include <stdlib.h>
#include <string.h>

bool Dkc2RewindHistoryInit(Dkc2RewindHistory *history,
                           size_t snapshot_size, size_t capacity) {
  if (!history || snapshot_size == 0 || capacity == 0 ||
      snapshot_size > SIZE_MAX / capacity)
    return false;
  memset(history, 0, sizeof *history);
  history->storage = (uint8_t *)malloc(snapshot_size * capacity);
  if (!history->storage) return false;
  history->snapshot_size = snapshot_size;
  history->capacity = capacity;
  return true;
}

void Dkc2RewindHistoryDestroy(Dkc2RewindHistory *history) {
  if (!history) return;
  free(history->storage);
  memset(history, 0, sizeof *history);
}

bool Dkc2RewindHistoryPush(Dkc2RewindHistory *history,
                           const void *snapshot) {
  if (!history || !history->storage || !snapshot) return false;
  memcpy(history->storage + history->write_index * history->snapshot_size,
         snapshot, history->snapshot_size);
  history->write_index = (history->write_index + 1) % history->capacity;
  if (history->count < history->capacity) history->count++;
  return true;
}

bool Dkc2RewindHistoryPop(Dkc2RewindHistory *history, void *snapshot) {
  if (!history || !history->storage || !snapshot || history->count == 0)
    return false;
  history->write_index =
      (history->write_index + history->capacity - 1) % history->capacity;
  memcpy(snapshot,
         history->storage + history->write_index * history->snapshot_size,
         history->snapshot_size);
  history->count--;
  return true;
}
