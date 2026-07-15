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

static bool parse_controller(const char *text, uint16_t *value) {
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > UINT16_MAX) {
        return false;
    }
    *value = (uint16_t)parsed;
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

static void print_state_hash(const char *label,
                             const uint8_t *data,
                             size_t size) {
    char hash[DKC2_SHA256_HEX_SIZE];
    dkc2_sha256_hex(data, size, hash);
    (void)printf("%-14s %s\n", label, hash);
}

static bool service_timing_interrupt(dkc2_snes_io *io,
                                     dkc2_bus *bus,
                                     dkc2_cpu *cpu,
                                     const dkc2_memory *memory) {
    uint64_t accesses_before;
    bool entered = false;

    accesses_before = bus->accesses;
    if (dkc2_snes_io_take_nmi(io)) {
        entered = dkc2_cpu_nmi(cpu, memory);
    } else if (dkc2_snes_io_irq_pending(io)) {
        entered = dkc2_cpu_irq(cpu, memory);
    }
    if (entered) {
        dkc2_snes_io_advance_cpu_accesses(
            io,
            bus->accesses - accesses_before);
    }
    return entered;
}

int main(int argc, char **argv) {
    uint64_t instruction_limit = UINT64_C(1000000);
    bool instruction_limit_set = false;
    bool with_apu = false;
    bool with_timing = false;
    uint16_t controller_buttons[2] = {0, 0};
    dkc2_rom_image rom;
    dkc2_bus bus;
    dkc2_snes_io io;
    dkc2_memory memory;
    dkc2_cpu cpu;
    dkc2_step_result step_result = DKC2_STEP_OK;
    char error[256];
    int result;

    int argument;

    if (argc < 2 || argc > 6) {
        (void)fprintf(stderr,
                      "Usage: %s <path-to-dkc2-rom> [instruction-limit] "
                      "[--with-apu|--with-timing] "
                      "[--controller1=<mask>] [--controller2=<mask>]\n",
                      argv[0]);
        return 64;
    }
    for (argument = 2; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--with-apu") == 0) {
            with_apu = true;
        } else if (strcmp(argv[argument], "--with-timing") == 0) {
            with_apu = true;
            with_timing = true;
        } else if (strncmp(argv[argument], "--controller1=", 14) == 0 &&
                   parse_controller(argv[argument] + 14,
                                    &controller_buttons[0])) {
            continue;
        } else if (strncmp(argv[argument], "--controller2=", 14) == 0 &&
                   parse_controller(argv[argument] + 14,
                                    &controller_buttons[1])) {
            continue;
        } else if (!instruction_limit_set &&
                   parse_limit(argv[argument], &instruction_limit)) {
            instruction_limit_set = true;
        } else {
            (void)fprintf(stderr,
                          "expected a positive instruction limit or "
                          "--with-apu/--with-timing/controller mask\n");
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
    dkc2_snes_io_enable_master_scheduler(&io, with_timing);
    (void)dkc2_snes_io_set_controller(&io, 0, controller_buttons[0]);
    (void)dkc2_snes_io_set_controller(&io, 1, controller_buttons[1]);
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
        uint64_t accesses_before;

        if (with_timing &&
            service_timing_interrupt(&io, &bus, &cpu, &memory)) {
            step_result = DKC2_STEP_OK;
            continue;
        }
        if (with_timing && cpu.waiting) {
            if (!dkc2_snes_io_interrupt_source_enabled(&io)) {
                step_result = DKC2_STEP_WAITING;
                break;
            }
            dkc2_snes_io_advance_master_cycles(
                &io,
                DKC2_NTSC_MASTER_CYCLES_PER_SCANLINE);
            continue;
        }
        dkc2_snes_io_set_current_instruction(
            &io,
            dkc2_cpu_program_address(&cpu));
        accesses_before = bus.accesses;
        step_result = dkc2_cpu_step(&cpu, &memory);
        if (with_timing) {
            dkc2_snes_io_advance_cpu_accesses(
                &io,
                bus.accesses - accesses_before);
        }
        if (with_timing && step_result == DKC2_STEP_WAITING) {
            continue;
        }
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
    if (with_timing) {
        (void)printf("HDMA:          %" PRIu64 " line transfer(s), %"
                     PRIu64 " bytes\n",
                     io.hdma_transfers,
                     io.hdma_bytes);
        (void)printf("Timing:        %" PRIu64 " provisional master cycles, "
                     "frame %" PRIu64 " beam %u:%u\n",
                     io.master_cycles,
                     io.frames,
                     (unsigned)io.v_counter,
                     (unsigned)io.h_counter);
        (void)printf("Interrupts:    NMITIMEN=$%02" PRIX8
                     " HTIME=%u VTIME=%u NMI=%u TIMEUP=%u\n",
                     io.registers[UINT16_C(0x2200)],
                     (unsigned)(io.registers[UINT16_C(0x2207)] |
                                ((uint16_t)(io.registers[
                                     UINT16_C(0x2208)] & UINT8_C(1))
                                 << 8)),
                     (unsigned)(io.registers[UINT16_C(0x2209)] |
                                ((uint16_t)(io.registers[
                                     UINT16_C(0x220A)] & UINT8_C(1))
                                 << 8)),
                     io.nmi_pending ? 1U : 0U,
                     io.timeup_flag ? 1U : 0U);
        (void)printf("Controllers:   JOY1=$%04" PRIX16
                     " JOY2=$%04" PRIX16 "\n",
                     io.controllers[0],
                     io.controllers[1]);
    }
    (void)printf("VRAM clear:    %s\n",
                 io.vram_clear_confirmed ? "confirmed" : "not reached");
    if (with_timing) {
        print_state_hash("WRAM SHA-256:", bus.wram, DKC2_WRAM_SIZE);
        print_state_hash("SRAM SHA-256:", bus.sram, DKC2_SRAM_SIZE);
        print_state_hash("VRAM SHA-256:", io.vram, DKC2_VRAM_SIZE);
        print_state_hash("CGRAM SHA-256:", io.cgram, DKC2_CGRAM_SIZE);
        print_state_hash("OAM SHA-256:", io.oam, DKC2_OAM_SIZE);
    }
    if (with_apu) {
        uint8_t *aram = (uint8_t *)malloc(DKC2_ARAM_SIZE);
        char aram_hash[DKC2_SHA256_HEX_SIZE];
        (void)printf("APU cycles:    %" PRIu32 " (%s)\n",
                     dkc2_apu_cycle_count(io.apu),
                     with_timing
                         ? "provisional master scheduler"
                         : "port-access scheduler");
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
            with_apu && !with_timing &&
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
        if (with_timing) {
            (void)printf("Checkpoint:    timed hardware path remained "
                         "barrier-free to requested limit\n");
            result = 0;
        } else {
            result = 4;
        }
    }
    print_cpu(&cpu);

    dkc2_snes_io_free(&io);
    dkc2_bus_free(&bus);
    dkc2_rom_image_free(&rom);
    return result;
}
