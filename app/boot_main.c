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

static bool parse_address(const char *text, uint32_t *value) {
    char *end;
    unsigned long parsed;

    errno = 0;
    parsed = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > UINT32_C(0xFFFFFF)) {
        return false;
    }
    *value = (uint32_t)parsed;
    return true;
}

static bool parse_word_dump(const char *text,
                            uint32_t *address,
                            unsigned *count) {
    char *end;
    unsigned long parsed_address;
    unsigned long parsed_count;

    errno = 0;
    parsed_address = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != ':' ||
        parsed_address > UINT32_C(0xFFFFFF)) {
        return false;
    }
    text = end + 1;
    errno = 0;
    parsed_count = strtoul(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed_count == 0 || parsed_count > 64) {
        return false;
    }
    *address = (uint32_t)parsed_address;
    *count = (unsigned)parsed_count;
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

static void print_mode7_state(const dkc2_snes_io *io) {
    (void)printf("Mode 7 state:  M7SEL=$%02" PRIX8
                 " H=%04" PRIX16 " V=%04" PRIX16
                 " A=%04" PRIX16 " B=%04" PRIX16
                 " C=%04" PRIX16 " D=%04" PRIX16
                 " X=%04" PRIX16 " Y=%04" PRIX16 "\n",
                 io->registers[UINT16_C(0x011A)],
                 io->mode7_hofs,
                 io->mode7_vofs,
                 (uint16_t)io->mode7_a,
                 (uint16_t)io->mode7_b,
                 (uint16_t)io->mode7_c,
                 (uint16_t)io->mode7_d,
                 (uint16_t)io->mode7_x,
                 (uint16_t)io->mode7_y);
}

