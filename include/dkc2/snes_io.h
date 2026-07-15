#ifndef DKC2_SNES_IO_H
#define DKC2_SNES_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "dkc2/apu.h"
#include "dkc2/bus.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DKC2_VRAM_SIZE ((size_t)64 * 1024)
#define DKC2_CGRAM_SIZE ((size_t)512)
#define DKC2_OAM_SIZE ((size_t)544)

typedef enum dkc2_snes_barrier {
    DKC2_SNES_BARRIER_NONE,
    DKC2_SNES_BARRIER_APU,
    DKC2_SNES_BARRIER_UNSUPPORTED_READ,
    DKC2_SNES_BARRIER_UNSUPPORTED_WRITE,
    DKC2_SNES_BARRIER_UNSUPPORTED_DMA
} dkc2_snes_barrier;

/*
 * Bring-up model for CPU/PPU registers and general DMA. It is intentionally
 * not a complete timing, PPU, APU, or HDMA implementation.
 */
typedef struct dkc2_snes_io {
    dkc2_bus *bus;
    dkc2_apu *apu;
    uint8_t registers[0x4000];
    uint8_t vram[DKC2_VRAM_SIZE];
    uint8_t cgram[DKC2_CGRAM_SIZE];
    uint8_t oam[DKC2_OAM_SIZE];
    uint16_t vram_address;
    uint16_t cgram_address;
    uint16_t oam_address;
    bool cgram_high;
    bool stop_on_apu_after_dma;
    bool vram_clear_confirmed;
    uint64_t io_reads;
    uint64_t io_writes;
    uint64_t dma_transfers;
    uint64_t dma_bytes;
    uint32_t current_instruction;
    uint32_t barrier_address;
    uint32_t barrier_instruction;
    uint8_t barrier_value;
    dkc2_snes_barrier barrier;
} dkc2_snes_io;

bool dkc2_snes_io_init(dkc2_snes_io *io, dkc2_bus *bus);
void dkc2_snes_io_free(dkc2_snes_io *io);
void dkc2_snes_io_set_current_instruction(dkc2_snes_io *io,
                                           uint32_t address);
void dkc2_snes_io_stop_on_apu_after_dma(dkc2_snes_io *io, bool enabled);

bool dkc2_snes_io_read(void *context, uint32_t address, uint8_t *value);
bool dkc2_snes_io_write(void *context, uint32_t address, uint8_t value);

const char *dkc2_snes_barrier_name(dkc2_snes_barrier barrier);
bool dkc2_snes_vram_is_zero(const dkc2_snes_io *io);

#ifdef __cplusplus
}
#endif

#endif
