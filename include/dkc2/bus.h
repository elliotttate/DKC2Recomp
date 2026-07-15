#ifndef DKC2_BUS_H
#define DKC2_BUS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dkc2/rom.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DKC2_WRAM_SIZE ((size_t)128 * 1024)
#define DKC2_SRAM_SIZE ((size_t)2 * 1024)

typedef bool (*dkc2_bus_io_read_fn)(void *context,
                                    uint32_t address,
                                    uint8_t *value);
typedef bool (*dkc2_bus_io_write_fn)(void *context,
                                     uint32_t address,
                                     uint8_t value);

typedef enum dkc2_bus_region {
    DKC2_BUS_OPEN,
    DKC2_BUS_WRAM,
    DKC2_BUS_IO,
    DKC2_BUS_SRAM,
    DKC2_BUS_ROM
} dkc2_bus_region;

typedef struct dkc2_bus {
    const dkc2_rom_image *rom;
    uint8_t *wram;
    uint8_t *sram;
    uint8_t open_bus;
    dkc2_bus_io_read_fn io_read;
    dkc2_bus_io_write_fn io_write;
    void *io_context;
} dkc2_bus;

/* Allocates zeroed host-side WRAM and SRAM. The ROM remains caller-owned. */
bool dkc2_bus_init(dkc2_bus *bus, const dkc2_rom_image *rom);
void dkc2_bus_free(dkc2_bus *bus);

void dkc2_bus_set_io(dkc2_bus *bus,
                     dkc2_bus_io_read_fn read_callback,
                     dkc2_bus_io_write_fn write_callback,
                     void *context);

/* Exact-size helpers for host save-file persistence. */
bool dkc2_bus_load_sram(dkc2_bus *bus,
                        const uint8_t *data,
                        size_t size);
bool dkc2_bus_copy_sram(const dkc2_bus *bus,
                        uint8_t *data,
                        size_t size);

dkc2_bus_region dkc2_bus_region_for(const dkc2_bus *bus,
                                    uint32_t address);
const char *dkc2_bus_region_name(dkc2_bus_region region);

/* Reads always return a value; unmapped and unhandled I/O reads return MDR. */
uint8_t dkc2_bus_read8(dkc2_bus *bus, uint32_t address);

/* ROM/unmapped writes are ignored after updating the bus data latch. */
void dkc2_bus_write8(dkc2_bus *bus, uint32_t address, uint8_t value);

/* Generic callback adapters for dkc2_memory. */
uint8_t dkc2_bus_memory_read8(void *context, uint32_t address);
void dkc2_bus_memory_write8(void *context,
                            uint32_t address,
                            uint8_t value);

#ifdef __cplusplus
}
#endif

#endif
