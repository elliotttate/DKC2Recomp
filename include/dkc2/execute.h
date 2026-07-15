#ifndef DKC2_EXECUTE_H
#define DKC2_EXECUTE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    DKC2_P_CARRY = 0x01,
    DKC2_P_ZERO = 0x02,
    DKC2_P_IRQ_DISABLE = 0x04,
    DKC2_P_DECIMAL = 0x08,
    DKC2_P_INDEX_8 = 0x10,
    DKC2_P_MEMORY_8 = 0x20,
    DKC2_P_OVERFLOW = 0x40,
    DKC2_P_NEGATIVE = 0x80
};

typedef uint8_t (*dkc2_memory_read8_fn)(void *context, uint32_t address);
typedef void (*dkc2_memory_write8_fn)(void *context,
                                      uint32_t address,
                                      uint8_t value);

typedef struct dkc2_memory {
    void *context;
    dkc2_memory_read8_fn read8;
    dkc2_memory_write8_fn write8;
} dkc2_memory;

typedef struct dkc2_cpu {
    uint16_t a;
    uint16_t x;
    uint16_t y;
    uint16_t s;
    uint16_t d;
    uint16_t pc;
    uint8_t dbr;
    uint8_t pbr;
    uint8_t p;
    bool e;
    bool waiting;
    bool stopped;
    uint64_t instructions;
} dkc2_cpu;

typedef enum dkc2_step_result {
    DKC2_STEP_OK,
    DKC2_STEP_WAITING,
    DKC2_STEP_STOPPED,
    DKC2_STEP_INVALID_ARGUMENT
} dkc2_step_result;

/* Deterministic host initialization followed by the hardware reset vector. */
bool dkc2_cpu_reset(dkc2_cpu *cpu, const dkc2_memory *memory);

/* External interrupt entry. IRQ returns false when the I flag masks it. */
bool dkc2_cpu_nmi(dkc2_cpu *cpu, const dkc2_memory *memory);
bool dkc2_cpu_irq(dkc2_cpu *cpu, const dkc2_memory *memory);

dkc2_step_result dkc2_cpu_step(dkc2_cpu *cpu,
                               const dkc2_memory *memory);

bool dkc2_cpu_accumulator_is_8_bit(const dkc2_cpu *cpu);
bool dkc2_cpu_index_is_8_bit(const dkc2_cpu *cpu);
uint32_t dkc2_cpu_program_address(const dkc2_cpu *cpu);
const char *dkc2_step_result_name(dkc2_step_result result);

#ifdef __cplusplus
}
#endif

#endif
