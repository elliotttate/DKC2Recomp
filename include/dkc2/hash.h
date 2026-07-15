#ifndef DKC2_HASH_H
#define DKC2_HASH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DKC2_SHA256_DIGEST_SIZE = 32,
    DKC2_SHA256_HEX_SIZE = 65
};

uint32_t dkc2_crc32(const void *data, size_t size);

void dkc2_sha256(const void *data,
                 size_t size,
                 uint8_t digest[DKC2_SHA256_DIGEST_SIZE]);

void dkc2_sha256_hex(const void *data,
                     size_t size,
                     char hex[DKC2_SHA256_HEX_SIZE]);

#ifdef __cplusplus
}
#endif

#endif
