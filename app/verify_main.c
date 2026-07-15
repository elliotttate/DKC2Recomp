#include "dkc2/rom.h"

#include <inttypes.h>
#include <stdio.h>

static const char *mapper_description(uint8_t map_mode) {
    switch (map_mode) {
        case UINT8_C(0x21):
            return "HiROM/SlowROM";
        case UINT8_C(0x31):
            return "HiROM/FastROM";
        default:
            return "unrecognized";
    }
}

int main(int argc, char **argv) {
    dkc2_rom_report report;
    char error[256];

    if (argc != 2) {
        (void)fprintf(stderr, "Usage: %s <path-to-dkc2-rom>\n", argv[0]);
        return 64;
    }

    if (!dkc2_rom_inspect_file(argv[1], &report, error, sizeof(error))) {
        (void)fprintf(stderr, "ROM inspection failed: %s\n", error);
        return 1;
    }

    (void)printf("File:          %s\n", argv[1]);
    (void)printf("File size:     %zu bytes\n", report.file_size);
    (void)printf("Copier header: %s",
                 report.copier_header_size == 0 ? "none" : "present");
    if (report.copier_header_size != 0) {
        (void)printf(" (%zu bytes)", report.copier_header_size);
    }
    (void)printf("\n");
    (void)printf("Payload size:  %zu bytes\n", report.payload_size);
    (void)printf("CRC32:         %08" PRIX32 "\n", report.crc32);
    (void)printf("SHA-256:       %s\n", report.sha256);

    if (report.header_available) {
        (void)printf("Internal name: %s\n", report.title);
        (void)printf("Map mode:      $%02" PRIX8 " (%s)\n",
                     report.map_mode,
                     mapper_description(report.map_mode));
        (void)printf("ROM/SRAM code: $%02" PRIX8 "/$%02" PRIX8 "\n",
                     report.rom_size_code,
                     report.sram_size_code);
        (void)printf("Region/version:$%02" PRIX8 "/$%02" PRIX8 "\n",
                     report.destination_code,
                     report.version);
        (void)printf("Vectors:       NMI=$%04" PRIX16
                     " IRQ=$%04" PRIX16 " RESET=$%04" PRIX16 "\n",
                     report.native_nmi_vector,
                     report.native_irq_vector,
                     report.reset_vector);
    }

    if (report.ready_for_build) {
        (void)printf("Result:        supported DKC2 USA v1.0 baseline\n");
        return 0;
    }

    if (report.payload_matches_usa_v10) {
        (void)printf("Result:        correct ROM payload, but remove the 512-byte copier header\n");
        return 2;
    }

    (void)printf("Result:        unsupported ROM revision or modified data\n");
    return 3;
}
