#include "dkc2/bus.h"
#include "dkc2/rom.h"
#include "dkc2/snes_io.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct timing_fixture {
    uint8_t dummy_rom_byte;
    dkc2_rom_image rom;
    dkc2_bus bus;
    dkc2_snes_io io;
} timing_fixture;

static void fail(const char *message) {
    (void)fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void fixture_init(timing_fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->rom.data = &fixture->dummy_rom_byte;
    fixture->rom.size = 1;
    if (!dkc2_bus_init(&fixture->bus, &fixture->rom) ||
        !dkc2_snes_io_init(&fixture->io, &fixture->bus)) {
        fail("cannot initialize timing fixture");
    }
    dkc2_bus_set_io(&fixture->bus,
                    dkc2_snes_io_read,
                    dkc2_snes_io_write,
                    &fixture->io);
    dkc2_snes_io_stop_on_apu_after_dma(&fixture->io, false);
    dkc2_snes_io_enable_master_scheduler(&fixture->io, true);
}

static void fixture_free(timing_fixture *fixture) {
    dkc2_snes_io_free(&fixture->io);
    dkc2_bus_free(&fixture->bus);
}

static void test_nmi_and_status(void) {
    timing_fixture fixture;
    uint8_t status;

    fixture_init(&fixture);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004200), UINT8_C(0x80));
    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        (uint64_t)DKC2_NTSC_VBLANK_START *
            DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE);

    if (fixture.io.v_counter != DKC2_NTSC_VBLANK_START ||
        fixture.io.h_counter != 0 ||
        !dkc2_snes_io_take_nmi(&fixture.io) ||
        dkc2_snes_io_take_nmi(&fixture.io)) {
        fail("VBlank did not latch exactly one NMI edge");
    }
    status = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004210));
    if ((status & UINT8_C(0x8F)) != UINT8_C(0x82)) {
        fail("RDNMI did not expose the VBlank flag and CPU version");
    }
    status = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004210));
    if ((status & UINT8_C(0x80)) != 0) {
        fail("RDNMI did not clear on read");
    }
    status = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004212));
    if ((status & UINT8_C(0x80)) == 0) {
        fail("HVBJOY did not report VBlank");
    }

    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        (uint64_t)(DKC2_NTSC_SCANLINES_PER_FRAME -
                   DKC2_NTSC_VBLANK_START) *
            DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE);
    dkc2_snes_io_advance_master_cycles(&fixture.io,
                                        DKC2_NTSC_HBLANK_START);
    status = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004212));
    if (fixture.io.frames != 1 || fixture.io.v_counter != 0 ||
        fixture.io.h_counter != DKC2_NTSC_HBLANK_START ||
        (status & UINT8_C(0xC0)) != UINT8_C(0x40)) {
        fail("frame wrap or HBlank status is incorrect");
    }
    fixture_free(&fixture);
}

static void test_h_timer_irq(void) {
    timing_fixture fixture;
    uint8_t status;

    fixture_init(&fixture);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004207), UINT8_C(0x02));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004208), 0);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004200), UINT8_C(0x10));
    dkc2_snes_io_advance_master_cycles(&fixture.io, 8);

    if (!dkc2_snes_io_irq_pending(&fixture.io)) {
        fail("H-timer comparison did not latch TIMEUP");
    }
    status = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004211));
    if ((status & UINT8_C(0x80)) == 0 ||
        dkc2_snes_io_irq_pending(&fixture.io)) {
        fail("TIMEUP did not report and clear on read");
    }
    status = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004211));
    if ((status & UINT8_C(0x80)) != 0) {
        fail("TIMEUP was not clear on the second read");
    }

    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004209), UINT8_C(0x01));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x00420A), 0);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004200), UINT8_C(0x20));
    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE - 8U);
    if (!dkc2_snes_io_irq_pending(&fixture.io) ||
        (dkc2_bus_read8(&fixture.bus, UINT32_C(0x004211)) &
         UINT8_C(0x80)) == 0) {
        fail("V-timer comparison did not latch TIMEUP");
    }

    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004207), UINT8_C(0x02));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004209), UINT8_C(0x02));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004200), UINT8_C(0x30));
    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE + 8U);
    if (!dkc2_snes_io_irq_pending(&fixture.io) ||
        (dkc2_bus_read8(&fixture.bus, UINT32_C(0x004211)) &
         UINT8_C(0x80)) == 0) {
        fail("combined H/V comparison did not latch TIMEUP");
    }
    fixture_free(&fixture);
}

static void test_intro_style_direct_hdma(void) {
    timing_fixture fixture;
    uint64_t next_hblank =
        (uint64_t)(DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE -
                   DKC2_NTSC_HBLANK_START) +
        DKC2_NTSC_HBLANK_START;

    fixture_init(&fixture);
    fixture.bus.wram[UINT16_C(0x0100)] = UINT8_C(0x02);
    fixture.bus.wram[UINT16_C(0x0101)] = UINT8_C(0x03);
    fixture.bus.wram[UINT16_C(0x0102)] = 0;

    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004320), 0);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004321), UINT8_C(0x05));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004322), 0);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004323), UINT8_C(0x01));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004324), UINT8_C(0x7E));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x00420C), UINT8_C(0x04));

    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        (uint64_t)DKC2_NTSC_SCANLINES_PER_FRAME *
            DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE);
    dkc2_snes_io_advance_master_cycles(&fixture.io,
                                        DKC2_NTSC_HBLANK_START);
    if (fixture.io.registers[UINT16_C(0x0105)] != UINT8_C(0x03) ||
        fixture.io.hdma_transfers != 1 || fixture.io.hdma_bytes != 1) {
        fail("direct mode-0 HDMA did not write the first table value");
    }

    dkc2_snes_io_advance_master_cycles(&fixture.io, next_hblank);
    if (fixture.io.hdma_transfers != 1) {
        fail("write-once HDMA repeated before its line count expired");
    }
    dkc2_snes_io_advance_master_cycles(&fixture.io, next_hblank);
    if (fixture.io.hdma[2].active || fixture.io.hdma_transfers != 1) {
        fail("HDMA table terminator did not stop the channel");
    }
    fixture_free(&fixture);
}