static bool write_ppm(const char *path, const uint8_t *rgb) {
    FILE *file;
    bool ok;

    if (path == NULL || rgb == NULL) {
        return false;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return false;
    }
    ok = fprintf(file,
                 "P6\n%zu %zu\n255\n",
                 DKC2_PPU_FRAME_WIDTH,
                 DKC2_PPU_FRAME_HEIGHT) > 0;
    if (ok) {
        ok = fwrite(rgb, 1, DKC2_PPU_RGB_SIZE, file) ==
             DKC2_PPU_RGB_SIZE;
    }
    if (fclose(file) != 0) {
        ok = false;
    }
    return ok;
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
    bool with_render = false;
    bool frame_output_failed = false;
    bool breakpoint_reached = false;
    bool break_pc_set = false;
    bool dump_words_set = false;
    const char *frame_output = NULL;
    uint32_t break_pc = 0;
    unsigned long break_hit_target = 1;
    unsigned long break_hit_count = 0;
    uint32_t dump_words_address = 0;
    unsigned dump_word_count = 0;
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

    if (argc < 2 || argc > 11) {
        (void)fprintf(stderr,
                      "Usage: %s <path-to-dkc2-rom> [instruction-limit] "
                      "[--with-apu|--with-timing|--with-render] "
                      "[--controller1=<mask>] [--controller2=<mask>] "
                      "[--frame-output=<private.ppm>] "
                      "[--break-pc=<24-bit-address>] "
                      "[--break-hit=<positive-count>] "
                      "[--dump-words=<24-bit-address>:<count>]\n",
                      argv[0]);
        return 64;
    }
    for (argument = 2; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--with-apu") == 0) {
            with_apu = true;
        } else if (strcmp(argv[argument], "--with-timing") == 0) {
            with_apu = true;
            with_timing = true;
        } else if (strcmp(argv[argument], "--with-render") == 0) {
            with_apu = true;
            with_timing = true;
            with_render = true;
        } else if (strncmp(argv[argument], "--controller1=", 14) == 0 &&
                   parse_controller(argv[argument] + 14,
                                    &controller_buttons[0])) {
            continue;
        } else if (strncmp(argv[argument], "--controller2=", 14) == 0 &&
                   parse_controller(argv[argument] + 14,
                                    &controller_buttons[1])) {
            continue;
        } else if (strncmp(argv[argument], "--frame-output=", 15) == 0 &&
                   argv[argument][15] != '\0') {
            frame_output = argv[argument] + 15;
            with_apu = true;
            with_timing = true;
            with_render = true;
        } else if (strncmp(argv[argument], "--break-pc=", 11) == 0 &&
                   parse_address(argv[argument] + 11, &break_pc)) {
            break_pc_set = true;
        } else if (strncmp(argv[argument], "--break-hit=", 12) == 0) {
            char *end = NULL;
            errno = 0;
            break_hit_target = strtoul(argv[argument] + 12, &end, 0);
            if (errno != 0 || end == argv[argument] + 12 || *end != '\0' ||
                break_hit_target == 0) {
                (void)fprintf(stderr, "break hit must be a positive count\n");
                return 64;
            }
        } else if (strncmp(argv[argument], "--dump-words=", 13) == 0 &&
                   parse_word_dump(argv[argument] + 13,
                                   &dump_words_address,
                                   &dump_word_count)) {
            dump_words_set = true;
        } else if (!instruction_limit_set &&
                   parse_limit(argv[argument], &instruction_limit)) {
            instruction_limit_set = true;
        } else {
            (void)fprintf(stderr,
                          "expected a positive instruction limit or "
                          "--with-apu/--with-timing/--with-render/"
                          "controller mask/frame output/break PC/word dump\n");
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
    if (!dkc2_snes_io_enable_renderer(&io, with_render)) {
        dkc2_snes_io_free(&io);
        dkc2_bus_free(&bus);
        dkc2_rom_image_free(&rom);
        (void)fprintf(stderr, "cannot allocate headless framebuffer\n");
        return 1;
    }
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

        if (break_pc_set && dkc2_cpu_program_address(&cpu) == break_pc) {
            break_hit_count++;
            if (break_hit_count == break_hit_target) {
                breakpoint_reached = true;
                break;
            }
        }

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
        (void)printf("PPU usage:     modes=$%02" PRIX8
                     " main=$%02" PRIX8 " sub=$%02" PRIX8
                     " math=$%02" PRIX8 " features=$%02" PRIX8 "\n",
                     io.ppu_mode_mask,
                     io.ppu_main_screen_mask,
                     io.ppu_sub_screen_mask,
                     io.ppu_color_math_mask,
                     io.ppu_feature_mask);
        (void)printf("PPU lines:     0=%" PRIu64 " 1=%" PRIu64
                     " 2=%" PRIu64 " 3=%" PRIu64
                     " 4=%" PRIu64 " 5=%" PRIu64
                     " 6=%" PRIu64 " 7=%" PRIu64
                     " blank=%" PRIu64 "\n",
                     io.ppu_mode_scanlines[0],
                     io.ppu_mode_scanlines[1],
                     io.ppu_mode_scanlines[2],
                     io.ppu_mode_scanlines[3],
                     io.ppu_mode_scanlines[4],
                     io.ppu_mode_scanlines[5],
                     io.ppu_mode_scanlines[6],
                     io.ppu_mode_scanlines[7],
                     io.ppu_forced_blank_scanlines);
    }
    (void)printf("VRAM clear:    %s\n",
                 io.vram_clear_confirmed ? "confirmed" : "not reached");
    if (with_timing) {
        print_state_hash("WRAM SHA-256:", bus.wram, DKC2_WRAM_SIZE);
        print_state_hash("SRAM SHA-256:", bus.sram, DKC2_SRAM_SIZE);
        print_state_hash("VRAM SHA-256:", io.vram, DKC2_VRAM_SIZE);
        print_state_hash("CGRAM SHA-256:", io.cgram, DKC2_CGRAM_SIZE);
        print_state_hash("OAM SHA-256:", io.oam, DKC2_OAM_SIZE);
        print_state_hash("PPU writes SHA-256:",
                         io.registers + UINT16_C(0x0100),
                         UINT16_C(0x0034));
        print_mode7_state(&io);
        if (with_render) {
            const uint8_t *frame = dkc2_ppu_frame_rgb(&io.renderer);
            (void)printf("Render:        %" PRIu64 " frame(s), %" PRIu64
                         " scanline(s), %" PRIu64
                         " limited, features=$%02" PRIX32 "\n",
                         io.renderer.completed_frames,
                         io.renderer.scanlines,
                         io.renderer.unsupported_scanlines,
                         io.renderer.limitations_seen);
            (void)printf("Published:     modes=$%02" PRIX8
                         " limited=%u features=$%02" PRIX32 "\n",
                         io.renderer.frame_mode_mask,
                         (unsigned)io.renderer.frame_limited_scanlines,
                         io.renderer.frame_limitations);
            (void)printf("OBJ limits:    range=%" PRIu64
                         " time=%" PRIu64 " scanline(s)\n",
                         io.renderer.object_range_over_scanlines,
                         io.renderer.object_time_over_scanlines);
            if (frame != NULL && io.renderer.completed_frames != 0) {
                print_state_hash("Frame SHA-256:",
                                 frame,
                                 DKC2_PPU_RGB_SIZE);
                if (frame_output != NULL) {
                    if (write_ppm(frame_output, frame)) {
                        (void)printf("Frame output:  %s\n", frame_output);
                    } else {
                        (void)fprintf(stderr,
                                      "cannot write private frame output: %s\n",
                                      frame_output);
                        frame_output_failed = true;
                    }
                }
            } else {
                (void)printf("Frame SHA-256: unavailable\n");
                frame_output_failed = frame_output != NULL;
            }
        }
    }
    if (with_apu) {
        uint8_t *aram = (uint8_t *)malloc(DKC2_ARAM_SIZE);
        char aram_hash[DKC2_SHA256_HEX_SIZE];
        (void)printf("APU cycles:    %" PRIu32 " (%s)\n",
                     dkc2_apu_cycle_count(io.apu),
                     with_timing
                         ? "provisional master scheduler"
                         : "port-access scheduler");
        (void)printf("APU IPL ROM:   %s\n",
                     dkc2_apu_ipl_rom_enabled(io.apu)
                         ? "enabled"
                         : "disabled");
        if (aram != NULL &&
            dkc2_apu_copy_aram(io.apu, aram, DKC2_ARAM_SIZE)) {
            dkc2_sha256_hex(aram, DKC2_ARAM_SIZE, aram_hash);
            (void)printf("ARAM SHA-256:  %s\n", aram_hash);
        } else {
            (void)printf("ARAM SHA-256:  unavailable\n");
        }
        free(aram);
    }

    if (dump_words_set) {
        (void)printf("Memory words:  $%06" PRIX32 ":", dump_words_address);
        for (unsigned i = 0; i < dump_word_count; ++i) {
            uint32_t address = (dump_words_address + i * 2U) &
                               UINT32_C(0xFFFFFF);
            uint16_t value = memory.read8(memory.context, address);
            value |= (uint16_t)memory.read8(
                         memory.context,
                         (address + 1U) & UINT32_C(0xFFFFFF)) << 8;
            (void)printf(" %04" PRIX16, value);
        }
        (void)printf("\n");
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
    } else if (breakpoint_reached) {
        (void)printf("Outcome:       breakpoint reached at $%06" PRIX32
                     " (hit %lu)\n", break_pc, break_hit_count);
        result = 0;
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

    if (frame_output_failed && result == 0) {
        result = 5;
    }

    dkc2_snes_io_free(&io);
    dkc2_bus_free(&bus);
    dkc2_rom_image_free(&rom);
    return result;
}
