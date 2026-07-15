#include "dkc2/execute.h"

#include "dkc2/decode.h"

#include <stddef.h>
#include <string.h>

typedef enum execute_operation {
    EXEC_NONE,
    EXEC_ORA,
    EXEC_AND,
    EXEC_EOR,
    EXEC_ADC,
    EXEC_STA,
    EXEC_LDA,
    EXEC_CMP,
    EXEC_SBC,
    EXEC_ASL,
    EXEC_ROL,
    EXEC_LSR,
    EXEC_ROR,
    EXEC_STX,
    EXEC_LDX,
    EXEC_DEC,
    EXEC_INC,
    EXEC_STY,
    EXEC_LDY,
    EXEC_CPX,
    EXEC_CPY,
    EXEC_BIT,
    EXEC_TSB,
    EXEC_TRB,
    EXEC_STZ
} execute_operation;

static bool memory_valid(const dkc2_memory *memory) {
    return memory != NULL && memory->read8 != NULL && memory->write8 != NULL;
}

static uint8_t read8(const dkc2_memory *memory, uint32_t address) {
    return memory->read8(memory->context, address & UINT32_C(0xFFFFFF));
}

static void write8(const dkc2_memory *memory,
                   uint32_t address,
                   uint8_t value) {
    memory->write8(memory->context,
                   address & UINT32_C(0xFFFFFF),
                   value);
}

static uint32_t increment_in_bank(uint32_t address) {
    return (address & UINT32_C(0xFF0000)) |
           ((address + UINT32_C(1)) & UINT32_C(0xFFFF));
}

static uint16_t read16_bank(const dkc2_memory *memory, uint32_t address) {
    uint16_t low = read8(memory, address);
    uint16_t high = read8(memory, increment_in_bank(address));
    return (uint16_t)(low | (uint16_t)(high << 8));
}

static uint32_t read24_bank(const dkc2_memory *memory, uint32_t address) {
    uint32_t low = read8(memory, address);
    address = increment_in_bank(address);
    {
        uint32_t middle = read8(memory, address);
        uint32_t high = read8(memory, increment_in_bank(address));
        return low | (middle << 8) | (high << 16);
    }
}

static uint32_t increment_data_address(uint32_t address,
                                       bool cross_bank) {
    return cross_bank
               ? (address + UINT32_C(1)) & UINT32_C(0xFFFFFF)
               : increment_in_bank(address);
}

static uint8_t fetch8(dkc2_cpu *cpu, const dkc2_memory *memory) {
    uint32_t address = ((uint32_t)cpu->pbr << 16) | cpu->pc;
    uint8_t value = read8(memory, address);
    cpu->pc = (uint16_t)(cpu->pc + UINT16_C(1));
    return value;
}

static uint16_t fetch16(dkc2_cpu *cpu, const dkc2_memory *memory) {
    uint16_t low = fetch8(cpu, memory);
    uint16_t high = fetch8(cpu, memory);
    return (uint16_t)(low | (uint16_t)(high << 8));
}

static uint32_t fetch24(dkc2_cpu *cpu, const dkc2_memory *memory) {
    uint32_t low = fetch8(cpu, memory);
    uint32_t middle = fetch8(cpu, memory);
    uint32_t high = fetch8(cpu, memory);
    return low | (middle << 8) | (high << 16);
}

bool dkc2_cpu_accumulator_is_8_bit(const dkc2_cpu *cpu) {
    return cpu == NULL || cpu->e ||
           (cpu->p & DKC2_P_MEMORY_8) != 0;
}

bool dkc2_cpu_index_is_8_bit(const dkc2_cpu *cpu) {
    return cpu == NULL || cpu->e || (cpu->p & DKC2_P_INDEX_8) != 0;
}

uint32_t dkc2_cpu_program_address(const dkc2_cpu *cpu) {
    return cpu == NULL ? 0 : ((uint32_t)cpu->pbr << 16) | cpu->pc;
}

static bool flag(const dkc2_cpu *cpu, uint8_t mask) {
    return (cpu->p & mask) != 0;
}

static void set_flag(dkc2_cpu *cpu, uint8_t mask, bool value) {
    if (value) {
        cpu->p = (uint8_t)(cpu->p | mask);
    } else {
        cpu->p = (uint8_t)(cpu->p & (uint8_t)~mask);
    }
}

static void set_nz(dkc2_cpu *cpu, uint16_t value, bool width8) {
    uint16_t mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);
    uint16_t sign = width8 ? UINT16_C(0x0080) : UINT16_C(0x8000);
    value = (uint16_t)(value & mask);
    set_flag(cpu, DKC2_P_ZERO, value == 0);
    set_flag(cpu, DKC2_P_NEGATIVE, (value & sign) != 0);
}

static void truncate_indexes(dkc2_cpu *cpu) {
    if (dkc2_cpu_index_is_8_bit(cpu)) {
        cpu->x = (uint16_t)(cpu->x & UINT16_C(0x00FF));
        cpu->y = (uint16_t)(cpu->y & UINT16_C(0x00FF));
    }
}

static void normalize_emulation_state(dkc2_cpu *cpu) {
    if (!cpu->e) {
        return;
    }
    cpu->p = (uint8_t)(cpu->p | DKC2_P_MEMORY_8 |
                       DKC2_P_INDEX_8);
    cpu->s = (uint16_t)(UINT16_C(0x0100) |
                        (cpu->s & UINT16_C(0x00FF)));
    truncate_indexes(cpu);
}

static void decrement_stack(dkc2_cpu *cpu) {
    if (cpu->e) {
        cpu->s = (uint16_t)(UINT16_C(0x0100) |
                            ((cpu->s - UINT16_C(1)) & UINT16_C(0x00FF)));
    } else {
        cpu->s = (uint16_t)(cpu->s - UINT16_C(1));
    }
}

static void increment_stack(dkc2_cpu *cpu) {
    if (cpu->e) {
        cpu->s = (uint16_t)(UINT16_C(0x0100) |
                            ((cpu->s + UINT16_C(1)) & UINT16_C(0x00FF)));
    } else {
        cpu->s = (uint16_t)(cpu->s + UINT16_C(1));
    }
}

static void push8(dkc2_cpu *cpu,
                  const dkc2_memory *memory,
                  uint8_t value) {
    write8(memory, cpu->s, value);
    decrement_stack(cpu);
}

static uint8_t pull8(dkc2_cpu *cpu, const dkc2_memory *memory) {
    increment_stack(cpu);
    return read8(memory, cpu->s);
}

