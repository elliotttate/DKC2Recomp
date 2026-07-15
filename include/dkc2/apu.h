#ifndef DKC2_APU_H
#define DKC2_APU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DKC2_ARAM_SIZE ((size_t)64 * 1024)

typedef struct dkc2_apu dkc2_apu;

/* Creates a reset S-SMP/S-DSP instance with the built-in IPL ROM enabled. */
dkc2_apu *dkc2_apu_create(void);
void dkc2_apu_destroy(dkc2_apu *apu);
void dkc2_apu_reset(dkc2_apu *apu);

/* Runs complete SPC700 instructions until at least minimum_cycles elapsed. */
uint32_t dkc2_apu_run_cycles(dkc2_apu *apu, uint32_t minimum_cycles);
uint32_t dkc2_apu_cycle_count(const dkc2_apu *apu);

/* CPU-side views of SNES APUIO0-APUIO3. */
uint8_t dkc2_apu_cpu_read_port(const dkc2_apu *apu, unsigned port);
bool dkc2_apu_cpu_write_port(dkc2_apu *apu,
                             unsigned port,
                             uint8_t value);

uint8_t dkc2_apu_read_aram(const dkc2_apu *apu, uint16_t address);
bool dkc2_apu_copy_aram(const dkc2_apu *apu,
                        uint8_t *destination,
                        size_t size);

#ifdef __cplusplus
}
#endif

#endif
