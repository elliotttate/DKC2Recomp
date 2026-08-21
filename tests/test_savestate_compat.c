#include "common_cpu_infra.h"

#include <stdio.h>
#include <stdlib.h>

int main(void) {
  RtlGameInfo legacy_compatible = {0};
  RtlGameInfo dkc2 = {0};
  dkc2.minimum_save_state_version = 7;

  if (!RtlGameAcceptsSaveStateVersion(NULL, 4) ||
      !RtlGameAcceptsSaveStateVersion(&legacy_compatible, 4) ||
      RtlGameAcceptsSaveStateVersion(&dkc2, 6) ||
      !RtlGameAcceptsSaveStateVersion(&dkc2, 7) ||
      !RtlGameAcceptsSaveStateVersion(&dkc2, 8)) {
    fputs("save-state compatibility gate failed\n", stderr);
    return EXIT_FAILURE;
  }
  puts("save-state compatibility gate tests passed");
  return EXIT_SUCCESS;
}
