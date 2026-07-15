#include "dkc2/hash.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void expect_sha256(const char *input, const char *expected) {
    char actual[DKC2_SHA256_HEX_SIZE];
    dkc2_sha256_hex(input, strlen(input), actual);
    if (strcmp(actual, expected) != 0) {
        (void)fprintf(stderr,
                      "SHA-256 mismatch for '%s'\nactual:   %s\nexpected: %s\n",
                      input,
                      actual,
                      expected);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    static const uint8_t crc_input[] = "123456789";

    expect_sha256("",
                  "e3b0c44298fc1c149afbf4c8996fb924"
                  "27ae41e4649b934ca495991b7852b855");
    expect_sha256("abc",
                  "ba7816bf8f01cfea414140de5dae2223"
                  "b00361a396177a9cb410ff61f20015ad");

    if (dkc2_crc32(crc_input, sizeof(crc_input) - 1) != UINT32_C(0xCBF43926)) {
        (void)fprintf(stderr, "CRC32 test vector mismatch\n");
        return EXIT_FAILURE;
    }

    (void)puts("Hash tests passed");
    return EXIT_SUCCESS;
}
