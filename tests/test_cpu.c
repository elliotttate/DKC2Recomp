#include "dkc2/cpu.h"

#include <stdio.h>
#include <stdlib.h>

static void require_state(const dkc2_decode_state *state,
                          dkc2_known_bit e,
                          dkc2_known_bit m,
                          dkc2_known_bit x,
                          dkc2_known_bit c,
                          const char *stage) {
    if (state->e != e || state->m != m || state->x != x || state->c != c) {
        (void)fprintf(stderr,
                      "%s: got E%c M%c X%c C%c\n",
                      stage,
                      dkc2_known_bit_character(state->e),
                      dkc2_known_bit_character(state->m),
                      dkc2_known_bit_character(state->x),
                      dkc2_known_bit_character(state->c));
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    dkc2_decode_state state;
    dkc2_decode_state copy;

    dkc2_decode_state_reset(&state);
    require_state(&state,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ONE,
                  DKC2_BIT_UNKNOWN,
                  "reset");

    dkc2_decode_state_apply(&state, UINT8_C(0x18), 0); /* CLC */
    dkc2_decode_state_apply(&state, UINT8_C(0xFB), 0); /* XCE */
    require_state(&state,
                  DKC2_BIT_ZERO,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ONE,
                  "enter native mode");

    dkc2_decode_state_apply(&state, UINT8_C(0xC2), UINT8_C(0x30)); /* REP #$30 */
    require_state(&state,
                  DKC2_BIT_ZERO,
                  DKC2_BIT_ZERO,
                  DKC2_BIT_ZERO,
                  DKC2_BIT_ONE,
                  "select 16-bit widths");

    dkc2_decode_state_apply(&state, UINT8_C(0xE2), UINT8_C(0x20)); /* SEP #$20 */
    require_state(&state,
                  DKC2_BIT_ZERO,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ZERO,
                  DKC2_BIT_ONE,
                  "select 8-bit accumulator");

    copy = state;
    if (!dkc2_decode_state_equal(&state, &copy)) {
        (void)fprintf(stderr, "equal states were not recognized\n");
        return EXIT_FAILURE;
    }

    dkc2_decode_state_reset(&state);
    dkc2_decode_state_apply(&state, UINT8_C(0xC2), UINT8_C(0x30));
    require_state(&state,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ONE,
                  DKC2_BIT_ONE,
                  DKC2_BIT_UNKNOWN,
                  "emulation mode width constraint");

    (void)puts("CPU decode-state tests passed");
    return EXIT_SUCCESS;
}
