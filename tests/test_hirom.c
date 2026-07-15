#include "dkc2/hirom.h"

#include <stdio.h>
#include <stdlib.h>

#define ROM_SIZE ((size_t)4 * 1024 * 1024)

static void expect_snes_to_rom(uint32_t address, size_t expected) {
    size_t actual = 0;
    if (!dkc2_hirom_snes_to_rom(address, ROM_SIZE, &actual) || actual != expected) {
        (void)fprintf(stderr,
                      "SNES $%06X mapped to $%zX; expected $%zX\n",
                      (unsigned)address,
                      actual,
                      expected);
        exit(EXIT_FAILURE);
    }
}

static void expect_not_rom(uint32_t address) {
    size_t ignored = 0;
    if (dkc2_hirom_snes_to_rom(address, ROM_SIZE, &ignored)) {
        (void)fprintf(stderr, "SNES $%06X unexpectedly mapped as ROM\n", (unsigned)address);
        exit(EXIT_FAILURE);
    }
}

static void expect_rom_to_snes(size_t offset, uint32_t expected) {
    uint32_t actual = 0;
    if (!dkc2_hirom_rom_to_snes(offset, ROM_SIZE, &actual) || actual != expected) {
        (void)fprintf(stderr,
                      "ROM $%zX mapped to $%06X; expected $%06X\n",
                      offset,
                      (unsigned)actual,
                      (unsigned)expected);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    expect_snes_to_rom(UINT32_C(0xC00000), 0x000000);
    expect_snes_to_rom(UINT32_C(0xC0FFC0), DKC2_HIROM_HEADER_OFFSET);
    expect_snes_to_rom(UINT32_C(0x80FFC0), DKC2_HIROM_HEADER_OFFSET);
    expect_snes_to_rom(UINT32_C(0xB38000), 0x338000);
    expect_snes_to_rom(UINT32_C(0xF70000), 0x370000);
    expect_snes_to_rom(UINT32_C(0xFFFFFC), 0x3FFFFC);
    expect_snes_to_rom(UINT32_C(0x400000), 0x000000);
    expect_snes_to_rom(UINT32_C(0x7DFFFF), 0x3DFFFF);

    expect_not_rom(UINT32_C(0x007FFF));
    expect_not_rom(UINT32_C(0x807FFF));
    expect_not_rom(UINT32_C(0x7E8000));
    expect_not_rom(UINT32_C(0x7F0000));

    expect_rom_to_snes(0x000000, UINT32_C(0xC00000));
    expect_rom_to_snes(DKC2_HIROM_HEADER_OFFSET, UINT32_C(0xC0FFC0));
    expect_rom_to_snes(0x338000, UINT32_C(0xF38000));
    expect_rom_to_snes(0x3FFFFC, UINT32_C(0xFFFFFC));

    {
        uint32_t ignored = 0;
        if (dkc2_hirom_rom_to_snes(ROM_SIZE, ROM_SIZE, &ignored)) {
            (void)fprintf(stderr, "out-of-range ROM offset unexpectedly mapped\n");
            return EXIT_FAILURE;
        }
    }

    (void)puts("HiROM mapping tests passed");
    return EXIT_SUCCESS;
}
