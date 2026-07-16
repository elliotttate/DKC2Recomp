#include "dkc2/snes_io.h"

#include <string.h>

enum {
    /* One complete SPC opcode is the smallest step exposed by this core. */
    DKC2_APU_PORT_SLICE_CYCLES = 1,
    /* 21.47727 MHz master clock divided by the nominal 1.024 MHz S-SMP. */
    DKC2_MASTER_CYCLES_PER_APU_CYCLE = 21,
    DKC2_PPU_FEATURE_MOSAIC = 0x01,
    DKC2_PPU_FEATURE_WINDOW = 0x02,
    DKC2_PPU_FEATURE_SUBSCREEN = 0x04,
    DKC2_PPU_FEATURE_COLOR_MATH = 0x08,
    DKC2_PPU_FEATURE_DIRECT_COLOR = 0x10,
    DKC2_PPU_FEATURE_EXTBG = 0x20,
    DKC2_PPU_FEATURE_HIRES = 0x40,
    DKC2_PPU_FEATURE_INTERLACE = 0x80
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
        dkc2_ppu_renderer_free(&io->renderer);
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

void dkc2_snes_io_enable_master_scheduler(dkc2_snes_io *io, bool enabled) {
    if (io != NULL) {
        io->master_scheduler_enabled = enabled;
        io->apu_master_balance = 0;
    }
}

bool dkc2_snes_io_enable_renderer(dkc2_snes_io *io, bool enabled) {
    if (io == NULL) {
        return false;
    }
    if (enabled && !io->renderer_enabled) {
        if (!dkc2_ppu_renderer_init(&io->renderer)) {
            return false;
        }
    } else if (!enabled && io->renderer_enabled) {
        dkc2_ppu_renderer_free(&io->renderer);
    }
    io->renderer_enabled = enabled;
    return true;
}

bool dkc2_snes_io_set_controller(dkc2_snes_io *io,
                                  unsigned port,
                                  uint16_t buttons) {
    if (io == NULL || port >= 2U) {
        return false;
    }
    io->controllers[port] = buttons;
    if (io->joy_strobe) {
        io->controller_shift[port] = buttons;
    }
    return true;
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

static void update_mode7_product(dkc2_snes_io *io) {
    int8_t multiplier =
        (int8_t)((uint16_t)io->mode7_b >> 8);
    io->mode7_product = (int32_t)io->mode7_a * multiplier;
}

static void write_mode7_matrix(dkc2_snes_io *io,
                               uint16_t address,
                               uint8_t value) {
    int16_t matrix = (int16_t)((uint16_t)io->ppu_write_latch |
                               ((uint16_t)value << 8));
    switch (address) {
        case 0x211B:
            io->mode7_a = matrix;
            break;
        case 0x211C:
            io->mode7_b = matrix;
            break;
        case 0x211D:
            io->mode7_c = matrix;
            break;
        case 0x211E:
            io->mode7_d = matrix;
            break;
        case 0x211F:
            io->mode7_x = matrix;
            break;
        case 0x2120:
            io->mode7_y = matrix;
            break;
        default:
            break;
    }
    io->ppu_write_latch = value;
    if (address == UINT16_C(0x211B) ||
        address == UINT16_C(0x211C)) {
        update_mode7_product(io);
    }
}

static void write_bg_scroll(dkc2_snes_io *io,
                            uint16_t address,
                            uint8_t value) {
    unsigned background = (unsigned)(address - UINT16_C(0x210D)) / 2U;
    bool horizontal = (address & UINT16_C(1)) != 0;

    if (horizontal) {
        io->bg_hofs[background] = (uint16_t)(
            ((uint16_t)value << 8) |
            (io->bg_scroll_latch & UINT8_C(0xF8)) |
            ((io->bg_hofs[background] >> 8) & UINT16_C(7)));
    } else {
        io->bg_vofs[background] = (uint16_t)(
            ((uint16_t)value << 8) | io->bg_scroll_latch);
    }
    io->bg_scroll_latch = value;

    if (address == UINT16_C(0x210D)) {
        io->mode7_hofs = (uint16_t)(((uint16_t)value << 8) |
                                    io->ppu_write_latch);
        io->ppu_write_latch = value;
    } else if (address == UINT16_C(0x210E)) {
        io->mode7_vofs = (uint16_t)(((uint16_t)value << 8) |
                                    io->ppu_write_latch);
        io->ppu_write_latch = value;
    }
}

static void record_ppu_feature_write(dkc2_snes_io *io,
                                     uint16_t address,
                                     uint8_t value) {
    if (address == UINT16_C(0x2105)) {
        unsigned mode = value & UINT8_C(7);
        io->ppu_mode_mask |= (uint8_t)(UINT8_C(1) << mode);
        if (mode == 5U || mode == 6U) {
            io->ppu_feature_mask |= DKC2_PPU_FEATURE_HIRES;
        }
    } else if (address == UINT16_C(0x2106) &&
               (value & UINT8_C(0x0F)) != 0 &&
               (value & UINT8_C(0xF0)) != 0) {
        io->ppu_feature_mask |= DKC2_PPU_FEATURE_MOSAIC;
    } else if (address >= UINT16_C(0x2123) &&
               address <= UINT16_C(0x212B) && value != 0) {
        io->ppu_feature_mask |= DKC2_PPU_FEATURE_WINDOW;
    } else if (address == UINT16_C(0x212C)) {
        io->ppu_main_screen_mask |= value & UINT8_C(0x1F);
    } else if (address == UINT16_C(0x212D)) {
        io->ppu_sub_screen_mask |= value & UINT8_C(0x1F);
        if ((value & UINT8_C(0x1F)) != 0) {
            io->ppu_feature_mask |= DKC2_PPU_FEATURE_SUBSCREEN;
        }
    } else if (address == UINT16_C(0x2130) &&
               (value & UINT8_C(1)) != 0) {
        io->ppu_feature_mask |= DKC2_PPU_FEATURE_DIRECT_COLOR;
    } else if (address == UINT16_C(0x2131)) {
        io->ppu_color_math_mask |= value & UINT8_C(0x3F);
        if ((value & UINT8_C(0x3F)) != 0) {
            io->ppu_feature_mask |= DKC2_PPU_FEATURE_COLOR_MATH;
        }
    } else if (address == UINT16_C(0x2133)) {
        if ((value & UINT8_C(0x40)) != 0) {
            io->ppu_feature_mask |= DKC2_PPU_FEATURE_EXTBG;
        }
        if ((value & UINT8_C(0x09)) != 0) {
            io->ppu_feature_mask |=
                (value & UINT8_C(1)) != 0
                    ? DKC2_PPU_FEATURE_INTERLACE
                    : DKC2_PPU_FEATURE_HIRES;
        }
    }
}

static void write_wram_port(dkc2_snes_io *io,
                            uint16_t address,
                            uint8_t value) {
    io->registers[register_index(address)] = value;
    switch (address) {
        case 0x2180:
            io->bus->wram[io->wram_address] = value;
            io->wram_address =
                (io->wram_address + UINT32_C(1)) & UINT32_C(0x1FFFF);
            break;
        case 0x2181:
            io->wram_address =
                (io->wram_address & UINT32_C(0x1FF00)) | value;
            break;
        case 0x2182:
            io->wram_address =
                (io->wram_address & UINT32_C(0x100FF)) |
                ((uint32_t)value << 8);
            break;
        case 0x2183:
            io->wram_address =
                (io->wram_address & UINT32_C(0x0FFFF)) |
                ((uint32_t)(value & UINT8_C(1)) << 16);
            break;
        default:
            break;
    }
}

static void ppu_write(dkc2_snes_io *io,
                      uint16_t address,
                      uint8_t value) {
    uint8_t control;
    uint16_t mapped;
    size_t byte_address;

    if (address >= UINT16_C(0x2184)) {
        return;
    }
    io->registers[register_index(address)] = value;
    record_ppu_feature_write(io, address, value);
    switch (address) {
        case 0x2102:
            io->oam_address =
                (uint16_t)((io->oam_address & UINT16_C(0x0100)) | value);
            if ((io->registers[
                     register_index(UINT16_C(0x2103))] &
                 UINT8_C(0x80)) != 0) {
                io->first_sprite = (uint8_t)((io->oam_address &
                                               UINT16_C(0x00FE)) >>
                                              1);
            }
            io->oam_high = false;
            break;
        case 0x2103:
            io->oam_address =
                (uint16_t)((io->oam_address & UINT16_C(0x00FF)) |
                           ((uint16_t)(value & UINT8_C(1)) << 8));
            io->first_sprite = (value & UINT8_C(0x80)) != 0
                                   ? (uint8_t)((io->oam_address &
                                                UINT16_C(0x00FE)) >>
                                               1)
                                   : 0;
            io->oam_high = false;
            break;
        case 0x2104:
            if (!io->oam_high) {
                io->oam_write_latch = value;
            }
            if (io->oam_address >= UINT16_C(0x0100)) {
                byte_address = 512U +
                               (size_t)(io->oam_address &
                                        UINT16_C(0x000F)) *
                                   2U +
                               (io->oam_high ? 1U : 0U);
                io->oam[byte_address] = value;
            } else if (io->oam_high) {
                byte_address = (size_t)io->oam_address * 2U;
                io->oam[byte_address] = io->oam_write_latch;
                io->oam[byte_address + 1U] = value;
            }
            if (io->oam_high) {
                io->oam_address = (uint16_t)(
                    (io->oam_address + UINT16_C(1)) &
                    UINT16_C(0x01FF));
                if ((io->registers[
                         register_index(UINT16_C(0x2103))] &
                     UINT8_C(0x80)) != 0) {
                    io->first_sprite = (uint8_t)(
                        (io->oam_address & UINT16_C(0x00FE)) >> 1);
                }
            }
            io->oam_high = !io->oam_high;
            break;
        case 0x2116:
            io->vram_address =
                (uint16_t)((io->vram_address & UINT16_C(0xFF00)) | value);
            break;
        case 0x210D:
        case 0x210E:
        case 0x210F:
        case 0x2110:
        case 0x2111:
        case 0x2112:
        case 0x2113:
        case 0x2114:
            write_bg_scroll(io, address, value);
            break;
        case 0x2117:
            io->vram_address =
                (uint16_t)((io->vram_address & UINT16_C(0x00FF)) |
                           ((uint16_t)value << 8));
            break;
        case 0x211B:
        case 0x211C:
        case 0x211D:
        case 0x211E:
        case 0x211F:
        case 0x2120:
            write_mode7_matrix(io, address, value);
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
        case 0x2132:
            if ((value & UINT8_C(0x20)) != 0) {
                io->fixed_color_red = value & UINT8_C(0x1F);
            }
            if ((value & UINT8_C(0x40)) != 0) {
                io->fixed_color_green = value & UINT8_C(0x1F);
            }
            if ((value & UINT8_C(0x80)) != 0) {
                io->fixed_color_blue = value & UINT8_C(0x1F);
            }
            break;
        case 0x2180:
        case 0x2181:
        case 0x2182:
        case 0x2183:
            write_wram_port(io, address, value);
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

static unsigned dma_pattern_length(uint8_t mode) {
    static const uint8_t lengths[8] = {1, 2, 2, 4, 4, 4, 2, 4};
    return lengths[mode & UINT8_C(7)];
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

static uint16_t read_register16(const dkc2_snes_io *io,
                                uint16_t low_address) {
    return (uint16_t)(io->registers[register_index(low_address)] |
                      ((uint16_t)io->registers[
                           register_index((uint16_t)(low_address + 1U))]
                       << 8));
}

static void write_register16(dkc2_snes_io *io,
                             uint16_t low_address,
                             uint16_t value) {
    io->registers[register_index(low_address)] = (uint8_t)value;
    io->registers[register_index((uint16_t)(low_address + 1U))] =
        (uint8_t)(value >> 8);
}

static void initialize_hdma(dkc2_snes_io *io) {
    uint8_t enabled = io->registers[register_index(UINT16_C(0x420C))];
    unsigned channel;

    for (channel = 0; channel < 8U; ++channel) {
        uint16_t base =
            (uint16_t)(UINT16_C(0x4300) + channel * 0x10U);
        dkc2_hdma_channel_state *state = &io->hdma[channel];

        memset(state, 0, sizeof(*state));
        if ((enabled & (uint8_t)(UINT8_C(1) << channel)) == 0) {
            continue;
        }
        state->active = true;
        state->transfer_this_line = true;
        write_register16(io,
                         (uint16_t)(base + 8U),
                         read_register16(io, (uint16_t)(base + 2U)));
        io->registers[register_index((uint16_t)(base + 0x0AU))] = 0;
    }
}

static uint8_t read_hdma_table_byte(dkc2_snes_io *io, uint16_t base) {
    uint16_t table_address = read_register16(io, (uint16_t)(base + 8U));
    uint8_t table_bank =
        io->registers[register_index((uint16_t)(base + 4U))];
    uint8_t value = dkc2_bus_read8(
        io->bus,
        ((uint32_t)table_bank << 16) | table_address);

    write_register16(io,
                     (uint16_t)(base + 8U),
                     (uint16_t)(table_address + UINT16_C(1)));
    return value;
}

static bool reload_hdma_line_descriptor(dkc2_snes_io *io,
                                        unsigned channel,
                                        uint16_t base) {
    dkc2_hdma_channel_state *state = &io->hdma[channel];
    uint8_t descriptor = read_hdma_table_byte(io, base);

    if (descriptor == 0) {
        state->active = false;
        io->registers[register_index((uint16_t)(base + 0x0AU))] = 0;
        return false;
    }

    state->repeat = (descriptor & UINT8_C(0x80)) != 0;
    state->lines_remaining = descriptor & UINT8_C(0x7F);
    if (state->lines_remaining == 0) {
        state->lines_remaining = 128;
    }
    state->transfer_this_line = true;

    if ((io->registers[register_index(base)] & UINT8_C(0x40)) != 0) {
        uint16_t indirect = read_hdma_table_byte(io, base);
        indirect |= (uint16_t)read_hdma_table_byte(io, base) << 8;
        write_register16(io, (uint16_t)(base + 5U), indirect);
    }
    return true;
}

static void transfer_hdma_line(dkc2_snes_io *io,
                               unsigned channel,
                               uint16_t base) {
    dkc2_hdma_channel_state *state = &io->hdma[channel];
    uint8_t control = io->registers[register_index(base)];
    uint8_t b_address =
        io->registers[register_index((uint16_t)(base + 1U))];
    uint16_t source_offset;
    uint8_t source_bank;
    unsigned length;
    unsigned index;

    if ((control & UINT8_C(0x80)) != 0) {
        set_barrier(io,
                    DKC2_SNES_BARRIER_UNSUPPORTED_DMA,
                    (uint16_t)(UINT16_C(0x2100) | b_address),
                    control);
        return;
    }
    if ((control & UINT8_C(0x40)) != 0) {
        source_offset = read_register16(io, (uint16_t)(base + 5U));
        source_bank =
            io->registers[register_index((uint16_t)(base + 7U))];
    } else {
        source_offset = read_register16(io, (uint16_t)(base + 8U));
        source_bank =
            io->registers[register_index((uint16_t)(base + 4U))];
    }

    length = dma_pattern_length(control);
    for (index = 0; index < length; ++index) {
        uint8_t value = dkc2_bus_read8(
            io->bus,
            ((uint32_t)source_bank << 16) | source_offset);
        uint16_t ppu_address =
            (uint16_t)(UINT16_C(0x2100) |
                       (uint8_t)(b_address +
                                 dma_pattern(control, index)));
        ppu_write(io, ppu_address, value);
        io->bus->open_bus = value;
        source_offset = (uint16_t)(source_offset + UINT16_C(1));
    }

    if ((control & UINT8_C(0x40)) != 0) {
        write_register16(io, (uint16_t)(base + 5U), source_offset);
    } else {
        write_register16(io, (uint16_t)(base + 8U), source_offset);
    }
    ++io->hdma_transfers;
    io->hdma_bytes += length;
    state->transfer_this_line = false;
}

static void run_hdma_scanline(dkc2_snes_io *io) {
    uint8_t enabled = io->registers[register_index(UINT16_C(0x420C))];
    unsigned channel;

    for (channel = 0; channel < 8U; ++channel) {
        uint16_t base =
            (uint16_t)(UINT16_C(0x4300) + channel * 0x10U);
        dkc2_hdma_channel_state *state = &io->hdma[channel];
        uint16_t visible_remaining;

        if ((enabled & (uint8_t)(UINT8_C(1) << channel)) == 0 ||
            !state->active) {
            continue;
        }
        if (state->lines_remaining == 0 &&
            !reload_hdma_line_descriptor(io, channel, base)) {
            continue;
        }
        if (state->transfer_this_line) {
            transfer_hdma_line(io, channel, base);
            if (io->barrier != DKC2_SNES_BARRIER_NONE) {
                return;
            }
        }

        --state->lines_remaining;
        if (state->lines_remaining != 0) {
            state->transfer_this_line = state->repeat;
        }
        visible_remaining = state->lines_remaining == 128
                                ? 0
                                : state->lines_remaining;
        io->registers[register_index((uint16_t)(base + 0x0AU))] =
            (uint8_t)((state->repeat ? UINT8_C(0x80) : 0) |
                      (uint8_t)visible_remaining);
    }
}

static uint16_t irq_h_time(const dkc2_snes_io *io) {
    return (uint16_t)(read_register16(io, UINT16_C(0x4207)) &
                      UINT16_C(0x01FF));
}

static uint16_t irq_v_time(const dkc2_snes_io *io) {
    return (uint16_t)(read_register16(io, UINT16_C(0x4209)) &
                      UINT16_C(0x01FF));
}

static void check_irq_at_scanline_start(dkc2_snes_io *io) {
    uint8_t mode = (uint8_t)(io->registers[
        register_index(UINT16_C(0x4200))] & UINT8_C(0x30));
    bool h_match = irq_h_time(io) == 0;
    bool v_match = io->v_counter == irq_v_time(io);

    if ((mode == UINT8_C(0x10) && h_match) ||
        (mode == UINT8_C(0x20) && v_match) ||
        (mode == UINT8_C(0x30) && h_match && v_match)) {
        io->timeup_flag = true;
    }
}

static void finish_autojoy(dkc2_snes_io *io) {
    unsigned port;
    for (port = 0; port < 2U; ++port) {
        uint16_t address = (uint16_t)(UINT16_C(0x4218) + port * 2U);
        write_register16(io, address, io->controllers[port]);
    }
    write_register16(io, UINT16_C(0x421C), 0);
    write_register16(io, UINT16_C(0x421E), 0);
}

static void advance_autojoy(dkc2_snes_io *io, uint64_t master_cycles) {
    if (io->autojoy_cycles_remaining == 0) {
        return;
    }
    if (master_cycles >= io->autojoy_cycles_remaining) {
        io->autojoy_cycles_remaining = 0;
        finish_autojoy(io);
    } else {
        io->autojoy_cycles_remaining -= (uint32_t)master_cycles;
    }
}

static void finish_cpu_math(dkc2_snes_io *io) {
    if (io->cpu_math_is_division) {
        uint16_t quotient;
        uint16_t remainder;

        if (io->cpu_math_divisor == 0) {
            quotient = UINT16_C(0xFFFF);
            remainder = io->cpu_math_dividend;
        } else {
            quotient = (uint16_t)(io->cpu_math_dividend /
                                  io->cpu_math_divisor);
            remainder = (uint16_t)(io->cpu_math_dividend %
                                   io->cpu_math_divisor);
        }
        io->registers[register_index(UINT16_C(0x4214))] =
            (uint8_t)quotient;
        io->registers[register_index(UINT16_C(0x4215))] =
            (uint8_t)(quotient >> 8);
        io->registers[register_index(UINT16_C(0x4216))] =
            (uint8_t)remainder;
        io->registers[register_index(UINT16_C(0x4217))] =
            (uint8_t)(remainder >> 8);
    } else {
        uint16_t product = (uint16_t)(io->cpu_math_dividend *
                                      io->cpu_math_divisor);
        io->registers[register_index(UINT16_C(0x4216))] =
            (uint8_t)product;
        io->registers[register_index(UINT16_C(0x4217))] =
            (uint8_t)(product >> 8);
    }
}

static void advance_cpu_math(dkc2_snes_io *io,
                             uint64_t master_cycles) {
    if (io->cpu_math_cycles_remaining == 0) {
        return;
    }
    if (master_cycles >= io->cpu_math_cycles_remaining) {
        io->cpu_math_cycles_remaining = 0;
        finish_cpu_math(io);
    } else {
        io->cpu_math_cycles_remaining -= (uint32_t)master_cycles;
    }
}

static void render_current_scanline(dkc2_snes_io *io) {
    dkc2_ppu_render_source source;
    unsigned line;

    if (!io->renderer_enabled || io->v_counter == 0 ||
        io->v_counter > DKC2_PPU_FRAME_HEIGHT) {
        return;
    }
    source.registers = io->registers;
    source.vram = io->vram;
    source.cgram = io->cgram;
    source.oam = io->oam;
    source.bg_hofs = io->bg_hofs;
    source.bg_vofs = io->bg_vofs;
    source.mode7_hofs = io->mode7_hofs;
    source.mode7_vofs = io->mode7_vofs;
    source.mode7_a = io->mode7_a;
    source.mode7_b = io->mode7_b;
    source.mode7_c = io->mode7_c;
    source.mode7_d = io->mode7_d;
    source.mode7_x = io->mode7_x;
    source.mode7_y = io->mode7_y;
    source.fixed_color_red = io->fixed_color_red;
    source.fixed_color_green = io->fixed_color_green;
    source.fixed_color_blue = io->fixed_color_blue;
    source.first_sprite = io->first_sprite;
    line = (unsigned)io->v_counter - 1U;
    (void)dkc2_ppu_render_scanline(&io->renderer, &source, line);
    if (line + 1U == DKC2_PPU_FRAME_HEIGHT) {
        (void)dkc2_ppu_finish_frame(&io->renderer);
    }
}

static void begin_scanline(dkc2_snes_io *io) {
    if (io->v_counter == 0) {
        initialize_hdma(io);
    }
    if (io->v_counter == DKC2_NTSC_VBLANK_START) {
        io->nmi_flag = true;
        if ((io->registers[register_index(UINT16_C(0x4200))] &
             UINT8_C(0x01)) != 0) {
            io->controller_shift[0] = io->controllers[0];
            io->controller_shift[1] = io->controllers[1];
            io->autojoy_cycles_remaining = DKC2_AUTOJOY_MASTER_CYCLES;
        }
        if ((io->registers[register_index(UINT16_C(0x4200))] &
             UINT8_C(0x80)) != 0) {
            io->nmi_pending = true;
        }
    }
    check_irq_at_scanline_start(io);
}

static void advance_apu_from_master_cycles(dkc2_snes_io *io,
                                           uint64_t master_cycles) {
    if (master_cycles > (uint64_t)INT64_MAX) {
        master_cycles = (uint64_t)INT64_MAX;
    }
    io->apu_master_balance += (int64_t)master_cycles;
    while (io->apu_master_balance >=
           DKC2_MASTER_CYCLES_PER_APU_CYCLE) {
        int64_t available = io->apu_master_balance /
                            DKC2_MASTER_CYCLES_PER_APU_CYCLE;
        uint32_t request = available > UINT32_MAX
                               ? UINT32_MAX
                               : (uint32_t)available;
        uint32_t ran = dkc2_apu_run_cycles(io->apu, request);
        if (ran == 0) {
            break;
        }
        io->apu_master_balance -=
            (int64_t)ran * DKC2_MASTER_CYCLES_PER_APU_CYCLE;
    }
}

void dkc2_snes_io_advance_master_cycles(dkc2_snes_io *io,
                                         uint64_t master_cycles) {
    if (io == NULL || !io->master_scheduler_enabled ||
        master_cycles == 0) {
        return;
    }

    advance_apu_from_master_cycles(io, master_cycles);
    while (master_cycles != 0 &&
           io->barrier == DKC2_SNES_BARRIER_NONE) {
        uint16_t old_h = io->h_counter;
        uint64_t until_line_end =
            DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE - io->h_counter;
        uint64_t step = master_cycles < until_line_end
                            ? master_cycles
                            : until_line_end;
        uint16_t new_h = (uint16_t)(io->h_counter + step);
        uint8_t irq_mode = (uint8_t)(io->registers[
            register_index(UINT16_C(0x4200))] & UINT8_C(0x30));
        uint16_t irq_h = (uint16_t)(irq_h_time(io) * 4U);
        bool irq_line_matches = irq_mode == UINT8_C(0x10) ||
                                (irq_mode == UINT8_C(0x30) &&
                                 io->v_counter == irq_v_time(io));

        if (irq_line_matches && irq_h != 0 &&
            irq_h < DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE &&
            old_h < irq_h && new_h >= irq_h) {
            io->timeup_flag = true;
        }
        if (io->v_counter < DKC2_NTSC_VBLANK_START &&
            old_h < DKC2_NTSC_HBLANK_START &&
            new_h >= DKC2_NTSC_HBLANK_START) {
            uint8_t mode = io->registers[
                register_index(UINT16_C(0x2105))] & UINT8_C(7);
            ++io->ppu_mode_scanlines[mode];
            if ((io->registers[
                     register_index(UINT16_C(0x2100))] &
                 UINT8_C(0x80)) != 0) {
                ++io->ppu_forced_blank_scanlines;
            }
            render_current_scanline(io);
            run_hdma_scanline(io);
        }

        io->h_counter = new_h;
        io->master_cycles += step;
        advance_autojoy(io, step);
        advance_cpu_math(io, step);
        master_cycles -= step;
        if (io->h_counter == DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE) {
            io->h_counter = 0;
            ++io->v_counter;
            if (io->v_counter == DKC2_NTSC_SCANLINES_PER_FRAME) {
                io->v_counter = 0;
                ++io->frames;
            }
            begin_scanline(io);
        }
    }
}

void dkc2_snes_io_advance_cpu_accesses(dkc2_snes_io *io,
                                        uint64_t accesses) {
    while (accesses > UINT64_MAX /
                      DKC2_PROVISIONAL_MASTER_CYCLES_PER_CPU_ACCESS) {
        dkc2_snes_io_advance_master_cycles(
            io,
            (UINT64_MAX /
             DKC2_PROVISIONAL_MASTER_CYCLES_PER_CPU_ACCESS) *
                DKC2_PROVISIONAL_MASTER_CYCLES_PER_CPU_ACCESS);
        accesses -= UINT64_MAX /
                    DKC2_PROVISIONAL_MASTER_CYCLES_PER_CPU_ACCESS;
    }
    dkc2_snes_io_advance_master_cycles(
        io,
        accesses * DKC2_PROVISIONAL_MASTER_CYCLES_PER_CPU_ACCESS);
}

bool dkc2_snes_io_take_nmi(dkc2_snes_io *io) {
    bool pending;
    if (io == NULL) {
        return false;
    }
    pending = io->nmi_pending;
    io->nmi_pending = false;
    return pending;
}

bool dkc2_snes_io_irq_pending(const dkc2_snes_io *io) {
    return io != NULL && io->timeup_flag;
}

bool dkc2_snes_io_interrupt_source_enabled(const dkc2_snes_io *io) {
    uint8_t enables;
    if (io == NULL) {
        return false;
    }
    enables = io->registers[register_index(UINT16_C(0x4200))];
    return (enables & UINT8_C(0xB0)) != 0;
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
    if (offset >= UINT16_C(0x2134) &&
        offset <= UINT16_C(0x2136)) {
        unsigned shift =
            (unsigned)(offset - UINT16_C(0x2134)) * 8U;
        *value = (uint8_t)((uint32_t)io->mode7_product >> shift);
        return true;
    }
    if (offset == UINT16_C(0x2180)) {
        *value = io->bus->wram[io->wram_address];
        io->wram_address =
            (io->wram_address + UINT32_C(1)) & UINT32_C(0x1FFFF);
        return true;
    }
    if (offset >= UINT16_C(0x2184) &&
        offset <= UINT16_C(0x21FF)) {
        *value = io->bus->open_bus;
        return true;
    }
    if (io->master_scheduler_enabled &&
        (offset == UINT16_C(0x4016) ||
         offset == UINT16_C(0x4017))) {
        unsigned port = offset == UINT16_C(0x4016) ? 0U : 1U;
        uint8_t serial =
            (uint8_t)((io->controller_shift[port] >> 15) & UINT16_C(1));
        uint8_t open_bits = io->bus != NULL
                                ? (uint8_t)(io->bus->open_bus & UINT8_C(0xFC))
                                : 0;
        *value = (uint8_t)(open_bits | serial);
        if (!io->joy_strobe) {
            io->controller_shift[port] =
                (uint16_t)((io->controller_shift[port] << 1) |
                           UINT16_C(1));
        }
        return true;
    }
    if (offset >= UINT16_C(0x2140) && offset <= UINT16_C(0x2143)) {
        if (io->stop_on_apu_after_dma && io->dma_transfers != 0) {
            *value = 0;
            set_barrier(io, DKC2_SNES_BARRIER_APU, address, *value);
        } else {
            if (!io->master_scheduler_enabled) {
                (void)dkc2_apu_run_cycles(io->apu,
                                          DKC2_APU_PORT_SLICE_CYCLES);
            }
            *value = dkc2_apu_cpu_read_port(
                io->apu,
                (unsigned)(offset - UINT16_C(0x2140)));
        }
        return true;
    }
    if (io->master_scheduler_enabled &&
        offset == UINT16_C(0x4210)) {
        uint8_t open_bits = io->bus != NULL
                                ? (uint8_t)(io->bus->open_bus & UINT8_C(0x70))
                                : 0;
        *value = (uint8_t)(open_bits | UINT8_C(0x02) |
                           (io->nmi_flag ? UINT8_C(0x80) : 0));
        io->nmi_flag = false;
        return true;
    }
    if (io->master_scheduler_enabled &&
        offset == UINT16_C(0x4211)) {
        uint8_t open_bits = io->bus != NULL
                                ? (uint8_t)(io->bus->open_bus & UINT8_C(0x7F))
                                : 0;
        *value = (uint8_t)(open_bits |
                           (io->timeup_flag ? UINT8_C(0x80) : 0));
        io->timeup_flag = false;
        return true;
    }
    if (io->master_scheduler_enabled &&
        offset == UINT16_C(0x4212)) {
        uint8_t open_bits = io->bus != NULL
                                ? (uint8_t)(io->bus->open_bus & UINT8_C(0x3E))
                                : 0;
        *value = (uint8_t)(open_bits |
                           (io->v_counter >= DKC2_NTSC_VBLANK_START
                                ? UINT8_C(0x80)
                                : 0) |
                           (io->h_counter >= DKC2_NTSC_HBLANK_START
                                ? UINT8_C(0x40)
                                : 0) |
                           (io->autojoy_cycles_remaining != 0
                                ? UINT8_C(0x01)
                                : 0));
        return true;
    }
    if (io->master_scheduler_enabled &&
        offset >= UINT16_C(0x4218) &&
        offset <= UINT16_C(0x421F)) {
        *value = io->registers[register_index(offset)];
        return true;
    }
    if (offset >= UINT16_C(0x4214) &&
        offset <= UINT16_C(0x4217)) {
        *value = io->registers[register_index(offset)];
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
    if (io->master_scheduler_enabled &&
        offset == UINT16_C(0x4016)) {
        bool new_strobe = (value & UINT8_C(1)) != 0;
        io->registers[register_index(offset)] = value;
        if (new_strobe || io->joy_strobe != new_strobe) {
            io->controller_shift[0] = io->controllers[0];
            io->controller_shift[1] = io->controllers[1];
        }
        io->joy_strobe = new_strobe;
        return true;
    }
    if (offset >= UINT16_C(0x2180) &&
        offset <= UINT16_C(0x2183)) {
        write_wram_port(io, offset, value);
        return true;
    }
    if (offset >= UINT16_C(0x2184) &&
        offset <= UINT16_C(0x21FF)) {
        return true;
    }
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
            if (!io->master_scheduler_enabled) {
                (void)dkc2_apu_run_cycles(io->apu,
                                          DKC2_APU_PORT_SLICE_CYCLES);
            }
        }
        return true;
    }
    if ((offset >= UINT16_C(0x4200) && offset <= UINT16_C(0x420D)) ||
        (offset >= UINT16_C(0x4300) && offset <= UINT16_C(0x437F))) {
        io->registers[register_index(offset)] = value;
        if (offset == UINT16_C(0x4200) &&
            (value & UINT8_C(0x80)) != 0 && io->nmi_flag) {
            io->nmi_pending = true;
        }
        if (offset == UINT16_C(0x4203)) {
            io->cpu_math_is_division = false;
            io->cpu_math_dividend = io->registers[
                register_index(UINT16_C(0x4202))];
            io->cpu_math_divisor = value;
            io->cpu_math_cycles_remaining =
                DKC2_MULTIPLY_MASTER_CYCLES;
        } else if (offset == UINT16_C(0x4206)) {
            io->cpu_math_is_division = true;
            io->cpu_math_dividend = (uint16_t)(
                io->registers[register_index(UINT16_C(0x4204))] |
                ((uint16_t)io->registers[
                     register_index(UINT16_C(0x4205))]
                 << 8));
            io->cpu_math_divisor = value;
            io->cpu_math_cycles_remaining = DKC2_DIVIDE_MASTER_CYCLES;
        }
        if (offset == UINT16_C(0x420C)) {
            unsigned channel;
            for (channel = 0; channel < 8U; ++channel) {
                if ((value & (uint8_t)(UINT8_C(1) << channel)) == 0) {
                    io->hdma[channel].active = false;
                }
            }
        }
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
