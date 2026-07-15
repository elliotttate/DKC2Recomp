#include "dkc2/symbols.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void set_error(char *error, size_t error_size, const char *message) {
    if (error != NULL && error_size > 0) {
        (void)snprintf(error, error_size, "%s", message);
    }
}

static char *copy_string(const char *source) {
    size_t length = strlen(source) + 1;
    char *copy = (char *)malloc(length);
    if (copy != NULL) {
        memcpy(copy, source, length);
    }
    return copy;
}

static int name_priority(const char *name) {
    if (strncmp(name, "macro_", 6) == 0) {
        return 3;
    }
    if (strncmp(name, "DATA_", 5) == 0 || strncmp(name, "CODE_", 5) == 0) {
        return 2;
    }
    if (strchr(name, '.') != NULL) {
        return 1;
    }
    return 0;
}

static int compare_symbols(const void *left_pointer, const void *right_pointer) {
    const dkc2_symbol *left = (const dkc2_symbol *)left_pointer;
    const dkc2_symbol *right = (const dkc2_symbol *)right_pointer;
    int priority_difference;

    if (left->address < right->address) {
        return -1;
    }
    if (left->address > right->address) {
        return 1;
    }
    priority_difference = name_priority(left->name) - name_priority(right->name);
    if (priority_difference != 0) {
        return priority_difference;
    }
    return strcmp(left->name, right->name);
}

static bool append_symbol(dkc2_symbol_table *table,
                          uint32_t address,
                          const char *name) {
    dkc2_symbol *resized;
    char *name_copy;

    if (table->count == table->capacity) {
        size_t new_capacity = table->capacity == 0 ? 1024 : table->capacity * 2;
        if (new_capacity < table->capacity ||
            new_capacity > SIZE_MAX / sizeof(*table->entries)) {
            return false;
        }
        resized = (dkc2_symbol *)realloc(table->entries,
                                         new_capacity * sizeof(*table->entries));
        if (resized == NULL) {
            return false;
        }
        table->entries = resized;
        table->capacity = new_capacity;
    }

    name_copy = copy_string(name);
    if (name_copy == NULL) {
        return false;
    }
    table->entries[table->count].address = address;
    table->entries[table->count].name = name_copy;
    ++table->count;
    return true;
}

bool dkc2_symbols_load_wla(const char *path,
                           dkc2_symbol_table *table,
                           char *error,
                           size_t error_size) {
    FILE *file;
    char line[1024];
    bool in_labels = false;

    if (path == NULL || table == NULL) {
        set_error(error, error_size, "symbol path and table output are required");
        return false;
    }
    memset(table, 0, sizeof(*table));

    file = fopen(path, "r");
    if (file == NULL) {
        char message[256];
        (void)snprintf(message,
                       sizeof(message),
                       "cannot open symbol file: %s",
                       strerror(errno));
        set_error(error, error_size, message);
        return false;
    }

    while (fgets(line, sizeof(line), file) != NULL) {
        unsigned bank;
        unsigned offset;
        char name[768];

        if (line[0] == '[') {
            in_labels = strncmp(line, "[labels]", 8) == 0;
            continue;
        }
        if (!in_labels || line[0] == ';' || line[0] == '\n' || line[0] == '\r') {
            continue;
        }
        if (sscanf(line, "%2x:%4x %767s", &bank, &offset, name) == 3) {
            uint32_t address = ((uint32_t)bank << 16) | (uint32_t)offset;
            if (!append_symbol(table, address, name)) {
                (void)fclose(file);
                dkc2_symbols_free(table);
                set_error(error, error_size, "not enough memory for symbol table");
                return false;
            }
        }
    }

    if (ferror(file) || fclose(file) != 0) {
        dkc2_symbols_free(table);
        set_error(error, error_size, "cannot read complete symbol file");
        return false;
    }
    if (table->count == 0) {
        dkc2_symbols_free(table);
        set_error(error, error_size, "symbol file has no WLA [labels] entries");
        return false;
    }

    qsort(table->entries, table->count, sizeof(*table->entries), compare_symbols);
    return true;
}

void dkc2_symbols_free(dkc2_symbol_table *table) {
    size_t i;
    if (table != NULL) {
        for (i = 0; i < table->count; ++i) {
            free(table->entries[i].name);
        }
        free(table->entries);
        memset(table, 0, sizeof(*table));
    }
}

static uint32_t canonical_address(uint32_t address) {
    uint8_t bank = (uint8_t)(address >> 16);
    uint16_t offset = (uint16_t)address;

    if (bank <= UINT8_C(0x3F) && offset >= UINT16_C(0x8000)) {
        bank = (uint8_t)(bank | UINT8_C(0x80));
    } else if (bank >= UINT8_C(0x40) && bank <= UINT8_C(0x7D)) {
        bank = (uint8_t)(bank | UINT8_C(0x80));
    }
    return ((uint32_t)bank << 16) | offset;
}

const char *dkc2_symbols_lookup(const dkc2_symbol_table *table,
                                uint32_t snes_address) {
    uint32_t address;
    size_t low;
    size_t high;

    if (table == NULL || table->entries == NULL) {
        return NULL;
    }

    address = canonical_address(snes_address & UINT32_C(0xFFFFFF));
    low = 0;
    high = table->count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        if (table->entries[middle].address < address) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }

    return low < table->count && table->entries[low].address == address
               ? table->entries[low].name
               : NULL;
}