static void push16(dkc2_cpu *cpu,
                   const dkc2_memory *memory,
                   uint16_t value) {
    push8(cpu, memory, (uint8_t)(value >> 8));
    push8(cpu, memory, (uint8_t)value);
}

static uint16_t pull16(dkc2_cpu *cpu, const dkc2_memory *memory) {
    uint16_t low = pull8(cpu, memory);
    uint16_t high = pull8(cpu, memory);
    return (uint16_t)(low | (uint16_t)(high << 8));
}

/*
 * A small set of W65C816 instructions deliberately crosses the emulation
 * stack-page boundary while transferring two or three bytes. These helpers
 * model that documented exception without changing legacy 6502 stack
 * behavior used by instructions such as JSR, RTS, interrupts, and RTI.
 */
static void push8_linear(dkc2_cpu *cpu,
                         const dkc2_memory *memory,
                         uint8_t value) {
    write8(memory, cpu->s, value);
    cpu->s = (uint16_t)(cpu->s - UINT16_C(1));
}

static uint8_t pull8_linear(dkc2_cpu *cpu,
                            const dkc2_memory *memory) {
    cpu->s = (uint16_t)(cpu->s + UINT16_C(1));
    return read8(memory, cpu->s);
}

static void push16_linear(dkc2_cpu *cpu,
                          const dkc2_memory *memory,
                          uint16_t value) {
    push8_linear(cpu, memory, (uint8_t)(value >> 8));
    push8_linear(cpu, memory, (uint8_t)value);
}

static uint16_t pull16_linear(dkc2_cpu *cpu,
                              const dkc2_memory *memory) {
    uint16_t low = pull8_linear(cpu, memory);
    uint16_t high = pull8_linear(cpu, memory);
    return (uint16_t)(low | (uint16_t)(high << 8));
}

static uint16_t direct_address(const dkc2_cpu *cpu, uint8_t operand) {
    return (uint16_t)(cpu->d + operand);
}

static uint16_t direct_indexed_address(const dkc2_cpu *cpu,
                                       uint8_t operand,
                                       uint16_t index) {
    if (cpu->e && (cpu->d & UINT16_C(0x00FF)) == 0) {
        return (uint16_t)((cpu->d & UINT16_C(0xFF00)) |
                          ((operand + index) & UINT16_C(0x00FF)));
    }
    return (uint16_t)(cpu->d + operand + index);
}

static uint16_t direct_pointer_next(const dkc2_cpu *cpu,
                                    uint16_t address,
                                    unsigned increment) {
    if (cpu->e && (cpu->d & UINT16_C(0x00FF)) == 0) {
        return (uint16_t)((address & UINT16_C(0xFF00)) |
                          ((address + increment) & UINT16_C(0x00FF)));
    }
    return (uint16_t)(address + increment);
}

static uint16_t read_direct_pointer16(const dkc2_cpu *cpu,
                                      const dkc2_memory *memory,
                                      uint16_t address) {
    uint16_t low = read8(memory, address);
    uint16_t high = read8(memory, direct_pointer_next(cpu, address, 1));
    return (uint16_t)(low | (uint16_t)(high << 8));
}

static uint32_t read_direct_pointer24(const dkc2_cpu *cpu,
                                      const dkc2_memory *memory,
                                      uint16_t address) {
    uint32_t low = read8(memory, address);
    uint32_t middle = read8(memory,
                            (uint16_t)(address + UINT16_C(1)));
    uint32_t high = read8(memory,
                          (uint16_t)(address + UINT16_C(2)));
    (void)cpu;
    return low | (middle << 8) | (high << 16);
}

static uint32_t add24(uint32_t address, uint16_t index) {
    return (address + index) & UINT32_C(0xFFFFFF);
}

static bool resolve_data_address(dkc2_cpu *cpu,
                                 const dkc2_memory *memory,
                                 dkc2_addressing_mode mode,
                                 uint32_t *address) {
    uint8_t direct;
    uint16_t absolute;
    uint16_t pointer;
    uint32_t long_pointer;

    switch (mode) {
        case DKC2_AM_DIRECT:
            *address = direct_address(cpu, fetch8(cpu, memory));
            return true;
        case DKC2_AM_DIRECT_X:
            *address = direct_indexed_address(cpu,
                                              fetch8(cpu, memory),
                                              cpu->x);
            return true;
        case DKC2_AM_DIRECT_Y:
            *address = direct_indexed_address(cpu,
                                              fetch8(cpu, memory),
                                              cpu->y);
            return true;
        case DKC2_AM_DIRECT_INDIRECT:
            direct = fetch8(cpu, memory);
            pointer = read_direct_pointer16(cpu,
                                            memory,
                                            direct_address(cpu, direct));
            *address = ((uint32_t)cpu->dbr << 16) | pointer;
            return true;
        case DKC2_AM_DIRECT_X_INDIRECT:
            direct = fetch8(cpu, memory);
            pointer = read16_bank(
                memory,
                direct_indexed_address(cpu, direct, cpu->x));
            *address = ((uint32_t)cpu->dbr << 16) | pointer;
            return true;
        case DKC2_AM_DIRECT_INDIRECT_Y:
            direct = fetch8(cpu, memory);
            pointer = read_direct_pointer16(cpu,
                                            memory,
                                            direct_address(cpu, direct));
            *address = add24(((uint32_t)cpu->dbr << 16) | pointer,
                             cpu->y);
            return true;
        case DKC2_AM_DIRECT_LONG_INDIRECT:
            direct = fetch8(cpu, memory);
            *address = read_direct_pointer24(cpu,
                                             memory,
                                             direct_address(cpu, direct));
            return true;
        case DKC2_AM_DIRECT_LONG_INDIRECT_Y:
            direct = fetch8(cpu, memory);
            long_pointer = read_direct_pointer24(cpu,
                                                 memory,
                                                 direct_address(cpu, direct));
            *address = add24(long_pointer, cpu->y);
            return true;
        case DKC2_AM_STACK_RELATIVE:
            absolute = cpu->e
                           ? (uint16_t)(UINT16_C(0x0100) |
                                        (cpu->s & UINT16_C(0x00FF)))
                           : cpu->s;
            *address = (uint16_t)(absolute + fetch8(cpu, memory));
            return true;
        case DKC2_AM_STACK_RELATIVE_INDIRECT_Y:
            direct = fetch8(cpu, memory);
            absolute = cpu->e
                           ? (uint16_t)(UINT16_C(0x0100) |
                                        (cpu->s & UINT16_C(0x00FF)))
                           : cpu->s;
            pointer = read16_bank(memory, (uint16_t)(absolute + direct));
            *address = add24(((uint32_t)cpu->dbr << 16) | pointer,
                             cpu->y);
            return true;
        case DKC2_AM_ABSOLUTE:
            *address = ((uint32_t)cpu->dbr << 16) | fetch16(cpu, memory);
            return true;
        case DKC2_AM_ABSOLUTE_X:
            absolute = fetch16(cpu, memory);
            *address = add24(((uint32_t)cpu->dbr << 16) | absolute,
                             cpu->x);
            return true;
        case DKC2_AM_ABSOLUTE_Y:
            absolute = fetch16(cpu, memory);
            *address = add24(((uint32_t)cpu->dbr << 16) | absolute,
                             cpu->y);
            return true;
        case DKC2_AM_LONG:
            *address = fetch24(cpu, memory);
            return true;
        case DKC2_AM_LONG_X:
            *address = add24(fetch24(cpu, memory), cpu->x);
            return true;
        default:
            return false;
    }
}

