#include "dkc2/hirom.h"

bool dkc2_hirom_snes_to_rom(uint32_t snes_address,
                            size_t rom_size,
                            size_t *rom_offset) {
    uint8_t bank;
    uint16_t address;
    bool full_bank_window;
    bool upper_half_window;
    size_t candidate;

    if (rom_offset == NULL || rom_size == 0 ||
        rom_size > DKC2_HIROM_MAX_ROM_SIZE || snes_address > UINT32_C(0xFFFFFF)) {
        return false;
    }

    bank = (uint8_t)(snes_address >> 16);
    address = (uint16_t)snes_address;

    /* $7E-$7F are always WRAM. */
    if (bank == UINT8_C(0x7E) || bank == UINT8_C(0x7F)) {
        return false;
    }

    full_bank_window =
        (bank >= UINT8_C(0x40) && bank <= UINT8_C(0x7D)) ||
        bank >= UINT8_C(0xC0);
    upper_half_window =
        (bank <= UINT8_C(0x3F) ||
         (bank >= UINT8_C(0x80) && bank <= UINT8_C(0xBF))) &&
        address >= UINT16_C(0x8000);

    if (!full_bank_window && !upper_half_window) {
        return false;
    }

    candidate = ((size_t)(bank & UINT8_C(0x3F)) << 16) | address;
    if (candidate >= rom_size) {
        return false;
    }

    *rom_offset = candidate;
    return true;
}

bool dkc2_hirom_rom_to_snes(size_t rom_offset,
                            size_t rom_size,
                            uint32_t *snes_address) {
    uint32_t bank;

    if (snes_address == NULL || rom_size == 0 ||
        rom_size > DKC2_HIROM_MAX_ROM_SIZE || rom_offset >= rom_size) {
        return false;
    }

    bank = UINT32_C(0xC0) + (uint32_t)(rom_offset >> 16);
    *snes_address = (bank << 16) | (uint32_t)(rom_offset & 0xFFFFU);
    return true;
}
