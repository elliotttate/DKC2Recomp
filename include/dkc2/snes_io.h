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

enum {
    DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE = 1364,
    DKC2_NTSC_SCANLINES_PER_FRAME = 262,
    DKC2_NTSC_HBLANK_START = 1096,
    DKC2_NTSC_VBLANK_START = 225,
    DKC2_PROVISIONAL_MASTER_CYCLES_PER_CPU_ACCESS = 8,
    DKC2_AUTOJOY_MASTER_CYCLES = 4224,
    DKC2_MULTIPLY_MASTER_CYCLES = 48,
    DKC2_DIVIDE_MASTER_CYCLES = 96
};

enum {
    DKC2_BUTTON_B = 0x8000,
    DKC2_BUTTON_Y = 0x4000,
    DKC2_BUTTON_SELECT = 0x2000,
    DKC2_BUTTON_START = 0x1000,
    DKC2_BUTTON_UP = 0x0800,
    DKC2_BUTTON_DOWN = 0x0400,
    DKC2_BUTTON_LEFT = 0x0200,
    DKC2_BUTTON_RIGHT = 0x0100,
    DKC2_BUTTON_A = 0x0080,
    DKC2_BUTTON_X = 0x0040,
    DKC2_BUTTON_L = 0x0020,
    DKC2_BUTTON_R = 0x0010
};

typedef enum dkc2_snes_barrier {
    DKC2_SNES_BARRIER_NONE,
    DKC2_SNES_BARRIER_APU,
    DKC2_SNES_BARRIER_UNSUPPORTED_READ,
    DKC2_SNES_BARRIER_UNSUPPORTED_WRITE,
    DKC2_SNES_BARRIER_UNSUPPORTED_DMA
} dkc2_snes_barrier;

typedef struct dkc2_hdma_channel_state {
    uint16_t lines_remaining;
    bool active;
    bool repeat;
    bool transfer_this_line;
} dkc2_hdma_channel_state;

/*
 * Bring-up model for CPU/PPU registers, DMA/HDMA, provisional timing, and
 * controller input. It is intentionally not a complete or cycle-accurate
 * SNES implementation.
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
    uint32_t wram_address;
    uint8_t ppu_write_latch;
    int16_t mode7_a;
    int16_t mode7_b;
    int16_t mode7_c;
    int16_t mode7_d;
    int16_t mode7_x;
    int16_t mode7_y;
    int32_t mode7_product;
    bool cgram_high;
    bool stop_on_apu_after_dma;
    bool master_scheduler_enabled;
    bool vram_clear_confirmed;
    bool nmi_flag;
    bool nmi_pending;
    bool timeup_flag;
    bool joy_strobe;
    uint16_t controllers[2];
    uint16_t controller_shift[2];
    uint32_t autojoy_cycles_remaining;
    uint32_t cpu_math_cycles_remaining;
    uint16_t cpu_math_dividend;
    uint8_t cpu_math_divisor;
    bool cpu_math_is_division;
    uint16_t h_counter;
    uint16_t v_counter;
    int64_t apu_master_balance;
    uint64_t master_cycles;
    uint64_t frames;
    uint64_t io_reads;
    uint64_t io_writes;
    uint64_t dma_transfers;
    uint64_t dma_bytes;
    uint64_t hdma_transfers;
    uint64_t hdma_bytes;
    dkc2_hdma_channel_state hdma[8];
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

/*
 * Enables the provisional timing path used after the v0.4.0 APU checkpoint.
 * The scheduler itself consumes SNES master cycles. Until the CPU exposes
 * exact instruction cycles, the boot probe supplies eight master cycles for
 * each host-visible A-bus byte access.
 */
void dkc2_snes_io_enable_master_scheduler(dkc2_snes_io *io, bool enabled);
void dkc2_snes_io_advance_master_cycles(dkc2_snes_io *io,
                                         uint64_t master_cycles);
void dkc2_snes_io_advance_cpu_accesses(dkc2_snes_io *io,
                                        uint64_t accesses);

/* NMI is edge-latched; TIMEUP remains asserted until $4211 is read. */
bool dkc2_snes_io_take_nmi(dkc2_snes_io *io);
bool dkc2_snes_io_irq_pending(const dkc2_snes_io *io);
bool dkc2_snes_io_interrupt_source_enabled(const dkc2_snes_io *io);

/* SNES button bits in JOY1/JOY2 register order; port is zero or one. */
bool dkc2_snes_io_set_controller(dkc2_snes_io *io,
                                  unsigned port,
                                  uint16_t buttons);

bool dkc2_snes_io_read(void *context, uint32_t address, uint8_t *value);
bool dkc2_snes_io_write(void *context, uint32_t address, uint8_t value);

const char *dkc2_snes_barrier_name(dkc2_snes_barrier barrier);
bool dkc2_snes_vram_is_zero(const dkc2_snes_io *io);

#ifdef __cplusplus
}
#endif

#endif
