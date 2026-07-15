#include "dkc2/rom.h"

#include "dkc2/hirom.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    SNES_TITLE_SIZE = 21,
    COPIER_HEADER_SIZE = 512,
    ROM_BLOCK_SIZE = 32768
};

static uint16_t read_le16(const uint8_t *bytes) {
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static void copy_title(char destination[22], const uint8_t *source) {
    size_t length = SNES_TITLE_SIZE;

    while (length > 0 && (source[length - 1] == ' ' || source[length - 1] == 0)) {
        --length;
    }
    memcpy(destination, source, length);
    destination[length] = '\0';
}

static bool read_file(const char *path,
                      uint8_t **data,
                      size_t *size,
                      char *error,
                      size_t error_size) {
    FILE *file;
    long length;
    uint8_t *buffer;
    size_t bytes_read;
    int close_result;

    if (path == NULL || data == NULL || size == NULL) {
        set_error(error, error_size, "ROM path and output pointers are required");
        return false;
    }

    file = fopen(path, "rb");
    if (file == NULL) {
        char message[256];
        (void)snprintf(message,
                       sizeof(message),
                       "cannot open ROM: %s",
                       strerror(errno));
        set_error(error, error_size, message);
        return false;
    }

    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) {
        (void)fclose(file);
        set_error(error, error_size, "cannot determine ROM size");
        return false;
    }

    buffer = (uint8_t *)malloc((size_t)length == 0 ? 1 : (size_t)length);
    if (buffer == NULL) {
        (void)fclose(file);
        set_error(error, error_size, "not enough memory to inspect ROM");
        return false;
    }

    bytes_read = fread(buffer, 1, (size_t)length, file);
    close_result = fclose(file);
    if (bytes_read != (size_t)length || close_result != 0) {
        free(buffer);
        set_error(error, error_size, "cannot read complete ROM");
        return false;
    }

    *data = buffer;
    *size = (size_t)length;
    return true;
}

bool dkc2_rom_inspect(const uint8_t *file_data,
                      size_t file_size,
                      dkc2_rom_report *report,
                      char *error,
                      size_t error_size) {
    const uint8_t *payload;
    const uint8_t *header;
    size_t payload_size;
    size_t copier_header_size;

    if (file_data == NULL || report == NULL) {
        set_error(error, error_size, "ROM data and report output are required");
        return false;
    }

    memset(report, 0, sizeof(*report));
    report->file_size = file_size;

    copier_header_size =
        file_size > COPIER_HEADER_SIZE &&
        file_size % ROM_BLOCK_SIZE == COPIER_HEADER_SIZE
            ? COPIER_HEADER_SIZE
            : 0;
    payload = file_data + copier_header_size;
    payload_size = file_size - copier_header_size;

    report->copier_header_size = copier_header_size;
    report->payload_size = payload_size;
    report->crc32 = dkc2_crc32(payload, payload_size);
    dkc2_sha256_hex(payload, payload_size, report->sha256);

    if (payload_size >= DKC2_HIROM_HEADER_OFFSET + 0x40U) {
        header = payload + DKC2_HIROM_HEADER_OFFSET;
        report->header_available = true;
        copy_title(report->title, header);
        report->map_mode = header[0x15];
        report->cartridge_type = header[0x16];
        report->rom_size_code = header[0x17];
        report->sram_size_code = header[0x18];
        report->destination_code = header[0x19];
        report->version = header[0x1B];
        report->checksum_complement = read_le16(header + 0x1C);
        report->checksum = read_le16(header + 0x1E);
        report->native_nmi_vector =
            read_le16(payload + DKC2_HIROM_NATIVE_NMI_VECTOR_OFFSET);
        report->native_irq_vector =
            read_le16(payload + DKC2_HIROM_NATIVE_IRQ_VECTOR_OFFSET);
        report->reset_vector =
            read_le16(payload + DKC2_HIROM_RESET_VECTOR_OFFSET);
    }

    report->payload_matches_usa_v10 =
        payload_size == DKC2_USA_V10_ROM_SIZE &&
        report->crc32 == DKC2_USA_V10_CRC32 &&
        strcmp(report->sha256, DKC2_USA_V10_SHA256) == 0;
    report->ready_for_build =
        report->payload_matches_usa_v10 && copier_header_size == 0;

    return true;
}

bool dkc2_rom_inspect_file(const char *path,
                           dkc2_rom_report *report,
                           char *error,
                           size_t error_size) {
    uint8_t *data;
    size_t size;
    bool result;

    if (report == NULL) {
        set_error(error, error_size, "ROM report output is required");
        return false;
    }

    if (!read_file(path, &data, &size, error, error_size)) {
        return false;
    }

    result = dkc2_rom_inspect(data, size, report, error, error_size);
    free(data);
    return result;
}

bool dkc2_rom_image_load(const char *path,
                         dkc2_rom_image *image,
                         char *error,
                         size_t error_size) {
    uint8_t *data;
    size_t size;
    dkc2_rom_report report;

    if (image == NULL) {
        set_error(error, error_size, "ROM image output is required");
        return false;
    }
    memset(image, 0, sizeof(*image));

    if (!read_file(path, &data, &size, error, error_size)) {
        return false;
    }
    if (!dkc2_rom_inspect(data, size, &report, error, error_size)) {
        free(data);
        return false;
    }
    if (!report.ready_for_build) {
        free(data);
        set_error(error,
                  error_size,
                  "analysis requires the exact headerless DKC2 USA v1.0 ROM");
        return false;
    }

    image->data = data;
    image->size = size;
    image->report = report;
    return true;
}

void dkc2_rom_image_free(dkc2_rom_image *image) {
    if (image != NULL) {
        free(image->data);
        memset(image, 0, sizeof(*image));
    }
}

bool dkc2_rom_image_read8(const dkc2_rom_image *image,
                          uint32_t snes_address,
                          uint8_t *value) {
    size_t offset;

    if (image == NULL || image->data == NULL || value == NULL ||
        !dkc2_hirom_snes_to_rom(snes_address, image->size, &offset)) {
        return false;
    }

    *value = image->data[offset];
    return true;
}
