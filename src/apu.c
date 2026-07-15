#include "dkc2/apu.h"

#include "apu.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct dkc2_apu {
    Apu *core;
};

dkc2_apu *dkc2_apu_create(void) {
    dkc2_apu *apu = (dkc2_apu *)calloc(1, sizeof(*apu));
    if (apu == NULL) {
        return NULL;
    }
    apu->core = apu_init();
    if (apu->core == NULL) {
        free(apu);
        return NULL;
    }
    apu_reset(apu->core);
    return apu;
}

void dkc2_apu_destroy(dkc2_apu *apu) {
    if (apu != NULL) {
        apu_free(apu->core);
        free(apu);
    }
}

void dkc2_apu_reset(dkc2_apu *apu) {
    if (apu != NULL && apu->core != NULL) {
        apu_reset(apu->core);
    }
}

uint32_t dkc2_apu_run_cycles(dkc2_apu *apu, uint32_t minimum_cycles) {
    uint32_t total = 0;
    if (apu == NULL || apu->core == NULL || minimum_cycles == 0) {
        return 0;
    }
    while (total < minimum_cycles) {
        uint32_t slice = minimum_cycles - total;
        int ran;
        if (slice > (uint32_t)INT_MAX) {
            slice = (uint32_t)INT_MAX;
        }
        ran = apu_runCycles(apu->core, (int)slice);
        if (ran <= 0) {
            break;
        }
        total += (uint32_t)ran;
    }
    return total;
}

uint32_t dkc2_apu_cycle_count(const dkc2_apu *apu) {
    return apu != NULL && apu->core != NULL ? apu->core->cycles : 0;
}

uint8_t dkc2_apu_cpu_read_port(const dkc2_apu *apu, unsigned port) {
    if (apu == NULL || apu->core == NULL || port >= 4U) {
        return 0;
    }
    return apu->core->outPorts[port];
}

bool dkc2_apu_cpu_write_port(dkc2_apu *apu,
                             unsigned port,
                             uint8_t value) {
    if (apu == NULL || apu->core == NULL || port >= 4U) {
        return false;
    }
    apu->core->inPorts[port] = value;
    return true;
}

uint8_t dkc2_apu_read_aram(const dkc2_apu *apu, uint16_t address) {
    if (apu == NULL || apu->core == NULL) {
        return 0;
    }
    return apu->core->ram[address];
}

bool dkc2_apu_copy_aram(const dkc2_apu *apu,
                        uint8_t *destination,
                        size_t size) {
    if (apu == NULL || apu->core == NULL || destination == NULL ||
        size != DKC2_ARAM_SIZE) {
        return false;
    }
    memcpy(destination, apu->core->ram, DKC2_ARAM_SIZE);
    return true;
}
