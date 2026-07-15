#include "dkc2/rom.h"

#include "dkc2/hirom.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_le16(uint8_t *destination, uint16_t value) {
    destination[0] = (uint8_t)value;
    destination[1] = (uint8_t)(value >> 8);
}

int main(void) {
    const size_t size = 0x10000;
    uint8_t *rom = (uint8_t *)calloc(size, 1);
    dkc2_rom_report report;
    dkc2_rom_report headered_report;
    char error[128];
    uint8_t *header;
    uint8_t *headered_rom;

    if (rom == NULL) {
        return EXIT_FAILURE;
    }

    header = rom + DKC2_HIROM_HEADER_OFFSET;
    memcpy(header, "DIDDY'S KONG QUEST   ", 21);
    header[0x15] = UINT8_C(0x31);
    header[0x16] = UINT8_C(0x02);
    header[0x17] = UINT8_C(0x0C);
    header[0x18] = UINT8_C(0x01);
    header[0x19] = UINT8_C(0x01);
    header[0x1B] = UINT8_C(0x00);
    write_le16(header + 0x1C, UINT16_C(0xEDFD));
    write_le16(header + 0x1E, UINT16_C(0x1202));
    write_le16(rom + DKC2_HIROM_NATIVE_NMI_VECTOR_OFFSET, UINT16_C(0x8123));
    write_le16(rom + DKC2_HIROM_NATIVE_IRQ_VECTOR_OFFSET, UINT16_C(0x8456));
    write_le16(rom + DKC2_HIROM_RESET_VECTOR_OFFSET, UINT16_C(0x8789));

    if (!dkc2_rom_inspect(rom, size, &report, error, sizeof(error))) {
        (void)fprintf(stderr, "inspection failed: %s\n", error);
        free(rom);
        return EXIT_FAILURE;
    }

    headered_rom = (uint8_t *)calloc(size + 512, 1);
    if (headered_rom == NULL) {
        free(rom);
        return EXIT_FAILURE;
    }
    memcpy(headered_rom + 512, rom, size);
    if (!dkc2_rom_inspect(headered_rom,
                          size + 512,
                          &headered_report,
                          error,
                          sizeof(error))) {
        (void)fprintf(stderr, "headered inspection failed: %s\n", error);
        free(headered_rom);
        free(rom);
        return EXIT_FAILURE;
    }

    free(headered_rom);
    free(rom);

    if (!report.header_available || strcmp(report.title, "DIDDY'S KONG QUEST") != 0 ||
        report.map_mode != UINT8_C(0x31) || report.cartridge_type != UINT8_C(0x02) ||
        report.rom_size_code != UINT8_C(0x0C) || report.sram_size_code != UINT8_C(0x01) ||
        report.destination_code != UINT8_C(0x01) || report.version != UINT8_C(0x00) ||
        report.checksum_complement != UINT16_C(0xEDFD) ||
        report.checksum != UINT16_C(0x1202) ||
        report.native_nmi_vector != UINT16_C(0x8123) ||
        report.native_irq_vector != UINT16_C(0x8456) ||
        report.reset_vector != UINT16_C(0x8789) || report.ready_for_build) {
        (void)fprintf(stderr, "parsed ROM metadata did not match fixture\n");
        return EXIT_FAILURE;
    }

    if (headered_report.copier_header_size != 512 ||
        headered_report.payload_size != size ||
        !headered_report.header_available ||
        strcmp(headered_report.title, "DIDDY'S KONG QUEST") != 0) {
        (void)fprintf(stderr, "512-byte copier header was not detected correctly\n");
        return EXIT_FAILURE;
    }

    (void)puts("ROM metadata tests passed");
    return EXIT_SUCCESS;
}
