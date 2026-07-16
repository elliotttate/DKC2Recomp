#include "dkc2/bus.h"
#include "dkc2/rom.h"
#include "dkc2/snes_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fail(const char *message) {
    (void)fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void write_dma_register(dkc2_bus *bus,
                               uint16_t address,
                               uint8_t value) {
    dkc2_bus_write8(bus, address, value);
}

int main(void) {
    uint8_t dummy_rom_byte = 0;
    dkc2_rom_image rom;
    dkc2_bus bus;
    dkc2_snes_io io;
    uint8_t apu_value;
    uint32_t mode7_result;

    memset(&rom, 0, sizeof(rom));
    rom.data = &dummy_rom_byte;
    rom.size = 1;
    if (!dkc2_bus_init(&bus, &rom) || !dkc2_snes_io_init(&io, &bus)) {
        fail("cannot initialize SNES I/O test fixture");
    }
    dkc2_bus_set_io(&bus, dkc2_snes_io_read, dkc2_snes_io_write, &io);
    dkc2_snes_io_stop_on_apu_after_dma(&io, true);

    dkc2_bus_write8(&bus, UINT32_C(0x00211B), UINT8_C(0x34));
    dkc2_bus_write8(&bus, UINT32_C(0x00211B), UINT8_C(0x12));
    dkc2_bus_write8(&bus, UINT32_C(0x00211C), UINT8_C(0xFE));
    mode7_result = dkc2_bus_read8(&bus, UINT32_C(0x002134));
    mode7_result |= (uint32_t)dkc2_bus_read8(
                        &bus,
                        UINT32_C(0x002135))
                    << 8;
    mode7_result |= (uint32_t)dkc2_bus_read8(
                        &bus,
                        UINT32_C(0x002136))
                    << 16;
    if (io.mode7_a != INT16_C(0x1234) ||
        mode7_result != UINT32_C(0x00FFDB98)) {
        fail("signed Mode-7 multiplication is incorrect");
    }

    dkc2_bus_write8(&bus, UINT32_C(0x00211B), UINT8_C(0x00));
    dkc2_bus_write8(&bus, UINT32_C(0x00211B), UINT8_C(0x80));
    dkc2_bus_write8(&bus, UINT32_C(0x00211C), UINT8_C(0x02));
    mode7_result = dkc2_bus_read8(&bus, UINT32_C(0x002134));
    mode7_result |= (uint32_t)dkc2_bus_read8(
                        &bus,
                        UINT32_C(0x002135))
                    << 8;
    mode7_result |= (uint32_t)dkc2_bus_read8(
                        &bus,
                        UINT32_C(0x002136))
                    << 16;
    if (io.mode7_a != INT16_MIN ||
        io.mode7_b != (int16_t)UINT16_C(0x0280) ||
        mode7_result != UINT32_C(0x00FF0000)) {
        fail("Mode-7 shared write latch or negative product is incorrect");
    }
    dkc2_bus_write8(&bus, UINT32_C(0x00211D), UINT8_C(0xEF));
    dkc2_bus_write8(&bus, UINT32_C(0x00211D), UINT8_C(0xBE));
    dkc2_bus_write8(&bus, UINT32_C(0x00211E), UINT8_C(0x34));
    if ((uint16_t)io.mode7_c != UINT16_C(0xBEEF) ||
        (uint16_t)io.mode7_d != UINT16_C(0x34BE)) {
        fail("Mode-7 C/D shared write latch is incorrect");
    }

    dkc2_bus_write8(&bus, UINT32_C(0x00210D), UINT8_C(0x34));
    dkc2_bus_write8(&bus, UINT32_C(0x00210D), UINT8_C(0x12));
    dkc2_bus_write8(&bus, UINT32_C(0x00210E), UINT8_C(0x78));
    dkc2_bus_write8(&bus, UINT32_C(0x00210E), UINT8_C(0x56));
    dkc2_bus_write8(&bus, UINT32_C(0x00210F), UINT8_C(0xAA));
    dkc2_bus_write8(&bus, UINT32_C(0x00210F), UINT8_C(0x03));
    if (io.bg_hofs[0] != UINT16_C(0x1234) ||
        io.bg_vofs[0] != UINT16_C(0x5678) ||
        io.bg_hofs[1] != UINT16_C(0x03AA) ||
        io.mode7_hofs != UINT16_C(0x1234) ||
        io.mode7_vofs != UINT16_C(0x5678)) {
        fail("background/Mode-7 shared scroll latches are incorrect");
    }

    dkc2_bus_write8(&bus, UINT32_C(0x002105), UINT8_C(0x01));
    dkc2_bus_write8(&bus, UINT32_C(0x00212C), UINT8_C(0x13));
    dkc2_bus_write8(&bus, UINT32_C(0x00212D), UINT8_C(0x02));
    dkc2_bus_write8(&bus, UINT32_C(0x002131), UINT8_C(0x21));
    if (io.ppu_mode_mask != UINT8_C(0x02) ||
        io.ppu_main_screen_mask != UINT8_C(0x13) ||
        io.ppu_sub_screen_mask != UINT8_C(0x02) ||
        io.ppu_color_math_mask != UINT8_C(0x21)) {
        fail("PPU mode or feature telemetry is incorrect");
    }

    dkc2_bus_write8(&bus, UINT32_C(0x002102), UINT8_C(1));
    dkc2_bus_write8(&bus, UINT32_C(0x002103), 0);
    io.oam[2] = UINT8_C(0x55);
    dkc2_bus_write8(&bus, UINT32_C(0x002104), UINT8_C(0xA1));
    if (io.oam[2] != UINT8_C(0x55) ||
        io.oam_write_latch != UINT8_C(0xA1)) {
        fail("first low-table OAM byte did not remain latched");
    }
    dkc2_bus_write8(&bus, UINT32_C(0x002104), UINT8_C(0xB2));
    dkc2_bus_write8(&bus, UINT32_C(0x002102), 0);
    dkc2_bus_write8(&bus, UINT32_C(0x002103), UINT8_C(1));
    dkc2_bus_write8(&bus, UINT32_C(0x002104), UINT8_C(0xC3));
    dkc2_bus_write8(&bus, UINT32_C(0x002104), UINT8_C(0xD4));
    if (io.oam[2] != UINT8_C(0xA1) ||
        io.oam[3] != UINT8_C(0xB2) ||
        io.oam[512] != UINT8_C(0xC3) ||
        io.oam[513] != UINT8_C(0xD4) ||
        io.oam_address != UINT16_C(0x0101) || io.oam_high) {
        fail("OAM word address, byte latch, or high-table mapping is incorrect");
    }
    dkc2_bus_write8(&bus, UINT32_C(0x002102), UINT8_C(3));
    dkc2_bus_write8(&bus, UINT32_C(0x002103), UINT8_C(0x80));
    if (io.first_sprite != UINT8_C(1)) {
        fail("OAM priority rotation did not use the programmed address");
    }
    dkc2_bus_write8(&bus, UINT32_C(0x002104), UINT8_C(0x11));
    dkc2_bus_write8(&bus, UINT32_C(0x002104), UINT8_C(0x22));
    if (io.first_sprite != UINT8_C(2)) {
        fail("OAM priority rotation did not follow address increment");
    }

    dkc2_bus_write8(&bus, UINT32_C(0x002181), UINT8_C(0xFE));
    dkc2_bus_write8(&bus, UINT32_C(0x002182), UINT8_C(0xFF));
    dkc2_bus_write8(&bus, UINT32_C(0x002183), UINT8_C(0x01));
    dkc2_bus_write8(&bus, UINT32_C(0x002180), UINT8_C(0xA1));
    dkc2_bus_write8(&bus, UINT32_C(0x002180), UINT8_C(0xB2));
    dkc2_bus_write8(&bus, UINT32_C(0x002180), UINT8_C(0xC3));
    if (bus.wram[UINT32_C(0x1FFFE)] != UINT8_C(0xA1) ||
        bus.wram[UINT32_C(0x1FFFF)] != UINT8_C(0xB2) ||
        bus.wram[0] != UINT8_C(0xC3) || io.wram_address != 1) {
        fail("WRAM data port write or 17-bit wraparound is incorrect");
    }
    dkc2_bus_write8(&bus, UINT32_C(0x002181), UINT8_C(0xFE));
    dkc2_bus_write8(&bus, UINT32_C(0x002182), UINT8_C(0xFF));
    dkc2_bus_write8(&bus, UINT32_C(0x002183), UINT8_C(0x01));
    if (dkc2_bus_read8(&bus, UINT32_C(0x002180)) != UINT8_C(0xA1) ||
        dkc2_bus_read8(&bus, UINT32_C(0x002180)) != UINT8_C(0xB2) ||
        dkc2_bus_read8(&bus, UINT32_C(0x002180)) != UINT8_C(0xC3) ||
        io.wram_address != 1) {
        fail("WRAM data port read or auto-increment is incorrect");
    }
    dkc2_bus_write8(&bus, UINT32_C(0x002184), UINT8_C(0xD4));
    if (dkc2_bus_read8(&bus, UINT32_C(0x002184)) != UINT8_C(0xD4) ||
        io.barrier != DKC2_SNES_BARRIER_NONE) {
        fail("unmapped B-bus register did not preserve open-bus behavior");
    }

    bus.wram[0] = 0x11;
    bus.wram[1] = 0x22;
    bus.wram[2] = 0x33;
    bus.wram[3] = 0x44;
    write_dma_register(&bus, 0x2115, 0x80);
    write_dma_register(&bus, 0x2116, 0x00);
    write_dma_register(&bus, 0x2117, 0x00);
    write_dma_register(&bus, 0x4300, 0x01);
    write_dma_register(&bus, 0x4301, 0x18);
    write_dma_register(&bus, 0x4302, 0x00);
    write_dma_register(&bus, 0x4303, 0x00);
    write_dma_register(&bus, 0x4304, 0x7E);
    write_dma_register(&bus, 0x4305, 0x04);
    write_dma_register(&bus, 0x4306, 0x00);
    write_dma_register(&bus, 0x420B, 0x01);
    if (io.dma_transfers != 1 || io.dma_bytes != 4 ||
        io.vram[0] != 0x11 || io.vram[1] != 0x22 ||
        io.vram[2] != 0x33 || io.vram[3] != 0x44 ||
        io.vram_address != 2 || io.barrier != DKC2_SNES_BARRIER_NONE) {
        fail("mode-1 WRAM-to-VRAM DMA is incorrect");
    }

    memset(io.vram, 0xFF, sizeof(io.vram));
    bus.wram[4] = 0;
    write_dma_register(&bus, 0x2116, 0x00);
    write_dma_register(&bus, 0x2117, 0x00);
    write_dma_register(&bus, 0x4300, 0x09);
    write_dma_register(&bus, 0x4301, 0x18);
    write_dma_register(&bus, 0x4302, 0x04);
    write_dma_register(&bus, 0x4303, 0x00);
    write_dma_register(&bus, 0x4304, 0x7E);
    write_dma_register(&bus, 0x4305, 0x00);
    write_dma_register(&bus, 0x4306, 0x00);
    write_dma_register(&bus, 0x420B, 0x01);
    if (io.dma_transfers != 2 || io.dma_bytes != UINT64_C(65540) ||
        !dkc2_snes_vram_is_zero(&io) || !io.vram_clear_confirmed) {
        fail("fixed-source 64 KiB VRAM-clear DMA is incorrect");
    }

    apu_value = dkc2_bus_read8(&bus, UINT32_C(0x002140));
    if (apu_value != 0 || io.barrier != DKC2_SNES_BARRIER_APU) {
        fail("APU transport barrier was not reported after DMA bring-up");
    }

    dkc2_snes_io_free(&io);
    dkc2_bus_free(&bus);
    (void)puts("SNES register and general-DMA tests passed");
    return EXIT_SUCCESS;
}
