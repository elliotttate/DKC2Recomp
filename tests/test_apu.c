#include "dkc2/apu.h"

#include <stdio.h>
#include <stdlib.h>

static void fail(const char *message) {
    (void)fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void wait_for_port(dkc2_apu *apu,
                          unsigned port,
                          uint8_t expected,
                          const char *message) {
    unsigned attempts;
    for (attempts = 0; attempts < 100000U; ++attempts) {
        (void)dkc2_apu_run_cycles(apu, 1);
        if (dkc2_apu_cpu_read_port(apu, port) == expected) {
            return;
        }
    }
    fail(message);
}

int main(void) {
    dkc2_apu *apu = dkc2_apu_create();
    if (apu == NULL) {
        fail("cannot allocate APU test fixture");
    }

    wait_for_port(apu, 0, 0xAA, "SPC700 IPL did not publish $AA");
    wait_for_port(apu, 1, 0xBB, "SPC700 IPL did not publish $BB");

    /* Ask the IPL to receive two synthetic bytes at ARAM $0200. */
    if (!dkc2_apu_cpu_write_port(apu, 2, 0x00) ||
        !dkc2_apu_cpu_write_port(apu, 3, 0x02) ||
        !dkc2_apu_cpu_write_port(apu, 1, 0x01) ||
        !dkc2_apu_cpu_write_port(apu, 0, 0xCC)) {
        fail("cannot write CPU-to-APU ports");
    }
    wait_for_port(apu, 0, 0xCC, "SPC700 IPL did not acknowledge $CC");

    (void)dkc2_apu_cpu_write_port(apu, 1, 0x42);
    (void)dkc2_apu_cpu_write_port(apu, 0, 0x00);
    wait_for_port(apu, 0, 0x00, "SPC700 IPL did not acknowledge byte 0");
    (void)dkc2_apu_run_cycles(apu, 16);
    if (dkc2_apu_read_aram(apu, 0x0200) != 0x42) {
        fail("SPC700 IPL stored byte 0 at the wrong ARAM value/address");
    }

    (void)dkc2_apu_cpu_write_port(apu, 1, 0x99);
    (void)dkc2_apu_cpu_write_port(apu, 0, 0x01);
    wait_for_port(apu, 0, 0x01, "SPC700 IPL did not acknowledge byte 1");
    (void)dkc2_apu_run_cycles(apu, 16);
    if (dkc2_apu_read_aram(apu, 0x0201) != 0x99 ||
        dkc2_apu_cycle_count(apu) == 0) {
        fail("SPC700 IPL byte transfer state is incorrect");
    }

    dkc2_apu_destroy(apu);
    (void)puts("SPC700 IPL and CPU/APU port tests passed");
    return EXIT_SUCCESS;
}
