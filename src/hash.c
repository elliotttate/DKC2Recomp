#include "dkc2/hash.h"

#include <string.h>

typedef struct sha256_context {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_size;
} sha256_context;

static const uint32_t sha256_round_constants[64] = {
    UINT32_C(0x428A2F98), UINT32_C(0x71374491), UINT32_C(0xB5C0FBCF), UINT32_C(0xE9B5DBA5),
    UINT32_C(0x3956C25B), UINT32_C(0x59F111F1), UINT32_C(0x923F82A4), UINT32_C(0xAB1C5ED5),
    UINT32_C(0xD807AA98), UINT32_C(0x12835B01), UINT32_C(0x243185BE), UINT32_C(0x550C7DC3),
    UINT32_C(0x72BE5D74), UINT32_C(0x80DEB1FE), UINT32_C(0x9BDC06A7), UINT32_C(0xC19BF174),
    UINT32_C(0xE49B69C1), UINT32_C(0xEFBE4786), UINT32_C(0x0FC19DC6), UINT32_C(0x240CA1CC),
    UINT32_C(0x2DE92C6F), UINT32_C(0x4A7484AA), UINT32_C(0x5CB0A9DC), UINT32_C(0x76F988DA),
    UINT32_C(0x983E5152), UINT32_C(0xA831C66D), UINT32_C(0xB00327C8), UINT32_C(0xBF597FC7),
    UINT32_C(0xC6E00BF3), UINT32_C(0xD5A79147), UINT32_C(0x06CA6351), UINT32_C(0x14292967),
    UINT32_C(0x27B70A85), UINT32_C(0x2E1B2138), UINT32_C(0x4D2C6DFC), UINT32_C(0x53380D13),
    UINT32_C(0x650A7354), UINT32_C(0x766A0ABB), UINT32_C(0x81C2C92E), UINT32_C(0x92722C85),
    UINT32_C(0xA2BFE8A1), UINT32_C(0xA81A664B), UINT32_C(0xC24B8B70), UINT32_C(0xC76C51A3),
    UINT32_C(0xD192E819), UINT32_C(0xD6990624), UINT32_C(0xF40E3585), UINT32_C(0x106AA070),
    UINT32_C(0x19A4C116), UINT32_C(0x1E376C08), UINT32_C(0x2748774C), UINT32_C(0x34B0BCB5),
    UINT32_C(0x391C0CB3), UINT32_C(0x4ED8AA4A), UINT32_C(0x5B9CCA4F), UINT32_C(0x682E6FF3),
    UINT32_C(0x748F82EE), UINT32_C(0x78A5636F), UINT32_C(0x84C87814), UINT32_C(0x8CC70208),
    UINT32_C(0x90BEFFFA), UINT32_C(0xA4506CEB), UINT32_C(0xBEF9A3F7), UINT32_C(0xC67178F2)
};

static uint32_t rotate_right(uint32_t value, unsigned amount) {
    return (value >> amount) | (value << (32U - amount));
}

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void sha256_transform(sha256_context *context, const uint8_t block[64]) {
    uint32_t words[64];
    uint32_t a;
    uint32_t b;
    uint32_t c;
    uint32_t d;
    uint32_t e;
    uint32_t f;
    uint32_t g;
    uint32_t h;
    size_t i;

    for (i = 0; i < 16; ++i) {
        words[i] = read_be32(block + i * 4);
    }
    for (i = 16; i < 64; ++i) {
        uint32_t s0 = rotate_right(words[i - 15], 7) ^
                      rotate_right(words[i - 15], 18) ^
                      (words[i - 15] >> 3);
        uint32_t s1 = rotate_right(words[i - 2], 17) ^
                      rotate_right(words[i - 2], 19) ^
                      (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    a = context->state[0];
    b = context->state[1];
    c = context->state[2];
    d = context->state[3];
    e = context->state[4];
    f = context->state[5];
    g = context->state[6];
    h = context->state[7];

    for (i = 0; i < 64; ++i) {
        uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^ rotate_right(e, 25);
        uint32_t choose = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + sum1 + choose + sha256_round_constants[i] + words[i];
        uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^ rotate_right(a, 22);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = sum0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

static void sha256_init(sha256_context *context) {
    static const uint32_t initial_state[8] = {
        UINT32_C(0x6A09E667), UINT32_C(0xBB67AE85), UINT32_C(0x3C6EF372), UINT32_C(0xA54FF53A),
        UINT32_C(0x510E527F), UINT32_C(0x9B05688C), UINT32_C(0x1F83D9AB), UINT32_C(0x5BE0CD19)
    };

    memcpy(context->state, initial_state, sizeof(initial_state));
    context->bit_count = 0;
    context->buffer_size = 0;
}

static void sha256_update(sha256_context *context, const uint8_t *data, size_t size) {
    while (size > 0) {
        size_t available = sizeof(context->buffer) - context->buffer_size;
        size_t amount = size < available ? size : available;

        memcpy(context->buffer + context->buffer_size, data, amount);
        context->buffer_size += amount;
        context->bit_count += (uint64_t)amount * UINT64_C(8);
        data += amount;
        size -= amount;

        if (context->buffer_size == sizeof(context->buffer)) {
            sha256_transform(context, context->buffer);
            context->buffer_size = 0;
        }
    }
}

static void sha256_finish(sha256_context *context,
                          uint8_t digest[DKC2_SHA256_DIGEST_SIZE]) {
    uint64_t bit_count = context->bit_count;
    size_t i;

    context->buffer[context->buffer_size++] = UINT8_C(0x80);
    if (context->buffer_size > 56) {
        memset(context->buffer + context->buffer_size,
               0,
               sizeof(context->buffer) - context->buffer_size);
        sha256_transform(context, context->buffer);
        context->buffer_size = 0;
    }

    memset(context->buffer + context->buffer_size, 0, 56 - context->buffer_size);
    for (i = 0; i < 8; ++i) {
        context->buffer[63 - i] = (uint8_t)(bit_count >> (i * 8));
    }
    sha256_transform(context, context->buffer);

    for (i = 0; i < 8; ++i) {
        write_be32(digest + i * 4, context->state[i]);
    }
}

uint32_t dkc2_crc32(const void *data, size_t size) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = UINT32_C(0xFFFFFFFF);
    size_t i;

    for (i = 0; i < size; ++i) {
        unsigned bit;
        crc ^= bytes[i];
        for (bit = 0; bit < 8; ++bit) {
            uint32_t mask = (uint32_t)-(int32_t)(crc & UINT32_C(1));
            crc = (crc >> 1) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }

    return crc ^ UINT32_C(0xFFFFFFFF);
}

void dkc2_sha256(const void *data,
                 size_t size,
                 uint8_t digest[DKC2_SHA256_DIGEST_SIZE]) {
    sha256_context context;

    sha256_init(&context);
    sha256_update(&context, (const uint8_t *)data, size);
    sha256_finish(&context, digest);
}

void dkc2_sha256_hex(const void *data,
                     size_t size,
                     char hex[DKC2_SHA256_HEX_SIZE]) {
    static const char digits[] = "0123456789abcdef";
    uint8_t digest[DKC2_SHA256_DIGEST_SIZE];
    size_t i;

    dkc2_sha256(data, size, digest);
    for (i = 0; i < DKC2_SHA256_DIGEST_SIZE; ++i) {
        hex[i * 2] = digits[digest[i] >> 4];
        hex[i * 2 + 1] = digits[digest[i] & UINT8_C(0x0F)];
    }
    hex[DKC2_SHA256_HEX_SIZE - 1] = '\0';
}
