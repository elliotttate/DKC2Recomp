# Implementation journal

This is the separate plain-language record of what was done, why it was done,
how it was checked, and what comes next. It complements the technical API and
architecture documentation.

## Starting checkpoint: version 0.2.0

The project could already identify the exact North American revision 0 ROM,
decode all 256 CPU opcodes, walk likely control flow from reset/NMI/IRQ, load a
private symbol map, export graphs, and route ROM, WRAM, SRAM, I/O, and open-bus
accesses.

That layer could answer “where can the program go?” but could not answer “what
does it actually do?” The next required component was a CPU that maintains real
register values and executes one instruction at a time.

## Step 1: executable W65C816 CPU

Added `include/dkc2/execute.h` and `src/execute.c`. The CPU state now includes:

- accumulator A and index registers X/Y;
- stack pointer S and direct-page register D;
- program counter, program bank, and data bank;
- all processor status bits and the emulation-mode bit;
- wait/stop state; and
- a logical instruction counter.

All 256 opcodes and addressing modes execute against generic 24-bit memory
callbacks. This includes decimal ADC/SBC, 8/16-bit width changes, stack and
interrupt frames, long pointers, branches/calls/returns, and complete MVP/MVN
block moves.

### What the deep CPU tests found

The first synthetic tests proved every opcode had an implementation, but rare
hardware boundaries needed much stronger coverage. The external
SingleStepTests corpus exposed several details:

1. In emulation mode, M and X must be set, X/Y must be shortened, and the
   visible stack pointer must be on page `$01` at instruction boundaries.
2. Ordinary 6502-style stack operations wrap inside page `$01`, while a small
   group of multi-byte 65816 operations can temporarily cross it.
3. `PLB`, `PLD`, `JSL`, `RTL`, `RTI`, and interrupt frames do not all use the
   same boundary sequence.
4. Decimal ADC's overflow flag is based on a partially decimal-adjusted
   intermediate result, which matters when a nibble is not valid BCD.
5. A 24-bit direct pointer at the end of a direct page continues into the next
   page.
6. The high byte of a 16-bit A-bus operand can continue into the next bank.

Each issue was corrected and then retained as a small local regression test so
the project does not depend on the external corpus to remember it.

### CPU verification result

The final run checked every one of the 10,000 vectors for 254 opcodes in both
native and emulation mode:

```text
excluded cycle-capped block-move vectors: 44, 54
passed=5080000 failed=0
```

MVP and MVN are tested locally as complete logical instructions. Their external
files stop after 100 hardware cycles, often in the middle of the move, so those
partial states are not comparable to this API's one-complete-instruction step.

This proves register/memory state at instruction boundaries. It does not yet
prove exact bus cycles or interrupt sampling timing.

## Step 2: execute the actual DKC2 reset path

Added `dkc2_boot`, which privately loads only the exact verified ROM, attaches
the CPU to the real SNES bus map, and stops whenever progress would require an
unimplemented hardware behavior.

The first run executed 74,220 instructions and reached:

```text
STA $420B at $00:85AF
```

That is DKC2's `clear_vram` DMA trigger in the independently rebuilt reference
map. Reaching the same routine from the reset vector is a useful end-to-end
check of CPU widths, branches, stack returns, WRAM scanning, SRAM mapping, and
I/O register setup.

## Step 3: implement the first hardware barrier

Added `include/dkc2/snes_io.h` and `src/snes_io.c` with:

- PPU/CPU/DMA register storage;
- VRAM, CGRAM, and OAM host arrays and their basic write ports;
- all eight A-to-B general-DMA register patterns;
- fixed, incrementing, and decrementing DMA sources; and
- an explicit unsupported-hardware barrier instead of guessed values.

DKC2 requests a fixed-source, mode-1 transfer with a size register of zero.
On the SNES that means 65,536 bytes. The new runtime performs the transfer and
verifies every byte of VRAM is zero.

The next run reached:

```text
ROM:           exact DKC2 USA v1.0 baseline
Instructions:  74262
I/O accesses:  4 reads, 242 writes
DMA:           1 transfer(s), 65536 bytes
VRAM clear:    confirmed
Outcome:       APU/SPC700 communication required
Trigger:       $002140 (value $00) from $B5821A
```

The extra 42 logical instructions include returning from the DMA routine,
clearing both 64 KiB WRAM banks with two complete MVN instructions, copying the
startup marker, and entering the audio upload routine. `$B5:821A` compares the
APU ports with the SPC700 IPL ready word `$BBAA`.

The runtime stops there deliberately. Pretending that an SPC700 acknowledged
the upload would make later progress look better while making it less
trustworthy.

## Files added or materially changed in 0.3.0

| Area | Main files |
| --- | --- |
| CPU execution | `include/dkc2/execute.h`, `src/execute.c` |
| Bus adapters | `include/dkc2/bus.h`, `src/bus.c` |
| SNES I/O and DMA | `include/dkc2/snes_io.h`, `src/snes_io.c` |
| Real-ROM probe | `app/boot_main.c` |
| CPU corpus runner | `scripts/run_65816_tests.py` |
| Local regressions | `tests/test_execute.c`, `tests/test_snes_io.c` |
| Documentation | this journal, CPU conformance, architecture, hardware notes, roadmap |

