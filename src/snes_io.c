#include "dkc2/snes_io.h"

#include <string.h>

enum {
    /* One complete SPC opcode is the smallest step exposed by this core. */
    DKC2_APU_PORT_SLICE_CYCLES = 1
};

static size_t register_index(uint16_t address) {
    return (size_t)(address - UINT16_C(0x2000));
}

static void set_barrier(dkc2_snes_io *io,
                        dkc2_snes_barrier barrier,
                        uint32_t address,
                        uint8_t value) {
    if (io->barrier == DKC2_SNES_BARRIER_NONE) {
        io->barrier = barrier;
        io->barrier_address = address & UINT32_C(0xFFFFFF);
        io->barrier_instruction = io->current_instruction;
        io->barrier_value = value;
    }
}

bool dkc2_snes_io_init(dkc2_snes_io *io, dkc2_bus *bus) {
    if (io == NULL || bus == NULL) {
        return false;
    }
    memset(io, 0, sizeof(*io));
    io->bus = bus;
    io->apu = dkc2_apu_create();
    if (io->apu == NULL) {
        memset(io, 0, sizeof(*io));
        return false;
    }
    return true;
}

void dkc2_snes_io_free(dkc2_snes_io *io) {
    if (io != NULL) {
        dkc2_apu_destroy(io->apu);
        memset(io, 0, sizeof(*io));
    }
}

void dkc2_snes_io_set_current_instruction(dkc2_snes_io *io,
                                           uint32_t address) {
    if (io != NULL) {
        io->current_instruction = address & UINT32_C(0xFFFFFF);
    }
}

void dkc2_snes_io_stop_on_apu_after_dma(dkc2_snes_io *io, bool enabled) {
    if (io != NULL) {
        io->stop_on_apu_after_dma = enabled;
    }
}

static uint16_t vram_remap(uint16_t address, uint8_t control) {
    switch ((control >> 2) & UINT8_C(0x03)) {
        case 1:
            return (uint16_t)((address & UINT16_C(0xFF00)) |
                              ((address & UINT16_C(0x001F)) << 3) |
                              ((address & UINT16_C(0x00E0)) >> 5));
        case 2:
            return (uint16_t)((address & UINT16_C(0xFE00)) |
                              ((address & UINT16_C(0x003F)) << 3) |
                              ((address & UINT16_C(0x01C0)) >> 6));
        case 3:
            return (uint16_t)((address & UINT16_C(0xFC00)) |
                              ((address & UINT16_C(0x007F)) << 3) |
                              ((address & UINT16_C(0x0380)) >> 7));
        default:
            return address;
    }
}

static uint16_t vram_increment(uint8_t control) {
    switch (control & UINT8_C(0x03)) {
        case 1:
            return UINT16_C(32);
        case 2:
        case 3:
            return UINT16_C(128);
        default:
            return UINT16_C(1);
    }
}

static void increment_vram_address(dkc2_snes_io *io) {
    uint8_t control = io->registers[register_index(UINT16_C(0x2115))];
    io->vram_address =
        (uint16_t)(io->vram_address + vram_increment(control));
}

static void ppu_write(dkc2_snes_io *io,
                      uint16_t address,
                      uint8_t value) {
    uint8_t control;
    uint16_t mapped;
    size_t byte_address;

    io->registers[register_index(address)] = value;
    switch (address) {
        case 0x2102:
            io->oam_address =
                (uint16_t)((io->oam_address & UINT16_C(0x0100)) | value);
            break;
        case 0x2103:
            io->oam_address =
                (uint16_t)((io->oam_address & UINT16_C(0x00FF)) |
                           ((uint16_t)(value & UINT8_C(1)) << 8));
            break;
        case 0x2104:
            io->oam[io->oam_address % DKC2_OAM_SIZE] = value;
            io->oam_address = (uint16_t)(io->oam_address + UINT16_C(1));
            break;
        case 0x2116:
            io->vram_address =
                (uint16_t)((io->vram_address & UINT16_C(0xFF00)) | value);
            break;
        case 0x2117:
            io->vram_address =
                (uint16_t)((io->vram_address & UINT16_C(0x00FF)) |
                           ((uint16_t)value << 8));
            break;
        case 0x2118:
        case 0x2119:
            control = io->registers[register_index(UINT16_C(0x2115))];
            mapped = vram_remap(io->vram_address, control);
            byte_address = ((size_t)mapped << 1) |
                           (address == UINT16_C(0x2119) ? 1U : 0U);
            io->vram[byte_address & (DKC2_VRAM_SIZE - 1)] = value;
            if (((control & UINT8_C(0x80)) == 0) ==
                (address == UINT16_C(0x2118))) {
                increment_vram_address(io);
            }
            break;
        case 0x2121:
            io->cgram_address = value;
            io->cgram_high = false;
            break;
        case 0x2122:
            byte_address = ((size_t)io->cgram_address << 1) |
                           (io->cgram_high ? 1U : 0U);
            io->cgram[byte_address & (DKC2_CGRAM_SIZE - 1)] =
                io->cgram_high ? (uint8_t)(value & UINT8_C(0x7F)) : value;
            if (io->cgram_high) {
                io->cgram_address =
                    (uint16_t)((io->cgram_address + UINT16_C(1)) &
                               UINT16_C(0x00FF));
            }
            io->cgram_high = !io->cgram_high;
            break;
        default:
            break;
    }
}

