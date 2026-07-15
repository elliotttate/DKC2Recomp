#ifndef DKC2_ROM_H
#define DKC2_ROM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dkc2/hash.h"

#ifdef __cplusplus
extern "C" {
#endif

#define DKC2_USA_V10_ROM_SIZE ((size_t)4194304)
#define DKC2_USA_V10_CRC32 UINT32_C(0x006364DB)
#define DKC2_USA_V10_SHA256 \
    "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"

typedef struct dkc2_rom_report {
    size_t file_size;
    size_t payload_size;
    size_t copier_header_size;
    uint32_t crc32;
    char sha256[DKC2_SHA256_HEX_SIZE];
    char title[22];
    uint8_t map_mode;
    uint8_t cartridge_type;
    uint8_t rom_size_code;
    uint8_t sram_size_code;
    uint8_t destination_code;
    uint8_t version;
    uint16_t checksum_complement;
    uint16_t checksum;
    uint16_t native_nmi_vector;
    uint16_t native_irq_vector;
    uint16_t reset_vector;
    bool header_available;
    bool payload_matches_usa_v10;
    bool ready_for_build;
} dkc2_rom_report;

typedef struct dkc2_rom_image {
    uint8_t *data;
    size_t size;
    dkc2_rom_report report;
} dkc2_rom_image;

/* Inspects bytes already loaded in memory. No data is modified. */
bool dkc2_rom_inspect(const uint8_t *file_data,
                      size_t file_size,
                      dkc2_rom_report *report,
                      char *error,
                      size_t error_size);

/* Loads and inspects a ROM. The caller owns neither ROM data nor resources. */
bool dkc2_rom_inspect_file(const char *path,
                           dkc2_rom_report *report,
                           char *error,
                           size_t error_size);

/* Loads only the exact supported, headerless ROM into a read-only analysis image. */
bool dkc2_rom_image_load(const char *path,
                         dkc2_rom_image *image,
                         char *error,
                         size_t error_size);

void dkc2_rom_image_free(dkc2_rom_image *image);

/* Reads a byte from a ROM-mapped SNES bus address. */
bool dkc2_rom_image_read8(const dkc2_rom_image *image,
                          uint32_t snes_address,
                          uint8_t *value);

#ifdef __cplusplus
}
#endif

#endif
