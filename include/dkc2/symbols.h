#ifndef DKC2_SYMBOLS_H
#define DKC2_SYMBOLS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dkc2_symbol {
    uint32_t address;
    char *name;
} dkc2_symbol;

typedef struct dkc2_symbol_table {
    dkc2_symbol *entries;
    size_t count;
    size_t capacity;
} dkc2_symbol_table;

/* Loads the [labels] section of an Asar WLA symbol file. */
bool dkc2_symbols_load_wla(const char *path,
                           dkc2_symbol_table *table,
                           char *error,
                           size_t error_size);

void dkc2_symbols_free(dkc2_symbol_table *table);

/* Accepts either canonical ROM banks or their lower-bank HiROM mirrors. */
const char *dkc2_symbols_lookup(const dkc2_symbol_table *table,
                                uint32_t snes_address);

#ifdef __cplusplus
}
#endif

#endif
