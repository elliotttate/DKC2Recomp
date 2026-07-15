#include "dkc2/decode.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static dkc2_instruction decode_or_fail(const uint8_t bytes[4],
                                       uint32_t address,
                                       const dkc2_decode_state *state) {
    dkc2_instruction instruction;
    dkc2_decode_status status =
        dkc2_decode_instruction(bytes, 4, address, state, &instruction);
    if (status != DKC2_DECODE_OK) {
        (void)fprintf(stderr, "decode failed: %s\n", dkc2_decode_status_name(status));
        exit(EXIT_FAILURE);
    }
    return instruction;
}

static void require_format(const dkc2_instruction *instruction, const char *expected) {
    char text[64];
    if (!dkc2_format_instruction(instruction, text, sizeof(text)) ||
        strcmp(text, expected) != 0) {
        (void)fprintf(stderr, "format was '%s'; expected '%s'\n", text, expected);
        exit(EXIT_FAILURE);
    }
}

int main(void) {
    dkc2_decode_state narrow = {
        DKC2_BIT_ZERO, DKC2_BIT_ONE, DKC2_BIT_ONE, DKC2_BIT_UNKNOWN
    };
    dkc2_decode_state wide = {
        DKC2_BIT_ZERO, DKC2_BIT_ZERO, DKC2_BIT_ZERO, DKC2_BIT_UNKNOWN
    };
    dkc2_decode_state ambiguous = {
        DKC2_BIT_ZERO, DKC2_BIT_UNKNOWN, DKC2_BIT_UNKNOWN, DKC2_BIT_UNKNOWN
    };
    dkc2_instruction instruction;
    uint8_t bytes[4] = {0};
    unsigned opcode;

    for (opcode = 0; opcode < 256; ++opcode) {
        const dkc2_opcode_description *description =
            dkc2_opcode_description_for((uint8_t)opcode);
        bytes[0] = (uint8_t)opcode;
        if (description == NULL || description->mnemonic == NULL ||
            description->mnemonic[0] == '\0' ||
            dkc2_decode_instruction(bytes, 4, UINT32_C(0x808000), &narrow,
                                    &instruction) != DKC2_DECODE_OK ||
            dkc2_decode_instruction(bytes, 4, UINT32_C(0x808000), &wide,
                                    &instruction) != DKC2_DECODE_OK) {
            (void)fprintf(stderr, "opcode $%02X is not fully described\n", opcode);
            return EXIT_FAILURE;
        }
    }

    {
        const uint8_t lda[4] = {UINT8_C(0xA9), UINT8_C(0x34), UINT8_C(0x12), 0};
        instruction = decode_or_fail(lda, UINT32_C(0x808000), &narrow);
        if (instruction.length != 2 || instruction.operand != UINT32_C(0x34)) {
            return EXIT_FAILURE;
        }
        require_format(&instruction, "LDA #$34");

        instruction = decode_or_fail(lda, UINT32_C(0x808000), &wide);
        if (instruction.length != 3 || instruction.operand != UINT32_C(0x1234)) {
            return EXIT_FAILURE;
        }
        require_format(&instruction, "LDA #$1234");

        if (dkc2_decode_instruction(lda, 4, UINT32_C(0x808000), &ambiguous,
                                    &instruction) != DKC2_DECODE_AMBIGUOUS_M) {
            (void)fprintf(stderr, "ambiguous M width was not rejected\n");
            return EXIT_FAILURE;
        }
    }

    {
        const uint8_t branch[4] = {UINT8_C(0xD0), UINT8_C(0xFC), 0, 0};
        instruction = decode_or_fail(branch, UINT32_C(0x80A000), &narrow);
        if (!instruction.target_known ||
            instruction.target_address != UINT32_C(0x809FFE) ||
            instruction.next_address != UINT32_C(0x80A002)) {
            (void)fprintf(stderr, "relative branch target is incorrect\n");
            return EXIT_FAILURE;
        }
        require_format(&instruction, "BNE $809FFE");
    }

    {
        const uint8_t call[4] = {
            UINT8_C(0x22), UINT8_C(0x56), UINT8_C(0x34), UINT8_C(0xB8)
        };
        instruction = decode_or_fail(call, UINT32_C(0x808000), &narrow);
        if (!instruction.target_known ||
            instruction.target_address != UINT32_C(0xB83456) ||
            instruction.description->flow != DKC2_FLOW_CALL) {
            (void)fprintf(stderr, "long call target is incorrect\n");
            return EXIT_FAILURE;
        }
        require_format(&instruction, "JSL $B83456");
    }

    (void)puts("Instruction decoder tests passed (256 opcodes)");
    return EXIT_SUCCESS;
}
