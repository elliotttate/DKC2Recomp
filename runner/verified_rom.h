#pragma once

#include <stddef.h>
#include <stdint.h>

/* Loads the caller-owned private ROM, removes an optional 512-byte copier
 * header, and accepts only the supported headerless USA v1.0 payload. The
 * returned buffer belongs to the caller and must be released with free(). */
uint8_t *Dkc2ReadVerifiedRom(const char *path, size_t *size_out,
                             char *error, size_t error_size);