static uint16_t accumulator_value(const dkc2_cpu *cpu) {
    return dkc2_cpu_accumulator_is_8_bit(cpu)
               ? (uint16_t)(cpu->a & UINT16_C(0x00FF))
               : cpu->a;
}

static void set_accumulator(dkc2_cpu *cpu, uint16_t value) {
    if (dkc2_cpu_accumulator_is_8_bit(cpu)) {
        cpu->a = (uint16_t)((cpu->a & UINT16_C(0xFF00)) |
                            (value & UINT16_C(0x00FF)));
    } else {
        cpu->a = value;
    }
}

static uint16_t read_width(const dkc2_memory *memory,
                           uint32_t address,
                           bool width8,
                           bool cross_bank) {
    uint16_t low;
    uint16_t high;
    if (width8) {
        return read8(memory, address);
    }
    low = read8(memory, address);
    high = read8(memory, increment_data_address(address, cross_bank));
    return (uint16_t)(low | (uint16_t)(high << 8));
}

static void write_width(const dkc2_memory *memory,
                        uint32_t address,
                        uint16_t value,
                        bool width8,
                        bool cross_bank) {
    if (width8) {
        write8(memory, address, (uint8_t)value);
    } else {
        write8(memory, address, (uint8_t)value);
        write8(memory,
               increment_data_address(address, cross_bank),
               (uint8_t)(value >> 8));
    }
}

static bool addressing_crosses_bank(dkc2_addressing_mode mode) {
    switch (mode) {
        case DKC2_AM_DIRECT_INDIRECT:
        case DKC2_AM_DIRECT_X_INDIRECT:
        case DKC2_AM_DIRECT_INDIRECT_Y:
        case DKC2_AM_DIRECT_LONG_INDIRECT:
        case DKC2_AM_DIRECT_LONG_INDIRECT_Y:
        case DKC2_AM_STACK_RELATIVE_INDIRECT_Y:
        case DKC2_AM_ABSOLUTE:
        case DKC2_AM_ABSOLUTE_X:
        case DKC2_AM_ABSOLUTE_Y:
        case DKC2_AM_LONG:
        case DKC2_AM_LONG_X:
            return true;
        default:
            return false;
    }
}

static uint16_t fetch_width(dkc2_cpu *cpu,
                            const dkc2_memory *memory,
                            bool width8) {
    return width8 ? fetch8(cpu, memory) : fetch16(cpu, memory);
}

static uint16_t bcd_add(uint16_t left,
                        uint16_t right,
                        bool carry_in,
                        bool width8,
                        bool *carry_out,
                        bool *overflow_out) {
    unsigned digits = width8 ? 2U : 4U;
    unsigned carry = carry_in ? 1U : 0U;
    uint16_t result = 0;
    uint16_t sign = width8 ? UINT16_C(0x0080) : UINT16_C(0x8000);
    unsigned digit;

    for (digit = 0; digit < digits; ++digit) {
        unsigned shift = digit * 4U;
        unsigned sum = ((left >> shift) & 0xFU) +
                       ((right >> shift) & 0xFU) + carry;
        if (digit + 1U == digits) {
            uint16_t overflow_result =
                (sum & 0x8U) != 0 ? sign : UINT16_C(0);
            *overflow_out =
                ((~(left ^ right) & (left ^ overflow_result) & sign) != 0);
        }
        if (sum > 9U) {
            sum += 6U;
        }
        carry = sum > 0xFU;
        result = (uint16_t)(result | (uint16_t)((sum & 0xFU) << shift));
    }
    *carry_out = carry != 0;
    return result;
}

static uint16_t bcd_subtract(uint16_t left,
                             uint16_t right,
                             bool carry_in,
                             bool width8,
                             bool *carry_out) {
    unsigned digits = width8 ? 2U : 4U;
    int borrow = carry_in ? 0 : 1;
    uint16_t result = 0;
    unsigned digit;

    for (digit = 0; digit < digits; ++digit) {
        unsigned shift = digit * 4U;
        int difference = (int)((left >> shift) & 0xFU) -
                         (int)((right >> shift) & 0xFU) - borrow;
        if (difference < 0) {
            difference -= 6;
            borrow = 1;
        } else {
            borrow = 0;
        }
        result = (uint16_t)(result |
                            (uint16_t)(((unsigned)difference & 0xFU) << shift));
    }
    *carry_out = borrow == 0;
    return result;
}

static uint16_t execute_adc(dkc2_cpu *cpu,
                            uint16_t left,
                            uint16_t right,
                            bool width8) {
    uint32_t mask = width8 ? UINT32_C(0xFF) : UINT32_C(0xFFFF);
    uint32_t sign = width8 ? UINT32_C(0x80) : UINT32_C(0x8000);
    uint32_t binary = (left & mask) + (right & mask) +
                      (flag(cpu, DKC2_P_CARRY) ? 1U : 0U);
    uint16_t result;
    bool carry_out;
    bool overflow_out = false;

    if (flag(cpu, DKC2_P_DECIMAL)) {
        result = bcd_add(left,
                         right,
                         flag(cpu, DKC2_P_CARRY),
                         width8,
                         &carry_out,
                         &overflow_out);
    } else {
        result = (uint16_t)(binary & mask);
        carry_out = binary > mask;
        overflow_out =
            ((~((uint32_t)left ^ (uint32_t)right) &
              ((uint32_t)left ^ binary) & sign) != 0);
    }
    set_flag(cpu, DKC2_P_OVERFLOW, overflow_out);
    set_flag(cpu, DKC2_P_CARRY, carry_out);
    set_nz(cpu, result, width8);
    return result;
}

