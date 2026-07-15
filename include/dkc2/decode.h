#ifndef DKC2_DECODE_H
#define DKC2_DECODE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "dkc2/cpu.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum dkc2_addressing_mode {
    DKC2_AM_IMPLIED,
    DKC2_AM_ACCUMULATOR,
    DKC2_AM_IMMEDIATE8,
    DKC2_AM_IMMEDIATE_M,
    DKC2_AM_IMMEDIATE_X,
    DKC2_AM_DIRECT,
    DKC2_AM_DIRECT_X,
    DKC2_AM_DIRECT_Y,
    DKC2_AM_DIRECT_INDIRECT,
    DKC2_AM_DIRECT_X_INDIRECT,
    DKC2_AM_DIRECT_INDIRECT_Y,
    DKC2_AM_DIRECT_LONG_INDIRECT,
    DKC2_AM_DIRECT_LONG_INDIRECT_Y,
    DKC2_AM_STACK_RELATIVE,
    DKC2_AM_STACK_RELATIVE_INDIRECT_Y,
    DKC2_AM_ABSOLUTE,
    DKC2_AM_ABSOLUTE_X,
    DKC2_AM_ABSOLUTE_Y,
    DKC2_AM_ABSOLUTE_INDIRECT,
    DKC2_AM_ABSOLUTE_X_INDIRECT,
    DKC2_AM_ABSOLUTE_LONG_INDIRECT,
    DKC2_AM_LONG,
    DKC2_AM_LONG_X,
    DKC2_AM_RELATIVE8,
    DKC2_AM_RELATIVE16,
    DKC2_AM_BLOCK_MOVE
} dkc2_addressing_mode;

typedef enum dkc2_flow_kind {
    DKC2_FLOW_NEXT,
    DKC2_FLOW_CONDITIONAL_BRANCH,
    DKC2_FLOW_BRANCH,
    DKC2_FLOW_CALL,
    DKC2_FLOW_INDIRECT_CALL,
    DKC2_FLOW_JUMP,
    DKC2_FLOW_INDIRECT_JUMP,
    DKC2_FLOW_RETURN,
    DKC2_FLOW_TRAP,
    DKC2_FLOW_STOP
} dkc2_flow_kind;

typedef struct dkc2_opcode_description {
    const char *mnemonic;
    dkc2_addressing_mode addressing_mode;
    dkc2_flow_kind flow;
} dkc2_opcode_description;

typedef struct dkc2_instruction {
    uint32_t address;
    uint32_t next_address;
    uint32_t target_address;
    uint32_t operand;
    uint8_t bytes[4];
    uint8_t length;
    bool target_known;
    const dkc2_opcode_description *description;
} dkc2_instruction;

typedef enum dkc2_decode_status {
    DKC2_DECODE_OK,
    DKC2_DECODE_NEED_MORE_BYTES,
    DKC2_DECODE_AMBIGUOUS_M,
    DKC2_DECODE_AMBIGUOUS_X
} dkc2_decode_status;

const dkc2_opcode_description *dkc2_opcode_description_for(uint8_t opcode);

dkc2_decode_status dkc2_decode_instruction(const uint8_t *bytes,
                                           size_t available,
                                           uint32_t address,
                                           const dkc2_decode_state *state,
                                           dkc2_instruction *instruction);

bool dkc2_format_instruction(const dkc2_instruction *instruction,
                             char *buffer,
                             size_t buffer_size);

const char *dkc2_decode_status_name(dkc2_decode_status status);

#ifdef __cplusplus
}
#endif

#endif
