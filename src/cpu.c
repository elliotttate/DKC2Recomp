#include "dkc2/cpu.h"

#include <stddef.h>

void dkc2_decode_state_reset(dkc2_decode_state *state) {
    if (state != NULL) {
        state->e = DKC2_BIT_ONE;
        state->m = DKC2_BIT_ONE;
        state->x = DKC2_BIT_ONE;
        state->c = DKC2_BIT_UNKNOWN;
    }
}

bool dkc2_decode_state_equal(const dkc2_decode_state *left,
                             const dkc2_decode_state *right) {
    return left != NULL && right != NULL && left->e == right->e &&
           left->m == right->m && left->x == right->x && left->c == right->c;
}

static void apply_emulation_constraint(dkc2_decode_state *state) {
    if (state->e == DKC2_BIT_ONE) {
        state->m = DKC2_BIT_ONE;
        state->x = DKC2_BIT_ONE;
    } else if (state->e == DKC2_BIT_UNKNOWN) {
        if (state->m != DKC2_BIT_ONE) {
            state->m = DKC2_BIT_UNKNOWN;
        }
        if (state->x != DKC2_BIT_ONE) {
            state->x = DKC2_BIT_UNKNOWN;
        }
    }
}

static bool changes_carry(uint8_t opcode) {
    switch (opcode) {
        case 0x06: case 0x0A: case 0x0E: case 0x16: case 0x1E: /* ASL */
        case 0x26: case 0x2A: case 0x2E: case 0x36: case 0x3E: /* ROL */
        case 0x46: case 0x4A: case 0x4E: case 0x56: case 0x5E: /* LSR */
        case 0x61: case 0x63: case 0x65: case 0x67: case 0x69: /* ADC */
        case 0x6D: case 0x6F: case 0x71: case 0x72: case 0x73:
        case 0x75: case 0x77: case 0x79: case 0x7D: case 0x7F:
        case 0x66: case 0x6A: case 0x6E: case 0x76: case 0x7E: /* ROR */
        case 0xC0: case 0xC4: case 0xCC:                         /* CPY */
        case 0xC1: case 0xC3: case 0xC5: case 0xC7: case 0xC9: /* CMP */
        case 0xCD: case 0xCF: case 0xD1: case 0xD2: case 0xD3:
        case 0xD5: case 0xD7: case 0xD9: case 0xDD: case 0xDF:
        case 0xE0: case 0xE4: case 0xEC:                         /* CPX */
        case 0xE1: case 0xE3: case 0xE5: case 0xE7: case 0xE9: /* SBC */
        case 0xED: case 0xEF: case 0xF1: case 0xF2: case 0xF3:
        case 0xF5: case 0xF7: case 0xF9: case 0xFD: case 0xFF:
            return true;
        default:
            return false;
    }
}

void dkc2_decode_state_apply(dkc2_decode_state *state,
                             uint8_t opcode,
                             uint8_t first_operand_byte) {
    dkc2_known_bit old_e;
    dkc2_known_bit old_c;

    if (state == NULL) {
        return;
    }

    switch (opcode) {
        case 0x18: /* CLC */
            state->c = DKC2_BIT_ZERO;
            return;
        case 0x38: /* SEC */
            state->c = DKC2_BIT_ONE;
            return;
        case 0x28: /* PLP */
            state->c = DKC2_BIT_UNKNOWN;
            state->m = DKC2_BIT_UNKNOWN;
            state->x = DKC2_BIT_UNKNOWN;
            apply_emulation_constraint(state);
            return;
        case 0xC2: /* REP */
            if ((first_operand_byte & UINT8_C(0x01)) != 0) {
                state->c = DKC2_BIT_ZERO;
            }
            if ((first_operand_byte & UINT8_C(0x20)) != 0) {
                state->m = state->e == DKC2_BIT_ZERO
                               ? DKC2_BIT_ZERO
                               : state->e == DKC2_BIT_ONE ? DKC2_BIT_ONE
                                                         : DKC2_BIT_UNKNOWN;
            }
            if ((first_operand_byte & UINT8_C(0x10)) != 0) {
                state->x = state->e == DKC2_BIT_ZERO
                               ? DKC2_BIT_ZERO
                               : state->e == DKC2_BIT_ONE ? DKC2_BIT_ONE
                                                         : DKC2_BIT_UNKNOWN;
            }
            return;
        case 0xE2: /* SEP */
            if ((first_operand_byte & UINT8_C(0x01)) != 0) {
                state->c = DKC2_BIT_ONE;
            }
            if ((first_operand_byte & UINT8_C(0x20)) != 0) {
                state->m = DKC2_BIT_ONE;
            }
            if ((first_operand_byte & UINT8_C(0x10)) != 0) {
                state->x = DKC2_BIT_ONE;
            }
            return;
        case 0xFB: /* XCE */
            old_e = state->e;
            old_c = state->c;
            state->e = old_c;
            state->c = old_e;
            apply_emulation_constraint(state);
            return;
        default:
            if (changes_carry(opcode)) {
                state->c = DKC2_BIT_UNKNOWN;
            }
            return;
    }
}

char dkc2_known_bit_character(dkc2_known_bit bit) {
    switch (bit) {
        case DKC2_BIT_ZERO:
            return '0';
        case DKC2_BIT_ONE:
            return '1';
        default:
            return '?';
    }
}
