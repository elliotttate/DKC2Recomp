#include "dkc2/bus.h"
#include "dkc2/hirom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct io_fixture {
    uint8_t register_2100;
    unsigned reads;
    unsigned writes;
} io_fixture;

static bool io_read(void *context, uint32_t address, uint8_t *value) {
    io_fixture *fixture = (io_fixture *)context;
    if ((uint16_t)address != UINT16_C(0x2100)) {
        return false;
    }
    *value = fixture->register_2100;
    ++fixture->reads;
    return true;
}

static bool io_write(void *context, uint32_t address, uint8_t value) {
    io_fixture *fixture = (io_fixture *)context;
    if ((uint16_t)address != UINT16_C(0x2100)) {
        return false;
    }
    fixture->register_2100 = value;
    ++fixture->writes;
    return true;
}

static void expect_region(const dkc2_bus *bus,
                          uint32_t address,
                          dkc2_bus_region expected) {
    dkc2_bus_region actual = dkc2_bus_region_for(bus, address);
    if (actual != expected) {
        (void)fprintf(stderr,
                      "$%06X was %s; expected %s\n",
                      (unsigned)address,
                      dkc2_bus_region_name(actual),
                      dkc2_bus_region_name(expected));
        exit(EXIT_FAILURE);
    }
}

static void expect_read(dkc2_bus *bus, uint32_t address, uint8_t expected) {
    uint8_t actual = dkc2_bus_read8(bus, address);
    if (actual != expected) {
        (void)fprintf(stderr,
                      "$%06X read $%02X; expected $%02X\n",
                      (unsigned)address,
                      (unsigned)actual,
                      (unsigned)expected);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    dkc2_rom_image image;
    dkc2_bus bus;
    io_fixture io = {0};
    uint8_t save_in[DKC2_SRAM_SIZE];
    uint8_t save_out[DKC2_SRAM_SIZE];
    uint64_t accesses_before;

    memset(&image, 0, sizeof(image));
    image.size = (size_t)64 * 1024;
    image.data = (uint8_t *)calloc(image.size, 1);
    if (image.data == NULL) {
        return EXIT_FAILURE;
    }
    image.data[0] = UINT8_C(0x42);
    image.data[DKC2_HIROM_HEADER_OFFSET] = UINT8_C(0x31);

    if (!dkc2_bus_init(&bus, &image)) {
        free(image.data);
        return EXIT_FAILURE;
    }

    expect_region(&bus, UINT32_C(0x7E1234), DKC2_BUS_WRAM);
    expect_region(&bus, UINT32_C(0x001234), DKC2_BUS_WRAM);
    expect_region(&bus, UINT32_C(0x802100), DKC2_BUS_IO);
    expect_region(&bus, UINT32_C(0xB06000), DKC2_BUS_SRAM);
    expect_region(&bus, UINT32_C(0xC00000), DKC2_BUS_ROM);
    expect_region(&bus, UINT32_C(0x006000), DKC2_BUS_OPEN);

    dkc2_bus_write8(&bus, UINT32_C(0x7E0123), UINT8_C(0xA5));
    expect_read(&bus, UINT32_C(0x000123), UINT8_C(0xA5));
    expect_read(&bus, UINT32_C(0x800123), UINT8_C(0xA5));

    dkc2_bus_write8(&bus, UINT32_C(0x7F0001), UINT8_C(0x5A));
    expect_read(&bus, UINT32_C(0x7F0001), UINT8_C(0x5A));

    dkc2_bus_write8(&bus, UINT32_C(0xB06000), UINT8_C(0x17));
    expect_read(&bus, UINT32_C(0x306000), UINT8_C(0x17));
    expect_read(&bus, UINT32_C(0xA06800), UINT8_C(0x17));

    memset(save_in, UINT8_C(0x6B), sizeof(save_in));
    memset(save_out, 0, sizeof(save_out));
    if (!dkc2_bus_load_sram(&bus, save_in, sizeof(save_in)) ||
        !dkc2_bus_copy_sram(&bus, save_out, sizeof(save_out)) ||
        memcmp(save_in, save_out, sizeof(save_in)) != 0 ||
        dkc2_bus_load_sram(&bus, save_in, sizeof(save_in) - 1)) {
        (void)fprintf(stderr, "SRAM persistence helpers failed\n");
        dkc2_bus_free(&bus);
        free(image.data);
        return EXIT_FAILURE;
    }

    expect_read(&bus, UINT32_C(0xC00000), UINT8_C(0x42));
    dkc2_bus_write8(&bus, UINT32_C(0xC00000), UINT8_C(0x99));
    expect_read(&bus, UINT32_C(0x400000), UINT8_C(0x42));

    dkc2_bus_write8(&bus, UINT32_C(0x006000), UINT8_C(0xCC));
    expect_read(&bus, UINT32_C(0x006001), UINT8_C(0xCC));

    dkc2_bus_set_io(&bus, io_read, io_write, &io);
    dkc2_bus_write8(&bus, UINT32_C(0x802100), UINT8_C(0x8F));
    expect_read(&bus, UINT32_C(0x002100), UINT8_C(0x8F));
    if (io.reads != 1 || io.writes != 1) {
        (void)fprintf(stderr, "I/O callbacks did not receive mirrored accesses\n");
        dkc2_bus_free(&bus);
        free(image.data);
        return EXIT_FAILURE;
    }

    dkc2_bus_write8(&bus, UINT32_C(0x802000), UINT8_C(0xD3));
    expect_read(&bus, UINT32_C(0x002001), UINT8_C(0xD3));

    accesses_before = bus.accesses;
    dkc2_bus_write8(&bus, UINT32_C(0x7E0000), UINT8_C(0x5C));
    expect_read(&bus, UINT32_C(0x7E0000), UINT8_C(0x5C));
    if (bus.accesses != accesses_before + 2U) {
        (void)fprintf(stderr, "A-bus access accounting is incorrect\n");
        dkc2_bus_free(&bus);
        free(image.data);
        return EXIT_FAILURE;
    }

    dkc2_bus_free(&bus);
    free(image.data);
    (void)puts("SNES bus routing tests passed");
    return EXIT_SUCCESS;
}
