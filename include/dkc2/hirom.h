#ifndef DKC2_HIROM_H
#define DKC2_HIROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DKC2_HIROM_MAX_ROM_SIZE = 4 * 1024 * 1024,
    DKC2_HIROM_HEADER_OFFSET = 0x00FFC0,
    DKC2_HIROM_NATIVE_NMI_VECTOR_OFFSET = 0x00FFEA,
    DKC2_HIROM_NATIVE_IRQ_VECTOR_OFFSET = 0x00FFEE,
    DKC2_HIROM_RESET_VECTOR_OFFSET = 0x00FFFC
};

/*
 * Converts a 24-bit SNES bus address to an offset in a HiROM image.
 *
 * This deliberately handles ROM windows only. WRAM, I/O, SRAM, and unmapped
 * regions return false and will be implemented by the future bus layer.
 */
bool dkc2_hirom_snes_to_rom(uint32_t snes_address,
                            size_t rom_size,
                            size_t *rom_offset);

/* Returns the canonical $C0-$FF HiROM address for a ROM offset. */
bool dkc2_hirom_rom_to_snes(size_t rom_offset,
                            size_t rom_size,
                            uint32_t *snes_address);

#ifdef __cplusplus
}
#endif

#endif
