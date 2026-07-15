#include "dkc2/bus.h"

#include "dkc2/hirom.h"

#include <stdlib.h>
#include <string.h>

static bool is_system_bank(uint8_t bank) {
    return bank <= UINT8_C(0x3F) ||
           (bank >= UINT8_C(0x80) && bank <= UINT8_C(0xBF));
}

static bool is_dkc2_sram_bank(uint8_t bank) {
    uint8_t slow_bank = (uint8_t)(bank & UINT8_C(0x7F));
    return slow_bank >= UINT8_C(0x20) && slow_bank <= UINT8_C(0x3F);
}

bool dkc2_bus_init(dkc2_bus *bus, const dkc2_rom_image *rom) {
    if (bus == NULL || rom == NULL || rom->data == NULL || rom->size == 0) {
        return false;
    }

    memset(bus, 0, sizeof(*bus));
    bus->wram = (uint8_t *)calloc(DKC2_WRAM_SIZE, 1);
    bus->sram = (uint8_t *)calloc(DKC2_SRAM_SIZE, 1);
    if (bus->wram == NULL || bus->sram == NULL) {
        dkc2_bus_free(bus);
        return false;
    }
    bus->rom = rom;
    return true;
}

void dkc2_bus_free(dkc2_bus *bus) {
    if (bus != NULL) {
        free(bus->sram);
        free(bus->wram);
        memset(bus, 0, sizeof(*bus));
    }
}

void dkc2_bus_set_io(dkc2_bus *bus,
                     dkc2_bus_io_read_fn read_callback,
                     dkc2_bus_io_write_fn write_callback,
                     void *context) {
    if (bus != NULL) {
        bus->io_read = read_callback;
        bus->io_write = write_callback;
        bus->io_context = context;
    }
}

bool dkc2_bus_load_sram(dkc2_bus *bus,
                        const uint8_t *data,
                        size_t size) {
    if (bus == NULL || bus->sram == NULL || data == NULL ||
        size != DKC2_SRAM_SIZE) {
        return false;
    }
    memcpy(bus->sram, data, DKC2_SRAM_SIZE);
    return true;
}

bool dkc2_bus_copy_sram(const dkc2_bus *bus,
                        uint8_t *data,
                        size_t size) {
    if (bus == NULL || bus->sram == NULL || data == NULL ||
        size != DKC2_SRAM_SIZE) {
        return false;
    }
    memcpy(data, bus->sram, DKC2_SRAM_SIZE);
    return true;
}

dkc2_bus_region dkc2_bus_region_for(const dkc2_bus *bus,
                                    uint32_t address) {
    uint8_t bank;
    uint16_t offset;
    size_t rom_offset;

    if (bus == NULL) {
        return DKC2_BUS_OPEN;
    }

    address &= UINT32_C(0xFFFFFF);
    bank = (uint8_t)(address >> 16);
    offset = (uint16_t)address;
    if (bank == UINT8_C(0x7E) || bank == UINT8_C(0x7F)) {
        return DKC2_BUS_WRAM;
    }
    if (is_system_bank(bank)) {
        if (offset < UINT16_C(0x2000)) {
            return DKC2_BUS_WRAM;
        }
        if (offset < UINT16_C(0x6000)) {
            return DKC2_BUS_IO;
        }
        if (offset < UINT16_C(0x8000) && is_dkc2_sram_bank(bank)) {
            return DKC2_BUS_SRAM;
        }
    }
    if (bus->rom != NULL &&
        dkc2_hirom_snes_to_rom(address, bus->rom->size, &rom_offset)) {
        return DKC2_BUS_ROM;
    }
    return DKC2_BUS_OPEN;
}

const char *dkc2_bus_region_name(dkc2_bus_region region) {
    switch (region) {
        case DKC2_BUS_OPEN:
            return "open bus";
        case DKC2_BUS_WRAM:
            return "WRAM";
        case DKC2_BUS_IO:
            return "I/O";
        case DKC2_BUS_SRAM:
            return "SRAM";
        case DKC2_BUS_ROM:
            return "ROM";
    }
    return "unknown";
}

static size_t wram_offset(uint8_t bank, uint16_t offset) {
    if (bank == UINT8_C(0x7E) || bank == UINT8_C(0x7F)) {
        return ((size_t)(bank - UINT8_C(0x7E)) << 16) | offset;
    }
    return (size_t)(offset & UINT16_C(0x1FFF));
}

static size_t sram_offset(uint16_t offset) {
    /* DKC2 has 2 KiB SRAM mirrored through each 8 KiB mapping window. */
    return (size_t)((offset - UINT16_C(0x6000)) &
                    (uint16_t)(DKC2_SRAM_SIZE - 1));
}

uint8_t dkc2_bus_read8(dkc2_bus *bus, uint32_t address) {
    uint8_t bank;
    uint16_t offset;
    uint8_t value;

    if (bus == NULL) {
        return 0;
    }
    address &= UINT32_C(0xFFFFFF);
    bank = (uint8_t)(address >> 16);
    offset = (uint16_t)address;

    switch (dkc2_bus_region_for(bus, address)) {
        case DKC2_BUS_WRAM:
            value = bus->wram[wram_offset(bank, offset)];
            break;
        case DKC2_BUS_SRAM:
            value = bus->sram[sram_offset(offset)];
            break;
        case DKC2_BUS_ROM:
            if (!dkc2_rom_image_read8(bus->rom, address, &value)) {
                return bus->open_bus;
            }
            break;
        case DKC2_BUS_IO:
            value = bus->open_bus;
            if (bus->io_read == NULL ||
                !bus->io_read(bus->io_context, address, &value)) {
                return bus->open_bus;
            }
            break;
        case DKC2_BUS_OPEN:
            return bus->open_bus;
    }

    bus->open_bus = value;
    return value;
}

void dkc2_bus_write8(dkc2_bus *bus, uint32_t address, uint8_t value) {
    uint8_t bank;
    uint16_t offset;

    if (bus == NULL) {
        return;
    }
    address &= UINT32_C(0xFFFFFF);
    bank = (uint8_t)(address >> 16);
    offset = (uint16_t)address;
    bus->open_bus = value;

    switch (dkc2_bus_region_for(bus, address)) {
        case DKC2_BUS_WRAM:
            bus->wram[wram_offset(bank, offset)] = value;
            break;
        case DKC2_BUS_SRAM:
            bus->sram[sram_offset(offset)] = value;
            break;
        case DKC2_BUS_IO:
            if (bus->io_write != NULL) {
                (void)bus->io_write(bus->io_context, address, value);
            }
            break;
        case DKC2_BUS_OPEN:
        case DKC2_BUS_ROM:
            break;
    }
}

uint8_t dkc2_bus_memory_read8(void *context, uint32_t address) {
    return dkc2_bus_read8((dkc2_bus *)context, address);
}

void dkc2_bus_memory_write8(void *context,
                            uint32_t address,
                            uint8_t value) {
    dkc2_bus_write8((dkc2_bus *)context, address, value);
}
