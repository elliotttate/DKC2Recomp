#ifndef DKC2_CPU_H
#define DKC2_CPU_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dkc2_known_bit {
    DKC2_BIT_ZERO = 0,
    DKC2_BIT_ONE = 1,
    DKC2_BIT_UNKNOWN = 2
} dkc2_known_bit;

/*
 * The flags that can change the meaning or size of later instructions.
 * M selects 8/16-bit accumulator and memory operations; X selects 8/16-bit
 * index operations; E selects emulation/native mode; C matters to XCE.
 */
typedef struct dkc2_decode_state {
    dkc2_known_bit e;
    dkc2_known_bit m;
    dkc2_known_bit x;
    dkc2_known_bit c;
} dkc2_decode_state;

void dkc2_decode_state_reset(dkc2_decode_state *state);

bool dkc2_decode_state_equal(const dkc2_decode_state *left,
                             const dkc2_decode_state *right);

/* Applies only effects needed to decode subsequent instruction widths. */
void dkc2_decode_state_apply(dkc2_decode_state *state,
                             uint8_t opcode,
                             uint8_t first_operand_byte);

char dkc2_known_bit_character(dkc2_known_bit bit);

#ifdef __cplusplus
}
#endif

#endif