static uint16_t execute_sbc(dkc2_cpu *cpu,
                            uint16_t left,
                            uint16_t right,
                            bool width8) {
    uint32_t mask = width8 ? UINT32_C(0xFF) : UINT32_C(0xFFFF);
    uint32_t sign = width8 ? UINT32_C(0x80) : UINT32_C(0x8000);
    uint32_t borrow = flag(cpu, DKC2_P_CARRY) ? 0U : 1U;
    uint32_t binary = (left & mask) - (right & mask) - borrow;
    uint16_t result;
    bool carry_out;

    set_flag(cpu,
             DKC2_P_OVERFLOW,
             ((((uint32_t)left ^ (uint32_t)right) &
               ((uint32_t)left ^ binary) & sign) != 0));
    if (flag(cpu, DKC2_P_DECIMAL)) {
        result = bcd_subtract(left,
                              right,
                              flag(cpu, DKC2_P_CARRY),
                              width8,
                              &carry_out);
    } else {
        result = (uint16_t)(binary & mask);
        carry_out = (left & mask) >= ((right & mask) + borrow);
    }
    set_flag(cpu, DKC2_P_CARRY, carry_out);
    set_nz(cpu, result, width8);
    return result;
}

static void execute_compare(dkc2_cpu *cpu,
                            uint16_t left,
                            uint16_t right,
                            bool width8) {
    uint32_t mask = width8 ? UINT32_C(0xFF) : UINT32_C(0xFFFF);
    uint16_t result = (uint16_t)(((left & mask) - (right & mask)) & mask);
    set_flag(cpu, DKC2_P_CARRY, (left & mask) >= (right & mask));
    set_nz(cpu, result, width8);
}

static execute_operation operation_for(uint8_t opcode) {
    switch (opcode) {
        case 0x01: case 0x03: case 0x05: case 0x07: case 0x09:
        case 0x0D: case 0x0F: case 0x11: case 0x12: case 0x13:
        case 0x15: case 0x17: case 0x19: case 0x1D: case 0x1F:
            return EXEC_ORA;
        case 0x21: case 0x23: case 0x25: case 0x27: case 0x29:
        case 0x2D: case 0x2F: case 0x31: case 0x32: case 0x33:
        case 0x35: case 0x37: case 0x39: case 0x3D: case 0x3F:
            return EXEC_AND;
        case 0x41: case 0x43: case 0x45: case 0x47: case 0x49:
        case 0x4D: case 0x4F: case 0x51: case 0x52: case 0x53:
        case 0x55: case 0x57: case 0x59: case 0x5D: case 0x5F:
            return EXEC_EOR;
        case 0x61: case 0x63: case 0x65: case 0x67: case 0x69:
        case 0x6D: case 0x6F: case 0x71: case 0x72: case 0x73:
        case 0x75: case 0x77: case 0x79: case 0x7D: case 0x7F:
            return EXEC_ADC;
        case 0x81: case 0x83: case 0x85: case 0x87:
        case 0x8D: case 0x8F: case 0x91: case 0x92: case 0x93:
        case 0x95: case 0x97: case 0x99: case 0x9D: case 0x9F:
            return EXEC_STA;
        case 0xA1: case 0xA3: case 0xA5: case 0xA7: case 0xA9:
        case 0xAD: case 0xAF: case 0xB1: case 0xB2: case 0xB3:
        case 0xB5: case 0xB7: case 0xB9: case 0xBD: case 0xBF:
            return EXEC_LDA;
        case 0xC1: case 0xC3: case 0xC5: case 0xC7: case 0xC9:
        case 0xCD: case 0xCF: case 0xD1: case 0xD2: case 0xD3:
        case 0xD5: case 0xD7: case 0xD9: case 0xDD: case 0xDF:
            return EXEC_CMP;
        case 0xE1: case 0xE3: case 0xE5: case 0xE7: case 0xE9:
        case 0xED: case 0xEF: case 0xF1: case 0xF2: case 0xF3:
        case 0xF5: case 0xF7: case 0xF9: case 0xFD: case 0xFF:
            return EXEC_SBC;
        case 0x06: case 0x0A: case 0x0E: case 0x16: case 0x1E:
            return EXEC_ASL;
        case 0x26: case 0x2A: case 0x2E: case 0x36: case 0x3E:
            return EXEC_ROL;
        case 0x46: case 0x4A: case 0x4E: case 0x56: case 0x5E:
            return EXEC_LSR;
        case 0x66: case 0x6A: case 0x6E: case 0x76: case 0x7E:
            return EXEC_ROR;
        case 0x86: case 0x8E: case 0x96:
            return EXEC_STX;
        case 0xA2: case 0xA6: case 0xAE: case 0xB6: case 0xBE:
            return EXEC_LDX;
        case 0x3A: case 0xC6: case 0xCE: case 0xD6: case 0xDE:
            return EXEC_DEC;
        case 0x1A: case 0xE6: case 0xEE: case 0xF6: case 0xFE:
            return EXEC_INC;
        case 0x84: case 0x8C: case 0x94:
            return EXEC_STY;
        case 0xA0: case 0xA4: case 0xAC: case 0xB4: case 0xBC:
            return EXEC_LDY;
        case 0xE0: case 0xE4: case 0xEC:
            return EXEC_CPX;
        case 0xC0: case 0xC4: case 0xCC:
            return EXEC_CPY;
        case 0x24: case 0x2C: case 0x34: case 0x3C: case 0x89:
            return EXEC_BIT;
        case 0x04: case 0x0C:
            return EXEC_TSB;
        case 0x14: case 0x1C:
            return EXEC_TRB;
        case 0x64: case 0x74: case 0x9C: case 0x9E:
            return EXEC_STZ;
        default:
            return EXEC_NONE;
    }
}

static bool operation_uses_index_width(execute_operation operation) {
    return operation == EXEC_STX || operation == EXEC_LDX ||
           operation == EXEC_STY || operation == EXEC_LDY ||
           operation == EXEC_CPX || operation == EXEC_CPY;
}

