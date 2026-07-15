#include "dkc2/decode.h"

#include <stdio.h>
#include <string.h>

#define OP(name, mode) {name, mode, DKC2_FLOW_NEXT}
#define FLOW(name, mode, kind) {name, mode, kind}

static const dkc2_opcode_description opcode_table[256] = {
    /* 00 */
    FLOW("BRK", DKC2_AM_IMMEDIATE8, DKC2_FLOW_TRAP),
    OP("ORA", DKC2_AM_DIRECT_X_INDIRECT),
    FLOW("COP", DKC2_AM_IMMEDIATE8, DKC2_FLOW_TRAP),
    OP("ORA", DKC2_AM_STACK_RELATIVE),
    OP("TSB", DKC2_AM_DIRECT),
    OP("ORA", DKC2_AM_DIRECT),
    OP("ASL", DKC2_AM_DIRECT),
    OP("ORA", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("PHP", DKC2_AM_IMPLIED),
    OP("ORA", DKC2_AM_IMMEDIATE_M),
    OP("ASL", DKC2_AM_ACCUMULATOR),
    OP("PHD", DKC2_AM_IMPLIED),
    OP("TSB", DKC2_AM_ABSOLUTE),
    OP("ORA", DKC2_AM_ABSOLUTE),
    OP("ASL", DKC2_AM_ABSOLUTE),
    OP("ORA", DKC2_AM_LONG),
    /* 10 */
    FLOW("BPL", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("ORA", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("ORA", DKC2_AM_DIRECT_INDIRECT),
    OP("ORA", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("TRB", DKC2_AM_DIRECT),
    OP("ORA", DKC2_AM_DIRECT_X),
    OP("ASL", DKC2_AM_DIRECT_X),
    OP("ORA", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("CLC", DKC2_AM_IMPLIED),
    OP("ORA", DKC2_AM_ABSOLUTE_Y),
    OP("INC", DKC2_AM_ACCUMULATOR),
    OP("TCS", DKC2_AM_IMPLIED),
    OP("TRB", DKC2_AM_ABSOLUTE),
    OP("ORA", DKC2_AM_ABSOLUTE_X),
    OP("ASL", DKC2_AM_ABSOLUTE_X),
    OP("ORA", DKC2_AM_LONG_X),
    /* 20 */
    FLOW("JSR", DKC2_AM_ABSOLUTE, DKC2_FLOW_CALL),
    OP("AND", DKC2_AM_DIRECT_X_INDIRECT),
    FLOW("JSL", DKC2_AM_LONG, DKC2_FLOW_CALL),
    OP("AND", DKC2_AM_STACK_RELATIVE),
    OP("BIT", DKC2_AM_DIRECT),
    OP("AND", DKC2_AM_DIRECT),
    OP("ROL", DKC2_AM_DIRECT),
    OP("AND", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("PLP", DKC2_AM_IMPLIED),
    OP("AND", DKC2_AM_IMMEDIATE_M),
    OP("ROL", DKC2_AM_ACCUMULATOR),
    OP("PLD", DKC2_AM_IMPLIED),
    OP("BIT", DKC2_AM_ABSOLUTE),
    OP("AND", DKC2_AM_ABSOLUTE),
    OP("ROL", DKC2_AM_ABSOLUTE),
    OP("AND", DKC2_AM_LONG),
    /* 30 */
    FLOW("BMI", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("AND", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("AND", DKC2_AM_DIRECT_INDIRECT),
    OP("AND", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("BIT", DKC2_AM_DIRECT_X),
    OP("AND", DKC2_AM_DIRECT_X),
    OP("ROL", DKC2_AM_DIRECT_X),
    OP("AND", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("SEC", DKC2_AM_IMPLIED),
    OP("AND", DKC2_AM_ABSOLUTE_Y),
    OP("DEC", DKC2_AM_ACCUMULATOR),
    OP("TSC", DKC2_AM_IMPLIED),
    OP("BIT", DKC2_AM_ABSOLUTE_X),
    OP("AND", DKC2_AM_ABSOLUTE_X),
    OP("ROL", DKC2_AM_ABSOLUTE_X),
    OP("AND", DKC2_AM_LONG_X),
    /* 40 */
    FLOW("RTI", DKC2_AM_IMPLIED, DKC2_FLOW_RETURN),
    OP("EOR", DKC2_AM_DIRECT_X_INDIRECT),
    OP("WDM", DKC2_AM_IMMEDIATE8),
    OP("EOR", DKC2_AM_STACK_RELATIVE),
    OP("MVP", DKC2_AM_BLOCK_MOVE),
    OP("EOR", DKC2_AM_DIRECT),
    OP("LSR", DKC2_AM_DIRECT),
    OP("EOR", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("PHA", DKC2_AM_IMPLIED),
    OP("EOR", DKC2_AM_IMMEDIATE_M),
    OP("LSR", DKC2_AM_ACCUMULATOR),
    OP("PHK", DKC2_AM_IMPLIED),
    FLOW("JMP", DKC2_AM_ABSOLUTE, DKC2_FLOW_JUMP),
    OP("EOR", DKC2_AM_ABSOLUTE),
    OP("LSR", DKC2_AM_ABSOLUTE),
    OP("EOR", DKC2_AM_LONG),
    /* 50 */
    FLOW("BVC", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("EOR", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("EOR", DKC2_AM_DIRECT_INDIRECT),
    OP("EOR", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("MVN", DKC2_AM_BLOCK_MOVE),
    OP("EOR", DKC2_AM_DIRECT_X),
    OP("LSR", DKC2_AM_DIRECT_X),
    OP("EOR", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("CLI", DKC2_AM_IMPLIED),
    OP("EOR", DKC2_AM_ABSOLUTE_Y),
    OP("PHY", DKC2_AM_IMPLIED),
    OP("TCD", DKC2_AM_IMPLIED),
    FLOW("JML", DKC2_AM_LONG, DKC2_FLOW_JUMP),
    OP("EOR", DKC2_AM_ABSOLUTE_X),
    OP("LSR", DKC2_AM_ABSOLUTE_X),
    OP("EOR", DKC2_AM_LONG_X),
    /* 60 */
    FLOW("RTS", DKC2_AM_IMPLIED, DKC2_FLOW_RETURN),
    OP("ADC", DKC2_AM_DIRECT_X_INDIRECT),
    OP("PER", DKC2_AM_RELATIVE16),
    OP("ADC", DKC2_AM_STACK_RELATIVE),
    OP("STZ", DKC2_AM_DIRECT),
    OP("ADC", DKC2_AM_DIRECT),
    OP("ROR", DKC2_AM_DIRECT),
    OP("ADC", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("PLA", DKC2_AM_IMPLIED),
    OP("ADC", DKC2_AM_IMMEDIATE_M),
    OP("ROR", DKC2_AM_ACCUMULATOR),
    FLOW("RTL", DKC2_AM_IMPLIED, DKC2_FLOW_RETURN),
    FLOW("JMP", DKC2_AM_ABSOLUTE_INDIRECT, DKC2_FLOW_INDIRECT_JUMP),
    OP("ADC", DKC2_AM_ABSOLUTE),
    OP("ROR", DKC2_AM_ABSOLUTE),
    OP("ADC", DKC2_AM_LONG),
    /* 70 */
    FLOW("BVS", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("ADC", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("ADC", DKC2_AM_DIRECT_INDIRECT),
    OP("ADC", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("STZ", DKC2_AM_DIRECT_X),
    OP("ADC", DKC2_AM_DIRECT_X),
    OP("ROR", DKC2_AM_DIRECT_X),
    OP("ADC", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("SEI", DKC2_AM_IMPLIED),
    OP("ADC", DKC2_AM_ABSOLUTE_Y),
    OP("PLY", DKC2_AM_IMPLIED),
    OP("TDC", DKC2_AM_IMPLIED),
    FLOW("JMP", DKC2_AM_ABSOLUTE_X_INDIRECT, DKC2_FLOW_INDIRECT_JUMP),
    OP("ADC", DKC2_AM_ABSOLUTE_X),
    OP("ROR", DKC2_AM_ABSOLUTE_X),
    OP("ADC", DKC2_AM_LONG_X),
    /* 80 */
    FLOW("BRA", DKC2_AM_RELATIVE8, DKC2_FLOW_BRANCH),
    OP("STA", DKC2_AM_DIRECT_X_INDIRECT),
    FLOW("BRL", DKC2_AM_RELATIVE16, DKC2_FLOW_BRANCH),
    OP("STA", DKC2_AM_STACK_RELATIVE),
    OP("STY", DKC2_AM_DIRECT),
    OP("STA", DKC2_AM_DIRECT),
    OP("STX", DKC2_AM_DIRECT),
    OP("STA", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("DEY", DKC2_AM_IMPLIED),
    OP("BIT", DKC2_AM_IMMEDIATE_M),
    OP("TXA", DKC2_AM_IMPLIED),
    OP("PHB", DKC2_AM_IMPLIED),
    OP("STY", DKC2_AM_ABSOLUTE),
    OP("STA", DKC2_AM_ABSOLUTE),
    OP("STX", DKC2_AM_ABSOLUTE),
    OP("STA", DKC2_AM_LONG),
    /* 90 */
    FLOW("BCC", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("STA", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("STA", DKC2_AM_DIRECT_INDIRECT),
    OP("STA", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("STY", DKC2_AM_DIRECT_X),
    OP("STA", DKC2_AM_DIRECT_X),
    OP("STX", DKC2_AM_DIRECT_Y),
    OP("STA", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("TYA", DKC2_AM_IMPLIED),
    OP("STA", DKC2_AM_ABSOLUTE_Y),
    OP("TXS", DKC2_AM_IMPLIED),
    OP("TXY", DKC2_AM_IMPLIED),
    OP("STZ", DKC2_AM_ABSOLUTE),
    OP("STA", DKC2_AM_ABSOLUTE_X),
    OP("STZ", DKC2_AM_ABSOLUTE_X),
    OP("STA", DKC2_AM_LONG_X),
    /* A0 */
    OP("LDY", DKC2_AM_IMMEDIATE_X),
    OP("LDA", DKC2_AM_DIRECT_X_INDIRECT),
    OP("LDX", DKC2_AM_IMMEDIATE_X),
    OP("LDA", DKC2_AM_STACK_RELATIVE),
    OP("LDY", DKC2_AM_DIRECT),
    OP("LDA", DKC2_AM_DIRECT),
    OP("LDX", DKC2_AM_DIRECT),
    OP("LDA", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("TAY", DKC2_AM_IMPLIED),
    OP("LDA", DKC2_AM_IMMEDIATE_M),
    OP("TAX", DKC2_AM_IMPLIED),
    OP("PLB", DKC2_AM_IMPLIED),
    OP("LDY", DKC2_AM_ABSOLUTE),
    OP("LDA", DKC2_AM_ABSOLUTE),
    OP("LDX", DKC2_AM_ABSOLUTE),
    OP("LDA", DKC2_AM_LONG),
    /* B0 */
    FLOW("BCS", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("LDA", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("LDA", DKC2_AM_DIRECT_INDIRECT),
    OP("LDA", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("LDY", DKC2_AM_DIRECT_X),
    OP("LDA", DKC2_AM_DIRECT_X),
    OP("LDX", DKC2_AM_DIRECT_Y),
    OP("LDA", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("CLV", DKC2_AM_IMPLIED),
    OP("LDA", DKC2_AM_ABSOLUTE_Y),
    OP("TSX", DKC2_AM_IMPLIED),
    OP("TYX", DKC2_AM_IMPLIED),
    OP("LDY", DKC2_AM_ABSOLUTE_X),
    OP("LDA", DKC2_AM_ABSOLUTE_X),
    OP("LDX", DKC2_AM_ABSOLUTE_Y),
    OP("LDA", DKC2_AM_LONG_X),
    /* C0 */
    OP("CPY", DKC2_AM_IMMEDIATE_X),
    OP("CMP", DKC2_AM_DIRECT_X_INDIRECT),
    OP("REP", DKC2_AM_IMMEDIATE8),
    OP("CMP", DKC2_AM_STACK_RELATIVE),
    OP("CPY", DKC2_AM_DIRECT),
    OP("CMP", DKC2_AM_DIRECT),
    OP("DEC", DKC2_AM_DIRECT),
    OP("CMP", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("INY", DKC2_AM_IMPLIED),
    OP("CMP", DKC2_AM_IMMEDIATE_M),
    OP("DEX", DKC2_AM_IMPLIED),
    OP("WAI", DKC2_AM_IMPLIED),
    OP("CPY", DKC2_AM_ABSOLUTE),
    OP("CMP", DKC2_AM_ABSOLUTE),
    OP("DEC", DKC2_AM_ABSOLUTE),
    OP("CMP", DKC2_AM_LONG),
    /* D0 */
    FLOW("BNE", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("CMP", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("CMP", DKC2_AM_DIRECT_INDIRECT),
    OP("CMP", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("PEI", DKC2_AM_DIRECT_INDIRECT),
    OP("CMP", DKC2_AM_DIRECT_X),
    OP("DEC", DKC2_AM_DIRECT_X),
    OP("CMP", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("CLD", DKC2_AM_IMPLIED),
    OP("CMP", DKC2_AM_ABSOLUTE_Y),
    OP("PHX", DKC2_AM_IMPLIED),
    FLOW("STP", DKC2_AM_IMPLIED, DKC2_FLOW_STOP),
    FLOW("JML", DKC2_AM_ABSOLUTE_LONG_INDIRECT, DKC2_FLOW_INDIRECT_JUMP),
    OP("CMP", DKC2_AM_ABSOLUTE_X),
    OP("DEC", DKC2_AM_ABSOLUTE_X),
    OP("CMP", DKC2_AM_LONG_X),
    /* E0 */
    OP("CPX", DKC2_AM_IMMEDIATE_X),
    OP("SBC", DKC2_AM_DIRECT_X_INDIRECT),
    OP("SEP", DKC2_AM_IMMEDIATE8),
    OP("SBC", DKC2_AM_STACK_RELATIVE),
    OP("CPX", DKC2_AM_DIRECT),
    OP("SBC", DKC2_AM_DIRECT),
    OP("INC", DKC2_AM_DIRECT),
    OP("SBC", DKC2_AM_DIRECT_LONG_INDIRECT),
    OP("INX", DKC2_AM_IMPLIED),
    OP("SBC", DKC2_AM_IMMEDIATE_M),
    OP("NOP", DKC2_AM_IMPLIED),
    OP("XBA", DKC2_AM_IMPLIED),
    OP("CPX", DKC2_AM_ABSOLUTE),
    OP("SBC", DKC2_AM_ABSOLUTE),
    OP("INC", DKC2_AM_ABSOLUTE),
    OP("SBC", DKC2_AM_LONG),
    /* F0 */
    FLOW("BEQ", DKC2_AM_RELATIVE8, DKC2_FLOW_CONDITIONAL_BRANCH),
    OP("SBC", DKC2_AM_DIRECT_INDIRECT_Y),
    OP("SBC", DKC2_AM_DIRECT_INDIRECT),
    OP("SBC", DKC2_AM_STACK_RELATIVE_INDIRECT_Y),
    OP("PEA", DKC2_AM_ABSOLUTE),
    OP("SBC", DKC2_AM_DIRECT_X),
    OP("INC", DKC2_AM_DIRECT_X),
    OP("SBC", DKC2_AM_DIRECT_LONG_INDIRECT_Y),
    OP("SED", DKC2_AM_IMPLIED),
    OP("SBC", DKC2_AM_ABSOLUTE_Y),
    OP("PLX", DKC2_AM_IMPLIED),
    OP("XCE", DKC2_AM_IMPLIED),
    FLOW("JSR", DKC2_AM_ABSOLUTE_X_INDIRECT, DKC2_FLOW_INDIRECT_CALL),
    OP("SBC", DKC2_AM_ABSOLUTE_X),
    OP("INC", DKC2_AM_ABSOLUTE_X),
    OP("SBC", DKC2_AM_LONG_X)
};

static uint8_t fixed_length(dkc2_addressing_mode mode) {
    switch (mode) {
        case DKC2_AM_IMPLIED:
        case DKC2_AM_ACCUMULATOR:
            return 1;
        case DKC2_AM_IMMEDIATE8:
        case DKC2_AM_DIRECT:
        case DKC2_AM_DIRECT_X:
        case DKC2_AM_DIRECT_Y:
        case DKC2_AM_DIRECT_INDIRECT:
        case DKC2_AM_DIRECT_X_INDIRECT:
        case DKC2_AM_DIRECT_INDIRECT_Y:
        case DKC2_AM_DIRECT_LONG_INDIRECT:
        case DKC2_AM_DIRECT_LONG_INDIRECT_Y:
        case DKC2_AM_STACK_RELATIVE:
        case DKC2_AM_STACK_RELATIVE_INDIRECT_Y:
        case DKC2_AM_RELATIVE8:
            return 2;
        case DKC2_AM_ABSOLUTE:
        case DKC2_AM_ABSOLUTE_X:
        case DKC2_AM_ABSOLUTE_Y:
        case DKC2_AM_ABSOLUTE_INDIRECT:
        case DKC2_AM_ABSOLUTE_X_INDIRECT:
        case DKC2_AM_ABSOLUTE_LONG_INDIRECT:
        case DKC2_AM_RELATIVE16:
        case DKC2_AM_BLOCK_MOVE:
            return 3;
        case DKC2_AM_LONG:
        case DKC2_AM_LONG_X:
            return 4;
        case DKC2_AM_IMMEDIATE_M:
        case DKC2_AM_IMMEDIATE_X:
            return 0;
    }
    return 0;
}

static uint32_t next_address(uint32_t address, uint8_t length) {
    return (address & UINT32_C(0xFF0000)) |
           ((address + length) & UINT32_C(0xFFFF));
}

const dkc2_opcode_description *dkc2_opcode_description_for(uint8_t opcode) {
    return &opcode_table[opcode];
}

dkc2_decode_status dkc2_decode_instruction(const uint8_t *bytes,
                                           size_t available,
                                           uint32_t address,
                                           const dkc2_decode_state *state,
                                           dkc2_instruction *instruction) {
    const dkc2_opcode_description *description;
    uint8_t length;
    uint32_t operand = 0;
    size_t i;

    if (bytes == NULL || state == NULL || instruction == NULL || available == 0) {
        return DKC2_DECODE_NEED_MORE_BYTES;
    }

    description = &opcode_table[bytes[0]];
    if (description->addressing_mode == DKC2_AM_IMMEDIATE_M) {
        if (state->m == DKC2_BIT_UNKNOWN) {
            return DKC2_DECODE_AMBIGUOUS_M;
        }
        length = state->m == DKC2_BIT_ONE ? 2 : 3;
    } else if (description->addressing_mode == DKC2_AM_IMMEDIATE_X) {
        if (state->x == DKC2_BIT_UNKNOWN) {
            return DKC2_DECODE_AMBIGUOUS_X;
        }
        length = state->x == DKC2_BIT_ONE ? 2 : 3;
    } else {
        length = fixed_length(description->addressing_mode);
    }

    if (length == 0 || available < length) {
        return DKC2_DECODE_NEED_MORE_BYTES;
    }

    memset(instruction, 0, sizeof(*instruction));
    instruction->address = address & UINT32_C(0xFFFFFF);
    instruction->description = description;
    instruction->length = length;
    instruction->next_address = next_address(instruction->address, length);

    for (i = 0; i < length; ++i) {
        instruction->bytes[i] = bytes[i];
        if (i > 0) {
            operand |= (uint32_t)bytes[i] << ((i - 1) * 8);
        }
    }
    instruction->operand = operand;

    switch (description->flow) {
        case DKC2_FLOW_CONDITIONAL_BRANCH:
        case DKC2_FLOW_BRANCH:
            instruction->target_known = true;
            if (description->addressing_mode == DKC2_AM_RELATIVE8) {
                int8_t displacement = (int8_t)bytes[1];
                instruction->target_address =
                    (instruction->address & UINT32_C(0xFF0000)) |
                    ((instruction->next_address + displacement) & UINT32_C(0xFFFF));
            } else {
                int16_t displacement = (int16_t)(uint16_t)operand;
                instruction->target_address =
                    (instruction->address & UINT32_C(0xFF0000)) |
                    ((instruction->next_address + displacement) & UINT32_C(0xFFFF));
            }
            break;
        case DKC2_FLOW_CALL:
        case DKC2_FLOW_JUMP:
            instruction->target_known = true;
            if (description->addressing_mode == DKC2_AM_LONG) {
                instruction->target_address = operand & UINT32_C(0xFFFFFF);
            } else {
                instruction->target_address =
                    (instruction->address & UINT32_C(0xFF0000)) |
                    (operand & UINT32_C(0xFFFF));
            }
            break;
        default:
            break;
    }

    return DKC2_DECODE_OK;
}

static bool format_operand(const dkc2_instruction *instruction,
                           char *buffer,
                           size_t size) {
    uint32_t operand = instruction->operand;

    switch (instruction->description->addressing_mode) {
        case DKC2_AM_IMPLIED:
            return snprintf(buffer, size, "%s", instruction->description->mnemonic) >= 0;
        case DKC2_AM_ACCUMULATOR:
            return snprintf(buffer, size, "%s A", instruction->description->mnemonic) >= 0;
        case DKC2_AM_IMMEDIATE8:
            return snprintf(buffer, size, "%s #$%02X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_IMMEDIATE_M:
        case DKC2_AM_IMMEDIATE_X:
            return snprintf(buffer,
                            size,
                            instruction->length == 2 ? "%s #$%02X" : "%s #$%04X",
                            instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT:
            return snprintf(buffer, size, "%s $%02X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_X:
            return snprintf(buffer, size, "%s $%02X,X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_Y:
            return snprintf(buffer, size, "%s $%02X,Y", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_INDIRECT:
            return snprintf(buffer, size, "%s ($%02X)", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_X_INDIRECT:
            return snprintf(buffer, size, "%s ($%02X,X)", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_INDIRECT_Y:
            return snprintf(buffer, size, "%s ($%02X),Y", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_LONG_INDIRECT:
            return snprintf(buffer, size, "%s [$%02X]", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_DIRECT_LONG_INDIRECT_Y:
            return snprintf(buffer, size, "%s [$%02X],Y", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_STACK_RELATIVE:
            return snprintf(buffer, size, "%s $%02X,S", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_STACK_RELATIVE_INDIRECT_Y:
            return snprintf(buffer, size, "%s ($%02X,S),Y", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_ABSOLUTE:
            return snprintf(buffer, size, "%s $%04X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_ABSOLUTE_X:
            return snprintf(buffer, size, "%s $%04X,X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_ABSOLUTE_Y:
            return snprintf(buffer, size, "%s $%04X,Y", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_ABSOLUTE_INDIRECT:
            return snprintf(buffer, size, "%s ($%04X)", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_ABSOLUTE_X_INDIRECT:
            return snprintf(buffer, size, "%s ($%04X,X)", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_ABSOLUTE_LONG_INDIRECT:
            return snprintf(buffer, size, "%s [$%04X]", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_LONG:
            return snprintf(buffer, size, "%s $%06X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_LONG_X:
            return snprintf(buffer, size, "%s $%06X,X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_RELATIVE8:
        case DKC2_AM_RELATIVE16:
            if (instruction->target_known) {
                return snprintf(buffer, size, "%s $%06X", instruction->description->mnemonic,
                                (unsigned)instruction->target_address) >= 0;
            }
            return snprintf(buffer, size, "%s $%04X", instruction->description->mnemonic,
                            (unsigned)operand) >= 0;
        case DKC2_AM_BLOCK_MOVE:
            return snprintf(buffer, size, "%s $%02X,$%02X",
                            instruction->description->mnemonic,
                            (unsigned)(operand & UINT32_C(0xFF)),
                            (unsigned)((operand >> 8) & UINT32_C(0xFF))) >= 0;
    }
    return false;
}

bool dkc2_format_instruction(const dkc2_instruction *instruction,
                             char *buffer,
                             size_t buffer_size) {
    if (instruction == NULL || instruction->description == NULL || buffer == NULL ||
        buffer_size == 0) {
        return false;
    }
    return format_operand(instruction, buffer, buffer_size);
}

const char *dkc2_decode_status_name(dkc2_decode_status status) {
    switch (status) {
        case DKC2_DECODE_OK:
            return "ok";
        case DKC2_DECODE_NEED_MORE_BYTES:
            return "not enough bytes";
        case DKC2_DECODE_AMBIGUOUS_M:
            return "unknown accumulator width (M flag)";
        case DKC2_DECODE_AMBIGUOUS_X:
            return "unknown index width (X flag)";
    }
    return "unknown decoder error";
}

#undef OP
#undef FLOW
