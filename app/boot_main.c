#include "dkc2/bus.h"
#include "dkc2/execute.h"
#include "dkc2/hash.h"
#include "dkc2/rom.h"
#include "dkc2/snes_io.h"

#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool parse_limit(const char *text, uint64_t *value) {
    char *end;
    unsigned long long parsed;

    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed == 0) {
        return false;
    }
    *value = (uint64_t)parsed;
    return true;
}

static void print_cpu(const dkc2_cpu *cpu) {
    (void)printf("CPU:           %02" PRIX8 ":%04" PRIX16
                 " A=%04" PRIX16 " X=%04" PRIX16
                 " Y=%04" PRIX16 " S=%04" PRIX16
                 " D=%04" PRIX16 " DB=%02" PRIX8
                 " P=%02" PRIX8 " E=%u\n",
                 cpu->pbr,
                 cpu->pc,
                 cpu->a,
                 cpu->x,
                 cpu->y,
                 cpu->s,
                 cpu->d,
                 cpu->dbr,
                 cpu->p,
                 cpu->e ? 1U : 0U);
}

int main(int argc, char **argv) {
    uint64_t instruction_limit = UINT64_C(1000000);
    bool instruction_limit_set = false;
    bool with_apu = false;
    dkc2_rom_image rom;
    dkc2_bus bus;
    dkc2_snes_io io;
    dkc2_memory memory;
    dkc2_cpu cpu;
    dkc2_step_result step_result = DKC2_STEP_OK;
    char error[256];
    int result;

    int argument;

    if (argc < 2 || argc > 4) {
        (void)fprintf(stderr,
                      "Usage: %s <path-to-dkc2-rom> [instruction-limit] "
                      "[--with-apu]\n",
                      argv[0]);
        return 64;
    }
    for (argument = 2; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--with-apu") == 0) {
            with_apu = true;
        } else if (!instruction_limit_set &&
                   parse_limit(argv[argument], &instruction_limit)) {
            instruction_limit_set = true;
        } else {
            (void)fprintf(stderr,
                          "expected a positive instruction limit or "
                          "--with-apu\n");
            return 64;
        }
    }

    if (!dkc2_rom_image_load(argv[1], &rom, error, sizeof(error))) {
        (void)fprintf(stderr, "ROM load failed: %s\n", error);
        return 1;
    }
    if (!dkc2_bus_init(&bus, &rom)) {
        dkc2_rom_image_free(&rom);
        (void)fprintf(stderr, "cannot allocate runtime bus memory\n");
        return 1;
    }
    if (!dkc2_snes_io_init(&io, &bus)) {
        dkc2_bus_free(&bus);
        dkc2_rom_image_free(&rom);
        return 1;
    }
    dkc2_snes_io_stop_on_apu_after_dma(&io, !with_apu);
    dkc2_bus_set_io(&bus, dkc2_snes_io_read, dkc2_snes_io_write, &io);

    memory.context = &bus;
    memory.read8 = dkc2_bus_memory_read8;
    memory.write8 = dkc2_bus_memory_write8;
    if (!dkc2_cpu_reset(&cpu, &memory)) {
        dkc2_snes_io_free(&io);
        dkc2_bus_free(&bus);
        dkc2_rom_image_free(&rom);
        (void)fprintf(stderr, "CPU reset failed\n");
        return 1;
    }

    while (cpu.instructions < instruction_limit &&
           io.barrier == DKC2_SNES_BARRIER_NONE) {
        dkc2_snes_io_set_current_instruction(
            &io,
            dkc2_cpu_program_address(&cpu));
        step_result = dkc2_cpu_step(&cpu, &memory);
        if (step_result != DKC2_STEP_OK) {
            break;
        }
    }

    (void)printf("ROM:           exact DKC2 USA v1.0 baseline\n");
    (void)printf("Instructions:  %" PRIu64 "\n", cpu.instructions);
    (void)printf("I/O accesses:  %" PRIu64 " reads, %" PRIu64 " writes\n",
                 io.io_reads,
                 io.io_writes);
    (void)printf("DMA:           %" PRIu64 " transfer(s), %" PRIu64
                 " bytes\n",
                 io.dma_transfers,
                 io.dma_bytes);
    (void)printf("VRAM clear:    %s\n",
                 io.vram_clear_confirmed ? "confirmed" : "not reached");
    if (with_apu) {
        uint8_t *aram = (uint8_t *)malloc(DKC2_ARAM_SIZE);
        char aram_hash[DKC2_SHA256_HEX_SIZE];
        (void)printf("APU cycles:    %" PRIu32 " (port-access scheduler)\n",
                     dkc2_apu_cycle_count(io.apu));
        if (aram != NULL &&
            dkc2_apu_copy_aram(io.apu, aram, DKC2_ARAM_SIZE)) {
            dkc2_sha256_hex(aram, DKC2_ARAM_SIZE, aram_hash);
            (void)printf("ARAM SHA-256:  %s\n", aram_hash);
        } else {
            (void)printf("ARAM SHA-256:  unavailable\n");
        }
        free(aram);
    }

    if (io.barrier != DKC2_SNES_BARRIER_NONE) {
        bool expected_apu_boundary =
            with_apu &&
            io.barrier == DKC2_SNES_BARRIER_UNSUPPORTED_READ &&
            (uint16_t)io.barrier_address == UINT16_C(0x4211);
        (void)printf("Outcome:       %s\n",
                     dkc2_snes_barrier_name(io.barrier));
        (void)printf("Trigger:       $%06" PRIX32 " (value $%02" PRIX8
                     ") from $%06" PRIX32 "\n",
                     io.barrier_address,
                     io.barrier_value,
                     io.barrier_instruction);
        if (expected_apu_boundary) {
            (void)printf("Checkpoint:    APU upload path complete; "
                         "IRQ/timing model required\n");
        }
        result = io.barrier == DKC2_SNES_BARRIER_APU ||
                         expected_apu_boundary
                     ? 0
                     : 2;
    } else if (step_result != DKC2_STEP_OK) {
        (void)printf("Outcome:       CPU %s\n",
                     dkc2_step_result_name(step_result));
        result = 3;
    } else {
        (void)printf("Outcome:       instruction limit reached\n");
        result = 4;
    }
    print_cpu(&cpu);

    dkc2_snes_io_free(&io);
    dkc2_bus_free(&bus);
    dkc2_rom_image_free(&rom);
    return result;
}