static bool operation_is_store(execute_operation operation) {
    return operation == EXEC_STA || operation == EXEC_STX ||
           operation == EXEC_STY || operation == EXEC_STZ;
}

static bool execute_generic(dkc2_cpu *cpu,
                            const dkc2_memory *memory,
                            uint8_t opcode) {
    const dkc2_opcode_description *description =
        dkc2_opcode_description_for(opcode);
    execute_operation operation = operation_for(opcode);
    bool width8;
    bool accumulator;
    bool cross_bank;
    uint32_t address = 0;
    uint16_t value = 0;
    uint16_t left;
    uint16_t result;
    uint16_t mask;
    uint16_t sign;
    bool old_carry;

    if (operation == EXEC_NONE || description == NULL) {
        return false;
    }

    width8 = operation_uses_index_width(operation)
                 ? dkc2_cpu_index_is_8_bit(cpu)
                 : dkc2_cpu_accumulator_is_8_bit(cpu);
    accumulator = description->addressing_mode == DKC2_AM_ACCUMULATOR;
    cross_bank = addressing_crosses_bank(description->addressing_mode);
    if (description->addressing_mode == DKC2_AM_IMMEDIATE_M ||
        description->addressing_mode == DKC2_AM_IMMEDIATE_X) {
        value = fetch_width(cpu, memory, width8);
    } else if (!accumulator) {
        if (!resolve_data_address(cpu,
                                  memory,
                                  description->addressing_mode,
                                  &address)) {
            return false;
        }
        if (!operation_is_store(operation)) {
            value = read_width(memory, address, width8, cross_bank);
        }
    }

    mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);
    sign = width8 ? UINT16_C(0x0080) : UINT16_C(0x8000);

    switch (operation) {
        case EXEC_ORA:
            result = (uint16_t)(accumulator_value(cpu) | value);
            set_accumulator(cpu, result);
            set_nz(cpu, result, width8);
            break;
        case EXEC_AND:
            result = (uint16_t)(accumulator_value(cpu) & value);
            set_accumulator(cpu, result);
            set_nz(cpu, result, width8);
            break;
        case EXEC_EOR:
            result = (uint16_t)(accumulator_value(cpu) ^ value);
            set_accumulator(cpu, result);
            set_nz(cpu, result, width8);
            break;
        case EXEC_ADC:
            result = execute_adc(cpu,
                                 accumulator_value(cpu),
                                 value,
                                 width8);
            set_accumulator(cpu, result);
            break;
        case EXEC_STA:
            write_width(memory,
                        address,
                        accumulator_value(cpu),
                        width8,
                        cross_bank);
            break;
        case EXEC_LDA:
            set_accumulator(cpu, value);
            set_nz(cpu, value, width8);
            break;
        case EXEC_CMP:
            execute_compare(cpu,
                            accumulator_value(cpu),
                            value,
                            width8);
            break;
        case EXEC_SBC:
            result = execute_sbc(cpu,
                                 accumulator_value(cpu),
                                 value,
                                 width8);
            set_accumulator(cpu, result);
            break;
        case EXEC_ASL:
        case EXEC_ROL:
        case EXEC_LSR:
        case EXEC_ROR:
            left = accumulator ? accumulator_value(cpu) : value;
            old_carry = flag(cpu, DKC2_P_CARRY);
            if (operation == EXEC_ASL || operation == EXEC_ROL) {
                set_flag(cpu, DKC2_P_CARRY, (left & sign) != 0);
                result = (uint16_t)((left << 1) & mask);
                if (operation == EXEC_ROL && old_carry) {
                    result = (uint16_t)(result | UINT16_C(1));
                }
            } else {
                set_flag(cpu, DKC2_P_CARRY,
                         (left & UINT16_C(1)) != 0);
                result = (uint16_t)(left >> 1);
                if (operation == EXEC_ROR && old_carry) {
                    result = (uint16_t)(result | sign);
                }
            }
            if (accumulator) {
                set_accumulator(cpu, result);
            } else {
                write_width(memory,
                            address,
                            result,
                            width8,
                            cross_bank);
            }
            set_nz(cpu, result, width8);
            break;
        case EXEC_STX:
            write_width(memory,
                        address,
                        cpu->x,
                        width8,
                        cross_bank);
            break;
        case EXEC_LDX:
            cpu->x = (uint16_t)(value & mask);
            set_nz(cpu, cpu->x, width8);
            break;
        case EXEC_DEC:
        case EXEC_INC:
            left = accumulator ? accumulator_value(cpu) : value;
            result = operation == EXEC_INC
                         ? (uint16_t)((left + UINT16_C(1)) & mask)
                         : (uint16_t)((left - UINT16_C(1)) & mask);
            if (accumulator) {
                set_accumulator(cpu, result);
            } else {
                write_width(memory,
                            address,
                            result,
                            width8,
                            cross_bank);
            }
            set_nz(cpu, result, width8);
            break;
        case EXEC_STY:
            write_width(memory,
                        address,
                        cpu->y,
                        width8,
                        cross_bank);
            break;
        case EXEC_LDY:
            cpu->y = (uint16_t)(value & mask);
            set_nz(cpu, cpu->y, width8);
            break;
        case EXEC_CPX:
            execute_compare(cpu, cpu->x, value, width8);
            break;
        case EXEC_CPY:
            execute_compare(cpu, cpu->y, value, width8);
            break;
        case EXEC_BIT:
            set_flag(cpu,
                     DKC2_P_ZERO,
                     (accumulator_value(cpu) & value & mask) == 0);
            if (description->addressing_mode != DKC2_AM_IMMEDIATE_M) {
                set_flag(cpu, DKC2_P_NEGATIVE, (value & sign) != 0);
                set_flag(cpu,
                         DKC2_P_OVERFLOW,
                         (value & (uint16_t)(sign >> 1)) != 0);
            }
            break;
        case EXEC_TSB:
        case EXEC_TRB:
            left = accumulator_value(cpu);
            set_flag(cpu, DKC2_P_ZERO, (left & value & mask) == 0);
            result = operation == EXEC_TSB
                         ? (uint16_t)(value | left)
                         : (uint16_t)(value & (uint16_t)~left);
            write_width(memory,
                        address,
                        result,
                        width8,
                        cross_bank);
            break;
        case EXEC_STZ:
            write_width(memory, address, 0, width8, cross_bank);
            break;
        case EXEC_NONE:
            return false;
    }
    return true;
}

