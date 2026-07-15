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

    memset(&rom, 0, sizeof(rom));
    rom.data = &dummy_rom_byte;
    rom.size = 1;
    if (!dkc2_bus_init(&bus, &rom) || !dkc2_snes_io_init(&io, &bus)) {
        fail("cannot initialize SNES I/O test fixture");
    }
    dkc2_bus_set_io(&bus, dkc2_snes_io_read, dkc2_snes_io_write, &io);
    dkc2_snes_io_stop_on_apu_after_dma(&io, true);

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
