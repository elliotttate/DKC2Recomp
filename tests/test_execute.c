#include "dkc2/execute.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLAT_MEMORY_SIZE ((size_t)1 << 24)

typedef struct flat_memory {
    uint8_t *bytes;
} flat_memory;

static uint8_t flat_read(void *context, uint32_t address) {
    flat_memory *memory = (flat_memory *)context;
    return memory->bytes[address & UINT32_C(0xFFFFFF)];
}

static void flat_write(void *context, uint32_t address, uint8_t value) {
    flat_memory *memory = (flat_memory *)context;
    memory->bytes[address & UINT32_C(0xFFFFFF)] = value;
}

static void fail(const char *message) {
    (void)fprintf(stderr, "%s\n", message);
    exit(EXIT_FAILURE);
}

static void step_ok(dkc2_cpu *cpu, const dkc2_memory *memory) {
    dkc2_step_result result = dkc2_cpu_step(cpu, memory);
    if (result != DKC2_STEP_OK) {
        (void)fprintf(stderr,
                      "step failed at $%06X: %s\n",
                      (unsigned)dkc2_cpu_program_address(cpu),
                      dkc2_step_result_name(result));
        exit(EXIT_FAILURE);
    }
}

static void test_reset_and_program(flat_memory *flat,
                                   const dkc2_memory *memory) {
    static const uint8_t program[] = {
        0x18,                         /* CLC */
        0xFB,                         /* XCE */
        0xC2, 0x30,                   /* REP #$30 */
        0x18,                         /* CLC */
        0xA9, 0x34, 0x12,             /* LDA #$1234 */
        0x69, 0x01, 0x00,             /* ADC #$0001 */
        0xAA,                         /* TAX */
        0x48,                         /* PHA */
        0xA9, 0x00, 0x00,             /* LDA #$0000 */
        0x68,                         /* PLA */
        0x8D, 0x00, 0x20              /* STA $2000 */
    };
    dkc2_cpu cpu;
    size_t i;

    memcpy(&flat->bytes[0x008000], program, sizeof(program));
    flat->bytes[0x00FFFC] = 0x00;
    flat->bytes[0x00FFFD] = 0x80;
    if (!dkc2_cpu_reset(&cpu, memory) || cpu.pc != UINT16_C(0x8000) ||
        !cpu.e || cpu.s != UINT16_C(0x01FF) || cpu.p != UINT8_C(0x34)) {
        fail("reset state is incorrect");
    }

    for (i = 0; i < 11; ++i) {
        step_ok(&cpu, memory);
    }
    if (cpu.e || cpu.a != UINT16_C(0x1235) ||
        cpu.x != UINT16_C(0x1235) || cpu.s != UINT16_C(0x01FF) ||
        flat->bytes[0x002000] != UINT8_C(0x35) ||
        flat->bytes[0x002001] != UINT8_C(0x12)) {
        fail("wide-mode execution program produced the wrong state");
    }
}

static void test_calls(flat_memory *flat, const dkc2_memory *memory) {
    static const uint8_t caller[] = {
        0x20, 0x08, 0x90, /* JSR $9008 */
        0xA9, 0x77,       /* LDA #$77 */
        0xEA, 0xEA, 0xEA,
        0xA9, 0x42,       /* LDA #$42 */
        0x60              /* RTS */
    };
    dkc2_cpu cpu;

    memcpy(&flat->bytes[0x009000], caller, sizeof(caller));
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = true;
    cpu.p = DKC2_P_MEMORY_8 | DKC2_P_INDEX_8;
    cpu.s = UINT16_C(0x01FF);
    cpu.pc = UINT16_C(0x9000);

    step_ok(&cpu, memory);
    if (cpu.pc != UINT16_C(0x9008) || cpu.s != UINT16_C(0x01FD)) {
        fail("JSR did not enter the subroutine correctly");
    }
    step_ok(&cpu, memory);
    step_ok(&cpu, memory);
    if (cpu.pc != UINT16_C(0x9003) ||
        (cpu.a & UINT16_C(0x00FF)) != UINT16_C(0x0042)) {
        fail("RTS did not return correctly");
    }
    step_ok(&cpu, memory);
    if ((cpu.a & UINT16_C(0x00FF)) != UINT16_C(0x0077)) {
        fail("caller did not resume after RTS");
    }
}