static void replace_status(dkc2_cpu *cpu, uint8_t value) {
    bool old_index8 = dkc2_cpu_index_is_8_bit(cpu);
    cpu->p = value;
    if (cpu->e) {
        cpu->p = (uint8_t)(cpu->p | DKC2_P_MEMORY_8 |
                           DKC2_P_INDEX_8);
    }
    if (!old_index8 && dkc2_cpu_index_is_8_bit(cpu)) {
        truncate_indexes(cpu);
    }
}

static uint8_t status_for_push(const dkc2_cpu *cpu, bool break_flag) {
    uint8_t status = cpu->p;
    if (cpu->e) {
        status = (uint8_t)(status | DKC2_P_MEMORY_8);
        if (break_flag) {
            status = (uint8_t)(status | DKC2_P_INDEX_8);
        } else {
            status = (uint8_t)(status & (uint8_t)~DKC2_P_INDEX_8);
        }
    }
    return status;
}

static void enter_interrupt(dkc2_cpu *cpu,
                            const dkc2_memory *memory,
                            uint16_t vector,
                            bool break_flag) {
    normalize_emulation_state(cpu);
    if (!cpu->e) {
        push8(cpu, memory, cpu->pbr);
    }
    push16(cpu, memory, cpu->pc);
    push8(cpu, memory, status_for_push(cpu, break_flag));
    cpu->p = (uint8_t)((cpu->p | DKC2_P_IRQ_DISABLE) &
                       (uint8_t)~DKC2_P_DECIMAL);
    cpu->pbr = 0;
    cpu->pc = read16_bank(memory, vector);
    cpu->waiting = false;
    normalize_emulation_state(cpu);
}

bool dkc2_cpu_reset(dkc2_cpu *cpu, const dkc2_memory *memory) {
    if (cpu == NULL || !memory_valid(memory)) {
        return false;
    }
    memset(cpu, 0, sizeof(*cpu));
    cpu->e = true;
    cpu->p = DKC2_P_MEMORY_8 | DKC2_P_INDEX_8 | DKC2_P_IRQ_DISABLE;
    cpu->s = UINT16_C(0x01FF);
    cpu->pc = read16_bank(memory, UINT32_C(0x00FFFC));
    return true;
}

bool dkc2_cpu_nmi(dkc2_cpu *cpu, const dkc2_memory *memory) {
    uint16_t vector;
    if (cpu == NULL || !memory_valid(memory) || cpu->stopped) {
        return false;
    }
    vector = cpu->e ? UINT16_C(0xFFFA) : UINT16_C(0xFFEA);
    enter_interrupt(cpu, memory, vector, false);
    return true;
}

bool dkc2_cpu_irq(dkc2_cpu *cpu, const dkc2_memory *memory) {
    uint16_t vector;
    if (cpu == NULL || !memory_valid(memory) || cpu->stopped ||
        flag(cpu, DKC2_P_IRQ_DISABLE)) {
        return false;
    }
    vector = cpu->e ? UINT16_C(0xFFFE) : UINT16_C(0xFFEE);
    enter_interrupt(cpu, memory, vector, false);
    return true;
}

static void branch8(dkc2_cpu *cpu,
                    const dkc2_memory *memory,
                    bool take) {
    int8_t displacement = (int8_t)fetch8(cpu, memory);
    if (take) {
        cpu->pc = (uint16_t)(cpu->pc + displacement);
    }
}

static void transfer_to_index(dkc2_cpu *cpu,
                              uint16_t *destination,
                              uint16_t value) {
    bool width8 = dkc2_cpu_index_is_8_bit(cpu);
    *destination = width8 ? (uint16_t)(value & UINT16_C(0x00FF)) : value;
    set_nz(cpu, *destination, width8);
}

static void block_move(dkc2_cpu *cpu,
                       const dkc2_memory *memory,
                       bool increment) {
    uint8_t destination_bank = fetch8(cpu, memory);
    uint8_t source_bank = fetch8(cpu, memory);
    bool width8 = dkc2_cpu_index_is_8_bit(cpu);
    uint16_t mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);

    cpu->dbr = destination_bank;
    do {
        uint32_t source = ((uint32_t)source_bank << 16) | cpu->x;
        uint32_t destination =
            ((uint32_t)destination_bank << 16) | cpu->y;

        write8(memory, destination, read8(memory, source));
        cpu->a = (uint16_t)(cpu->a - UINT16_C(1));
        if (increment) {
            cpu->x = (uint16_t)((cpu->x + UINT16_C(1)) & mask);
            cpu->y = (uint16_t)((cpu->y + UINT16_C(1)) & mask);
        } else {
            cpu->x = (uint16_t)((cpu->x - UINT16_C(1)) & mask);
            cpu->y = (uint16_t)((cpu->y - UINT16_C(1)) & mask);
        }
    } while (cpu->a != UINT16_C(0xFFFF));
}

const char *dkc2_step_result_name(dkc2_step_result result) {
    switch (result) {
        case DKC2_STEP_OK:
            return "ok";
        case DKC2_STEP_WAITING:
            return "waiting for interrupt";
        case DKC2_STEP_STOPPED:
            return "processor stopped";
        case DKC2_STEP_INVALID_ARGUMENT:
            return "invalid CPU or memory interface";
    }
    return "unknown execution result";
}