static void test_controllers_and_autojoy(void) {
    timing_fixture fixture;
    uint16_t serial = 0;
    uint16_t autojoy;
    unsigned bit;

    fixture_init(&fixture);
    if (!dkc2_snes_io_set_controller(&fixture.io, 0, UINT16_C(0xA510))) {
        fail("controller state could not be installed");
    }
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004016), 1);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004016), 0);
    for (bit = 0; bit < 16U; ++bit) {
        serial = (uint16_t)((serial << 1) |
            (dkc2_bus_read8(&fixture.bus, UINT32_C(0x004016)) & 1U));
    }
    if (serial != UINT16_C(0xA510) ||
        (dkc2_bus_read8(&fixture.bus, UINT32_C(0x004016)) & 1U) == 0) {
        fail("manual controller serial shifting is incorrect");
    }

    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004200), UINT8_C(0x81));
    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        (uint64_t)DKC2_NTSC_VBLANK_START *
            DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE);
    if ((dkc2_bus_read8(&fixture.bus, UINT32_C(0x004212)) & 1U) == 0) {
        fail("autojoy busy did not start at VBlank");
    }
    dkc2_snes_io_advance_master_cycles(&fixture.io,
                                        DKC2_AUTOJOY_MASTER_CYCLES);
    autojoy = dkc2_bus_read8(&fixture.bus, UINT32_C(0x004218));
    autojoy |= (uint16_t)dkc2_bus_read8(
                   &fixture.bus,
                   UINT32_C(0x004219))
               << 8;
    if ((dkc2_bus_read8(&fixture.bus, UINT32_C(0x004212)) & 1U) != 0 ||
        autojoy != UINT16_C(0xA510)) {
        fail("autojoy completion or JOY1 result is incorrect");
    }
    fixture_free(&fixture);
}

static uint16_t read_math_result(dkc2_bus *bus, uint16_t low_address) {
    uint16_t value = dkc2_bus_read8(bus, low_address);
    value |= (uint16_t)dkc2_bus_read8(
                 bus,
                 (uint16_t)(low_address + 1U))
             << 8;
    return value;
}

static void test_cpu_math_registers(void) {
    timing_fixture fixture;

    fixture_init(&fixture);
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004202), UINT8_C(13));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004203), UINT8_C(17));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004202), UINT8_C(99));
    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        DKC2_MULTIPLY_MASTER_CYCLES - 1U);
    if (read_math_result(&fixture.bus, UINT16_C(0x4216)) != 0) {
        fail("CPU multiplication completed before its documented delay");
    }
    dkc2_snes_io_advance_master_cycles(&fixture.io, 1);
    if (read_math_result(&fixture.bus, UINT16_C(0x4216)) != 221U) {
        fail("CPU multiplication result is incorrect");
    }

    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004204), UINT8_C(0xE8));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004205), UINT8_C(0x03));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004206), UINT8_C(30));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004204), UINT8_C(0xFF));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004205), UINT8_C(0xFF));
    dkc2_snes_io_advance_master_cycles(
        &fixture.io,
        DKC2_DIVIDE_MASTER_CYCLES - 1U);
    if (read_math_result(&fixture.bus, UINT16_C(0x4214)) != 0) {
        fail("CPU division completed before its documented delay");
    }
    dkc2_snes_io_advance_master_cycles(&fixture.io, 1);
    if (read_math_result(&fixture.bus, UINT16_C(0x4214)) != 33U ||
        read_math_result(&fixture.bus, UINT16_C(0x4216)) != 10U) {
        fail("CPU division quotient or remainder is incorrect");
    }

    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004204), UINT8_C(0x34));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004205), UINT8_C(0x12));
    dkc2_bus_write8(&fixture.bus, UINT32_C(0x004206), 0);
    dkc2_snes_io_advance_master_cycles(&fixture.io,
                                        DKC2_DIVIDE_MASTER_CYCLES);
    if (read_math_result(&fixture.bus, UINT16_C(0x4214)) !=
            UINT16_C(0xFFFF) ||
        read_math_result(&fixture.bus, UINT16_C(0x4216)) !=
            UINT16_C(0x1234)) {
        fail("CPU divide-by-zero behavior is incorrect");
    }
    fixture_free(&fixture);
}

int main(void) {
    test_nmi_and_status();
    test_h_timer_irq();
    test_intro_style_direct_hdma();
    test_controllers_and_autojoy();
    test_cpu_math_registers();
    (void)puts("SNES timing, interrupt-status, and HDMA tests passed");
    return EXIT_SUCCESS;
}