static void test_decimal(flat_memory *flat, const dkc2_memory *memory) {
    dkc2_cpu cpu;

    flat->bytes[0x00A000] = 0x69; /* ADC #$55 */
    flat->bytes[0x00A001] = 0x55;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = false;
    cpu.p = DKC2_P_MEMORY_8 | DKC2_P_DECIMAL;
    cpu.a = UINT16_C(0x0045);
    cpu.pc = UINT16_C(0xA000);
    step_ok(&cpu, memory);
    if ((cpu.a & UINT16_C(0x00FF)) != 0 ||
        (cpu.p & (DKC2_P_CARRY | DKC2_P_ZERO)) !=
            (DKC2_P_CARRY | DKC2_P_ZERO)) {
        fail("8-bit decimal ADC is incorrect");
    }

    flat->bytes[0x00A100] = 0x69; /* ADC #$0001 */
    flat->bytes[0x00A101] = 0x01;
    flat->bytes[0x00A102] = 0x00;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = false;
    cpu.p = DKC2_P_DECIMAL;
    cpu.a = UINT16_C(0x9999);
    cpu.pc = UINT16_C(0xA100);
    step_ok(&cpu, memory);
    if (cpu.a != 0 || (cpu.p & DKC2_P_CARRY) == 0) {
        fail("16-bit decimal ADC is incorrect");
    }

    flat->bytes[0x00A200] = 0x69; /* ADC #$F7, invalid BCD flag edge */
    flat->bytes[0x00A201] = 0xF7;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = false;
    cpu.p = DKC2_P_MEMORY_8 | DKC2_P_DECIMAL;
    cpu.a = UINT16_C(0x0088);
    cpu.pc = UINT16_C(0xA200);
    step_ok(&cpu, memory);
    if ((cpu.a & UINT16_C(0x00FF)) != UINT16_C(0x00E5) ||
        (cpu.p & DKC2_P_OVERFLOW) != 0) {
        fail("decimal ADC intermediate overflow behavior is incorrect");
    }
}

static void test_stack_boundaries(flat_memory *flat,
                                  const dkc2_memory *memory) {
    dkc2_cpu cpu;

    flat->bytes[0x028000] = 0x22; /* JSL $12:3456 */
    flat->bytes[0x028001] = 0x56;
    flat->bytes[0x028002] = 0x34;
    flat->bytes[0x028003] = 0x12;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = true;
    cpu.p = DKC2_P_MEMORY_8 | DKC2_P_INDEX_8;
    cpu.pbr = UINT8_C(0x02);
    cpu.pc = UINT16_C(0x8000);
    cpu.s = UINT16_C(0x0100);
    step_ok(&cpu, memory);
    if (cpu.pbr != UINT8_C(0x12) || cpu.pc != UINT16_C(0x3456) ||
        cpu.s != UINT16_C(0x01FD) ||
        flat->bytes[0x000100] != UINT8_C(0x02) ||
        flat->bytes[0x0000FF] != UINT8_C(0x80) ||
        flat->bytes[0x0000FE] != UINT8_C(0x03)) {
        fail("JSL emulation stack boundary behavior is incorrect");
    }

    flat->bytes[0x038000] = 0x40; /* RTI */
    flat->bytes[0x0001FE] = 0;
    flat->bytes[0x0001FF] = 0x34;
    flat->bytes[0x000100] = 0x12;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = true;
    cpu.p = DKC2_P_MEMORY_8 | DKC2_P_INDEX_8;
    cpu.pbr = UINT8_C(0x03);
    cpu.pc = UINT16_C(0x8000);
    cpu.s = UINT16_C(0x01FD);
    step_ok(&cpu, memory);
    if (cpu.pc != UINT16_C(0x1234) || cpu.s != UINT16_C(0x0100)) {
        fail("RTI legacy stack-page wrapping is incorrect");
    }
}

static void test_address_boundaries(flat_memory *flat,
                                    const dkc2_memory *memory) {
    dkc2_cpu cpu;

    flat->bytes[0x058000] = 0xAD; /* LDA $FFFF */
    flat->bytes[0x058001] = 0xFF;
    flat->bytes[0x058002] = 0xFF;
    flat->bytes[0x33FFFF] = 0x52;
    flat->bytes[0x340000] = 0x3F;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = false;
    cpu.pbr = UINT8_C(0x05);
    cpu.pc = UINT16_C(0x8000);
    cpu.dbr = UINT8_C(0x33);
    step_ok(&cpu, memory);
    if (cpu.a != UINT16_C(0x3F52)) {
        fail("16-bit absolute read did not cross the bank boundary");
    }

    flat->bytes[0x068000] = 0x17; /* ORA [$FE],Y */
    flat->bytes[0x068001] = 0xFE;
    flat->bytes[0x00B7FE] = 0x00;
    flat->bytes[0x00B7FF] = 0x20;
    flat->bytes[0x00B800] = 0x7E;
    flat->bytes[0x7E2001] = 0x18;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = true;
    cpu.p = DKC2_P_MEMORY_8 | DKC2_P_INDEX_8;
    cpu.pbr = UINT8_C(0x06);
    cpu.pc = UINT16_C(0x8000);
    cpu.d = UINT16_C(0xB700);
    cpu.y = UINT16_C(1);
    cpu.a = UINT16_C(0x42A5);
    step_ok(&cpu, memory);
    if (cpu.a != UINT16_C(0x42BD)) {
        fail("24-bit direct pointer did not cross the direct-page boundary");
    }
}

