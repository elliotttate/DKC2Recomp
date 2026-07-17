#include "desktop_rewind.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static void ExpectPop(Dkc2RewindHistory *history, uint32_t expected) {
  uint32_t actual = 0;
  if (!Dkc2RewindHistoryPop(history, &actual) || actual != expected) {
    fprintf(stderr, "expected rewind value %u, got %u\n",
            expected, actual);
    exit(EXIT_FAILURE);
  }
}

int main(void) {
  Dkc2RewindHistory history;
  if (!Dkc2RewindHistoryInit(&history, sizeof(uint32_t), 3))
    return EXIT_FAILURE;
  for (uint32_t value = 1; value <= 4; value++) {
    if (!Dkc2RewindHistoryPush(&history, &value)) return EXIT_FAILURE;
  }
  ExpectPop(&history, 4);
  ExpectPop(&history, 3);
  ExpectPop(&history, 2);
  uint32_t unused = 0;
  if (Dkc2RewindHistoryPop(&history, &unused)) {
    fputs("rewind history returned an overwritten snapshot\n", stderr);
    return EXIT_FAILURE;
  }
  Dkc2RewindHistoryDestroy(&history);
  puts("desktop rewind history tests passed");
  return EXIT_SUCCESS;
}