static unsigned dma_pattern(uint8_t mode, uint32_t index) {
    static const uint8_t lengths[8] = {1, 2, 2, 4, 4, 4, 2, 4};
    static const uint8_t patterns[8][4] = {
        {0, 0, 0, 0},
        {0, 1, 0, 1},
        {0, 0, 0, 0},
        {0, 0, 1, 1},
        {0, 1, 2, 3},
        {0, 1, 0, 1},
        {0, 0, 0, 0},
        {0, 0, 1, 1}
    };
    mode &= UINT8_C(7);
    return patterns[mode][index % lengths[mode]];
}

static void run_dma_channel(dkc2_snes_io *io, unsigned channel) {
    uint16_t base = (uint16_t)(UINT16_C(0x4300) + channel * 0x10U);
    uint8_t control = io->registers[register_index(base)];
    uint8_t b_address =
        io->registers[register_index((uint16_t)(base + 1U))];
    uint16_t a_offset =
        (uint16_t)(io->registers[register_index((uint16_t)(base + 2U))] |
                   ((uint16_t)io->registers[
                        register_index((uint16_t)(base + 3U))]
                    << 8));
    uint8_t a_bank =
        io->registers[register_index((uint16_t)(base + 4U))];
    uint16_t size =
        (uint16_t)(io->registers[register_index((uint16_t)(base + 5U))] |
                   ((uint16_t)io->registers[
                        register_index((uint16_t)(base + 6U))]
                    << 8));
    uint32_t remaining = size == 0 ? UINT32_C(65536) : size;
    uint32_t transferred = 0;

    while (remaining != 0 && io->barrier == DKC2_SNES_BARRIER_NONE) {
        uint32_t a_address = ((uint32_t)a_bank << 16) | a_offset;
        uint16_t ppu_address =
            (uint16_t)(UINT16_C(0x2100) |
                       (uint8_t)(b_address +
                                 dma_pattern(control, transferred)));
        uint8_t value;

        if ((control & UINT8_C(0x80)) != 0) {
            set_barrier(io,
                        DKC2_SNES_BARRIER_UNSUPPORTED_DMA,
                        ppu_address,
                        control);
            break;
        }
        value = dkc2_bus_read8(io->bus, a_address);
        ppu_write(io, ppu_address, value);
        io->bus->open_bus = value;

        if ((control & UINT8_C(0x08)) == 0) {
            if ((control & UINT8_C(0x10)) != 0) {
                a_offset = (uint16_t)(a_offset - UINT16_C(1));
            } else {
                a_offset = (uint16_t)(a_offset + UINT16_C(1));
            }
        }
        --remaining;
        ++transferred;
    }

    io->registers[register_index((uint16_t)(base + 2U))] =
        (uint8_t)a_offset;
    io->registers[register_index((uint16_t)(base + 3U))] =
        (uint8_t)(a_offset >> 8);
    io->registers[register_index((uint16_t)(base + 5U))] = 0;
    io->registers[register_index((uint16_t)(base + 6U))] = 0;
    ++io->dma_transfers;
    io->dma_bytes += transferred;
}