static void test_block_move(flat_memory *flat,
                            const dkc2_memory *memory) {
    dkc2_cpu cpu;

    flat->bytes[0x049000] = 0x54; /* MVN $7E,$7F */
    flat->bytes[0x049001] = 0x7F;
    flat->bytes[0x049002] = 0x7E;
    flat->bytes[0x7E0000] = 0x11;
    flat->bytes[0x7E0001] = 0x22;
    flat->bytes[0x7E0002] = 0x33;
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = false;
    cpu.pbr = UINT8_C(0x04);
    cpu.pc = UINT16_C(0x9000);
    cpu.a = UINT16_C(2);
    cpu.y = UINT16_C(0x0010);
    step_ok(&cpu, memory);
    if (cpu.a != UINT16_C(0xFFFF) || cpu.x != UINT16_C(3) ||
        cpu.y != UINT16_C(0x0013) || cpu.dbr != UINT8_C(0x7F) ||
        cpu.pc != UINT16_C(0x9003) ||
        flat->bytes[0x7F0010] != UINT8_C(0x11) ||
        flat->bytes[0x7F0011] != UINT8_C(0x22) ||
        flat->bytes[0x7F0012] != UINT8_C(0x33)) {
        fail("MVN did not complete the requested logical block move");
    }
}

static void test_interrupt_round_trip(flat_memory *flat,
                                      const dkc2_memory *memory) {
    dkc2_cpu cpu;

    flat->bytes[0x00FFEA] = 0x78;
    flat->bytes[0x00FFEB] = 0x56;
    flat->bytes[0x005678] = 0x40; /* RTI */
    memset(&cpu, 0, sizeof(cpu));
    cpu.e = false;
    cpu.pbr = UINT8_C(0x80);
    cpu.pc = UINT16_C(0x1234);
    cpu.s = UINT16_C(0x01FF);

    if (!dkc2_cpu_nmi(&cpu, memory) || cpu.pbr != 0 ||
        cpu.pc != UINT16_C(0x5678) || cpu.s != UINT16_C(0x01FB)) {
        fail("native NMI entry is incorrect");
    }
    step_ok(&cpu, memory);
    if (cpu.pbr != UINT8_C(0x80) || cpu.pc != UINT16_C(0x1234) ||
        cpu.s != UINT16_C(0x01FF)) {
        fail("native RTI did not restore the interrupted state");
    }
}

static void test_every_opcode_is_executable(flat_memory *flat,
                                            const dkc2_memory *memory) {
    unsigned opcode;
    for (opcode = 0; opcode < 256; ++opcode) {
        dkc2_cpu cpu;
        dkc2_step_result result;
        uint32_t program = UINT32_C(0x018000);

        flat->bytes[program] = (uint8_t)opcode;
        flat->bytes[program + 1] = 0;
        flat->bytes[program + 2] = 0;
        flat->bytes[program + 3] = 0;
        memset(&cpu, 0, sizeof(cpu));
        cpu.e = false;
        cpu.pbr = UINT8_C(0x01);
        cpu.pc = UINT16_C(0x8000);
        cpu.s = UINT16_C(0x4000);
        result = dkc2_cpu_step(&cpu, memory);
        if (result == DKC2_STEP_INVALID_ARGUMENT) {
            (void)fprintf(stderr,
                          "opcode $%02X has no execution implementation\n",
                          opcode);
            exit(EXIT_FAILURE);
        }
    }
}

int main(void) {
    flat_memory flat;
    dkc2_memory memory;

    flat.bytes = (uint8_t *)calloc(FLAT_MEMORY_SIZE, 1);
    if (flat.bytes == NULL) {
        return EXIT_FAILURE;
    }
    memory.context = &flat;
    memory.read8 = flat_read;
    memory.write8 = flat_write;

    test_reset_and_program(&flat, &memory);
    test_calls(&flat, &memory);
    test_decimal(&flat, &memory);
    test_stack_boundaries(&flat, &memory);
    test_address_boundaries(&flat, &memory);
    test_block_move(&flat, &memory);
    test_interrupt_round_trip(&flat, &memory);
    test_every_opcode_is_executable(&flat, &memory);

    free(flat.bytes);
    (void)puts("W65C816 execution tests passed (256 opcodes reachable)");
    return EXIT_SUCCESS;
}