dkc2_step_result dkc2_cpu_step(dkc2_cpu *cpu,
                               const dkc2_memory *memory) {
    uint8_t opcode;
    uint8_t operand8;
    uint16_t operand16;
    uint16_t value16;
    uint32_t operand24;
    uint32_t pointer_address;
    bool width8;
    bool old_e;
    bool old_carry;
    uint16_t mask;

    if (cpu == NULL || !memory_valid(memory)) {
        return DKC2_STEP_INVALID_ARGUMENT;
    }
    normalize_emulation_state(cpu);
    if (cpu->stopped) {
        return DKC2_STEP_STOPPED;
    }
    if (cpu->waiting) {
        return DKC2_STEP_WAITING;
    }

    opcode = fetch8(cpu, memory);
    ++cpu->instructions;

    switch (opcode) {
        case 0x00: /* BRK */
            (void)fetch8(cpu, memory);
            enter_interrupt(cpu,
                            memory,
                            cpu->e ? UINT16_C(0xFFFE) : UINT16_C(0xFFE6),
                            true);
            break;
        case 0x02: /* COP */
            (void)fetch8(cpu, memory);
            enter_interrupt(cpu,
                            memory,
                            cpu->e ? UINT16_C(0xFFF4) : UINT16_C(0xFFE4),
                            true);
            break;
        case 0x08: /* PHP */
            push8(cpu, memory, status_for_push(cpu, true));
            break;
        case 0x0B: /* PHD */
            push16_linear(cpu, memory, cpu->d);
            break;
        case 0x10: /* BPL */
            branch8(cpu, memory, !flag(cpu, DKC2_P_NEGATIVE));
            break;
        case 0x18: /* CLC */
            set_flag(cpu, DKC2_P_CARRY, false);
            break;
        case 0x1B: /* TCS */
            cpu->s = cpu->e
                         ? (uint16_t)(UINT16_C(0x0100) |
                                      (cpu->a & UINT16_C(0x00FF)))
                         : cpu->a;
            break;
        case 0x20: /* JSR absolute */
            operand16 = fetch16(cpu, memory);
            push16(cpu, memory, (uint16_t)(cpu->pc - UINT16_C(1)));
            cpu->pc = operand16;
            break;
        case 0x22: /* JSL long */
            operand24 = fetch24(cpu, memory);
            push8_linear(cpu, memory, cpu->pbr);
            push16_linear(cpu,
                          memory,
                          (uint16_t)(cpu->pc - UINT16_C(1)));
            cpu->pbr = (uint8_t)(operand24 >> 16);
            cpu->pc = (uint16_t)operand24;
            break;
        case 0x28: /* PLP */
            replace_status(cpu, pull8(cpu, memory));
            break;
        case 0x2B: /* PLD */
            cpu->d = pull16_linear(cpu, memory);
            set_nz(cpu, cpu->d, false);
            break;
        case 0x30: /* BMI */
            branch8(cpu, memory, flag(cpu, DKC2_P_NEGATIVE));
            break;
        case 0x38: /* SEC */
            set_flag(cpu, DKC2_P_CARRY, true);
            break;
        case 0x3B: /* TSC */
            cpu->a = cpu->e
                         ? (uint16_t)(UINT16_C(0x0100) |
                                      (cpu->s & UINT16_C(0x00FF)))
                         : cpu->s;
            set_nz(cpu, cpu->a, false);
            break;
        case 0x40: /* RTI */
            replace_status(cpu, pull8(cpu, memory));
            cpu->pc = pull16(cpu, memory);
            if (!cpu->e) {
                cpu->pbr = pull8(cpu, memory);
            }
            break;
        case 0x42: /* WDM */
            (void)fetch8(cpu, memory);
            break;
        case 0x44: /* MVP */
            block_move(cpu, memory, false);
            break;
        case 0x48: /* PHA */
            if (dkc2_cpu_accumulator_is_8_bit(cpu)) {
                push8(cpu, memory, (uint8_t)cpu->a);
            } else {
                push16(cpu, memory, cpu->a);
            }
            break;
        case 0x4B: /* PHK */
            push8(cpu, memory, cpu->pbr);
            break;
        case 0x4C: /* JMP absolute */
            cpu->pc = fetch16(cpu, memory);
            break;
        case 0x50: /* BVC */
            branch8(cpu, memory, !flag(cpu, DKC2_P_OVERFLOW));
            break;
        case 0x54: /* MVN */
            block_move(cpu, memory, true);
            break;
        case 0x58: /* CLI */
            set_flag(cpu, DKC2_P_IRQ_DISABLE, false);
            break;
        case 0x5A: /* PHY */
            if (dkc2_cpu_index_is_8_bit(cpu)) {
                push8(cpu, memory, (uint8_t)cpu->y);
            } else {
                push16(cpu, memory, cpu->y);
            }
            break;
        case 0x5B: /* TCD */
            cpu->d = cpu->a;
            set_nz(cpu, cpu->d, false);
            break;
        case 0x5C: /* JML long */
            operand24 = fetch24(cpu, memory);
            cpu->pbr = (uint8_t)(operand24 >> 16);
            cpu->pc = (uint16_t)operand24;
            break;
        case 0x60: /* RTS */
            cpu->pc = (uint16_t)(pull16(cpu, memory) + UINT16_C(1));
            break;
        case 0x62: /* PER */
            operand16 = fetch16(cpu, memory);
            push16_linear(cpu,
                          memory,
                          (uint16_t)(cpu->pc + (int16_t)operand16));
            break;
        case 0x68: /* PLA */
            width8 = dkc2_cpu_accumulator_is_8_bit(cpu);
            value16 = width8 ? pull8(cpu, memory) : pull16(cpu, memory);
            set_accumulator(cpu, value16);
            set_nz(cpu, value16, width8);
            break;
        case 0x6B: /* RTL */
            cpu->pc = (uint16_t)(pull16_linear(cpu, memory) +
                                 UINT16_C(1));
            cpu->pbr = pull8_linear(cpu, memory);
            break;
        case 0x6C: /* JMP (absolute) */
            pointer_address = fetch16(cpu, memory);
            cpu->pc = read16_bank(memory, pointer_address);
            break;
        case 0x70: /* BVS */
            branch8(cpu, memory, flag(cpu, DKC2_P_OVERFLOW));
            break;
        case 0x78: /* SEI */
            set_flag(cpu, DKC2_P_IRQ_DISABLE, true);
            break;
        case 0x7A: /* PLY */
            width8 = dkc2_cpu_index_is_8_bit(cpu);
            cpu->y = width8 ? pull8(cpu, memory) : pull16(cpu, memory);
            set_nz(cpu, cpu->y, width8);
            break;
        case 0x7B: /* TDC */
            cpu->a = cpu->d;
            set_nz(cpu, cpu->a, false);
            break;
        case 0x7C: /* JMP (absolute,X) */
            operand16 = fetch16(cpu, memory);
            pointer_address = ((uint32_t)cpu->pbr << 16) |
                              (uint16_t)(operand16 + cpu->x);
            cpu->pc = read16_bank(memory, pointer_address);
            break;
        case 0x80: /* BRA */
            branch8(cpu, memory, true);
            break;
        case 0x82: /* BRL */
            operand16 = fetch16(cpu, memory);
            cpu->pc = (uint16_t)(cpu->pc + (int16_t)operand16);
            break;
        case 0x88: /* DEY */
            width8 = dkc2_cpu_index_is_8_bit(cpu);
            mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);
            cpu->y = (uint16_t)((cpu->y - UINT16_C(1)) & mask);
            set_nz(cpu, cpu->y, width8);
            break;
        case 0x8A: /* TXA */
            width8 = dkc2_cpu_accumulator_is_8_bit(cpu);
            set_accumulator(cpu, cpu->x);
            set_nz(cpu, cpu->x, width8);
            break;
        case 0x8B: /* PHB */
            push8(cpu, memory, cpu->dbr);
            break;
        case 0x90: /* BCC */
            branch8(cpu, memory, !flag(cpu, DKC2_P_CARRY));
            break;
        case 0x98: /* TYA */
            width8 = dkc2_cpu_accumulator_is_8_bit(cpu);
            set_accumulator(cpu, cpu->y);
            set_nz(cpu, cpu->y, width8);
            break;
        case 0x9A: /* TXS */
            cpu->s = cpu->e
                         ? (uint16_t)(UINT16_C(0x0100) |
                                      (cpu->x & UINT16_C(0x00FF)))
                         : cpu->x;
            break;
        case 0x9B: /* TXY */
            transfer_to_index(cpu, &cpu->y, cpu->x);
            break;
        case 0xA8: /* TAY */
            transfer_to_index(cpu, &cpu->y, cpu->a);
            break;
        case 0xAA: /* TAX */
            transfer_to_index(cpu, &cpu->x, cpu->a);
            break;
        case 0xAB: /* PLB */
            cpu->dbr = pull8_linear(cpu, memory);
            set_nz(cpu, cpu->dbr, true);
            break;
        case 0xB0: /* BCS */
            branch8(cpu, memory, flag(cpu, DKC2_P_CARRY));
            break;
        case 0xB8: /* CLV */
            set_flag(cpu, DKC2_P_OVERFLOW, false);
            break;
        case 0xBA: /* TSX */
            value16 = cpu->e
                          ? (uint16_t)(UINT16_C(0x0100) |
                                       (cpu->s & UINT16_C(0x00FF)))
                          : cpu->s;
            transfer_to_index(cpu, &cpu->x, value16);
            break;
        case 0xBB: /* TYX */
            transfer_to_index(cpu, &cpu->x, cpu->y);
            break;
        case 0xC2: /* REP */
            operand8 = fetch8(cpu, memory);
            replace_status(cpu, (uint8_t)(cpu->p & (uint8_t)~operand8));
            break;
        case 0xC8: /* INY */
            width8 = dkc2_cpu_index_is_8_bit(cpu);
            mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);
            cpu->y = (uint16_t)((cpu->y + UINT16_C(1)) & mask);
            set_nz(cpu, cpu->y, width8);
            break;
        case 0xCA: /* DEX */
            width8 = dkc2_cpu_index_is_8_bit(cpu);
            mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);
            cpu->x = (uint16_t)((cpu->x - UINT16_C(1)) & mask);
            set_nz(cpu, cpu->x, width8);
            break;
        case 0xCB: /* WAI */
            cpu->waiting = true;
            normalize_emulation_state(cpu);
            return DKC2_STEP_WAITING;
        case 0xD0: /* BNE */
            branch8(cpu, memory, !flag(cpu, DKC2_P_ZERO));
            break;
        case 0xD4: /* PEI */
            operand8 = fetch8(cpu, memory);
            operand16 = read16_bank(memory,
                                    direct_address(cpu, operand8));
            push16_linear(cpu, memory, operand16);
            break;
        case 0xD8: /* CLD */
            set_flag(cpu, DKC2_P_DECIMAL, false);
            break;
        case 0xDA: /* PHX */
            if (dkc2_cpu_index_is_8_bit(cpu)) {
                push8(cpu, memory, (uint8_t)cpu->x);
            } else {
                push16(cpu, memory, cpu->x);
            }
            break;
        case 0xDB: /* STP */
            cpu->stopped = true;
            normalize_emulation_state(cpu);
            return DKC2_STEP_STOPPED;
        case 0xDC: /* JML [absolute] */
            pointer_address = fetch16(cpu, memory);
            operand24 = read24_bank(memory, pointer_address);
            cpu->pbr = (uint8_t)(operand24 >> 16);
            cpu->pc = (uint16_t)operand24;
            break;
        case 0xE2: /* SEP */
            operand8 = fetch8(cpu, memory);
            replace_status(cpu, (uint8_t)(cpu->p | operand8));
            break;
        case 0xE8: /* INX */
            width8 = dkc2_cpu_index_is_8_bit(cpu);
            mask = width8 ? UINT16_C(0x00FF) : UINT16_C(0xFFFF);
            cpu->x = (uint16_t)((cpu->x + UINT16_C(1)) & mask);
            set_nz(cpu, cpu->x, width8);
            break;
        case 0xEA: /* NOP */
            break;
        case 0xEB: /* XBA */
            cpu->a = (uint16_t)((cpu->a << 8) | (cpu->a >> 8));
            set_nz(cpu, cpu->a, true);
            break;
        case 0xF0: /* BEQ */
            branch8(cpu, memory, flag(cpu, DKC2_P_ZERO));
            break;
        case 0xF4: /* PEA */
            push16_linear(cpu, memory, fetch16(cpu, memory));
            break;
        case 0xF8: /* SED */
            set_flag(cpu, DKC2_P_DECIMAL, true);
            break;
        case 0xFA: /* PLX */
            width8 = dkc2_cpu_index_is_8_bit(cpu);
            cpu->x = width8 ? pull8(cpu, memory) : pull16(cpu, memory);
            set_nz(cpu, cpu->x, width8);
            break;
        case 0xFB: /* XCE */
            old_e = cpu->e;
            old_carry = flag(cpu, DKC2_P_CARRY);
            cpu->e = old_carry;
            set_flag(cpu, DKC2_P_CARRY, old_e);
            if (cpu->e) {
                normalize_emulation_state(cpu);
            }
            break;
        case 0xFC: /* JSR (absolute,X) */
            operand16 = fetch16(cpu, memory);
            pointer_address = ((uint32_t)cpu->pbr << 16) |
                              (uint16_t)(operand16 + cpu->x);
            value16 = read16_bank(memory, pointer_address);
            push16(cpu, memory, (uint16_t)(cpu->pc - UINT16_C(1)));
            cpu->pc = value16;
            break;
        default:
            if (!execute_generic(cpu, memory, opcode)) {
                normalize_emulation_state(cpu);
                return DKC2_STEP_INVALID_ARGUMENT;
            }
            break;
    }

    normalize_emulation_state(cpu);
    return DKC2_STEP_OK;
}