static void run_dma(dkc2_snes_io *io, uint8_t channels) {
    unsigned channel;
    for (channel = 0; channel < 8U; ++channel) {
        if ((channels & (uint8_t)(UINT8_C(1) << channel)) != 0) {
            run_dma_channel(io, channel);
            if (io->barrier != DKC2_SNES_BARRIER_NONE) {
                return;
            }
        }
    }
}

bool dkc2_snes_io_read(void *context,
                       uint32_t address,
                       uint8_t *value) {
    dkc2_snes_io *io = (dkc2_snes_io *)context;
    uint16_t offset;

    if (io == NULL || value == NULL) {
        return false;
    }
    ++io->io_reads;
    offset = (uint16_t)address;
    if (offset == UINT16_C(0x213F)) {
        *value = 0; /* NTSC region bit. */
        return true;
    }
    if (offset >= UINT16_C(0x2140) && offset <= UINT16_C(0x2143)) {
        if (io->stop_on_apu_after_dma && io->dma_transfers != 0) {
            *value = 0;
            set_barrier(io, DKC2_SNES_BARRIER_APU, address, *value);
        } else {
            (void)dkc2_apu_run_cycles(io->apu,
                                      DKC2_APU_PORT_SLICE_CYCLES);
            *value = dkc2_apu_cpu_read_port(
                io->apu,
                (unsigned)(offset - UINT16_C(0x2140)));
        }
        return true;
    }

    *value = 0;
    set_barrier(io,
                DKC2_SNES_BARRIER_UNSUPPORTED_READ,
                address,
                *value);
    return true;
}

bool dkc2_snes_io_write(void *context,
                        uint32_t address,
                        uint8_t value) {
    dkc2_snes_io *io = (dkc2_snes_io *)context;
    uint16_t offset;

    if (io == NULL) {
        return false;
    }
    ++io->io_writes;
    offset = (uint16_t)address;
    if (offset >= UINT16_C(0x2100) && offset <= UINT16_C(0x213F)) {
        ppu_write(io, offset, value);
        return true;
    }
    if (offset >= UINT16_C(0x2140) && offset <= UINT16_C(0x2143)) {
        io->registers[register_index(offset)] = value;
        if (io->stop_on_apu_after_dma && io->dma_transfers != 0) {
            set_barrier(io, DKC2_SNES_BARRIER_APU, address, value);
        } else {
            (void)dkc2_apu_cpu_write_port(
                io->apu,
                (unsigned)(offset - UINT16_C(0x2140)),
                value);
            (void)dkc2_apu_run_cycles(io->apu,
                                      DKC2_APU_PORT_SLICE_CYCLES);
        }
        return true;
    }
    if ((offset >= UINT16_C(0x4200) && offset <= UINT16_C(0x420D)) ||
        (offset >= UINT16_C(0x4300) && offset <= UINT16_C(0x437F))) {
        io->registers[register_index(offset)] = value;
        if (offset == UINT16_C(0x420B) && value != 0) {
            run_dma(io, value);
            if (!io->vram_clear_confirmed &&
                dkc2_snes_vram_is_zero(io)) {
                io->vram_clear_confirmed = true;
            }
        }
        return true;
    }

    set_barrier(io,
                DKC2_SNES_BARRIER_UNSUPPORTED_WRITE,
                address,
                value);
    return true;
}

const char *dkc2_snes_barrier_name(dkc2_snes_barrier barrier) {
    switch (barrier) {
        case DKC2_SNES_BARRIER_NONE:
            return "none";
        case DKC2_SNES_BARRIER_APU:
            return "APU/SPC700 communication required";
        case DKC2_SNES_BARRIER_UNSUPPORTED_READ:
            return "unsupported I/O read";
        case DKC2_SNES_BARRIER_UNSUPPORTED_WRITE:
            return "unsupported I/O write";
        case DKC2_SNES_BARRIER_UNSUPPORTED_DMA:
            return "unsupported DMA direction or target";
    }
    return "unknown";
}

bool dkc2_snes_vram_is_zero(const dkc2_snes_io *io) {
    size_t index;
    if (io == NULL) {
        return false;
    }
    for (index = 0; index < DKC2_VRAM_SIZE; ++index) {
        if (io->vram[index] != 0) {
            return false;
        }
    }
    return true;
}
