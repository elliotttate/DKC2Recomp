#include "dkc2/symbols.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef TEST_SYMBOL_PATH
#define TEST_SYMBOL_PATH "tests/fixtures/test_symbols.sym"
#endif

static void expect_symbol(const dkc2_symbol_table *table,
                          uint32_t address,
                          const char *expected) {
    const char *actual = dkc2_symbols_lookup(table, address);
    if (actual == NULL || strcmp(actual, expected) != 0) {
        (void)fprintf(stderr,
                      "$%06X resolved to '%s'; expected '%s'\n",
                      (unsigned)address,
                      actual == NULL ? "<none>" : actual,
                      expected);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    dkc2_symbol_table table;
    char error[256];

    if (!dkc2_symbols_load_wla(TEST_SYMBOL_PATH, &table, error, sizeof(error))) {
        (void)fprintf(stderr, "cannot load symbol fixture: %s\n", error);
        return EXIT_FAILURE;
    }

    expect_symbol(&table, UINT32_C(0x8083F7), "RESET_start");
    expect_symbol(&table, UINT32_C(0x0083F7), "RESET_start");
    expect_symbol(&table, UINT32_C(0x80909A), "init_logo");
    expect_symbol(&table, UINT32_C(0xB58000), "upload_audio_engine");

    if (dkc2_symbols_lookup(&table, UINT32_C(0x80FFFF)) != NULL) {
        (void)fprintf(stderr, "unknown address unexpectedly had a symbol\n");
        dkc2_symbols_free(&table);
        return EXIT_FAILURE;
    }

    dkc2_symbols_free(&table);
    (void)puts("WLA symbol-map tests passed");
    return EXIT_SUCCESS;
}