No ROM bytes, extracted assets, external test vectors, or reference
disassembly source are stored in the project.

## Limitations at 0.3.0

- CPU execution is state-accurate at instruction boundaries, not cycle-accurate.
- General DMA is synchronous and A-to-B only; HDMA is not implemented.
- VRAM/CGRAM/OAM are storage models; there is no PPU renderer.
- There is no SPC700 or DSP execution, so there is no audio.
- NMI/IRQ scheduling, scanline timing, controllers, and a PC window are absent.
- Native-C emission has not started; the interpreter remains the execution path.

## Planned next task from 0.3.0

Implement or integrate a compatibly licensed SPC700 CPU and S-DSP strategy,
model the four CPU/APU communication ports, and reproduce the IPL `$BBAA`
handshake. The first validation target is not audible music yet: it is an ARAM
hash proving that DKC2's base engine upload completed with the right bytes.

After that, the next likely boundary is PPU/NMI timing for the Rareware logo.

## Checkpoint: version 0.4.0

### Step 4: select a reusable APU core

The source review found two different categories of project:

- `snesrecomp` validates the static-recompiler-plus-hardware-runtime design,
  but its README says that no overall license is declared. It remains a
  reference only.
- LakeSnes is an archived C emulator with an explicit MIT license. Only its
  SPC700, S-DSP, S-SMP timer/port, and state support were imported, from commit
  `9db90b86e46a377609305e298dd92d71cd1d4c8a`.

The imported files, license, exact revision, and adaptations are isolated in
`third_party/lakesnes_apu`. The project-owned API in `include/dkc2/apu.h` keeps
the rest of the port independent from LakeSnes type names.

### Step 5: execute the real SPC700 IPL

The new synthetic test resets the actual SPC700 core and executes the 64-byte
IPL ROM. It checks:

1. the IPL writes `$AA` and `$BB` to the APU-to-CPU ports;
2. the CPU sends destination `$0200`, transfer flag `$01`, and token `$CC`;
3. the IPL echoes `$CC`;
4. two synthetic bytes are acknowledged and stored at ARAM `$0200/$0201`.

The test also captured an easy-to-miss ordering fact: the IPL echoes a byte
index immediately before executing the instruction that writes that byte to
ARAM. The test therefore advances through the store before checking memory.

### Step 6: connect DKC2 to the APU

`$2140-$2143` now map to four independent CPU-to-APU and APU-to-CPU values.
The SPC core advances on port accesses. Because the 65816 interpreter does not
yet report hardware cycles, the scheduler runs the smallest unit exposed by
the imported core: one complete SPC opcode per port access.

An initial 64-cycle slice was wrong. DKC2 writes `$01CC` as one 16-bit store:
`$CC` reaches port 0 first and `$01` reaches port 1 on the next bus access. A
64-cycle slice let the IPL observe `$CC`, read the old zero from port 1, and
take the execute path instead of the upload path. Reducing the slice to one
SPC opcode preserves the intended ordering and allowed the transfer to finish.

The old default boot probe still stops at `$B5:821A`, so the 0.3.0 checkpoint
remains reproducible. The explicit continuation is:

```text
dkc2_boot <private-rom> 5000000 --with-apu
```

The new deterministic private checkpoint is:

```text
Instructions:  1359156
I/O accesses:  216520 reads, 98121 writes
DMA:           13 transfer(s), 157448 bytes
VRAM clear:    confirmed
APU cycles:    960481 (port-access scheduler)
ARAM SHA-256:  49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f
Outcome:       unsupported I/O read
Trigger:       $804211 (value $00) from $809360
Checkpoint:    APU upload path complete; IRQ/timing model required
```

This proves that the real main CPU and real SPC700 execute their upload
protocol far enough to leave the audio loader and continue through later DMA
and decompression work. The ARAM hash is now a regression value; it still needs
comparison with an accurate reference-emulator dump before it can be called a
hardware-validated oracle.

The VRAM-clear result is latched when the first 65,536-byte clear completes.
Later valid DMAs populate VRAM, so testing whether VRAM is still all zero at
the end of a longer probe would incorrectly report that the initial clear had
not happened.

### Verification

- Visual Studio 2022/MSVC Release build: passed.
- Ten synthetic unit suites: passed.
- Original private reset/DMA checkpoint at 74,262 instructions: unchanged.
- Private APU continuation: reached `$4211` after 1,359,156 instructions.
- No ROM, ARAM dump, or extracted game content was written into the project.

## Current limitations after 0.4.0

- APU scheduling is access-driven and instruction-granular, not cycle-accurate.
- The S-DSP executes, but no host audio device consumes its samples yet.
- The private ARAM hash has not been compared to an accurate emulator dump.
- `$4211` IRQ status, NMI/IRQ scheduling, scanlines, and HDMA are not modeled.
- PPU memories exist, but there is no renderer or PC window.
- Native-C emission has not started; the 65816 interpreter is still the oracle.

## Exact next task after 0.4.0

Add a master-cycle scheduler and the CPU timing/register subset needed at the
new `$4211` boundary: TIMEUP clear-on-read behavior, `$4200` interrupt enables,
H/V counters, NMI/IRQ delivery, and HDMA initiation. In parallel, capture a
private reference-emulator ARAM dump at the post-upload checkpoint and compare
its SHA-256 with the value above.
