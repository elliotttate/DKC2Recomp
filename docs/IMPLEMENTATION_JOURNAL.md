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

## Checkpoint: version 0.5.0

### Step 7: introduce a master-cycle timeline

The CPU conformance work proves state at complete instruction boundaries, but
the CPU still does not expose exact cycles. The new scheduler therefore has a
deliberately split contract:

1. hardware subsystems consume explicit SNES master cycles; and
2. the boot probe provisionally estimates eight master cycles for every
   host-visible A-bus byte access.

The second rule is measurable and reproducible, but not cycle accuracy. It
does not include internal or dummy CPU cycles and does not yet distinguish
slow and fast bus regions. The old `--with-apu` behavior remains unchanged;
the new work is selected explicitly with `--with-timing`.

The timed path advances the SPC700/S-DSP from the same timeline at a nominal
21 master cycles per APU cycle. Whole-SPC-instruction overshoot is carried as
clock debt instead of being discarded on each CPU instruction.

### Step 8: add CPU timing registers and interrupt delivery

The NTSC model now advances through 1,364-master-cycle scanlines and 262-line
frames, with HBlank at cycle 1,096 and VBlank at line 225. It implements:

- `$4200` NMI, H/V IRQ, and autojoy enables;
- the 9-bit `$4207-$420A` H/V timer positions;
- `$4210` RDNMI and `$4211` TIMEUP clear-on-read latches;
- `$4212` HBlank, VBlank, and autojoy-busy status; and
- NMI/IRQ entry through the already-tested CPU interrupt functions.

The boot runner now advances time while the CPU is in `WAI`, so a VBlank edge
can resume real DKC2 code instead of being reported as a terminal CPU state.

### Step 9: run the intro HDMA and controller path

HDMA initializes at frame start and runs enabled channels at visible HBlank.
Direct and indirect tables, all transfer patterns, repeat/write-once line
counts, the 128-line encoding, and register write-back are present. DKC2's
intro specifically exercises one-byte direct write-once tables on channels
2-4.

The `$4016/$4017` serial ports and `$4218-$421F` autojoy results were added to
let the real NMI setup and handler run with neutral input. Autojoy takes 4,224
master cycles and exposes its busy state through `$4212`.

### New private-ROM checkpoint

The deterministic `--with-timing` continuation reaches:

```text
Instructions:  1619491
DMA:           133 transfer(s), 218888 bytes
HDMA:          1071 line transfer(s), 1071 bytes
Timing:        83583440 provisional master cycles, frame 233 beam 232:248
Interrupts:    NMITIMEN=$B1 HTIME=0 VTIME=0 NMI=0 TIMEUP=1
VRAM clear:    confirmed
APU cycles:    3980167 (provisional master scheduler)
ARAM SHA-256:  908fdf532684a1205db34f5bb97ca48a1985851eafd7699fcf7447eed472298c
Outcome:       unsupported I/O read
Trigger:       $802135 (value $00) from $809667
Checkpoint:    NMI/HDMA path complete; Mode-7 multiplication required
```

The frame, beam, APU, and ARAM values are deterministic regression evidence
for this provisional scheduler only. They have not been compared with an
accurate emulator and must not be described as console-accurate timing.

The reference map identifies this read as `PPU.multiply_result_mid`: DKC2 is
using the Mode-7 multiplication result while constructing the Rareware-logo
palette. The runtime stops rather than returning a fabricated product.

### Verification

- Visual Studio 2022/MSVC Release build: passed with no project warnings.
- Eleven synthetic unit suites: passed.
- Original private reset/DMA and version-0.4 APU checkpoints: unchanged.
- The private timing continuation crosses `$4211`, resumes `WAI`, delivers
  NMI, performs 133 general-DMA and 1,071 HDMA transfers, and stops at `$2135`.
- No ROM, ARAM dump, extracted asset, or generated game content was written
  into the repository.

## Current limitations after 0.5.0

- CPU time is estimated from visible A-bus accesses; exact instruction and
  bus-region cycle timing is not implemented.
- General DMA is synchronous and HDMA has event ordering but no bus duration.
- Interlace, overscan, PAL, short scanlines, refresh stalls, and dot-level PPU
  timing are absent.
- `$2134-$2136` Mode-7 multiplication results are the next real-ROM boundary.
- There is still no PPU renderer, host audio device, desktop window, or
  native-C emission.
- The old and new ARAM hashes still need reference-emulator comparison at
  matching checkpoints.

## Exact next task after 0.5.0

Implement the PPU Mode-7 multiplication operand latches at `$211B/$211C` and
the signed 24-bit result reads at `$2134-$2136`, with synthetic signed-product
and write-latch tests. Then rerun `--with-timing` to identify the next honest
hardware boundary; do not begin rendering until the intro's required PPU read
semantics are mapped.

## Checkpoint: version 0.6.0

### Step 10: implement the Mode-7 arithmetic boundary

The PPU now has the shared write latch used by `$211B-$2120`. Each write forms
a signed 16-bit value from the previous latch byte and the new byte, then makes
the new byte the latch value for the next write. `$2134-$2136` expose the low,
middle, and high bytes of the signed 24-bit result of `M7A` multiplied by the
signed high byte of `M7B`.

Synthetic tests cover a positive multiplicand with a negative multiplier, the
`$8000` edge, all three result bytes, and latch sharing across the A/B and C/D
registers. The official Snes9x source was used as a behavior cross-check; no
source was copied.

### Step 11: follow each newly exposed hardware boundary

Removing `$2135` exposed three further concrete requirements during private
ROM execution:

1. At 10,088,119 instructions, DKC2 read `$4216`. The runtime now implements
   unsigned CPU multiplication and division, including 48/96-master-cycle
   delays, captured operands, and the hardware divide-by-zero result.
2. At 10,519,171 instructions, DKC2 wrote `$2181`. The `$2180-$2183` WRAM
   data/address ports now support 17-bit addressing, auto-increment, and wrap.
3. At 11,507,788 instructions, a 16-bit store crossed from `$2183` to unused
   `$2184`. The unused remainder of the B-bus register range now uses the
   existing open-bus/no-device behavior instead of raising a false barrier.

Each behavior has a focused synthetic regression. The implementation stops at
unknown hardware elsewhere; none of these boundaries was bypassed with a
hard-coded game-specific return value.

### Step 12: make long runs inspectable and reproducible

`dkc2_boot` now accepts deterministic `--controller1=<mask>` and
`--controller2=<mask>` held states. The standard SNES 16-bit autojoy layout is
used, so `$1000` represents Start. A separate Start-held exploratory run
reached 20,000,000 instructions on a different execution path without a
hardware barrier. It ended at `$80:B03E`, performed 63,642 DMA transfers, and
produced WRAM hash
`3a3c09970d354ca50a38f69d16ef148dd29ad0c5e030fc67197a6f72f821114f`,
confirming that input changes executed game state rather than only diagnostic
output.

Timed runs now print SHA-256 fingerprints for WRAM, SRAM, VRAM, CGRAM, OAM,
and ARAM. The private CTest regression was extended from 5,000,000 to
20,000,000 instructions and pins the resulting VRAM fingerprint. This makes a
silent state change fail even if the runner still reaches its instruction
limit.

The neutral-input checkpoint is:

```text
Instructions:  20000000
I/O accesses:  1223347 reads, 355309 writes
DMA:           5619 transfer(s), 3111598 bytes
HDMA:          4005 line transfer(s), 4005 bytes
Timing:        1588559648 provisional master cycles, frame 4445 beam 43:236
Interrupts:    NMITIMEN=$81 HTIME=0 VTIME=0 NMI=0 TIMEUP=1
Controllers:   JOY1=$0000 JOY2=$0000
VRAM clear:    confirmed
WRAM SHA-256:  e10559dffe4381d912c93d3c7548dd0056a90e490dd8b204020029ba7db4db2c
SRAM SHA-256:  e5a00aa9991ac8a5ee3109844d84a55583bd20572ad3ffcd42792f3c36b183ad
VRAM SHA-256:  fa7fa5b8d66b584757bbe01fa5e35906263791318c356e4080a91b2730274cdc
CGRAM SHA-256: 954dc4a87a134c68127a4cdcde9305adf94518d32ba5adcab9fe493df24ee17b
OAM SHA-256:   1f34bd49f1e33221c4d2fd58eb61e34c4d29fff40abce5bcda83cde50432a701
APU cycles:    75645698 (provisional master scheduler)
ARAM SHA-256:  c9e3dd1d8e7c5b0d5152457f87d543374f8a89d5b4a5c0fc8e06c5ceec4bbeda
Outcome:       instruction limit reached
Checkpoint:    timed hardware path remained barrier-free to requested limit
CPU:           BB:8AC2 A=0001 X=0002 Y=0DE2 S=01F5 D=0000 DB=80 P=05 E=0
```

An additional neutral-input exploration reached 50,000,000 instructions with
no hardware barrier. It is not part of the routine suite because the shorter
checkpoint already covers the new register paths at lower test cost.

### Build and verification hardening

The imported LakeSnes APU source is now its own CMake target. Project-owned
code builds with strict warnings, while imported conversion warnings are
isolated without suppressing compiler errors. The Make build now includes the
timing test. The PowerShell test runner explicitly checks every native command
exit code; this prevents stale binaries from appearing to pass if compilation
fails before CTest begins.

Verification for this checkpoint:

- fresh Visual Studio 2022/MSVC Release build: passed with no project warnings;
- all 18 configured synthetic and private integration tests: passed;
- the version-0.4 APU checkpoint remains exactly 1,359,156 instructions with
  ARAM SHA-256
  `49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f`;
- no ROM, memory dump, extracted asset, or generated game content was written
  into the repository.

## Current limitations after 0.6.0

- There is no framebuffer renderer, desktop window, or playable build.
- There is no host audio output even though the SPC700/S-DSP executes.
- CPU-to-master timing, DMA duration, and several PPU details remain
  provisional or incomplete.
- The long-run hashes have not yet been matched to an accurate reference
  emulator at an equivalent logical checkpoint.
- Native-C emission has not started; game code still runs in the interpreter.

## Exact next task after 0.6.0

Add a headless PPU rendering checkpoint for the Rareware logo/title path and
capture a reference-emulator snapshot at a matching logical event. Begin with
the display-control, background-mode, tilemap/tile-data, window, color-math,
and Mode-7 register state needed by that frame; produce a framebuffer hash
before adding a desktop window. This keeps visual progress testable and avoids
mistaking a window that displays incorrect pixels for a playable port.

## Checkpoint: version 0.7.0

### Step 13: measure the display path before implementing it

The timing runner now records PPU mode use, main/subscreen enables, color-math
state, and mode-specific visible scanline counts. A neutral 20,000,000-
instruction run observed background modes 0, 1, 3, 5, and 7. Mode 1 dominates
the measured path; modes 2, 4, and 6 were not observed. This evidence kept the
first renderer focused while still making unimplemented modes explicit.

The I/O model gained the shared background-offset latch for `$210D-$2114`,
retained the Mode-7 H/V values sharing those writes, reconstructed fixed-color
components written through `$2132`, and implemented OAM's word address,
paired low-table write latch, high-table mapping, and priority-rotation start.
Synthetic tests preserve each latch and address transition.

The official Snes9x PPU register and graphics-renderer sources were consulted
to cross-check externally observable latch, mode-field, tile-priority, and
object behavior. No code, comments, or data were copied; the project-owned
implementation is independently expressed and covered by synthetic tests.

### Step 14: add the headless scanline renderer

The new `dkc2_ppu_renderer` allocates separate working and published 512x224
RGB buffers only when `--with-render` or `--frame-output` is requested. At
visible HBlank it renders the just-completed scanline before HDMA changes
registers for the following line. Publishing after scanline 224 ensures that
hashing never observes a partially replaced frame.

The first implementation covers modes 0, 1, 3, and 5; 2/4/8-bpp planar tile
decoding; 8x8/16x16 background tiles; screen-map wrapping; tile flips,
palettes, and priority; low-resolution doubling; Mode-5 high resolution;
main/subscreen selection; fixed color; and add/subtract/half color math.

Sprites cover every OBSEL size pair, name selection, 9-bit X wrap, Y wrap,
palette, flips, priority, and priority rotation. Per-line evaluation enforces
the SNES 32-object and 34-sliver limits and reports range-over/time-over counts.
Focused tests include a 33rd-object overflow case so the limit is observable
rather than merely documented.

Unsupported modes and features are never silently treated as complete. Each
frame and the whole run retain limitation masks for unsupported modes,
windows, direct color, mosaic, pseudo-hires, EXTBG, interlace, and unsupported
object behavior. Object range/time overflow is modeled and reported
separately, not classified as an implementation failure.

### Step 15: produce private, reproducible visual checkpoints

`--with-render` implies the timed APU path and prints the completed-frame
SHA-256. `--frame-output=<path>` additionally writes a binary PPM to the exact
caller-supplied path. Export is opt-in because the pixels are ROM-derived and
must remain in an ignored private/build directory.

At 2,000,000 instructions, the private probe publishes 478 frames. The last
frame uses modes 1 and 5, has no limited scanline, and produces:

```text
Frame SHA-256: fd62d5bea3f0961e286bd4ae266ff1c09a30be9260da820003dc06b26d307b8d
```

The exported local image contains a recognizable `Nintendo Presents` frame.
That observation is useful bring-up evidence but is not stored in Git and is
not an accuracy assertion.

The renderer also completed the existing 20,000,000-instruction neutral-input
run without creating a hardware barrier:

```text
Render:        4445 frame(s), 995722 scanline(s), 11200 limited, features=$01
Published:     modes=$02 limited=0 features=$00
OBJ limits:    range=7307 time=5053 scanline(s)
Frame SHA-256: bbf512419991ea943dd5e61aa61096c043feeae94c43de0d37bf9d18ebe941ad
```

The final Mode-1 frame has no declared per-frame renderer limitation. The
global `$01` limitation comes from 11,200 earlier Mode-7 scanlines, which are
still unsupported. Existing CPU, DMA, HDMA, WRAM, SRAM, VRAM, CGRAM, and APU
checkpoints remain at their version-0.6 values, showing that enabling
observation did not redirect the emulated execution path. OAM intentionally
changes to `7702622a75dd378552bb607570e41238b01fdab0c8405380056856046328e3a5`
because version 0.7 corrects its paired write latch and address mapping.

### Verification for version 0.7.0

- fresh Visual Studio 2022/MSVC Release build: passed with no project warnings;
- all 20 configured synthetic and private integration tests: passed;
- the new synthetic renderer suite covers blanking, Mode-1 priority, flips,
  color math, sprites, Mode-5 pixels, object limits, and frame publication;
- the private render CTest pins the 2,000,000-instruction frame hash;
- the renderer ran through the full 20,000,000-instruction checkpoint; and
- no ROM, framebuffer, screenshot, memory dump, extracted asset, or generated
  game binary was added to the repository.

## Current limitations after 0.7.0

- There is a headless framebuffer but no desktop window, frame pacing, or
  playable input loop.
- Mode 7 pixels, modes 2/4/6, windows, direct color, mosaic, pseudo-hires,
  interlace, and EXTBG remain unsupported.
- There is no host audio output even though the SPC700/S-DSP executes.
- CPU-to-master timing, DMA duration, PPU access restrictions, and dot-level
  effects remain provisional or incomplete.
- The rendered frames and long-run state have not yet been matched to an
  accurate reference emulator at equivalent logical events.
- Native-C emission has not started; game code still runs in the interpreter.

## Exact next task after 0.7.0

Implement Mode-7 pixel rendering, then capture a reference-emulator frame and
PPU state snapshot at the same logical event as the 2,000,000-instruction
native checkpoint. Compare the RGB pixels together with VRAM, CGRAM, OAM, and
display registers. Resolve the first mismatch before adding a desktop window;
visual similarity alone is not a sufficient accuracy test.

## Checkpoint: version 0.8.0

### Step 16: implement the Mode-7 pixel path

The renderer now consumes the Mode-7 H/V offsets and signed A/B/C/D/X/Y
register values already maintained by `dkc2_snes_io`. Its independently
written sampling path implements:

- signed 13-bit offsets and centers;
- the hardware's clipped offset terms and low-six-bit product truncation;
- 8.8 affine matrix coordinates with scanline-one-based vertical sampling;
- M7SEL horizontal/vertical screen flips;
- 1024x1024 wrapping, transparent-outside, and tile-zero-outside modes;
- Mode-7's even-byte tile map and odd-byte pixel layout in VRAM;
- BG1's full eight-bit palette index and fixed priority; and
- EXTBG's seven-bit palette index plus low/high priority selection.

The official Snes9x PPU and graphics sources were used as a behavior
cross-check. No Snes9x code, comments, tables, or data were copied. The project
implementation is expressed through the existing small renderer interface and
covered by synthetic state created entirely by the tests.

The synthetic renderer suite now checks identity sampling, horizontal and
vertical screen flips, wrapping, M7SEL repeat mode 1, transparent outside,
tile-zero outside, interleaved VRAM, and EXTBG high priority. Mode 7 and EXTBG
no longer raise limitation bits. Modes 2/4/6, windows, direct color, mosaic,
pseudo-hires, and interlace remain explicitly reported when observed.

### Step 17: create and pin a real-ROM Mode-7 checkpoint

Instruction-limit probes isolated a stable Rareware-logo frame at 1,700,000
instructions. The new private CTest runs that exact command and pins:

```text
Instructions:  1700000
Render:        342 frame(s), 76608 scanline(s), 0 limited, features=$00
Published:     modes=$80 limited=0 features=$00
Frame SHA-256: ce5c1873327e39ba4d77c33e101ce9956ee86554c889855b8e3531b330923c2f
CPU:           80:9398 A=227E X=01FF Y=0000 S=01FF D=0000 DB=80 P=24 E=0
```

The 2,000,000-instruction regression keeps its existing frame hash while its
global render result improves from 11,200 limited Mode-7 scanlines to none:

```text
Render:        478 frame(s), 107072 scanline(s), 0 limited, features=$00
Frame SHA-256: fd62d5bea3f0961e286bd4ae266ff1c09a30be9260da820003dc06b26d307b8d
```

The complete 20,000,000-instruction render also reports zero limitations over
995,722 scanlines. Its late Mode-1 frame hash remains
`bbf512419991ea943dd5e61aa61096c043feeae94c43de0d37bf9d18ebe941ad`,
and every established CPU, DMA, HDMA, WRAM, SRAM, VRAM, CGRAM, OAM, APU, and
ARAM checkpoint remains unchanged.

### Step 18: compare actual RGB output with the reference emulator

The official Snes9x 1.63 Windows release was downloaded to a temporary local
directory and run with the user's private ROM. Screenshots spanning the
Rareware-logo animation were captured outside the repository. The stable
native Mode-7 image is 512x224 because low-resolution SNES pixels are doubled.
After verifying that every pair is equal and reducing it to 256x224, two
consecutive Snes9x captures independently produced the same result:

```text
Candidate SHA-256: 57b5636a6eee0295ff395771453092d8560de5e643208e2fb69cecae190d627f
Reference SHA-256: 57b5636a6eee0295ff395771453092d8560de5e643208e2fb69cecae190d627f
Differing pixels: 0 / 57344
Differing channels: 0 / 172032
Maximum channel error: 0
Mean absolute channel error: 0.000000000
Result: exact RGB match
```

This is exact pixel evidence, not a visual-similarity judgment. It validates
the composed output of that sustained Mode-7 frame. By itself it does not prove
matching CPU/PPU timing or underlying state; the following step separately
compares the display memories while retaining the register/timing caveat.

The new standard-library `scripts/compare_frames.py` makes the measurement
repeatable without a PNG dependency. It reads P6 PPM and non-interlaced 8-bit
RGB/RGBA PNG, validates PNG checksums and scanline filters, normalizes a
doubled-width candidate, reports both hashes and error metrics, optionally
writes an absolute-difference PPM, and returns a failing exit code for any
pixel mismatch. Reference captures and differences remain private ROM-derived
artifacts and are ignored by policy.

### Step 19: compare the underlying display memories

Snes9x's own save-slot menu command was invoked across the sustained matching
logo interval. The official snapshot-format-v12 stream contains raw VRAM and a
serialized PPU block with CGRAM and OAM. After restoring CGRAM words to the
runtime's little-endian byte layout, all three reference memories match the
1,700,000-instruction native checkpoint byte for byte:

```text
VRAM SHA-256:  011580629bf3007e8acd599b872173a08a6156c4f896ef9e6fbf35023e99cb7e
CGRAM SHA-256: bb867c40f4978157de0e761f13d2ed05fc4f697f8c23597ea110b3a26a01df2e
OAM SHA-256:   44ddd2f478477ebd1c1cd5b99400af48cd46033c59173195f48870e608cec810
```

The new `scripts/inspect_snes9x_snapshot.py` reproduces this extraction with
the Python standard library. It validates the v12 gzip and named-block framing,
checks required block sizes, converts CGRAM byte order, reports the three
display-memory hashes plus raw `$2100-$2133` write-register storage, and can
assert any expected hashes through command-line options.

`dkc2_boot` now prints a matching raw-write-register fingerprint and the
decoded Mode-7 values in addition to the existing memory hashes. The current
write-register arrays are not yet claimed to match: the native runner stops at
a provisional beam position in the frame following its published image, while
the reference save command can run on another scanline. Write-only data ports,
HDMA-updated values, and latches therefore need an agreed beam event and a
canonical logical-state schema before comparison. The exact RGB and three
display-memory matches do not depend on treating those incidental current
register bytes as equal.

### Verification for version 0.8.0

- Visual Studio 2022/MSVC Release build: passed with no project warnings.
- All 21 configured synthetic and private integration tests passed.
- The new Mode-7 private CTest pins the 1,700,000-instruction frame hash.
- The established 2,000,000- and 20,000,000-instruction frame hashes passed.
- The full render run reports zero unsupported scanlines.
- Direct comparison against two Snes9x captures reports an exact RGB match.
- An adjacent Snes9x state matches VRAM, CGRAM, and OAM byte for byte.
- Both standard-library reference inspection tools reproduced their results.
- No ROM, screenshot, framebuffer, memory dump, extracted asset, or generated
  game binary was added to the repository.

## Current limitations after 0.8.0

- There is still no desktop window, frame pacing, interactive host-input loop,
  or playable build.
- Modes 2/4/6, windows, direct color, mosaic, pseudo-hires, and interlace are
  not implemented in the renderer.
- There is no host audio output even though the SPC700/S-DSP executes.
- CPU-to-master timing, DMA duration, PPU access restrictions, and dot-level
  effects remain provisional or incomplete.
- Display registers are not yet compared at an agreed beam position; the title
  frame and long-run machine state remain unvalidated against a reference.
- Native-C emission has not started; game code still runs in the interpreter.

## Exact next task after 0.8.0

Capture a deterministic Snes9x debugger/trace event at an agreed beam position
for the exact matched Rareware-logo frame. Define a canonical logical display-
register schema that excludes incidental write-only port bytes, export it from
both runners, and resolve the first difference. Then repeat the RGB, VRAM,
CGRAM, OAM, and register comparison at the title screen before broadening the
renderer or adding a desktop window.

## Unreleased checkpoint: independent snesrecomp integration

### Why the framework is now a submodule

The repository remains the DKC2-owned project. `mstan/snesrecomp` is now the
`snesrecomp/` Git submodule rather than copied source or an informal sibling
checkout. It was initially pinned to upstream `main` commit `92e8a04` on
2026-07-15, then refreshed after an authoritative fetch on 2026-07-16 to
`c1ce97ec8ae3743b4b1dce092903bebcefd58896`, the new main tip. The intervening
host-overlay extraction changes touched no files in the DKC2 framework patch,
so the update fast-forwarded without conflict.

Using the newest commit was intentional: DKC2 needs the current multi-tier
interpreter, emitter, runtime hardware work, and diagnostics. Reproducibility
comes from the parent repository recording the exact tested Git link, not from
using an older commit indefinitely. The DKC2 integration branch can update the
pin only after the project gates pass.

The earlier interpreter/PPU work was preserved as the correctness oracle. It
still provides fast deterministic private checkpoints and all 21 of its clean
build tests pass. The new framework path is experimental and does not replace
evidence that already works.

### Generic HiROM framework work

The first private generation exposed a real upstream assumption: reset/NMI/IRQ
vectors and ROM offsets were hard-coded for LoROM. That initially decoded the
wrong reset address. A focused branch inside the submodule now:

- scores SNES headers and selects LoROM or HiROM conservatively;
- maps full-bank and upper-half HiROM execution mirrors;
- reads architectural vectors from the selected header window;
- uses the active mapper in v2 analysis and program emission;
- gives runtime `RomPtr`/`MvnPtr` a stable cartridge-mapped ROM pointer; and
- includes synthetic HiROM plus ambiguous-header compatibility tests.

The v2 suite passes 295/295, including the two new mapper tests. The upstream
top-level launcher currently stops on four width-lint violations in
`v2/codegen.py`; the same four failures occur in an untouched checkout of the
pinned commit, so they are recorded as an upstream baseline issue rather than
a regression from the HiROM patch.

These game-agnostic changes are kept separate from DKC2 configuration and
runner glue so they can become a focused upstream pull request after final
review and runtime mapper coverage are complete.

### Source-only generation and native build

`scripts/generate_snesrecomp.ps1` now validates the exact 4 MiB USA v1.0 ROM
and SHA-256 before invoking the upstream v2 emitter. Generated C remains under
ignored `generated/snesrecomp/`. The reproducible result is 9 architectural
roots, 13 exact AOT variants, and two generated ROM banks.

The CMake integration defaults off so ordinary source-only builds do not
depend on private generated output. With
`DKC2_BUILD_SNESRECOMP_HEADLESS=ON`, the project builds a minimal host adapter,
shared SNES runtime, generated program, and DKC2 frame driver into
`dkc2_snesrecomp_headless.exe`. This is the first linked native executable on
the `snesrecomp` path, but it is diagnostic and has no window or interactive
input loop.

### First honest runtime boundary

The executable accepts only the exact verified private ROM, enters reset at
`$00:83F7`, and executes into the real audio initialization. It then stops at
the interpreter safety cap instead of freezing indefinitely:

```text
wait loop:       $35:8423  CPX $2140 / BNE
CPU X:           $00F5
APU input:       $045DA0F5
APU output:      $0000BBAA
SPC PC:          $FFCF
ARAM SHA-256:    72da1c8733dd334809ce0e5356b19d4ad25a9b3a31ab14f420dfbf4696916426
```

The new private symbol importer read 16,749 address-annotated labels from the
user-supplied Yoshifanatic1 disassembly ZIP without copying assembly or incbin
data into the repository. It identifies `$B5:840D` as the audio block uploader;
the executing `$35` bank is the same HiROM mirror. The wait at `$8423` expects
the SPC engine to echo transaction `$F5`, but the SPC has returned to the IPL
`$AA/$BB` ready state at `$FFCF`.

A proposed APU word-write ordering change was tested and produced the identical
failure signature, so it was reverted instead of being kept as a speculative
framework modification.

### Verification and present status

- Fresh Visual Studio Release build: passed.
- Existing synthetic and private suite: 21/21 passed.
- Private v2 generation: reproducible, 9 roots / 13 exact variants.
- Focused snesrecomp v2 suite: 295/295 passed.
- Experimental native link: passed.
- First native frame: failed at the bounded audio checkpoint above.
- Playable build: no.
- Attract-demo gate: not started; reset has not yet completed.

### Exact next task

Use the imported symbols and a matching reference trace to find the first SPC
state divergence during the `$B5:840D` block upload. Compare transaction,
source/target/length, ARAM writes, SPC PC/registers, and port values at each
echo. Fix the generic SPC/runtime behavior if the divergence is in
`snesrecomp`; use a narrowly documented DKC2 HLE only if the framework's
intended integration contract explicitly requires that upload to be HLE'd.
Then rerun the one-frame gate before expanding AOT coverage or beginning the
attract-demo soak.

## Unreleased checkpoint: HiROM SRAM fix and DKC2 NMI contract

### Audio failure resolved

The Yoshifanatic1 checkout was used only as an ignored private research input.
Its SPC700 loader source showed that IPL ROM remaining mapped during the upload
was expected. Focused CPU-side probes then found the first bad value: DKC2 read
sample metadata from `$F0:09FC`, where the verified ROM contains a nonzero
length, but the recompiled CPU observed zero.

The cause was in the shared runtime's SRAM classifier. It recognized both the
LoROM and HiROM SRAM windows at once. `$F0:0000-$7FFF` is LoROM SRAM, but it is
ordinary full-address ROM in HiROM. DKC2 therefore read blank save RAM in place
of ROM metadata and sent an invalid zero-length block to the SPC loader.

`cpu_sram_offset` now gates each window on `cart->type`. The runtime-dispatch
test covers the overlapping `$F0:09FE` address in both modes. No port echo,
length, or ARAM value is fabricated. With that fix, the already-built native
runner completed 600 frames with `lle=1` and no safety-cap failure.

### First display diagnosis

The completed run was still entirely black. Permanent headless diagnostics
reported nonzero VRAM, normal brightness, forced blank disabled, and zero
CGRAM. The framework's DMA history then proved that DKC2 correctly triggers a
512-byte channel-1 transfer from `$7E:8928` to CGRAM `$2122` every frame. The
source palette buffer, rather than DMA or the renderer, remained zero.

WRAM probes showed `$20=$9378`, identifying the NMI continuation. The imported
source maps that continuation precisely:

1. the NMI handler saves registers and executes `JMP ($0020)`;
2. the intro frame routine resets `S` to `$01FF`;
3. it performs the palette DMA and advances the intro state; and
4. it ends at its own `WAI` loop instead of returning through `RTI`.

The original DKC2 adapter used the framework's conventional interrupt helper,
then resumed the old `$80:B0C6` wait loop. That is the wrong contract for this
non-returning NMI dispatcher. `runner/dkc2_game.c` now pushes the architectural
interrupt frame and runs NMI plus continuation with the quiescent bridge,
recording the continuation's next wait as the new resume point.

### Verification state

- snesrecomp v2 Python suite: 295/295 passed after the mapper/runtime changes.
- Last rebuilt native executable: 600 frames completed, but CGRAM/framebuffer
  remained zero before the NMI-adapter correction.
- NMI-adapter correction: source-reviewed and documented, not yet rebuilt.
- Rebuild blocker: the execution environment exhausted its allowance when
  Visual Studio requested installed Windows SDK metadata. No unbuilt result is
  being reported as a pass.

The next executable gate is a Release rebuild followed by 600 neutral-input
frames. The gate requires `$002A` to advance, CGRAM to become nonzero, the frame
hash to leave the all-zero value, and no LLE/watchdog failure. Only after that
passes should the run be extended toward two complete attract-demo cycles.

The source-only `scripts/test_snesrecomp_smoke.ps1` check codifies this gate and
is registered with CTest for private Windows native builds.

## Unreleased checkpoint: first repeated native transitions

The rebuilt NMI adapter passes the 600-frame gate with advancing intro state,
243 nonzero CGRAM words, 589 active video frames, and 553 frames containing
nonzero stereo samples. This replaces the former black-frame checkpoint.

The first 3,048-frame attempt exposed a deterministic freeze in DKC2's
object-list sort. Yoshifanatic1's privately held v1.0 annotations and an
independent interpreter run localized the loop to `$B5:F348`. Its `BRL -$72`
must branch from `$B5:F34B` to `$B5:F2D9`; the shared interpreter's compound
assignment allowed MSVC to add the displacement to the PC before the two-byte
operand fetch completed, landing at `$B5:F2D7` and executing `STZ $44`.

The framework fix reads the operand into a temporary before adding it to the
PC for both `BRA` and `BRL`. Positive- and negative-displacement CPU tests now
pin the rule. The 19-check interpreter suite and mapper/runtime-dispatch test
pass under MSVC, the v2 suite passes 295/295, and the project suite passes
21/21.

After the fix, frame 3,048 completes during an intentional forced-blank
transition, frame 3,100 renders again, and a 12,000-frame neutral-input soak
crosses the transition repeatedly without an interpreter bail, crash, freeze,
or unexpected reset. This is liveness evidence, not proof of two semantic
attract cycles.

The headless adapter now records active/blank video frames, longest blank run,
audio-active/silent frames, nonzero sample count, and peak magnitude. It also
supports `DKC2_FRAME_PPM` for ignored private captures. Inspection at frame
3,600 shows a coherent background but conspicuous foreground/sprite tile
artifacts. Therefore the playable/attract success criterion remains open.

### Exact next task

Build the accurate `snesref` oracle with a supplied libretro core, dump the
matching neutral-input frame window, and compare OAM, VRAM, PPU registers, and
pixels at the first bad sprite. Fix the earliest shared-runtime divergence,
then add a deterministic event-based two-cycle attract gate and reference
audio comparison before starting the desktop window/audio-device layer.

## Unreleased checkpoint: exact native sprite/reference match

The source-only libretro capture tool now runs the verified ROM in a supplied
reference core without bundling that core or any game data. A Snes9x capture
at neutral-input frame 3,578 aligns with paced native frame 3,600: both report
active-frame state `$0A50` and the same display memories.

The comparison isolated the apparent sprite corruption cleanly:

1. Snes9x PPU OAM equals DKC2's WRAM `$0200-$041F` OAM table.
2. Native WRAM `$0200-$041F` equals that reference table byte for byte.
3. Native PPU OAM differed, despite a logged 544-byte `$00:0200 -> $2104` DMA
   every frame.
4. The native OAM prefix appeared later in OAM, proving a stale destination
   address rather than bad sprite generation, ROM mapping, or tile data.

The shared PPU already implements the hardware VBlank reload from OAMADD, but
`Dkc2DrawPpuFrame` did not call it. The adapter now runs `ppu_checkOverscan`
and `ppu_handleVblank` after visible scanlines. The next frame's NMI therefore
starts its complete OAM DMA at the correct internal address.

The corrected aligned checkpoint is:

```text
VRAM SHA-256:  616c24e59dc9e2152d4b36e7fe1b8b2d6951c5e1d58a0fd0509be503397b621e
CGRAM SHA-256: 7367e1127ea308fcfae78bdbb0dfe76962e79803bf618c9c9f9caac758c66cab
OAM SHA-256:   683f8707be2dfe68dd7e38c96f65e3a241421b350d21ed833354046afccbae4e
Frame SHA-256: fe8fda176a365db9442d5c75ada9eebefd94618a168a9539f6b1ef819e4b7458
```

All three memories match Snes9x exactly. Its libretro output surface expands
green as RGB565 while the native renderer expands the SNES's five-bit green
directly; after reducing the reference green channel back to five bits, all
57,344 pixels match exactly. The private CTest regression pins the native
frame plus all three display-memory hashes. The short smoke reports both PPU
OAM and the WRAM staging table; equality is required only at checkpoints where
the game has just completed the full OAM DMA.

### Exact next task

Identify stable title/attract state events from the imported symbols and a
reference run, then require two complete neutral-input attract cycles with the
same ordered video checkpoints. Capture the native and reference DSP output
over those events and compare timing, silence gaps, pitch, and sample content
before calling the headless build fidelity-complete.

## Unreleased checkpoint: paced two-cycle attract and audio comparison

The former runner let a long interpreted loading routine complete atomically
inside one host callback. That compressed transitions and made a frame count a
poor representation of SNES time. The DKC2 adapter now budgets exactly
`1364 * 262` master clocks per host frame. The interpreter yields at the
deadline, advances idle hardware to the boundary, and resumes from the real
24-bit PC saved in the hardware interrupt frame. The shared interrupt-frame
helper therefore has an explicit `cpu_push_interrupt_frame_at` form; the
generated host-C wrapper retains its existing return-through-C contract.

With neutral input, both native and Snes9x traverse the same semantic sequence:
title, three built-in demo levels (`$000C`, `$000F`, `$0013`), return to title,
and repeat. The 12,000-frame native gate records 28 state transitions, six demo
starts, six demo ends, two complete `3 -> 0` attract-cycle wraps, and zero
ordering errors. It pins this transition fingerprint:

```text
state_event_sha256=51edd465f3945ed6fb529c5217617a903cccdfd5f133dca2834be0d9f64a892d
```

The same run produces 6,397,464 stereo frames through a fractional
`32040 / 60.098811862` accumulator rather than requesting 534 unconditionally.
It has 11,018 audio-active host frames, 982 silent host frames, no clipped
samples, a peak magnitude of 16,541, and a maximum adjacent same-channel jump
of 6,845. Its deterministic audio fingerprint is:

```text
audio_fnv1a=9c297ef645fe29dc
```

For stronger evidence, `DKC2_AUDIO_PCM` captured one complete ignored/private
cycle from both the native runner and Snes9x 1.63 commit `185488c` at the native
DSP rate. `scripts/compare_audio_pcm.py` compares artifact indicators and the
silence envelope instead of requiring two independent DSP implementations to
be integer-identical. The result passed:

```text
                         native          Snes9x
duration                 99.835581 s     99.899782 s
RMS                      2227.667020     2239.768898
peak                     16541           17303
maximum adjacent jump    6107            6271
clipped samples          0               0
long silence regions     7               7
result=pass
```

Synthetic tests prove that this comparison accepts a small clean level
difference but rejects clipping and an extra long dropout. The private PCM,
reference core, PPM, ROM, and generated game code remain ignored.

This is automated evidence for two stable attract cycles and clean audio, not
final perceptual sign-off. Native state transitions are about four frames late
at the title and 54 frames late by the end of the first cycle; most drift
accumulates in interpreted level-loading periods. The silence envelope shows
the same gradual offset and no unexplained dropout. Exact CPU/master-clock
alignment, a manual listen/watch pass, and the interactive desktop host remain
open rather than being inferred from the headless gate.

### Exact next task

Instrument interpreted master-clock accounting across the level-loading
windows and compare it with an instruction/cycle reference before changing any
timing constant. In parallel, stand up the desktop presentation/input/audio
device layer using the already-validated frame and PCM contracts. Do not tune a
global multiplier merely to make event frame numbers agree.

## Unreleased checkpoint: interactive Windows host

The validated `snesrecomp` core now has a project-owned Windows presentation
host instead of requiring a new emulator or copying SDL-dependent framework
launcher code. `runner/desktop_main.c` creates a resizable GDI window and
presents the existing 256x224 BGRX surface at 4:3. It keeps execution,
rendering, audio production, and message handling on one thread so the shared
runtime does not gain a new synchronization contract.

Audio remains governed by the same fractional `32040 / 60.098811862`
accumulator as the deterministic headless runner. Per-frame pulls of 533 or 534
stereo frames are packed into fixed 2,048-frame waveOut blocks. Three blocks
are prebuffered before wall-clock pacing begins, reducing scheduler-induced
underrun risk without changing which DSP samples are consumed. A performance
counter then advances an absolute fractional deadline at the NTSC host rate.

Focused keyboard state maps arrows, `Z/X/A/S`, Enter, Shift, and `Q/W` to the
12 SNES buttons. The host polls all four XInput user slots and uses the first
connected controller, so attaching or removing a pad does not require a
restart. The conventional face mapping is Xbox A/B/X/Y to SNES B/A/Y/X;
D-pad and left stick share directions. `runner/desktop_input.c` isolates this
translation from Windows, and `tests/test_desktop_input.c` pins every button,
deadzone boundary, and analog direction with synthetic state.

Both native hosts now call `Dkc2ReadVerifiedRom`. This single source-only loader
removes an optional 512-byte copier header in memory and accepts only the exact
4 MiB USA v1.0 SHA-256. Neither host can accidentally run an unverified game
image, and the private input remains outside Git.

Verification completed for this checkpoint:

- the complete configured CTest suite passes 27/27, including every private
  integration and native/reference gate;
- both `dkc2_snesrecomp_headless.exe` and
  `dkc2_snesrecomp_desktop.exe` compile under MSVC;
- the synthetic desktop-input test passes;
- the desktop target starts the verified ROM, creates its hidden test window,
  advances 180 frames, initializes waveOut, and exits cleanly; and
- the shared-loader headless runner still completes its 600-frame smoke run
  with active video/audio and no LLE or off-rails failure.

The automated desktop gate proves startup and lifecycle behavior, not human
perception or physical-controller hardware. The documented manual pass must
still watch and listen through a complete attract cycle, exercise keyboard and
a real XInput pad, and record any transition-specific artifact. DirectInput,
native PlayStation APIs, controller 2, rumble, menus, fullscreen, remapping,
and save-file UX remain outside this first host.

### Exact next task

Run the complete configured test suite, then launch the visible desktop build
for the manual keyboard/controller/watch/listen checklist in
`docs/DESKTOP_TESTING.md`. Treat any observed freeze, visual artifact, audio
dropout, or incorrect input as a reproducible defect before expanding gameplay
scope.

## Unreleased checkpoint: FastROM program-bank timing correction

Fresh ignored 6,000-frame traces made the timing error repeatable. Before the
fix, native reached the first, second, and third demo-play states at frames
3,417, 4,367, and 4,831; Snes9x reached the same states at 3,396, 4,328, and
4,778. Each native loading window was exactly 14 frames too long:

```text
                         demo 1   demo 2   demo 3
native before fix          166      149      167
Snes9x                      152      135      153
```

The shared interpreter installed a new program bank with `bank & 0x7f` for
`JSL`, `JML`, `RTL`, the bridge's synthetic RTL, and `JMP [abs]`. That is not a
valid mapper normalization: PBR is an eight-bit architectural register. DKC2
uses FastROM banks `$80`, `$B5`, and `$BB`; clearing bit 7 executes identical
ROM bytes through `$00`, `$35`, and `$3B` but charges the slower bus region.

Four new directed checks failed before the change with `$B5 -> $35` and
`$BB -> $3B`, then passed after removing the masks. The retained parent CTest
target reports 23/23 core checks, and the independently linked bridge harness
reports 52/52 checks after gaining timing-only stubs for the current bridge
dependencies.

After the correction, the three loading windows are 152, 134, and 152 native
frames versus 152, 135, and 153 reference frames. The complete ordered event
sequence passes `scripts/compare_state_traces.py`: every event is within six
frames and every load is within one. First-cycle completion is now frame 5,534
native versus 5,540 reference, replacing the former 54-frame late result with
six frames early.

The aligned visual checkpoint moves from native frame 3,600 to 3,575. At the
same `$0A50` active-frame state as Snes9x frame 3,578, the existing frame,
VRAM, CGRAM, and OAM hashes remain exact. The updated 12,000-frame native gate
still observes 28 events, six demo starts, six demo ends, two cycles, and zero
ordering errors. Its replacement deterministic fingerprints are:

```text
state_event_sha256=501cfdf7675ab049930bda21f92d373e492b8b759c0ae12d5cf3e59f1daf85c5
audio_fnv1a=73f8981735837ce1
```

The corrected one-cycle PCM comparison also passes. Native duration is
99.835581 seconds versus 99.899782 reference, RMS is 2,251.239404 versus
2,239.768898, peaks are 16,611 versus 17,303, maximum adjacent deltas are
6,850 versus 6,271, both have zero clipped samples, and all seven long-silence
regions correspond. This rules out fixing video timing by damaging audio
pacing.

### Exact next task

Retain the new six-frame semantic bound and localize its components rather
than globally retuning frame pacing: three frames occur before the first title
event, one appears during the second load, one during the third load, and one
at attract reset. Run the complete updated suite and the visible desktop
watch/listen/controller checklist before claiming perceptual attract-demo
sign-off.

## Unreleased checkpoint: double-clickable Windows launcher

The first desktop host was built as a console executable and required exactly
one command-line ROM argument. Double-clicking it therefore opened a transient
command prompt, printed usage text too briefly to read, and exited. That was a
launcher defect rather than a SNESRecomp runtime failure.

The target now uses the Windows GUI subsystem. Its entry point accepts the same
single explicit ROM path used by scripts and CTest, but a no-argument launch
opens the standard Windows file picker filtered to `.smc` and `.sfc`. Cancelling
is a successful no-op. A selected file still passes through the shared exact
USA v1.0 SHA-256 loader before SNESRecomp is initialized, and no ROM content is
copied into the repository.

Verification for this launcher correction:

- MSVC rebuilt `dkc2_snesrecomp_desktop.exe` successfully;
- launching the executable with no arguments left a responsive window titled
  `Select your DKC2 USA v1.0 ROM` instead of exiting;
- the explicit-path hidden desktop smoke test initialized the verified ROM,
  Windows window, renderer, waveOut device, and game loop for 180 frames; and
- the synthetic keyboard/XInput mapping regression continued to pass.

The complete configured suite then passed 30/30 tests, including the 180-frame
desktop lifecycle/audio test, the 12,000-frame two-attract-cycle gate, the
aligned visual checkpoint, both retained SNESRecomp interpreter suites, and all
private ROM probes. Human visible output, audible output, keyboard, and
physical-controller testing remain the next acceptance step.

## Unreleased checkpoint: atomic presentation, time controls, and battery SRAM

The user's 106.432-second first-play recording made the reported intermittent
flicker measurable. Full-frame extraction found twelve isolated 1/30-second
black game-surface frames during otherwise continuous gameplay, distinct from
the normal multi-frame black level/death transitions. A 6,000-frame neutral
native/reference comparison then observed the same title/demo sequence and
nearly identical blank-run bounds: native reported 5,264 active / 736 blank
frames with a 155-frame maximum blank run, while the Snes9x libretro reference
reported 5,257 / 743 with a 156-frame maximum. There was no recurring extra
SNES forced-blank event to explain the captured one-frame flashes.

The old Win32 paint path issued a visible black `FillRect` followed by a
visible `StretchDIBits`. A desktop compositor or recorder could sample between
those calls. `runner/desktop_present.c` now owns a reusable client-sized DIB,
composes the borders and scaled 256x224 image off-screen, and exposes it with a
single `BitBlt`. A synthetic GDI regression verifies letterbox, pillarbox, and
resize pixels. An eight-second corrected capture during active Pirate Panic
attract gameplay contained zero isolated black frames under the same black-
detection procedure. This is strong host-side evidence; the user's own visible
retest remains the perceptual acceptance gate.

The desktop host now supports fixed 3x time controls. Keyboard `1` and XInput
left trigger rewind; keyboard `2` and right trigger fast-forward. Trigger
actions are separate from the twelve SNES controller bits. Fast-forward runs
three console frames for each paced host iteration. Rewind captures a complete
state every three console frames into a 300-entry bounded ring and restores one
entry per paced host iteration, retaining approximately fifteen seconds. Audio
is rendered but discarded during fast-forward, not rendered during rewind,
and the waveOut queue is reset on every speed-mode transition so stale future
audio cannot play after a restore.

The existing shared-runtime file snapshots were not sufficient by themselves:
DKC2's PC continuation, external `CpuState`, frame deadline, FastROM `MEMSEL`,
HDMA enable, frame counter, and host-side APU pacing counters live outside the
SNES object graph. The framework branch therefore adds memory-backed snapshot
entry points using the existing versioned `SaveLoadInfo` stream. DKC2's state
extension serializes those fields with the RAM pointer cleared, then repairs
the pointer and clock/deadline anchors after load. Snapshots are taken before
the frame draw because the draw pass advances HDMA and VBlank state; the load
path redraws once to recreate the original post-draw boundary. Synthetic tests
cover ring overwrite/LIFO behavior and trigger thresholds, while the hidden
desktop integration captures history and performs an actual restore after
frame 120.

For normal battery saves, the current `mstan/SuperMarioWorldRecomp` repository
was inspected as a behavioral reference at commit
`9055e0f5e9e24c1dcad59d0c17b7eca0b6d5ce0f`. Its host anchors relative paths
to the executable, creates `saves`, reads SRAM after SNES initialization, and
writes on clean exit. The repository does not declare an overall compatible
license, so no source or comments were copied. DKC2 independently applies the
same lifecycle through SNESRecomp's existing `RtlReadSram`/`RtlWriteSram`
interfaces. The no-argument ROM picker also uses `OFN_NOCHANGEDIR`, preventing
the selected ROM folder from becoming the save folder.

The desktop runner now reads the exact 2,048-byte cartridge-RAM allocation from
`saves/save.srm` beside its executable and writes it after a clean exit. Before
each write, the previous file becomes `save.srm.bak`. Git already excludes
save data. CTest sets `DKC2_DESKTOP_DISABLE_SRAM=1`, so automation cannot alter
a player's progress. An ignored isolated copy of the executable was launched
twice: both processes exited zero; current and backup files were each 2,048
bytes; and both matched the first run's SHA-256. The real playable-build save
directory was not used by this validation.

Final verification for this checkpoint:

- the Release build completed under MSVC;
- desktop input, bounded rewind, and atomic presentation unit tests passed;
- the hidden 180-frame desktop test executed a three-frame fast-forward
  iteration and completed a real snapshot restore;
- the isolated two-launch SRAM current/backup check passed; and
- the complete configured suite passed 32/32, including the 12,000-frame
  two-attract-cycle gate, aligned native/reference visual checkpoint, PCM/state
  comparisons, retained SNESRecomp interpreter suites, and all private probes.

### Exact next task

Launch the rebuilt visible executable and manually verify `1`, `2`, both
controller triggers, normal in-game saving/relaunch at Kong Kollege, and a
complete attract cycle on the user's display/capture/audio setup. Preserve the
new automated gates; treat any visible flash, audio discontinuity after a time
control, failed save reload, or restore-time corruption as a reproducible
defect rather than inferring success from the hidden test.

## Superseded experiment: 99.91% static coverage and native analysis

The structural DKC2 import exposed control-flow shapes that the first native
analyzer did not yet model: HiROM aliases, finite pointer-tail dispatch,
pointer-pop calls, computed RTS stacks, caller-crossing returns, recursive exit
sets, declared function boundaries, data-region execution, and terminal inline
calls. These were implemented as analyzer classes rather than per-function
generated-C edits or optimistic CFG declarations. The exact result is 3,464
AOT-eligible variants out of 3,467 (99.91%). The three remaining variants are
a stack-reset continuation and two documented crash/invalid-code paths.
This records the experimental closure result; the 0.0.1 release uses the later
conservative 3,425/3,467 profile after poisoned exit facts were withheld.

The Rust analyzer was checked against a fresh Python-oracle manifest. Their
3,467-node emission contracts match and all 103 emitted C translation units
are byte-identical. The B5 closure generation measured 25.3 seconds with Rust
and 401.0 seconds with Python, an approximately 15.8x improvement. A cold
Release build of both DKC2 hosts still took about 3 minutes 38 seconds, making
generated-C compilation the dominant rebuild cost.

Runtime validation completed a 12,000-frame, two-attract-cycle full-AOT run
with no sequence errors or runtime bailouts. The final title capture and a
separate in-level capture were inspected and showed clean background rendering.
Same-frame AOT/interpreter VRAM hashes are no longer a valid alignment gate
because the two executions reach different scene/scroll phases; the Rust
promotion instead inherits runtime parity from byte-identical generated C.

The `$B5:F0E5` routine is now closed statically. The source importer recognizes
H4 `%offset(field, N)` record metadata, proves the symbolic handler word at
offset four, follows its `LDA field,x`/direct-page store to the synthetic-return
`JMP ($0036)`, and emits one six-target `ptrcall` contract. This promotes the
routine, its `$BB:9210` caller, and all six one-byte shift callbacks without a
generated-C edit or hand-written target list.

The shared direct-page bank correction was also audited in isolated MMX, SMW,
Zelda, and Super Metroid scratch builds. Their generated trees contain 46,445
direct-page accesses using bank `$00` and none using the old `$7E` bank. The
four executables advanced through 288, 201, 326, and 151 structured frame
snapshots respectively, with a different WRAM CRC at every captured frame.
No game repository branch was changed for this validation.

Per-hit interpreter, gap, deny-file, state-transition, and heartbeat console
logging was removed. Bounded coverage manifests, analyzer snapshots, write
rings, frame/memory captures, and the TCP inspection surface remain the
supported observability paths.

## 2026-07-19 — death/restart closure and five-game release gate

The Pirate Panic death transition reproduced deterministically from the user's
slot-0 state. The interpreter completed the restart, while the static build
could run a long MVN/MVP transfer atomically past frame deadlines and black
screen. Generated block moves also treated A=`$FFFF` as a zero-byte transfer,
omitted repeated-byte cycles, and failed to wrap X/Y in 8-bit index mode.

The shared generator now models the architectural instruction directly: one
byte is always transferred, DB and A/X/Y update after every byte, repeated
bytes cost seven CPU cycles at the mapped bus speed, and an active scheduler
may unwind only between bytes after its master deadline. The existing LLE
sentinel resumes at the same opcode with the updated machine state. Synthetic
MVN/MVP coverage and the full engine suite pass (353/353).

Regenerating Mega Man X exposed a separate C11 class bug: monolithic generated
banks declared cross-bank exact variants only inside one tail-call block, so a
later dispatch call could see an incompatible implicit declaration. Translation
unit publication now derives file-scope declarations from every referenced
exact variant, as sharded banks already did. This fix is generator-owned; no
generated C was edited.

The accepted DKC2 release profile is deliberately conservative: 3,425 of 3,467
exact variants are static (98.79%) and 42 remain authoritative interpreter
fallbacks. The release build completed two ordered attract cycles in 12,000
frames with zero sequence errors, a clean inspected title frame, active
non-clipping audio, and a manually verified death/restart recovery.

The identical engine revision was regenerated with the native Rust analyzer
and built in isolated worktrees for all supported games. Each completed a
12,000-frame attract soak with inspected rendered checkpoints and forward
machine-state progress:

| Game | Automated attract | Manual gameplay |
| --- | --- | --- |
| DKC2 | pass (two cycles) | pass, including death/restart |
| Super Mario World | pass | pass |
| A Link to the Past | pass | pass |
| Mega Man X | pass | pass |
| Super Metroid | pass | pass |

This matrix is the merge and 0.0.1 release gate. ROMs, save states, generated
ROM-derived C, screenshots, and audio captures remain private and ignored.

## 2026-07-20 — public-repository reconciliation and host telemetry

Development moved to `codex/reconcile-local-work` from public DKC2Recomp
commit `1c923cf` (`v0.0.1`). Before rebasing, the parent and both dirty
submodules were captured on `codex/pre-dkc2recomp-reconciliation` at `347868a`,
`8516437`, and `5c834f0`. The current tree keeps the public pins
`snesrecomp@cfa8e56` and `recomp-ui@7c18edd`; neither submodule was replaced by
its older local fork. The file-by-file disposition and remaining UI redesign
work are recorded in `docs/RECONCILIATION.md`.

The public repository superseded the former launcher, SRAM, save-state,
time-control, presentation, generation, and packaging implementations. Four
self-contained Windows-host features were still useful and were replayed:
once-per-second FPS title text, opt-in per-phase telemetry, Release speed
optimization, and external icon-resource configuration. FPS and telemetry
math have synthetic tests. The instrumentation is disabled by default and
does not alter emulated state or timing; GDI exposes no GPU timestamps, so the
log reports that limitation explicitly.

The available Python generator emitted 3,464 exact variants and three LLE
variants, not the release's conservative Rust-generated 3,425/42 profile. The
ROM-derived output and generated declaration header remain uncommitted. An
optimized MinGW build completed and all 32 tests passed, including the hidden
desktop exercise, sprite hashes, and 12,000-frame/two-attract-cycle gate.

With telemetry enabled for a hidden 180-frame run, presentation measured
59.22 and 61.00 FPS. Main-thread active time was 72.4-78.5%; emulation consumed
11.64-13.15 ms/frame while the measured GDI publish call consumed about
0.005-0.006 ms/frame. That run is CPU/emulation limited. GPU time is unknown,
not zero, because the current gameplay backend has no GPU timing API.

## 2026-07-21 — Player 2 launcher and Windows ACL repair

DKC2 now reports two supported players to the pinned shared ImGui launcher.
The desktop host persists both None/Keyboard/Gamepad selectors and deadzone
values, polls up to two XInput devices, and packs independently routed 12-bit
values into the two controller ports already accepted by `RtlRunFrame`.
Defaults remain keyboard for Player 1 and the first connected gamepad for
Player 2. The shared UI submodule was not modified.

Synthetic input coverage now proves keyboard/P2-gamepad routing, two-gamepad
port packing, shared keyboard routing, disabled sources, and host trigger
actions in addition to the existing button and analog tests.

The optimized desktop target rebuilt successfully. All 32 configured tests
passed, including the hidden desktop path, private sprite hashes, and the
12,000-frame/two-attract-cycle gate. The shared launcher's built-in ten-frame
smoke hook also opened and closed the real ImGui dashboard successfully with
DKC2's two-player metadata.

The Python emitter's atomic output replacement had left
`generated/snesrecomp` owned by the restricted build account with inherited
permissions disabled. The current tree was repaired by re-enabling inheritance
and granting the interactive account Full Control. The generation wrapper now
recursively re-enables parent ACL inheritance after a successful Windows run,
preventing the inaccessible-build-artifact state from recurring. `/generated/`
was already rooted in `.gitignore`; no private or ROM-derived file was staged.

## 2026-07-21 — 100% demanded static-variant closure

The starting point for this milestone was the freshly regenerated Python
profile: 3,464 exact AOT variants and three LLE-only variants. The earlier
3,425/3,467 release number remains historical evidence for 0.0.1, not the
current analyzer result. Each of the final three variants was traced back to
the supported USA v1.0 ROM and checked against the H4 and Yoshifanatic
disassemblies as research references; no research code, comments, or assets
were copied.

`$80:85E4` was ordinary reachable code blocked by an unusual stack contract.
Its JSR at `$80:85E8` enters `clear_full_wram` at `$80:8E7F`. That routine pops
and saves the JSR return, clears WRAM, resets S, then executes `JMP ($0032)` at
`$80:8EB8` to the saved continuation at `$80:85EB`. The DKC2 configuration now
marks the call as terminal to its lexical block and declares the indirect jump
as a one-target `ptrtail_popcall`. This exposes `$80:85EB` as a separate exact
AOT continuation and explains why the final denominator is 3,468 rather than
the previous 3,467.

The other two gaps were poisoned by real bugs in the original program. The JSR
at `$B3:BC20` enters `$B3:F289`, documented inside data, while the same-bank JSR
at `$BA:9C36` enters zero/garbage bytes at `$BA:F305` despite a preceding data-
bank change. Neither path has a truthful return state because both crash if
executed. A new generic `noreturn_jsr <site_pc16>` configuration contract tells
analysis not to decode a fictitious lexical continuation. Code generation
still performs the real JSR stack push, then enters LLE at the exact bad target.
`$BA:F305-$BA:F307` is also declared as data so it cannot become a code node.
This is distinct from `terminal_jsr`, whose specialized handlers may consume a
frame: `noreturn_jsr` always preserves the frame for the interpreter fault path.

The shared change was made in both analysis implementations and in the Python
emitter. Synthetic tests cover configuration parsing and mutual exclusion,
fallthrough suppression, JSR-frame preservation, and the emitted LLE tail.
Results:

| Gate | Result |
| --- | --- |
| Python shared-engine tests | 357/357 pass |
| Rust analyzer tests | 44/44 pass |
| Python private regeneration | 3,318 roots; 3,468 AOT; 0 LLE-only (447.84 s) |
| Rust private regeneration | 3,318 roots; 3,468 AOT; 0 LLE-only (13.710 s analysis, 39.51 s full generation) |
| Optimized DKC2 build | Release compile confirmed `-O3` |
| Complete DKC2 CTest suite | 32/32 pass in 64.17 s |

The private manifest confirms compiled bodies for `$80:85E4`, `$80:8E7F`, and
the newly separated `$80:85EB` continuation. The two glitch callers are also
compiled and contain explicit LLE demand edges to their non-code targets;
`$BA:F305` is absent as a code node. The 32-test gate includes supplied-ROM and
desktop smoke tests, sprite regression hashes, rewind/video tests, and the
12,000-frame/two-attract-cycle acceptance run.

“100%” here means every exact CPU-mode code variant demanded by the stabilized
whole-program graph is AOT-emitted. It does not mean the game is decompiled to
readable source, that every ROM byte is executable code, or that the interpreter
should be deleted. Runtime exact-width misses still fail safely into LLE, and
the two dormant original-game crash destinations intentionally remain faithful
interpreter paths. Generated ROM-derived C, manifests, binaries, saves, and
captures remain private and ignored.

## 2026-07-21 — OpenGL presenter and opt-in PSXRecomp CRT color model

The milestone began from the passing 32-test optimized build and the existing
atomic GDI presenter. `recomp-ui` already persisted `renderer`,
`texture_filter`, and `screen_kind` settings, but the DKC2 host did not consume
them. The desktop target already linked SDL/OpenGL transitively, so no second
settings UI or external binary dependency was needed.

The requested reference implementation was inspected at PSXRecomp revision
`d7815862e18ef939e5e6e5c6947f8c29667982d5`, the gitlink pinned by
MegaManX6Recomp. Its `runtime/src/color_lut.c` and
`runtime/include/color_lut.h` were byte-identical at revision
`d2006e02a3001495b1eedf2c1cc965d23c0de38f`, the gitlink pinned by
Tomba2Recomp. This upstream feature is a 32,768-entry BGR555-to-RGB888
colorimetric lookup table, not a scanline shader. It models phosphor primaries,
display gamma, luminance, and black floor for Raw/CRT/Composite/Trinitron
screen models.

The pinned SNESRecomp submodule already contained a present-time color-LUT
module. Rather than retain a duplicate DKC2 copy, the implementation extends
`snesrecomp/runner/src/snes/color_lut.{c,h}` with PSXRecomp's exact four screen
models and a programmatic selection API. `third_party/psxrecomp_color_lut/`
records both exact framework revisions, the locally inspected
JRickey/gba-recomp lineage revision, all local adaptations, and the complete
PolyForm Noncommercial 1.0.0, MIT, and Apache-2.0 license texts. Matching
standalone notices are included inside the submodule for an eventual upstream
SNESRecomp change. No shader, game asset, ROM data, or generated output was
imported.

`runner/desktop_filter.c` is the thin DKC2 adapter around that shared module.
Raw returns the original pixel pointer, avoiding even a copy. An opted-in model
quantizes the completed
BGRX8888 frame to the SNES five-bit channel domain, looks up transformed RGB,
and writes a host-only scratch frame while preserving the unused fourth byte.
The filter runs only at presentation: PPU state, snapshots, SRAM, raw PPM
exports, and deterministic frame hashes continue to use the untouched core
frame. `DKC2_SCREEN={raw,crt,composite,trinitron}` provides a process-local
diagnostic override without changing `launcher.cfg`; invalid names fail
explicitly.

`runner/desktop_present_gl.c` creates a double-buffered WGL context on the
existing Win32 window, uploads the completed BGRX texture, applies the selected
nearest/bilinear sampling mode, draws into the same centered 4:3 viewport used
by GDI, and swaps once per completed frame. The implementation deliberately
uses the Windows OpenGL 1.1 fixed-function surface because the requested CRT
effect is already applied by the authoritative upstream LUT; a second shader
would create conflicting behavior and raise the driver floor unnecessarily.
If OpenGL initialization fails, the host destroys and recreates the HWND
before entering GDI because `SetPixelFormat` is permanent for a window. The
diagnostic `DKC2_DESKTOP_REQUIRE_GPU=1` makes that fallback an error, while
`DKC2_DESKTOP_FORCE_GDI=1` exercises it explicitly. GDI retains its one-BitBlt
atomic publish and now honors nearest/bilinear scaling as COLORONCOLOR/HALFTONE.

The shared ImGui Display card now drives the real host settings. Defaults are
OpenGL, nearest, and Raw, making CRT strictly opt-in. Saved configurations gain
`Renderer`, `TextureFilter`, and `ScreenKind`; the old `LinearFilter` key still
migrates to the new texture setting. Runtime performance logs identify the
active backend and screen model. They measure main-thread presentation time,
not hardware GPU duration; GPU timestamp queries remain unimplemented and are
reported as unavailable.

Synthetic coverage verifies the shared 4:3 viewport, filter range/names and
diagnostic parser, invalid rejection, byte-exact Raw pointer bypass, visible
CRT conversion with fourth-byte preservation, nearest GDI publication, and
the HALFTONE GDI path. Private integration gates run 60 hidden frames with CRT
while requiring OpenGL and another 60 while forcing GDI. Both exit cleanly.

The first complete post-change CTest invocation had 34 product tests pass but
failed the two Python comparison launchers because the CMake refresh had
cached MSYS Python, which prepended a POSIX build path to Windows absolute
script paths. Reconfiguring the existing build with the bundled native Windows
Python fixed the tool boundary; both isolated comparisons then passed. The
pre-refactor clean gate passed all 36 tests in 64.88 seconds, including the
12,000-frame/two-attract-cycle gate, sprite/state hashes, PCM comparison,
OpenGL+CRT, GDI+CRT, rewind, and hardware probes.

The duplicate-free shared-module refactor then rebuilt cleanly under the same
optimized configuration. Focused unit and private integration checks passed
3/3: exact Raw/CRT/Composite/Trinitron output, required OpenGL+CRT, and forced
GDI+CRT. The final complete gate passed all 36 tests in 64.19 seconds, including
the 12,000-frame/two-attract-cycle gate and every private hardware/reference
probe.

Automated evidence proves initialization, conversion, presentation, fallback,
and unchanged deterministic core behavior. It cannot judge subjective CRT
appearance or monitor-dependent scaling. A visible Raw-versus-CRT play test on
the user's display remains the perceptual acceptance step. Scanlines,
curvature, bezels, phosphor persistence, and multi-pass signal simulation are
explicitly outside this initial color-model milestone.

## 2026-07-21 — SDL2 cross-platform gameplay foundation

Portability began without replacing the proven Windows host. The audit found
that generated DKC2 C, the SNESRecomp runtime/hardware model, game adapter,
verified-ROM loader, frame buffer, screen-color filter, input router, rewind
ring, and FPS counter were already platform-neutral. The remaining playable
wrapper was coupled to HWND/WGL/GDI, waveOut, XInput, and Windows performance
counters. Recomp-ui already supplied an SDL2/OpenGL launcher platform with
Linux file-picker helpers and macOS executable-path handling.

`runner/desktop_launcher.{c,h}` now owns the launcher defaults, persistent
settings, ROM-path cache, exact game identity, two-player cards, and optional
renderer labels. The Windows host consumes this module instead of carrying a
second implementation. `runner/desktop_viewport.{c,h}` similarly replaces a
RECT-specific 4:3 calculation with a small integer rectangle used by GDI, WGL,
and SDL. Synthetic viewport coverage checks 16:9 pillarboxing, square-window
letterboxing, exact 4:3 output, and invalid dimensions.

The new `dkc2_snesrecomp_sdl` target uses SDL2 for a resizable high-DPI-aware
window, an accelerated streaming ARGB8888 texture, centered 4:3 output,
nearest/bilinear selection, keyboard events/state, two hot-pluggable SDL Game
Controllers, a monotonic performance counter, and queued exact-format 32,040
Hz signed-16 stereo. It retains the existing Raw/CRT/Composite/Trinitron
present-time filter, Player 1/2 routing and deadzones, SRAM and backup
behavior, F5/F9 slot 0, 3x fast-forward, bounded in-memory rewind, exact
fractional audio production, and once-per-second FPS title. The executable is
named `DKC2RecompSDL.exe` beside the accepted Windows binary and
`DKC2Recomp` on non-Windows systems.

CMake first accepts a system SDL2 config package. When none exists and
`DKC2_FETCH_SDL2` remains enabled, it fetches the pinned SDL release-2.30.9
source. A clean environment can therefore reproduce the tested dependency
revision without requiring a machine-specific `SDL2_DIR`; offline/system-only
builders can disable the fallback. The existing submodule pins remain the
authoritative SNES runtime and UI dependencies.

The shared recomp-ui launcher exposed one Windows portability defect during
the new target's clean MSVC compile: its GCC/Clang extended-assembly compiler
barrier was unconditional. The submodule now selects MSVC's
`_ReadWriteBarrier()` and retains the original GNU barrier elsewhere. No UI
behavior or assets changed. This isolated framework fix is suitable for an
upstream recomp-ui change.

`scripts/generate_snesrecomp.py` mirrors the PowerShell private generation
contract using portable Python: exact size/SHA-256 verification, optional
native analyzer rebuild through Cargo, synchronized `funcs.h`, and private
emission under ignored `generated/snesrecomp/`. A synthetic test pins decimal
and hexadecimal argument parsing plus size rejection without including any
game data. Windows retains the PowerShell wrapper and its ACL repair because
that is a Windows-specific artifact concern.

Verification completed on the available Windows machine:

| Gate | Result |
| --- | --- |
| MSVC Release `dkc2_snesrecomp_sdl` | passed; `DKC2RecompSDL.exe` produced |
| MSVC Release existing Win32 host | passed; `DKC2Recomp.exe` preserved |
| Hidden SDL private-ROM lifecycle | passed 180 frames with video/audio, 3x fast-forward, and a real rewind restore |
| Existing hidden Win32 lifecycle | passed 180 frames |
| Shared GDI/WGL presenter unit test | passed |
| Portable viewport unit test | passed |
| Portable generator synthetic test | 2/2 passed |

The final optimized all-target build completed successfully, and the complete
configured product suite passed 39/39 tests in 83.22 seconds. That gate
includes both desktop hosts, required OpenGL and forced-GDI CRT paths, the SDL
audio/video/input/rewind lifecycle, sprite/state reference hashes, the full
12,000-frame two-attract-cycle run, PCM and trace comparison, interpreter
regressions, and every public hardware/unit probe.

Linux is not installed on the available machine and no macOS host is
available. Those platforms are therefore source-supported candidates, not
claimed releases. `docs/CROSS_PLATFORM.md` records native compile, full-suite,
visible-attract, keyboard/controller, audio, persistence, and packaging gates
that must pass before either checkbox is closed. Platform user-data locations,
Linux packaging, and macOS application bundling/signing remain explicit
follow-up work rather than guessed implementations.

## 2026-07-21 — Crash reports and privacy-allowlisted diagnostic bundles

The playable Win32 and SDL hosts now initialize one project-owned diagnostics
adapter after anchoring their relative paths beside the executable. It combines
the shared SNESRecomp host report—build/OS/SDL/module metadata, breadcrumbs,
fatal state, and Windows minidump support—with DKC2's host name, outcome, last
frame and resume PC, presenter, screen model, and audio availability. Normal
shutdown atomically refreshes `diagnostics/last_run_report.json`; a fatal
runtime exit also creates one timestamped support folder immediately.

Windows installs an unhandled-exception filter that asks the framework to
write a minidump before producing the rolling report and bundle. POSIX signal
handlers cannot safely allocate, enumerate modules, or format JSON, so they
write only `pending_crash_signal.txt` with signal-safe calls and terminate.
The next launch recognizes that marker, completes a normal support bundle, and
removes it. `DKC2_DIAGNOSTIC_BUNDLE=1` provides the same folder on clean exit
for problems that do not crash.

Bundle creation is allowlist-based. It writes `report.json` and `README.txt`
and may copy only `launcher.cfg`, `performance.log`, and a Windows minidump.
It never scans for or copies `rom.cfg`, a ROM or ROM path, private generated C,
SRAM, file states, screenshots, or audio. The JSON records those exclusions,
while the README warns that loaded-module paths and machine details are
present and should be reviewed before sharing.

Both game loops update the diagnostics heartbeat and call the framework's
existing controlled crash hook. A new private PowerShell harness launches the
hidden SDL host in three modes, parses the JSON, validates the outcome and
privacy declarations, rejects every non-allowlisted file, and requires a
non-empty minidump for the Windows exception case. All three registered CTest
drills passed: clean requested bundle, `Die()` fatal exit at frame 119, and a
real access violation at frame 119. Both MSVC Release playable hosts rebuilt
successfully after integration. The final complete configured gate passed
42/42 tests in 118.94 seconds, including both desktop hosts, all three
diagnostic drills, the two-cycle attract run, reference hashes, PCM/trace
comparisons, filters, rewind, and every hardware/unit probe.

Slot-0 state writes now use `saves/dkc2s0.sav`, which makes the game/slot
boundary explicit. Both hosts try that filename first and then load the former
`saves/dkc20.sav` path as a compatibility fallback. SRAM remains
`saves/save.srm`; no hypothetical mod directory or save migration was added.
Mod-aware save isolation is deliberately deferred until a versioned mod
manifest and loader can provide stable identities.

## 2026-07-21 — Confirmed launcher Restore Defaults action

The shared recomp-ui C ABI now accepts an optional pointer to a complete
host-owned default-settings snapshot. The launcher copies that snapshot into
its view model during initialization and, only when it is present, shows a
fixed **Restore Defaults** button in the Settings footer. The button opens a
confirmation dialog explaining the scope. Cancel leaves the working copy
unchanged; confirmation atomically replaces the full
`RecompLauncherCSettings` value. ROM selection, SRAM, and file save states live
outside that value and are not modified.

DKC2 constructs the snapshot through `Dkc2LauncherSettingsDefault`, the same
function used before loading `launcher.cfg`. This gives first-run and reset
behavior one source of truth: 3x window scale, OpenGL, nearest sampling, Raw
screen model, 32,040 Hz audio at 100%, Player 1 keyboard, Player 2 gamepad,
24% deadzones, and launcher-on-boot. Both the Win32 and SDL executables rebuilt
successfully with the additive ABI field.

A synthetic model test changes display, audio, controller, deadzone, and
skip-launcher values; verifies that Cancel is lossless; confirms that Restore
Defaults replaces the entire settings structure; proves the selected ROM path
survives; and verifies that hosts which provide no snapshot expose no action.
Scripted 1100x840 launcher renders were inspected for the Settings footer and
confirmation dialog: the button is clear, the modal text fits, and neither
overlaps Play or the settings cards. The final complete configured gate passed
43/43 tests in 123.84 seconds, including the new launcher-default regression,
both playable hosts, all crash drills, and the two-cycle attract reference.

## 2026-07-22 — Append-only numbered test versions

User-testable builds are now separated from the shared compiler output.
`scripts/create_windows_version.ps1` inspects the ignored `versions/` root,
chooses one greater than the highest existing `Version NN` directory, stages
through a temporary sibling, audits the complete contents, and atomically
renames the result. It refuses an explicitly requested sequence that already
exists, so an older handoff cannot be overwritten accidentally.

Each version contains only `DKC2Recomp.exe`, `DKC2RecompSDL.exe`, the nine
allowlisted launcher assets, README/changelog/licenses, and `VERSION.txt`.
The manifest records the sequence, UTC creation time, branch, commit, dirty
state, and both executable SHA-256 hashes. ROMs, SRAM, file states, generated
code, diagnostics, local configuration, logs, dumps, screenshots, audio, and
unrelated executables are rejected. The source repository and large CMake
build tree remain single copies; Git and the manifest identify their source
state while numbered folders preserve only practical testable snapshots.

A synthetic packaging regression creates two snapshots and proves they are
named `Version 01` and `Version 02`, then requests sequence 2 again and requires
an overwrite refusal. Temporary fake binaries/assets are removed after the
test.

The first pre-publication audit caught two invalid PowerShell line breaks in
the packager and its synthetic test before either was committed or used for a
handoff. Both were corrected to valid PowerShell continuations, and the test
was rerun rather than treating the design-only review as execution evidence.
`docs/BUILD_HYGIENE.md` now designates `build-snesrecomp/` as the one routine
compiler workspace, classifies the older investigation trees, and reserves
`versions/Version NN/` for manual-test handoffs.

The corrected packaging regression passed. A final complete configured gate
then passed 43/43 tests in 123.7 seconds, including both playable hosts, the
two-cycle attract run, both CRT presenter paths, diagnostics, launcher reset,
and the new snapshot test. Normal snapshot creation now refuses tracked,
untracked, or dirty-submodule changes unless `-AllowDirty` is explicitly used;
the manifest separately records the semantic project version and numbered
snapshot sequence without leaking an absolute local build path.

## 2026-07-22 — Pirate Panic route-test harness

Milestone 3 now has a deterministic first-level test boundary instead of only a
manual note. The desktop host's developer input recorder now writes the full
two-player 24-bit controller word per emulated frame, preserving Player 2 while
remaining compatible with the existing low-12-bit Player 1 recordings.

`runner/input_playback.{c,h}` owns the replay parser. It accepts one hex mask
per frame, ignores comments and blank lines, supports decimal repeat counts,
and returns neutral input after EOF. A synthetic unit test covers ordinary
frames, repeated frames, packed two-player input, and invalid oversized masks.

The headless host now consumes the shared parser and reports
`pirate_panic_stats` on every run. The telemetry uses the independently
rebuilt v1.0 map only as labels: Pirate Panic is level `$0003`,
`level_number` is direct-page `$00D3`, `parent_level_number` is `$08A8`,
`level_destination_number` is `$059D`, and the normal per-file progress flags
are `$08C2/$08C4`. The route gate counts entry, active frames, completion-flag
changes while Pirate Panic is active, and level-exit transitions.

`scripts/test_pirate_panic_route.ps1` replays a private recording through the
headless executable and fails if the run does not enter Pirate Panic, remain in
the level for a meaningful duration, change completion flags, trigger an exit,
complete the requested frame count, or keep audio unclipped. CMake exposes this
as `supplied_rom_pirate_panic_route` only when both `DKC2_ROM` and the private
`DKC2_PIRATE_PANIC_INPUT` cache path are configured.

Verification performed during this step:

| Gate | Result |
| --- | --- |
| MSVC Release `test_input_playback` build | passed |
| MSVC Release `dkc2_snesrecomp_headless` build | passed |
| `ctest -R input_playback` | passed |
| 300-frame private neutral-input headless smoke | passed; `pirate_panic_stats entered=0` printed as expected |

A private 11,275-frame Pirate Panic entrance-to-goal recording was subsequently
captured and replayed. It enters Pirate Panic at frame 1,267, keeps the level
active for 10,009 frames, observes 17 completion-flag changes and three exit
transitions, completes the requested frame count, and reports zero clipped
audio samples.

The route is not accepted yet. At frame 5,522 the replay reaches the indirect
dispatch at `$BA:B33F` and emits both `[interp_cap]` and
`[unresolved-abandon]`; the latter records that handler side effects were
skipped. The gate now rejects either diagnostic. Work-in-progress
configuration declares the dispatch and requests an M1/X0 analyzer variant,
and a matching analyzer experiment seeds configured forced variants as roots.
That experiment passes the analyzer unit suite but produces structurally
poisoned M1/X0 targets because the dispatch destination decodes in M0/X0. It
is preserved only as a pre-upstream-rebase checkpoint, not as a completed
fix. Roadmap #2 remains open.

A later reference pass should replay the same input against Snes9x/snesref and
compare event, frame, memory, and audio checkpoints after the dispatch is
modeled without skipped side effects.

## 2026-07-27 — SNESrecomp upstream rebase

Direct push access to `mstan/snesrecomp` was unavailable, so
`Nicktendonick/snesrecomp` was created as the integration fork. The original
remote is retained as `upstream` inside the submodule and the fork is `origin`.
The DKC2 repository's submodule URL now uses the fork so its pinned
DKC2-specific revisions remain fetchable.

Before rewriting history, both repositories were published to dated safety
branches named `codex/backup-pre-snesrecomp-rebase-20260727` for DKC2 and
`codex/backup-pre-upstream-rebase-20260727` for SNESrecomp. The latter pins
pre-rebase revision `19ac90e`.

The two commits unique to the DKC2 SNESrecomp branch were rebased from their
old `cfa8e56` base onto upstream `1d0f2e0`. One content conflict occurred:
upstream and DKC2 had added different parser tests at the same location in
`tests/v2/test_cfg_loader.py`. Both additions were independent, so the
resolution retained all four tests. The initial rebased branch pinned
`f246ff4`.

Validation completed inside the submodule:

| Gate | Result |
| --- | --- |
| Python v2 suite | 364/364 passed |
| Rust analyzer suite | 50/50 passed |

Private regeneration then succeeded with 3,472 exact variants: 3,470
AOT-eligible and two LLE-only forced-variant experiments. The first MSVC link
exposed two upstream integration gaps:

1. current generated `dispatch_v2.c` and `cpu_state.c` both supplied the
   RAM-routine guard globals because upstream's default table was weak only on
   GCC/Clang; and
2. the standalone interpreter-bridge harness did not fake the newly queried
   `cx4_irq_pending` symbol.

The fork now gates the fallback guard definitions behind
`SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS`, which all three generated DKC2 hosts
define. Standalone/legacy clients keep the fallback. The bridge harness
supplies a neutral Cx4 IRQ fake, consistent with its other synthetic hardware
dependencies. These changes are committed in the submodule at `a4ec65d`.

MSVC Release then produced fresh headless, Win32, and SDL executables. The
configured suite passed 44/45 tests. Startup, both desktop smoke paths, both
CRT backends, diagnostics, two complete attract cycles, audio comparison,
input, launcher, rewind, presenter, bridge, and private hardware probes pass.
The sole failure is the exact sprite-reference checkpoint: at frame 3,309 the
new upstream guest-frame/APU coupling produces different frame, WRAM, VRAM,
and OAM hashes. CGRAM still matches, live OAM matches its WRAM source, the
12,000-frame run completes two ordered cycles, and audio remains unclipped.
The remaining 44 configured tests also pass together when that single known
reference checkpoint is excluded.
The old trusted hashes were deliberately not replaced. A new event-aligned
Snes9x comparison is required to distinguish a shifted checkpoint from a
runtime divergence.

The pre-existing forced-variant experiment remains labeled WIP. Rebase and
build success do not resolve the Pirate Panic `$BA:B33F` dispatch or close
Roadmap #2.

## 2026-07-27 — in-game overlay and gated save states

The shared recomp-ui launcher is a pre-boot window and destroys its ImGui
context before gameplay. The new in-game menu therefore creates a separate
gameplay-lifetime ImGui context rather than trying to keep the launcher event
loop alive. The same vendored ImGui core and OpenGL backend already supplied
by the recomp-ui submodule are reused; no additional UI library or copied
third-party backend was added.

`runner/desktop_overlay_model.{c,h}` is the host-neutral policy boundary. It
owns menu visibility, the opt-in Assist Tools flag, a bounded/wrapping
five-slot selector, and one-shot Resume, Quit, Save, and Load actions.
Save/load requests are rejected by the model when assists are disabled.
`runner/desktop_overlay.cpp` renders Main, complete Settings, Assist Tools /
Cheats, two-player Controls, and Credits pages and adapts SDL events or the
small Win32 input subset required by ImGui.

The Windows OpenGL and SDL gameplay presenters now submit the overlay after
the completed game quad and before the same buffer swap. The SDL presenter
uses an OpenGL 2.1 compatibility context so it can share recomp-ui's existing
OpenGL renderer. While the overlay is open, both hosts:

- stop scheduling SNES frames at the completed frame boundary;
- suppress the packed game-controller word;
- clear or pause queued host audio;
- continue presenting the last completed game image behind the menu; and
- reset wall-clock/audio anchors before resuming.

Assist Tools default off and persist as `AssistTools` in `launcher.cfg`.
Enabling them unlocks the existing rewind, fast-forward, and five file-state
paths. The overlay's Save/Load buttons call the same host action as F5/F9.
Slot selection wraps across 0–4, while the UI labels it 1–5. DKC2 sets
SNESrecomp's `save_name_prefix` to `dkc2s` and delegates path construction to
`RtlSaveSlotPath`, producing `saves/dkc2s0.sav` through
`saves/dkc2s4.sav`; only the first slot probes legacy `saves/dkc20.sav`.
State format, audio reset, and rewind-history repair remain single-sourced.
An enabled run discloses `(Assist Tools: On)` in the window title.

The GDI compatibility presenter remains a minimal emergency fallback. It has
no ImGui renderer, so Escape keeps its Quit behavior there; an Assist opt-in
made in the pre-boot launcher still enables its keyboard shortcuts on the
first slot.

The pre-boot launcher uses additive generic recomp-ui fields for an optional
Assist Tools page and host-owned Credits text. DKC2's full settings value now
flows through the launcher and overlay instead of keeping a second
project-only Assist structure. Volume, texture filtering, screen model,
controller source/deadzone, and Assist policy apply live. Resource-owning
window scale/fullscreen/renderer/audio choices and startup-only
`SkipLauncher` persist for the next run. The shared sample-rate choice is
mirrored and persisted but does not falsely change playback speed: DKC2
continues to consume the native 32,040 Hz S-DSP stream until a tested host
resampler exists. Restore Defaults replaces the entire shared value and
immediately closes the Assist gate.

During the final review, official SNESrecomp `main` advanced from the rebased
base `1d0f2e0` to `2dd1dc7`. The new upstream commit is an opt-in `.snesmod`
package and trusted-plugin runtime and is directly relevant to the later mod
manifest/hook ABI milestone. It is deliberately not folded into this UI
checkpoint: using it requires a coordinated recomp-ui Mods-ABI update and a
separate backup/rebase/build gate. Existing `RtlSaveSlotPath` was adopted now
to avoid duplicating upstream slot naming.

Verification:

| Gate | Result |
| --- | --- |
| MSVC Release build | passed for Win32, SDL, headless, and all unit targets |
| Overlay policy unit test | passed; five-slot wrap/clamp, default-off gate, one-shot save action, and Resume behavior |
| Hidden Win32 OpenGL lifecycle | passed; opened/rendered for 30 host ticks, paused, closed, resumed, then completed rewind/fast-forward smoke |
| Hidden SDL/OpenGL lifecycle | passed with the same overlay/pause/resume sequence |
| OpenGL CRT and forced-GDI CRT focused checks | passed before the active-overlay smoke was added |
| Complete configured CTest run | final pass 45/46; only the already-documented frame-3,309 post-rebase reference mismatch remains. An earlier launcher display-name expectation typo was corrected and now passes |

No ROM, save state, screenshot, or generated game binary was added to source.
Visible keyboard/mouse/controller usability still requires a human pass; the
hidden gates prove lifecycle and rendering completion, not visual taste or
every navigation device.

The append-only manual handoff was created as `versions/Version 02`. Its
manifest marks the source dirty because this requested feature has not yet
been committed; it contains only the two Release executables, launcher assets,
notices, documentation, and hashes.

After completing shared settings, five slots, pre-boot Assist/Credits, and
two-player overlay controls, a new append-only manual handoff was created as
`versions/Version 03`; Versions 01 and 02 were not modified. Its manifest also
marks the source dirty because this checkpoint remains uncommitted. Scripted
real-framebuffer captures of both new pre-boot pages completed at 1100×840 and
were inspected for readable layout; those temporary validation PNGs remain
outside the package and source history.

A final read-only list of the hotkeys the DKC2 hosts actually implement was
then added to the pause Settings page. The hosts and all 46 tests were rebuilt
and rerun; the result remained 45/46 with only the same known frame-3,309
reference mismatch. Because Version 03 is immutable, the exact final binaries
were packaged separately as `versions/Version 04`.

## 2026-07-27 — launcher box-art repair and personal test bundles

The reported box-art glitch was reproduced in a real 1100×840 launcher
framebuffer capture. The `boxart.tga` SHA-256 was identical in Versions 02,
03, 04, and the canonical build, ruling out asset corruption. The expanded
Settings, Assist Tools, and Credits navigation wrapped Settings onto a second
header row. That reduced the dashboard body height enough for ImGui to add a
striped vertical scrollbar immediately beside the box-art card, which made the
art appear corrupted or unstable.

The shared ImGui launcher now positions the right-side navigation group in
window-local coordinates and restores both cursor axes afterward. A corrected
1100×840 capture shows the complete brand/title, all three navigation buttons
on one row, no scrollbar beside the art, and an intact cover texture.

Normal `versions/Version NN` handoffs remain ROM/save-free. A new source-only
`scripts/create_personal_test_version.ps1` helper creates a second, explicitly
private copy outside the repository. It verifies the exact USA v1.0 ROM hash,
writes a relative `rom.cfg`, optionally copies the selected saves and launcher
configuration, refuses in-repository destinations, and refuses overwrites.
This makes repeated manual testing convenient without weakening the public
source/release boundary.

Both Win32/OpenGL and SDL/OpenGL Release hosts were rebuilt with the layout
repair. The complete configured suite finished 45/46: all launcher, desktop,
audio, input, save, rewind, presentation, and two-attract-cycle gates passed;
only the existing post-rebase frame-3,309 reference hash mismatch remained.

The normal append-only handoff is `versions/Version 05`. The personal copy is
`..\DKC2 Personal Test Builds\Version 05`; it contains the hash-verified ROM,
the current `saves` folder, and current `launcher.cfg`. A hidden 60-frame run
launched from that external folder, resolved the relative ROM path, rendered
through OpenGL, and exited cleanly with SRAM writing disabled so the copied
saves were not changed.

## 2026-07-28 — runtime-consumed pre-boot input remapping

DKC2 had set `GameInfo.hide_rebind = 1` because both desktop hosts still read
hard-coded keyboard and controller layouts. Removing that flag alone would
have exposed recomp-ui's generic editor while leaving gameplay unchanged. The
launcher ABI therefore gained opt-in settings-owned binding arrays and a
host-supplied Assist action catalog. Games that do not advertise
`settings_bindings` retain their existing binding stores and UI.

DKC2 now seeds and persists complete Player 1/2 keyboard and standard gamepad
maps in `launcher.cfg`. Every SNES action has Keyboard and Controller chips on
the Controller Configure page. A compact global row on that page exposes
Rewind and Fast-forward; the top-level Assist Tools page exposes Rewind,
Fast-forward, Save State, and Load State. Each chip captures a key, standard
controller button, or signed axis. Per-player Reset and Reset Assist Controls
copy the same host-owned defaults used on first launch.

The Win32 and SDL hosts both consume these mappings. SDL evaluates its native
scancode state; Win32 translates the same scancode vocabulary to focused
virtual-key state. Standard-controller bindings share one button/axis
encoding, including trigger axes. The existing Assist policy mask remains
authoritative, so customized Assist inputs do nothing while the gate is off.

Synthetic model/input tests cover keyboard capture, controller capture,
per-player reset, Assist capture/reset, keyboard mapping, gamepad mapping, and
Assist action mapping. Scripted 1100×840 framebuffer captures verified the
complete Controller layout and Assist page. A hidden 60-frame private-ROM run
resolved the mappings in the Win32 host, exited cleanly with SRAM disabled,
and wrote all Player 1/2 and Assist binding keys to `launcher.cfg`.

The complete Release build succeeded. The final configured regression result
was 45/46: both desktop lifecycle tests, both focused mapping/model tests, all
audio/input/save/rewind checks, and the two-cycle attract test passed. The
only failure remains the previously documented post-rebase frame-3,309
reference hash mismatch; this input feature did not change its observed hash.

The immutable public-safe handoff is `versions/Version 06`. Its matching
private test copy is `..\DKC2 Personal Test Builds\Version 06`, containing the
verified ROM, current saves, and a `launcher.cfg` with all new binding entries.
A hidden 60-frame run from the private folder resolved its relative ROM,
rendered through OpenGL, verified the copied binding keys, and exited cleanly
with SRAM writes disabled.

## 2026-07-28 — in-game Controls binding editor

The pause overlay's Controls page previously exposed only Player 1/2 input
source and deadzone. It now edits the same settings-owned bindings as the
pre-boot launcher. Nested Player 1 and Player 2 pages expose all 12 logical
SNES actions with independent Keyboard and Controller chips; the Assist page
exposes Rewind, Fast-forward, Save State, and Load State. A Fixed Shortcuts
page documents Escape, Guide/Start+Back, and F without making those recovery
and diagnostic inputs remappable.

The existing `RecompLauncherCSettings` arrays remain the sole source of truth.
An accepted pause-menu capture therefore applies to the running host on the
next settings synchronization and is saved to `launcher.cfg` on clean exit.
Player and Assist reset buttons copy only their matching binding arrays from
`Dkc2LauncherSettingsDefault`; Restore All Settings retains its broader
behavior.

`Dkc2DesktopOverlayModel` now validates and owns the lifetime of one active
binding capture. Escape cancels capture before it can close the overlay, and
closing or resuming cancels any unfinished capture. Both hosts pass the first
connected controller's full button/axis state to the overlay. Controller
capture must see a neutral poll before accepting a button or signed axis, so
the button used to enter the editor cannot bind itself. ImGui navigation is
released while capture is active so the newly selected input cannot also
activate another widget. The Win32 keyboard adapter now distinguishes
left/right modifiers and keypad Enter and evaluates keypad, lock, print,
pause, and GUI-key scancodes that its editor can capture.

Synthetic overlay-model coverage now checks valid and invalid targets, neutral
controller arming, explicit cancellation, and automatic cancellation on menu
close. The focused overlay, launcher-default, and desktop-input tests passed,
and both Release gameplay hosts rebuilt successfully. The complete configured
suite remained 45/46: both hidden OpenGL overlay lifecycles and the
two-attract-cycle gate passed; the only failure was the unchanged,
already-documented frame-3,309 reference hash mismatch
(`27601b1b...` observed versus `52e2b6bf...` expected).

Per the request, this change was left in the current working build and no new
numbered public or personal test version was created.

## 2026-07-28 — Alpha Pre-Release title and Version 07 publication

The pre-boot launcher and both gameplay hosts now share the product title
`DKC2 Recomp Alpha Pre-Release`. FPS, optional CPU telemetry, transient
save/load status, and `(Assist Tools: On)` are appended to that base title
rather than restoring the former `DKC2Recomp v0.0.1` text. The internal
project version remains available to diagnostics and package manifests.

The settings-owned launcher bindings required a fetchable recomp-ui revision.
With the owner's explicit permission, `mstan/recomp-ui` was forked to
`Nicktendonick/recomp-ui`. The complete additive launcher capability work was
committed as `0b1ac7f` on `codex/dkc2-launcher-bindings`; the DKC2 submodule
URL and gitlink now use that integration fork while upstream review is
pending. No code was squashed into the DKC2 repository.

The complete MSVC Release build succeeded. The configured regression suite
remained 45/46: both playable-host lifecycle tests, CRT OpenGL/GDI tests,
diagnostics, input/overlay models, and the two-attract-cycle test passed. The
only failure was the unchanged frame-3,309 reference mismatch
(`27601b1b...` observed versus `52e2b6bf...` expected).

The requested append-only public-safe handoff is `versions/Version 07`; its
matching external personal test copy contains the verified ROM, current saves,
and launcher settings. Neither private content nor generated ROM-derived code
is part of the Git commit or GitHub publication.

## 2026-07-28 — experimental 16:9 foundation and Pirate Panic inspection

The new video contract keeps authentic mode at 256x224 and adds an opt-in
342x224 mode. Forty-three PPU source columns are added symmetrically; applying
the SNES 7:6 pixel aspect ratio produces a 1.78125 display aspect, within one
source pixel of exact 16:9. Win32/GDI, Win32/OpenGL, and SDL/OpenGL now accept
the active source width and compute their viewport from that geometry.
Framebuffer/filter/export allocations use the maximum width but process only
the active pixels.

The shared launcher setting defaults off, persists as `Widescreen=0/1`, and is
visible as **Widescreen 16:9** before boot. The pause overlay adds
**Widescreen (16:9, experimental)** to its Settings page. A live change clears
both host frame buffers, updates the PPU pitch and Windows bitmap width, and
recomputes presentation aspect. The same state is implemented in the portable
SDL host.

DKC2's generated code is still private and disposable. Independent symbol and
instruction analysis identified the common placement-radius function at
`$BB:BB07` and both paths of the world-sprite renderer beginning at
`$B5:9FC9`. Widescreen adds 43 to the placement/render left allowance and 86
to each total horizontal span; authentic mode returns the exact native table
values and `$30/$160` renderer constants. The source-owned
`apply_dkc2_widescreen_overrides.py` finds the generated units by function
name, checks unique ROM-address/trace-block anchors, inserts the calls
idempotently, and fails closed if future SNESrecomp output changes. Synthetic
fixtures cover success, idempotence, and an altered-anchor failure.

The first raw 342x224 captures exposed a real policy problem: the 32-column
title and Pirate Panic cabin tilemaps wrapped after 256 pixels. The adapter now
extends only an enabled background advertising a 64-column horizontal
tilemap. Bounded 32-column screens use SNESrecomp's centered-extra-space mode,
and every 342-pixel row is cleared first so no preceding wide frame can remain
in the sidebars. Pirate Panic's streamable deck keeps full margins and allows
BG3 foreground scenery to widen.

Trace-enabled MSVC compilation initially stopped because SNESrecomp's debug
server used `RtlApuLock`/`RtlApuUnlock` before their block-local declarations.
Moving declarations to the file's external-reference section restored both
headless and desktop trace builds without changing runtime behavior. This is
an integration-submodule change suitable for a separate upstream patch.

### TCP visual evidence

All diagnostics below remain ignored under `.cache/widescreen-captures`.
The TCP `screenshot` command reported 342x224 for every wide capture.

| Checkpoint | Result |
| --- | --- |
| Copyright screen | centered; margins black |
| Diddy's Kong Quest title | centered; former repeated title fragments removed |
| Pirate Panic cabin/dialogue | centered; former repeated room/text removed |
| Pirate Panic deck frame 4,000 | full-width composite visually continuous |
| BG1 isolation | deck, hull, mast, and foreground continuous at both edges |
| BG2 isolation | sky/ocean fill both margins without a visible stale seam |
| BG3 isolation | rigging reaches the right margin without stale host pixels |
| OBJ isolation | HUD/player/enemy composition remains coherent |
| Route frames 5,500/7,000/9,000/11,000 | no visible stale tile strip or missing layer |
| Route finish | a green enemy is visibly rendered beyond the native right edge |

A TCP scan of the complete 11,275-frame private Pirate Panic recording found
22,964 render-time OAM samples in the extra margins: 9,374 on the left and
13,590 on the right. The route entered Pirate Panic at frame 1,267, remained
active for 10,009 frames, and reached the requested frame count with active
video/audio and no clipping. It still emitted the already-known safety-tier
trap diagnostics at `$B5:E298` and `$BA:B33F`; therefore this evidence proves
the exercised widescreen presentation/object bounds, not byte-identical
whole-route correctness.

The inspected MegaManXSNESRecomp revision had no explicit root license and was
used only for architectural comparison. No source, comments, generated game
code, or assets were copied; exact provenance and the reference's own
documented widescreen limitations are recorded in
`third_party/megamanxsnesrecomp-reference.md`.

The remaining acceptance work is deliberately broad: manual normal-speed
edge behavior, every level archetype, bonuses, bosses, maps, Mode-7 and
vertical screens, special foreground effects, and automatic stale-column
detection. Widescreen remains experimental and off by default until those
routes are proven.

The final non-trace MSVC Release build completed for the headless, Win32, and
SDL hosts. The complete configured suite finished 47/48. All new widescreen
geometry, generated-override, launcher-toggle, desktop presentation, desktop
lifecycle, diagnostics, and two-attract-cycle tests passed. The only failure
was the unchanged, previously documented frame-3,309 reference checkpoint:
the authentic-mode frame remained `27601b1b...` while the stale expectation is
`52e2b6bf...`. This confirms that disabled widescreen did not move the known
4:3 result; it does not resolve that older reference-alignment task.

A final hidden optimized Win32/OpenGL lifecycle forced `DKC2_WIDESCREEN=1`,
opened and closed the pause overlay, exercised fast-forward and rewind restore,
and exited cleanly after 180 frames. Its private PPM was 229,839 bytes: a
15-byte header plus exactly `342 * 224 * 3` RGB bytes, confirming that the
final non-trace GUI executable used the widescreen surface.

The developer override is process-local. A second 60-frame hidden run forced
16:9, produced the same 229,839-byte wide PPM, exited cleanly, and left the
saved launcher line at `Widescreen=0`.

## 2026-07-28 — moving-margin correction and Version 08 candidate

The user's normal-speed video invalidated part of the preceding static
widescreen inspection. Sharp vertical pieces of unrelated level art moved
through both margins even though individual screenshots sometimes looked
plausible. A 64-column BGxSC declaration only describes address capacity:
DKC2 continually recycles those two 32-column VRAM pages. Rendering the raw
off-screen address exposed old page contents before the game streamed the new
world column. The earlier claims that the route had no stale strip and that
BG3 could be widened generically are superseded by this section.

The DKC2 adapter now registers enabled 64-column BG1/BG2 layers with
SNESrecomp's world-keyed shadow tilemap before scanout. The full camera at
WRAM `$17BA/$17C0` anchors each layer's repeating 10-bit PPU scroll phase.
The shadow captures the authentic 256-pixel viewport and associates later
game VRAM uploads with the same world coordinates. Missing cells return a
bounded tile entry rather than the renderer's raw wrapped VRAM. BG3 is
excluded because DKC2 uses it for both foreground effects and HUD/staging
content; it stays centered pending screen-specific policies.

Pirate Panic's enabled 32-column BG2 is the parallax sky/ocean, not collision
geometry. It is intentionally cyclic, so the PPU repeats its already-rendered
native scanline into both margins. BG1 remains world-keyed and is never
repeated. This fills the backdrop without reintroducing the duplicated deck
and mast chunks visible in the user's recording.

The object-path audit found no banana-only viewport gate. Level enemies,
collectibles including bananas, and placed props enter the shared placement
radius at `$BB:BB07`; active world sprites use the shared renderer paths at
`$B5:9FC9/$B5:A00E`. The existing generated-source adaptation therefore
widens spawn/despawn and render visibility for all of those classes. This is a
code-path result, not yet a manual assertion that every archetype behaves
correctly.

Synthetic coverage now checks BG1/BG2-only layer classification and scroll
phase unwrapping across the `$03FF/$0400` boundary. The TCP capture report also
records the shared runtime's per-side shadow hit/miss counters. Private route
captures at frames 4,000, 5,500, and 7,000 show continuous sky/ocean,
world-relative deck history, and no previous moving vertical page strips.
The known Release-only `$B5:E298` safety diagnostic remains present and is
unrelated to this presentation change.

The remaining acceptance gate is the user's interactive normal-speed test,
especially sustained left/right camera motion, bananas and enemies entering
both margins, death/restart, and the special BG3 foreground screens. Widescreen
remains experimental and off by default.

The final optimized non-trace Release build completed for the headless, Win32,
and portable SDL hosts. The complete configured CTest suite finished 48/49.
All new widescreen classification, scroll-unwrapping, TCP screenshot-tool,
desktop lifecycle, OpenGL/GDI presenter, SDL, and two-attract-cycle tests
passed. The sole failure is the pre-existing supplied-ROM sprite reference at
frame 3,309: the build still produces `27601b1b...` while the stored expectation
is `52e2b6bf...`.

The explicit 45,000-frame Pirate Panic entrance-to-goal gate passed with the
11,275-frame recording. It entered at frame 1,267, accumulated 43,734 active
Pirate Panic frames, observed 17 completion-flag changes and three exit
transitions, completed without an unresolved-dispatch/interpreter-cap result,
and reported zero clipped audio samples. Frames after the recording ended used
neutral input, as the gate documents.

A final hidden 240-frame optimized Win32 run forced widescreen, opened the
overlay, exercised rewind and fast-forward, disabled SRAM writes, and exited
with status zero. Static TCP captures and automated checks are now complete;
normal-speed human play remains the required acceptance test for the original
intermittent motion artifact.

## 2026-07-28 — exact BG1 prefill after Version 08 user rejection

The user's second widescreen recording and close-up screenshots rejected the
Version 08 candidate. Its shadow hit counters proved only that margin keys were
filled; they did not prove that those keys held the correct world columns.
Frames 5,500 and 7,000 contained coherent but horizontally wrong ship sections
at both authentic-view boundaries, and frame 9,000 exposed a colorful strip at
the far-right room edge. No Version 09 was packaged from that failed state.

The TCP capture helper now optionally exports the selected historical frame's
matching WRAM and VRAM images. A frame-5,499 calibration compared reconstructed
BG1 entries against all 2,048 cells of the live 64x32 tilemap. Source tile
`shadow tile - 32` matched 1,754 cells (85.6%); the next-best candidate matched
746. This exactly agrees with the cartridge routines: `$B5:ACA8-$B5:ACB7`
build a source column at camera X, while `$B5:ADF0-$B5:AE01` stages it in the
rolling page for X plus `$0100`.

The DKC2 adapter now decodes unseen 8x8 BG1 cells directly from the active
decompressed WRAM map and metatile-definition table. It preserves horizontal
and vertical metatile flips, the 65816 ASL carry used in definition addressing,
and the 36-entry-to-32-row vertical rotation performed by
`$B5:ADA9-$B5:ADD0`. The reconstructed value is stored under the camera-world
shadow key, but its source X is 32 tiles earlier. This retains compatibility
with the runtime's capture of later game-authored VRAM writes.

The remaining frame-9,000 strip was a distinct level-boundary overread.
`$0AFC` identifies the maximum horizontal scroll after the initializer removes
the native 256-pixel viewport. The game's one staged 32-pixel guard metatile is
valid, but the next metatile belongs to unrelated WRAM. The adapter now keeps
that guard and fills later cells with an all-zero 4bpp character verified from
the scene's live VRAM. Widened object placement and render bounds remain at
their cartridge values until exact terrain preparation succeeds.

Focused synthetic tests cover the decoder's normal/horizontal-flip/both-flip
paths, invalid sources, readiness-gated object bounds, and transparent 4bpp
character selection. Trace captures after the correction show continuous
terrain at route frames 5,500 and 7,000. The frame-9,000 room edge retains its
real guard scenery and cleanly reveals the ocean layer beyond it without the
colorful WRAM strip. A complete 11,275-frame OAM scan recorded 22,710 margin
samples (9,368 left and 13,342 right), from frames 1,315 through 11,241,
confirming that active objects are presented in both widened margins.

These results are visual and structural developer checks, not final gameplay
acceptance. Normal-speed user testing must still cover object timing,
collisions, collectible pop-in, leftward travel, death/restart, and special
screen types. Widescreen remains experimental and off by default.

The final non-trace MSVC Release build completed for the headless, Win32, and
portable SDL hosts. CTest finished 48/49: every new video, decoder, capture
tool, desktop, SDL, attract-cycle, and private integration test passed. The
only failure remains the pre-existing frame-3,309 sprite reference
(`27601b1b...` produced versus the stored `52e2b6bf...`), unchanged from the
previous documented baseline. The separate 45,000-frame Pirate Panic gate
passed with entry at frame 1,267, 43,734 active frames, 17 completion-flag
changes, three exit transitions, and zero clipped audio samples.

Append-only `versions/Version 09` is the public-safe package. The matching
private test bundle was created outside Git at
`C:\Users\Nickt\Documents\DKC2 Personal Test Builds\Version 09`; it includes
the hash-verified ROM, the existing saves folder, and launcher configuration
solely for the owner's local test.

## 2026-07-28 — deterministic widescreen layer/object diagnosis bundle

Moving screenshots could demonstrate pop-in but could not identify whether a
missing floor, barrel, banana, or enemy failed at level streaming, object
activation, sprite submission, or final PPU presentation. The existing TCP
commands also required several unrelated manual invocations and did not expose
DKC2's game-sprite records.

`scripts/capture_widescreen_diagnostics.py` now launches a fresh trace-enabled
headless process for composite, BG1, BG2, BG3, and OBJ at the same deterministic
input frame. Each run records an isolated BMP, PPU registers, shadow counters,
the complete newest render-consumed OAM snapshot, and logs. The composite run
also records private historical WRAM/VRAM and decodes the documented 25-slot
DKC2 sprite table: type, world/camera-relative position, graphic, display mode,
state, placement, and despawn fields. Game sprites and compound OAM tiles
remain deliberately separate evidence.

The first implementation incorrectly measured non-black margin pixels. SNES
layer isolation preserves the scene backdrop color, so an empty OBJ-only frame
can be non-black everywhere. The corrected BMP analyzer finds the dominant
backdrop and measures non-backdrop pixels per left/native/right region.
Automatic findings are now intentionally narrow: missing background-margin
detail, active-game-sprite without margin OAM, margin OAM without OBJ pixels,
or a blocking runtime-integrity event. Wrong-but-present imagery remains a
manual comparison.

Eight synthetic tool tests cover snapshot validation, signed 9-bit OAM X,
margin classification, DKC2 sprite-field decoding, BMP region analysis, and
pipeline findings. A separate MSVC Release trace build completed. An end-to-end
Pirate Panic frame-5,499 run produced distinct composite/BG1/BG2/BG3/OBJ
captures, matching private WRAM/VRAM, two active Kong game-sprite records, 15
rendered OAM entries, and continuous isolated BG1 deck plus BG2 sky/ocean.
The trace logs retained the existing non-abandoning unresolved-stub trap for
investigation; the tool records it but only treats unresolved abandon,
interpreter cap, or fatal output as invalidating.

`docs/WIDESCREEN_DIAGNOSTICS.md` defines the terrain-first workflow. Capture a
frame immediately before and after a defect, isolate BG1 floors/walls first,
then follow game sprite → OAM → OBJ pixels for objects. Every confirmed flow
must become a synthetic regression before moving to a new object or screen
class. Diagnostic output stays ignored and private.

## 2026-07-30 — private Version 10 diagnostic test kit

The previous manual recording attempt produced no file because a relative
output path depended on the launcher's working directory and the host did not
report an open failure. Input recording is now a shared Win32/SDL component
that opens before the frame loop, marks the window title while active, emits
one byte-stable six-hex-digit LF line per emulated frame, flushes every sample,
and makes every file error visible. A synthetic unit test covers exact bytes,
frame counts, path failure, injected write failure, and close behavior. The
first test correctly exposed Windows text-mode CRLF expansion; binary output
now makes recordings identical on every host.

`scripts/create_private_diagnostic_version.ps1` creates an append-only
diagnostic-only version outside the repository. It verifies the supported ROM
hash, records source provenance and executable hashes, includes both normal
playable hosts plus normal and trace headless runners, and localizes the
diagnostic tools. `Record-Pirate-Panic.ps1` uses paths rooted at its own
package, refuses replacement, records session metadata, and preserves starting
SRAM beside the input. `Diagnose-Frame.ps1` replays that exact SRAM into a new
timestamped capture. The helpers accept both relative and absolute `rom.cfg`
content because a successful desktop launch may normalize that setting.

The private kit was created at
`C:\Users\Nickt\Documents\DKC2 Personal Test Builds\Version 10`. Its packaged
verification recorded exactly 120 frames (840 bytes), retained the starting
SRAM, replayed frame 119 through the trace executable, and generated
composite/BG1/OBJ images plus JSON/HTML with zero findings and no fatal,
interpreter-cap, or unresolved-abandon event. This smoke test also exposed and
corrected two harness defects: stale PowerShell process exit state and doubled
package paths after `rom.cfg` became absolute.

The final Release test run completed 50 of 51 tests. Both new diagnostic-tool
and input-recording tests pass, as does the packaged record/replay/capture
check. The sole failure is the unchanged private frame-3,309 sprite reference:
`27601b1b...` is still produced where the stored baseline expects
`52e2b6bf...`. Version 10 is therefore ready for the owner's targeted
widescreen diagnosis, but it is not a public release and does not close manual
Pirate Panic gameplay acceptance.

## 2026-07-30 — first owner-recorded Version 10 route

The owner completed Pirate Panic through the private Version 10 recorder and
closed the host normally. The resulting
`pirate-panic-full-route.input` contains exactly 7,322 frames, has its matching
2 KiB `pirate-panic-full-route.start.srm`, and records a clean exit at frame
7,321. The executable and ROM hashes in its session metadata match the
Version 10 manifest.

Coarse deterministic captures at frames 1,500 through 7,200 reproduced the
complete route, including the bounded bonus screen. They also found a concrete
late-level margin defect that a single static screenshot had not classified.
Frame 6,500 (camera `$1A13,$015F`) has clean BG1 margins. By frame 6,750
(camera `$1BBF,$012A`), an unrelated horizontal terrain strip appears at the
upper-left margin and remains visible at frame 7,200.

Full same-frame layer isolation proves that the strip is emitted by BG1. It is
present in the isolated BG1 image and absent from isolated OBJ; BG2, BG3, and
presenter composition are therefore not the source. This narrows the next
investigation to BG1 world-key lookup, end-of-level metatile bounds, or
transparent fallback selection during the late vertical-camera transition.
The private evidence is retained under Version 10's timestamped frame-6,500
and frame-6,750 capture folders and remains outside Git.

## 2026-07-30 — Pirate Panic vertical-phase BG1 correction

The frame-6,750 WRAM/VRAM evidence disproved the initial horizontal-boundary
hypothesis. The affected upper-left BG1 margin should decode to the scene's
verified-transparent character, while the visible 28x8 strip matches terrain
31 source rows away. The reconstruction mixed `first_map_row` from the PPU
scroll with destination/subrow values from WRAM camera Y. On an observed NMI
boundary, camera Y had crossed an 8-pixel row while the rendered PPU phase was
still one pixel behind. The modulo expression therefore evaluated a `-1` row
difference as `31` and seeded the distant terrain into the correct shadow key.
Because exact prefill intentionally yields to an existing history entry, the
one-frame error persisted.

`Dkc2VideoLevelSourceTileY` now unwraps each rendered 10-bit PPU tile row near
the full camera anchor. `Dkc2PrefillWidescreenLevelBg1` uses that one result for
both the shadow key and source-map row, so a future WRAM camera value cannot be
mixed with the current rendered phase. This preserves history priority and
does not force transparent map cells over possible game-authored dynamic BG
writes. A source-clean test fixes the observed camera `$0130` / PPU `$002F`
boundary and a 10-bit wrap case.

The diagnostic BMP analyzer now reports logical `upper_left_margin` bounds
`x=[0,43), y=[0,64)` independent of bottom-up BMP storage. The external route
gate replays the matching input/SRAM at frame 6,750, verifies camera
`$1BBF,$012A`, requires active native BG1, rejects blocking runtime events, and
requires zero non-backdrop pixels in that region. Before correction it counted
224; after correction it counts zero. Byte comparison found exactly those 224
left-margin pixels changed in isolated BG1, with zero changed authentic-center
or right-margin pixels.

Corrected composite checkpoints at frames 1,500, 3,500, 5,500, 6,500, 6,750,
and 7,200 were visually inspected. The entrance, bounded bonus room, mid-level
objects, late ramp, goal action, and former strip location remain coherent.
The full 7,322-frame widescreen replay entered Pirate Panic at frame 1,097,
kept it active for 6,205 frames, recorded 17 completion-flag changes and two
exit transitions, and produced zero clipped audio samples.

All normal Win32, SDL, and headless Release targets plus the trace runner
rebuilt successfully. The three focused trace tests, including the private
frame-6,750 route gate, pass. The complete normal suite remains 50/51: the only
failure is the unchanged frame-3,309 sprite-reference mismatch
(`27601b1b...` produced versus `52e2b6bf...` expected). No new regression was
introduced. Final normal-speed motion acceptance remains with the owner.

The rebuilt private handoff is external append-only `Version 11`. Its packaged
120-frame record/replay/layer-capture smoke test passed. The first frame-6,750
gate attempt then exposed a packaging—not runtime—defect: existing `.input`
files had been carried forward without their same-basename starting SRAM.
The private packager now copies optional `.start.srm` and `.session.json`
companions with each recording. Version 11 was completed with the missing
paired state before handoff, and its manifest records that post-assembly
repair. The rerun passed at camera `$1BBF,$012A`, with 26,695 non-backdrop
native BG1 pixels, zero upper-left regression pixels, zero automatic findings,
and no blocking runtime event.

## 2026-07-30 — Swanky runtime-dispatch soft-hang diagnosis

The owner's external Version 11 session contains 34,960 host frames and ends
with a clean process exit. It nevertheless reproduces the reported 1-3 FPS
behavior in Swanky's Bonus Bonanza. The original tier-2 coverage artifact
records Swanky state `$B4:A3E0` first executing at internal frame 32,841,
`$B4:A475` at 32,879, and `$B4:A4CB` at 32,910. The first two return cleanly.
The 30th `$B4:A4CB` hit exhausts the default 2,000,000-instruction interpreter
cap. During that frame, execution cascades through invalid targets
`$34:A807`, `$3A:C7E4`, `$33:8007`, `$3C:FA78`, and `$33:0000`, then reaches
PPU data-port addresses in bank `$00`. The following resume breadcrumbs remain
near `$80:F105` until the recording ends.

This is a host CPU soft hang, not a native process crash and not evidence of a
GPU presenter bottleneck. A single emulated frame spent enough time in the
interpreter to reduce interactive presentation to 1-3 FPS, while the coarse
600-frame averages obscured the brief extreme stall. The clean-exit status is
therefore compatible with the user's observed near-freeze.

The failure also narrows a limitation in the previous "100% static coverage"
milestone. That count proves every exact entry state demanded by the static
graph was emitted; it does not prove that every interior address later loaded
from mutable game state was declared as a root. Swanky's dispatcher at
`$B4:9EDC` calls the pointer stored at `$079C`. The prior dispatch table did
not expose the observed interior state entries `$B4:A3E0`, `$B4:A475`, and
`$B4:A4CB` as independent AOT targets. The remaining states in that family
must be independently callable for later game-show phases as well.

`recomp/bankb4.cfg` now splits the routine into explicit roots at
`$B4:A3E0`, `$B4:A475`, `$B4:A4CB`, `$B4:A5D9`, and `$B4:A665`, and splits
the prize path at helper `$B4:A7CA`. This change preserves the surrounding
routine bytes; it changes which runtime-selected entry states receive their
own generated dispatch entries.

State `$B4:A4CB` contains an intentional M=0 `PLA; RTL` sequence. In 16-bit
accumulator mode, `PLA` removes the two-byte return installed by the
runtime-pointer JSR. `RTL` then removes the outer three-byte JSL return and
returns past the compiled caller. The shared interpreter call bridge now
compares the final S with its balanced post-call value, resolves a
`RecompReturn` ancestor skip when the guest returned non-locally, and
propagates that result to the generated caller. A clean balanced return remains
`NORMAL`; a clean non-local return is clamped to at least `SKIP_1`; a step-cap
bailout still restores the balanced post-call S. A focused bridge regression
models the same 16-bit `PLA; RTL` stack shape and passes all 62 checks. This is
generic runtime-call semantics rather than a DKC2-specific forced return.

Two exact headless attempts using the recorded input and its paired starting
SRAM did not reach Swanky: both remained in level `$0009` at the corresponding
frames and recorded no `$B4:A4CB` hit. Comparison with the original artifact
shows the route had already diverged before the Swanky transition. The reason
is a recording-format boundary: the file stores controller input once per
forward emulated frame, but does not store host rewind or save-state
save/load operations. Rewind restores older guest state while the recording's
host sequence continues. The original artifact remains valid evidence of the
soft hang, but that input file cannot be treated as an exact standalone
reproduction if either unrecorded host action occurred.

Regeneration completed successfully across 13 banks. The resulting graph has
3,325 roots, 3,475 exact AOT variants, and the same two deliberate
original-game fault variants on LLE. `dispatch_v2.c` contains exact M0X0
entries for `$B4:A3E0`, `$B4:A475`, `$B4:A4CB`, `$B4:A5D9`, `$B4:A665`, and
`$B4:A7CA`. The canonicalized diagnostic addresses clear the ROM bank's mirror
bit, so their `$34:*` spellings identify the same cartridge bytes as the
CPU-visible `$B4:*` entries.

Both the optimized Release build and the trace-enabled build completed. The
full configured CTest run is 52/53. Its only failure is the exact pre-existing
supplied-ROM sprite reference at frame 3,309: the current frame hash remains
`27601b1b...` while the stored baseline expects `52e2b6bf...`. The Swanky
static-entry test, diagnostic-validator test, 62/62 shared bridge regression,
and all other configured tests pass; no new test failure was introduced.

DKC2's rolling run report now serializes the shared dispatch ring's final 1,024
runtime indirect-dispatch events. `scripts/validate_swanky_run.py` combines
that evidence with tier-2 coverage and optional performance telemetry. It
requires a native M0X0 `$B4:9EDC -> $B4:A4CB` event and rejects a missing
Swanky AOT target, any interpreter cap, interpreted Swanky entry, known
corrupt edge, or SNES MMIO code address. Its synthetic regression passes. The
validator also fails the original Version 11 artifact as intended, detecting
the interpreter hit, cap, corrupt sequence, and MMIO execution instead of
mistaking its clean process exit for success.

Engineering verification is therefore complete for regeneration, dispatch
presence, builds, bridge semantics, and failure recognition. The fresh owner
Swanky run remains pending. Because the old recording does not encode rewind
or save-state actions, it cannot prove the repaired gameplay path; the owner
must record a focused run without those actions, close soon after completing
the game show so `$B4:A4CB` remains in the 1,024-event ring, and run the new
validator before normal-speed acceptance can be closed.

The repaired hosts and evidence tooling were assembled outside Git as private
append-only `Version 12`. The package carries the verified ROM, two save
files, launcher settings, control bindings, the Version 11 recordings with
their paired state/session files, both playable hosts, normal and trace
headless runners, and the focused validator. Recording-specific artifacts are
collision-protected. The two rolling host logs are cleared immediately before
launch and must be freshly recreated before the recorder copies them under
the session basename, preventing a failed run from inheriting stale evidence.

The packaged verification then launched the optimized OpenGL host, recorded
exactly 120 frames, produced fresh performance, tier-2, and last-run reports,
replayed frame 119 through the trace runner, and generated composite/BG1/OBJ
evidence with zero findings. The clean report retained all 84 indirect
dispatch events from the smoke run. The ROM hash remained
`35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`.
This proves Version 12's capture path is operational; it does not claim the
Swanky gameplay path has passed before the owner's focused run reaches it.

## 2026-07-30 — `bg-01` multi-area background scan

The owner's private Version 12 `bg-01.input` contains 5,188 frames, has paired
starting SRAM and complete diagnostics, and exits cleanly. Composite captures
every 300 frames found the first unambiguous later-level margin defect at
frames 4,500, 4,800, and 5,100. Full composite/BG1/BG2/BG3/OBJ isolation at
4,500 and 4,800 proves the purple right gutter and lower-edge colored cells
are layer-source defects rather than presenter persistence.

At both focused frames, WRAM `$17B6=$7800` matches BG2's tilemap base; BG1's
base is `$7000`. The prior `Dkc2PrefillWidescreenLevelBg1` always decoded
the decompressed map into shadow layer 0. On this screen that puts sparse,
wrong terrain cells into BG1 while the real BG2 terrain has no unseen eastern
source. Cumulative BG2 east misses are 740,390 at frame 4,500 and 1,086,159 at
frame 4,800; frame 4,800 is automatically classified as
`background_load_or_render`.

The correction adds a tested source-owned selector that matches live `$17B6`
to enabled BG1/BG2 tilemap bases. Terrain prefill, character selection, PPU
vertical phase, world shadow keys, and decoded tile writes now all use that
selected layer. BG2 keeps its periodic parallax fold only when it is not the
terrain owner, and an unmatched destination fails closed.

Fresh deterministic captures at frames 4,500 and 4,800 key shadow BG2 to the
exact camera coordinates (`390,384` and `1088,375`). BG2 right-margin
non-backdrop pixels improve from 347 to 4,842 and from 324 to 3,939,
respectively. Visual comparison confirms that the missing eastern ground is
filled and the prior colored BG1 margin garbage is gone. The layer selector's
synthetic tests and all four widescreen tests pass. The complete suite remains
52/53, with only the unchanged supplied-ROM frame-3,309 sprite-reference
mismatch. The diagnostic classifier still reports a sparse secondary BG1
margin, so normal-speed owner acceptance and further foreground-layer auditing
remain pending; this checkpoint claims the BG2 terrain-source fix only.

Owner motion testing confirmed that the swamp improved but remained visibly
unfinished. The next isolated defect was not terrain: BG3's forest silhouettes
stopped at the authentic viewport, leaving flat purple bands in both margins.
At frames 4,500 and 4,800, Mode 1 enables BG3 `$6C00` and level number `$002C`
identifies Mudhole Marsh. A source-owned repeat-policy selector now opts only
that signature into SNESrecomp's rendered-scanline repeat path. It does not
widen raw BG3 fetches and leaves every unaudited BG3 use clamped.

Fresh composite/BG3 captures at both frames have zero diagnostic findings.
The isolated BG3 margin palette rises from one sampled color on each side to
11/9 colors at frame 4,500 and 9/9 at frame 4,800; visual inspection confirms
continuous forest art across the 342-pixel surface. The focused selector test
and all four widescreen tests pass. The complete suite remains 52/53 with the
same pre-existing frame-3,309 sprite-reference mismatch.

An attempted continuation to frame 4,900 did not reproduce reliably: the
trace host stopped advancing at recorded frame 3,543. Therefore frames through
4,800 remain useful focused evidence, but the long multi-area recording is not
promoted to an end-of-route deterministic gate. The next owner artifact should
be a short swamp-only recording with its starting SRAM and no rewind or
save-state operations.

## 2026-07-30 — `bg-02` vertical terrain-history correction

The owner's private Version 12 `bg-02.input` contains 4,850 frames, has its
paired 2 KiB starting SRAM and complete session diagnostics, and exits cleanly.
It enters Mudhole Marsh at frame 1,542 and includes repeated vertical camera
movement through the level. Consecutive composite/BG1/BG2/BG3 captures isolate
the reported lower-margin corruption to BG2; the authentic central terrain and
the repeated BG3 backdrop remain stable.

Raw frame 4,155 state identifies BG2 `$7800` as the live terrain destination,
camera `($107E,$01CD)`, and BG2 PPU scroll `($007F,$00CB)`. An independent
comparison decoded every one of the 957 currently visible BG2 map cells from
the live WRAM metatile source with the existing `world X - 32` / PPU-source-Y
mapping. This rules out the decoder, map base, metatile table, and general PPU
scroll progression.

The inconsistency was inside shadow ownership. Exact prefill and margin lookup
used the PPU source-row domain (`$00CB` at that frame), but
`WsShadowFrame` and `WsShadowOnVramWrite` were anchored to raw camera Y
`$01CD`. DKC2 stages this rolling terrain one `$0100`-pixel page above the
camera. Consequently live tiles and later game writes were remembered 32 tile
rows away from the exact cells they represented. Vertical camera movement
could bring those misplaced history keys back into lookup range, where history
correctly outranked prefill but supplied the wrong row.

`Dkc2VideoTerrainShadowY` now unwraps the rendered 10-bit PPU phase near the
camera. The layer selected dynamically from `$17B6` uses that source-Y origin
for native capture, VRAM-write association, lookup, and exact prefill, while X
continues to use full camera coordinates and the proven source-X page offset.
This is an engine-level rule for DKC2's standard rolling terrain on either BG1
or BG2. It contains no Mudhole Marsh level check. Unmatched destinations,
Mode 7, bounded rooms, and other unaudited screen models continue to fail
closed.

The source-clean video test retains the observed `$00CB/$01CD` swamp case, the
earlier `$002F/$0130` NMI-boundary case, and a 10-bit wrap case. The focused
test passes, and the corrected 4,000–4,200 BG2 sequence was regenerated for
visual comparison. Final normal-speed acceptance remains with the owner; this
checkpoint does not claim that authored off-viewport voids or every nonstandard
stage archetype are automatically solved.

Final optimized verification rebuilt the Win32 OpenGL/GDI, headless, and SDL
hosts plus the focused video test. The complete 4,850-frame `bg-02` route then
replayed with widescreen enabled and its paired starting SRAM, reached level
`$002C`, and exited normally with zero sequence errors. The earlier 5,188-frame
`bg-01` multi-area route also replayed to completion with zero sequence errors.
The complete configured suite remains 52/53: all widescreen, video, desktop,
SDL, input, bridge, and attract tests pass, while the unchanged supplied-ROM
frame-3,309 sprite-reference hash is the only failure.

The final trace-only executable could not be rebuilt in this session because
the host environment exhausted its privileged-command allowance after the
optimized builds succeeded. Therefore the corrected trace screenshot sequence
is not claimed as final evidence, and the private Version 12 executable was
not overwritten. The verified optimized executable remains in
`build-snesrecomp/Release/` for owner testing; final visual acceptance must use
that build or a subsequently refreshed Version 12 package.

## 2026-08-01 — Reference-backed screen and map-layout classification

The H4v0c21 v1.0 disassembly and DonkeyHacks documents were consulted as
private factual references only. No assembly, comments, tables, or assets were
copied. The reference structure definition corrected the diagnostic meaning
of `$0515-$0539`: these are mostly 16-bit level-configuration fields, and
`$0529` is the live gameplay sub-mode. The DKC2 gameplay dispatch identifies
which sub-modes use horizontal, vertical, square, or special scroll handlers.

The diagnostic decoder now records level type, tileset and layout numbers,
NMI/gameplay sub-modes, effects, PPU/VRAM configuration numbers, camera bounds,
the terrain destination, and metatile source. It combines those values with
live PPU state to report the terrain-owning BG layer and the proven map layout.
Unknown Mode-1 configurations are reported rather than guessed. Automated
background findings now require margin pixels only for the classified terrain
owner and an explicitly repeated BG3, eliminating the swamp's sparse non-owner
BG1 false positive. Ten synthetic diagnostic tests pass.

The native decoder now supports both proven rolling organizations. Horizontal
gameplay uses DKC2's column-major map calculation; vertical gameplay uses its
row-major calculation. Square and special gameplay sub-modes fail closed to a
centered 256-column guest frame until independently reconstructed. A focused C
test covers horizontal, vertical, and unknown classification plus both address
calculations.

The 4,850-frame `bg-02` and 5,188-frame `bg-01` private routes both replay to
completion with zero sequence errors. Their final state and frame hashes are
unchanged, demonstrating that the horizontal path did not regress. A fresh
frame-2,600 composite/BG2/BG3 diagnostic reports the swamp as BG2-owned,
horizontal/column-major, with the audited BG3 repeat and zero findings.
Independent decoding agrees with all 928 sampled native BG2 tilemap entries in
the representative vertical-motion frame. The narrow edge pieces visible in
some frames are present in both the decompressed WRAM map and live VRAM, so
this checkpoint classifies them as authored off-screen content, not stale
margin data. Vertical-stage gameplay is implemented from the validated map
formula but remains visually unverified pending a focused vertical-stage
recording.

The normal optimized configuration rebuilt all Win32, SDL, headless, and test
targets. Its authoritative suite passes 52/53; the only failure is the same
pre-existing frame-3,309 sprite-reference hash mismatch. The trace diagnostic
configuration passes 45/55: its two long widescreen routes and all new tests
pass, while ten normal-host smoke/drill tests are incompatible with that
folder's trace stderr and missing copied runtime DLL layout. Those ten are
configuration artifacts, not accepted as product regressions; the equivalent
normal optimized smoke, GPU/GDI, SDL, and diagnostic-drill tests all pass.
The refreshed playable binaries are in `build-snesrecomp/Release`; the external
personal Version 12 folder was not overwritten during this checkpoint.

## 2026-08-01 — right-margin banana activation and OAM packing

The owner reported that collectible bananas still appeared only after entering
the native 4:3 viewport. The normal object diagnostics could not account for
them because bananas are not ordinary entries in DKC2's 25-slot sprite table.
Reference-guided address orientation, followed by local generated-code and
WRAM/OAM traces, identified a dedicated banana-list path. No reference code,
comments, tables, or ROM-derived data were copied.

Four native-width constants in the dedicated index, group walker, and clip
routine (`$0107`, `$0100`, `$010F`, and `$0107`) were adapted through the same
terrain-readiness-gated widening used by common objects. An exact function
watch proved the statically recompiled banana group routine executed. At
private `bg-02` frame 2,582, the live list record had base world X=2,256 while
camera X=1,968, placing the group at X=288 in the right margin; its widened
scratch boundary was also the expected camera+299. Yet banana tiles 232/238
were consumed from OAM at X=35.

The remaining error was the direct OAM writer. Its native `XBA; ASL` packing
derives OAM high-X from coordinate bit 15, which handles negative off-left
positions but not new positive coordinates `$0100-$012A`. A narrow
`Dkc2VideoPromoteOamXHigh` helper now mirrors bit 8 into bit 15 at the two
banana coordinate writes before the original packing sequence. Native mode,
unready terrain, and all non-banana OAM paths are unchanged. Synthetic helper
and generated-adapter tests pass and cover idempotent regeneration.

The same deterministic frame now consumes tiles 232/238 at X=291 in the right
margin, and composite plus OBJ isolation visibly show the banana pair at that
edge with zero automatic findings. The entire 4,850-frame `bg-02` route then
completed with zero sequence errors, zero interpreter caps, and zero fatal
errors. Its final post-gameplay screen is deliberately reported as an
unclassified/centered screen. This validates the recorded path, not every
banana formation or dedicated effect renderer in the game; owner normal-speed
testing and broader routes remain required.

Final verification rebuilt every optimized target. The configured optimized
suite remains 52/53: the banana adapter/tool/video tests, Win32 and SDL hosts,
GPU/GDI presentation, diagnostics, packaging, and two-cycle attract run pass;
only the pre-existing frame-3,309 sprite-reference hash mismatch remains. The
trace configuration remains 45/55 with both long widescreen routes and all
new tests passing; its ten known failures are the trace-stderr and missing-
desktop-DLL layout incompatibilities already documented for that build folder.

The existing external private Version 12 kit was refreshed in place for owner
testing; no Version 13 was created. Its prior four executables were preserved
under `previous-executables/20260801-banana-oam-fix`, then the verified Win32,
SDL, optimized headless, and trace headless binaries plus the current capture
tool and diagnostics guide were copied in. ROM, SRAM, recordings, launcher
settings, key bindings, captures, and personal progress were not modified.

## 2026-08-02 — presentation synchronization and current-route rescan

The user reported horizontal screen tearing while continuing normal-speed
widescreen testing. The Windows OpenGL presenter created and swapped a
double-buffered context but never requested a WGL swap interval. The SDL host
requested interval one but silently fell back to interval zero when the driver
rejected it. A shared, synthetic-testable VSync policy now requests interval
one for visible OpenGL gameplay contexts, reports `on`, `request-failed`, or
`unsupported` in the presentation backend and diagnostic report, and retains
GDI as compositor-managed. Hidden automated windows explicitly use interval
zero; this prevents a graphics-driver swap wait from hanging noninteractive
smoke tests. The SDL integration run on the local NVIDIA driver reported
`vsync=on`. Visible Win32 WGL behavior and perceptual tearing remain owner
acceptance items. This host-only policy does not change the emulated
60.098811862 Hz clock, so cadence and audio stability on a 60.000 Hz display
must also be observed.

Fresh current-code sweeps replayed all 4,850 frames of `bg-02` and all 5,188
frames of `bg-01` with zero input-sequence errors. Coarse contact sheets did
not expose another safe universal terrain correction. A focused composite,
BG2, and BG3 bundle at `bg-02` frame 2,350 classified standard BG2-owned
horizontal terrain with the audited BG3 repeat, zero west-shadow misses, and
zero automatic findings. Isolation showed the apparent lower-left black block
is a transparent authored BG2 region revealing the legitimate dark BG3 fade,
not stale margin memory. No replacement art or guessed tile was added.

The `bg-01` route contains only a short Bramble Scramble sample around frames
3,600–3,900. That screen remains centered with 43-pixel black margins because
its family fails closed, which is safer than applying the rolling-terrain
decoder without evidence. The next widescreen milestone is therefore a
focused Bramble Scramble entrance-to-goal recording with paired starting SRAM,
followed by profile classification and separate terrain, bramble-layer,
object, and margin validation.

Final optimized verification rebuilt the Win32 OpenGL/GDI, SDL, and headless
hosts and passed 52/53 configured tests. The only failure remains the known
frame-3,309 sprite-reference mismatch (`27601b1b...` produced versus the
stored `52e2b6bf...`); all hidden GPU/GDI/SDL, VSync-policy, diagnostics,
input, attract, and widescreen tests pass. The rebuilt trace configuration
passes 45/55, including both long private widescreen routes. Its ten failures
are the already documented trace-stderr and missing desktop-DLL layout
incompatibilities; no additional failure class appeared.

The existing external private Version 12 kit was refreshed in place; no new
version folder was created. Its prior four executables are preserved under
`previous-executables/20260802-vsync`, and byte-identical copies of the
verified Win32, SDL, optimized headless, and trace headless builds replaced
only their corresponding executable files. The private ROM, SRAM, recordings,
settings, bindings, diagnostics, captures, tools, and personal progress were
left untouched.

## 2026-08-03 — Bramble Scramble square-layout widescreen

The owner supplied `bramble-01.input`, a valid 3,134-frame recording with its
2 KiB starting SRAM and session metadata. The recording and all generated
screenshots/WRAM/VRAM evidence remain external or ignored. The owner corrected
the initial map identification: frame 1,200 is the second Krazy Kremland map
screen, not Crocodile Cauldron. Bramble Scramble gameplay begins around frame
1,400; the route contains a transition/death interval near frame 2,000 and a
long second attempt through frame 3,133.

All gameplay samples reported level `$002E`, game sub-mode `$0010`, BG1 terrain
target `$7800`, BG2 `$7000`, BG3 `$7400`, camera bounds `(3072,2576)`, and the
previously unsupported `square_or_special` classification. Sub-mode `$0010`
uses the square scroll family. Its metatile source advances 48 entries, or
`$60` bytes, for each 32-pixel source row. An independent private calibration
at frame 1,600 compared both existing formulas and the new one against 957
visible native BG1 cells: horizontal matched 601 (62.8%), vertical matched 553
(57.8%), and square matched 954 (99.69%). The three remaining cells are
consistent with live or partially staged writes.

`Dkc2VideoLevelLayout` now includes a square variant, but only sub-mode `$10`
selects it. Every other unproven square/special mode still fails closed. The
decoder uses the independently expressed `$60`-byte row formula, and vertical
source bounds also apply to square terrain. Synthetic C coverage checks the
new classification and address, while the diagnostic classifier reports
`row_major_square_96_byte_stride` and refuses to mark an unknown
`square_or_special` profile safe for objects.

Fresh captures at frames 1,600, 2,400, 2,800, and 3,000 show continuous BG1
platform/bramble art across the 342-pixel surface during horizontal movement
and vertical climbing. The existing safe rendered-scanline repeat covers
bounded BG2 continuously. BG3 remains bounded because the audited composites
do not expose a gap; no broader BG3 exception was added. Frame 1,600 reports
267,447 west and 247,968 east BG1 shadow hits with only nine west and zero east
misses, and the private deterministic regression requires non-empty BG1 in
both margins with zero diagnostic findings.

The complete route records 7,991 render-consumed OAM margin samples over 1,100
frames: 7,357 left and 634 right, spanning X=-43 through X=298. Its final
optimized replay completes all 3,134 inputs with `terrain_ready=1`, widened
banana/object limits, zero sequence errors, no clipped audio, and no blocking
runtime event. The recording ends while level `$002E` remains active, so this
checkpoint validates the supplied entrance-to-late-stage route rather than an
entrance-to-goal clear. Goal, bonus, death/restart, and normal-speed owner
acceptance remain open.

Final optimized verification rebuilt the Win32 OpenGL/GDI, SDL, optimized
headless, and trace headless targets. The normal configuration passes 52/53
tests; its only failure is the pre-existing frame-3,309 sprite-reference hash
mismatch (`27601b1b...` produced versus stored `52e2b6bf...`). The trace
configuration passes 46/56. The new
`supplied_rom_widescreen_bramble_route` test passes, as do the existing Pirate
Panic and long BG1 widescreen routes. Its ten failures remain the documented
trace-stderr and missing desktop-DLL build-folder incompatibilities; no new
failure class appeared.

The external private Version 12 kit was refreshed in place; no Version 13 was
created. The previous four executables are preserved under
`previous-executables/20260803-bramble-square`. Byte-identical copies of the
verified Win32, SDL, optimized headless, and trace headless binaries replaced
only their corresponding executable files, and the current capture tool and
diagnostics guide were refreshed. ROM, SRAM, recordings, launcher settings,
bindings, captures, and personal progress were not modified.

## 2026-08-03 — Pirate Panic transparent-margin history cleanup

The owner supplied the 16,778-frame `Pirate Panic - 02` recording and its
paired 2 KiB starting SRAM. A complete optimized replay entered Pirate Panic
at frame 1,138, remained active for 15,505 frames, finished with zero input
sequence errors, no clipped audio, and no runtime failure. The recording,
SRAM, ROM, diagnostic images, and reports remain private or ignored.

A layer-isolated diagnostic at frame 14,400 located the reported brown/black
fragments in BG1's upper-left 43x64 margin. The initial report counted 295
non-backdrop pixels there. Independent calibration of the live decompressed
map and 8x8 definition table agreed with all 896 inspected native BG1 cells;
the source cells under the fragments decoded to the verified transparent
character. This ruled out an incorrect map formula, bad PPU presenter, or an
OBJ-spawn error: an old viewport/VRAM capture was surviving in a shadow-history
slot that static prefill deliberately left untouched.

An initial broad experiment force-wrote every decoded map entry. It removed
the fragment but also replaced legitimate dynamic ship details with static map
tiles, so it was rejected. The final policy force-clears only out-of-map and
verified-transparent source cells. Non-transparent cells still prefill only
missing history, and the terrain layer preserves a live game write from the
current or preceding frame. Fresh captures through the full route remove the
previous upper-left and left-edge deck fragments without the broad experiment's
new solid deck bands.

`Dkc2VideoIsTransparentTileEntry` is covered by a synthetic C test, including
palette/priority bits. A new optional CMake private gate,
`supplied_rom_widescreen_pirate_panic_late_route`, replays the paired fixture
to frame 14,400, asserts camera `(5673,419)`, and requires zero non-backdrop
pixels in upper-left BG1. Focused widescreen CTest passes the original BG1,
new Pirate Panic late, and Bramble routes. Full optimized verification remains
52/53 with only the pre-existing frame-3,309 sprite-reference hash mismatch;
the trace suite remains 47/57 with its ten documented trace-stderr/missing-DLL
layout failures and no new failure category.

The external private Version 12 kit was refreshed in place for normal-speed
owner testing; no Version 13 was created. Its four previous executables are
preserved under
`previous-executables/20260803-pirate-panic-transparent-margin-fix`. Verified
Win32, SDL, optimized headless, and trace headless binaries plus the current
diagnostic tool and guide were copied with matching SHA-256 values. The ROM,
SRAM, input recordings, launcher settings, bindings, captures, and saves were
not targeted.

## 2026-08-03 — Lv01-02 horizontal source-page rollover

The owner's `Lv01-02` request was reproduced inside the existing 16,778-frame
`Pirate Panic - 02` route. A coarse 300-frame scan located a strong BG1 defect
at frame 15,900: disconnected ship pieces occupied the expanded left and
right margins. Layer isolation proved BG2 sky/ocean and OBJ output were clean;
the defect was entirely collision-bearing BG1. Camera was `(7030,256)`,
rendered BG1 scroll was `(886,255)`, and the screen remained the proven
horizontal rolling-terrain profile.

The decompressed-map calibration explained why the preceding transparent-cell
cleanup could not solve it. The old horizontal source row used the PPU's full
10-bit physical tilemap phase, selecting rows 31-59 at the rollover and
matching only 277/957 native BG1 entries. DKC2's column builder stages source
terrain `$0100` pixels above camera Y. Selecting the PPU low-eight-bit phase
nearest that source-page anchor chooses the preceding page and matches
924/957 native cells. A neighboring clean frame 15,600 remains an exact
957/957 match with the same generalized formula.

Terrain history continues to use rendered world-Y keys; only horizontal static
map decoding uses the staged source page. Across a fresh full-route 300-frame
scan, only five sampled frames changed (12,000, 12,300, 12,900, 13,800, and
15,900), all at the affected page phase. Owner review accepted only 12,000 and
12,300. The other three remain diagnosis checkpoints, not completed fixes.

Synthetic video coverage retains clean-page, one-pixel-lag, rollover, and
first-row-after-rollover cases. The premature frame-15,900 occupancy/hash gate
was removed after owner review showed that its deterministic image was still
wrong. Determinism is necessary evidence, but it is not visual correctness.

## 2026-08-03 — Lv01-02 owner review and dynamic-layer split

Owner review classified frames 12,000 and 12,300 as fixed and rejected the
remaining sampled frames. Layer isolation then separated their causes:

- frame 12,900 contains a coherent 15-tile OAM transition effect in slots
  33-47 at screen X `-36..-12`; it is not a stale BG tile. The independently
  streamed ship-rigging BG3 was missing from the margins.
- frame 13,800 is at camera X=max X=`1280`; BG1 reaches the authored hard end
  of the bonus room at the native right edge. Reading beyond that point is not
  valid level data, so it needs an explicit bounded-room presentation policy.
- frame 15,900's remaining blocks are isolated to BG1. BG2, BG3, and OBJ are
  clean at those coordinates; broad row-history clearing was tested and
  rejected because it damaged the already accepted frames.

Pirate Panic's BG3 rigging path is identified by level-effects bit 0 and BG3
map register `$79` (`$7800`, 64 columns). Widening only that proven
configuration changes margin pixels while leaving the native 256-pixel region
pixel-identical at all six inspected checkpoints. The route still completes
with zero sequence/runtime errors. A broad retained-row confidence clear and
a stored camera-extent mutation were both rejected: the former erased valid
mast/ocean/deck content, and the latter collapsed the rendered scene.

The next candidate is a presentation-only hard-room-edge treatment that does
not mutate `$0AFC`; it must be rebuilt and visually replayed before retention.
The final build request was blocked by the Codex execution-usage limit, so no
new executable or Version 12 deployment was produced from this investigation.

## 2026-08-09 â€” validated decompilation-symbol promotion

The active `recomp/bank*.cfg` set was audited against the private imported WLA
overlay. It already contained the large revision-0 H4-derived symbol set:
1,505 of 3,314 CFG functions had descriptive names and 1,809 retained generic
`CODE_...` identities before this pass. A blind same-address import was
rejected. Conditional assembly can move revision-0 routine starts relative to
addresses embedded in research labels, while adjacent seven-byte sprite-table
entries can still produce plausible but incorrect object names.

`scripts/promote_snesrecomp_symbols.py` now implements the conservative rule.
It accepts a generic function only when the private WLA map contains exactly
one valid `context_CODE_BBXXXX` alias preserving that same full generic
identity. The tool defaults to a review-only dry run and fails closed on bank
mismatches, collisions, and ambiguous aliases. It ignores data labels and raw
address coincidences. `tests/test_promote_snesrecomp_symbols.py` covers
selection, exact/idempotent application, ambiguity, and bank mismatch.

Ten names passed that proof and were promoted:

- mine-glint and Castle Crush floor-movement paths in bank `$80`;
- three animal-icon paths, the no-animal-sign path, rideable-balloon path, and
  barrel-cannon path in bank `$B3`;
- Kong state 11 in bank `$B8`; and
- the camera-unlock-trigger sprite path in bank `$BE`.

The resulting CFG has 1,515 descriptive and 1,799 generic names. Remaining
generic functions are deliberately unchanged rather than guessed. The private
overlay remains ignored and no ROM, generated source, assembly, comment,
asset, save, screenshot, or recording entered Git. Provenance and the inspected
Yoshifanatic1 GPL-3.0 revision are recorded under `third_party/`.

The declaration synchronizer rewrote `recomp/funcs.h` for all 3,314 functions.
A complete private regeneration reported 3,325 roots, 3,475 exact AOT variants,
and the same two deliberate LLE variants, then reapplied all five widescreen
adapters. Generated definitions were inspected for the new names. The ignored
generated directory initially had a sandbox-owner-only ACL, so the normal
MSBuild account could not enumerate it; full access was granted only to the
workstation owner on that ignored directory. No source-tree ACL was changed.

Controlled Release builds of optimized headless, Win32 desktop, and SDL
desktop targets all linked. The configured `build-snesrecomp` matrix passes
53/54 tests, including two attract cycles, all desktop smoke checks, generation,
the new symbol test, and private ROM analysis/render probes. The sole failure
is the already documented frame-3,309 sprite-reference hash mismatch:
`27601b1b...` remains produced versus stored `52e2b6bf...`; this pass did not
change that result. The older root `build/` baseline passed all 12 public tests
but its nine private tests referenced a missing historical ROM path, so that
configuration was not treated as behavioral evidence.

## 2026-08-10 - revision-addressed semantic symbol database

The earlier promotion pass improved ten names, but it did not preserve
function purpose, confidence, provenance, WRAM objects, or sprite field
layouts. Generated C could become readable only one function at a time, while
diagnostic scripts still duplicated hard-coded offsets.

Added `recomp/symbols.toml` as the curated function-name source and
`recomp/layouts.toml` as the WRAM/structure source. Actual USA v1.0 CFG entry
addresses are the stable keys. Historical names whose embedded addresses came
from a different conditional-assembly layout remain aliases. The first set
contains 20 widescreen/object/background functions, 19 WRAM objects, and 13
fields of the live `Dkc2Sprite` record.

`scripts/build_dkc2_symbol_database.py` parses all 3,314 CFG functions,
validates the semantic records, applies only exact-boundary CFG names, and
generates tracked Python constants, tracked `docs/SYMBOL_DATABASE.md`, and an
ignored `.cache/dkc2-symbols.json` complete inventory. It fails closed on
missing boundaries, collisions, malformed addresses, invalid WRAM spans,
duplicate constants, unknown field widths, and field overlap. `--check`
rejects stale outputs. `scripts/lookup_dkc2_symbol.py` searches by name, alias,
tag, note, function address, or WRAM offset.

The TCP screenshot tool now imports generated camera, level, sprite-table, and
sprite-field constants instead of carrying a second layout definition. Private
diagnostic packaging copies that generated module with the capture tool. Four
existing widescreen adapters were rebound to their semantic function names
while retaining each old `CODE_...` identity for search continuity.

Synthetic tests cover exact application/idempotence, stale-output rejection,
non-boundary refusal, and structure-field overlap. Private regeneration
reported 3,325 roots, 3,475 exact AOT variants, and the same two intentional
LLE-only variants; all five widescreen adapters applied. Optimized headless,
Win32, and SDL Release hosts linked. The configured matrix passed 55/56 tests.
The sole failure is the pre-existing frame-3,309 reference mismatch
(`27601b1b...` produced versus stored `52e2b6bf...`); the two attract cycles,
desktop smoke checks, private ROM probes, and all three symbol tests passed.

The owner also reports that expanded 16:9 margins still show graphical
glitches during attract-demo playback. Automated attract completion is only a
liveness/correctness signal and does not close this visual acceptance issue.
Version 13 is therefore a diagnostic checkpoint, not a widescreen-complete
release.

Private Version 13 was created at
`C:\Users\Nickt\Documents\DKC2 Personal Test Builds\Version 13`. Its normal
and trace hosts were rebuilt from this checkpoint. The kit's 120-frame
record/replay/capture self-test completed with zero findings. The useful
Version 12 owner recordings (`bg-01`, `bg-02`, `bramble-01`, Pirate Panic, and
Swanky routes plus paired metadata/SRAM) and `dkc2s0.sav` were carried forward
without overwriting Version 13's newer `save.srm`; old self-test recordings
were deliberately omitted.

## 2026-08-10 - symmetric marsh banana margin rendering

The owner's marsh test exposed an asymmetric result: collectible bananas were
visible in the added right margin but not throughout the added left margin.
The complete 4,850-frame `bg-02` trace confirmed this was not list activation.
Banana OAM tiles 232/238 produced 318 right-margin samples at X=256..298, but
only 17 left-margin samples at X=-14..-1.

The dedicated `render_banana_tiles_CODE_B5F5E1` emitter retained a separate
native `$000F` negative-X magnitude cutoff. The nearby `$0167` constant was
tested as a hypothesis, rejected when the private assembly and unchanged
replay proved it was the vertical span, and restored without retention. The
source-owned regeneration adapter now replaces only the `$000F` literal with
`Dkc2VideoExpandCullLeft(0xf)`. Consequently, it remains `$000F` in 4:3 or
before terrain readiness and becomes `$003A` only for a proven widescreen
terrain source.

The post-fix full-route report records 63 left-margin banana samples at
X=-43..-1 while preserving the exact 318 right-margin samples and X=256..298
range. Total banana margin samples increase from 335 to 381. The run completed
with zero unresolved abandons, interpreter caps, or fatal errors. Optimized
headless, Win32, and SDL hosts linked; low-parallelism compilation was used to
avoid MSVC heap exhaustion after the full private regeneration. The complete
configured suite passes 55/56 tests. Its sole failure remains the pre-existing
frame-3,309 sprite-reference mismatch (`27601b1b...` produced versus stored
`52e2b6bf...`), so this change introduced no new automated regression.

Private Version 13 was refreshed in place rather than creating Version 14.
Its previous four executables are preserved under
`previous-executables/20260810-left-banana-before-fix`; the ROM, saves,
settings, existing recordings, and captures were not replaced. The updated
Win32, SDL, normal headless, and trace headless hosts plus current diagnostic
tools were copied into the kit and its executable hashes were regenerated.
The kit's own 120-frame record/replay and composite/BG1/OBJ capture completed
with zero findings.

## 2026-08-10 - true 16:9 attract-demo routes

The neutral boot sequence was traced rather than treated as one screen. Demo 1
is Mainbrace Mayhem, active at frames 3,276-4,071; demo 2 is Rickety Race at
4,132-4,427; demo 3 is Parrot Chute Panic at 4,505-5,248. A second cycle
repeats the same transitions 5,193 frames later. The trace-only headless host
now optionally emits the relevant named state fields under `DKC2_STATE_TRACE`,
and the TCP decoder records the attract status/sequence in each private report.

Mainbrace Mayhem already selected proven vertical BG1 terrain, but isolated
captures showed its BG3 cloud/lighting layer only in the original 256 columns.
The missing overlay caused brightness seams at X=43 and X=299. Level `$000C`,
enabled Mode-1 BG3 `$6C00` now repeats the fully rendered authentic scanline;
no raw off-screen BG3 VRAM is exposed. Rickety Race required no new runtime
policy because its horizontal BG1 terrain and cyclic BG2 backdrop were already
covered.

Parrot Chute Panic had been intentionally centered because sub-mode `$03` was
unclassified. The imported DKC2 disassembly identifies the level-selected
alternate wasp-hive path at `$B5:B317`, which calls `$B5:B0FC/$B5:B20D`.
Its address math proves a 16-metatile, `$20`-byte row-major map. A new
`NarrowVertical` source layout is enabled only for level `$0013` plus sub-mode
`$03`; live `$17B6=$7800` selects BG2 terrain. Bounded cyclic BG1 `$6C00` and
BG3 `$6800` repeat only after that BG2 source is proven ready. Other wasp-hive
rooms remain unclassified rather than inheriting this geometry by analogy.

The diagnostic classifier was corrected to combine main and sub-screen enables,
matching the runtime PPU policy. Representative composite captures at frames
3,350, 3,650, 4,000, 4,265, 4,550, 4,880, and 5,200 render all three routes
edge-to-edge. All background checks pass; the lone frame-4,000 object finding
is a margin sprite record with `current_graphic=0`, and the visible composite
contains no missing object at that position. Midpoint composite reports have
zero findings, and isolated Parrot Chute Panic BG1/BG2/BG3 captures confirm
that the unusual far-left honey geometry comes from decoded terrain rather
than stale host pixels.

The optimized Release host completes 12,000 widescreen frames with 28 state
events, six demo starts, six demo ends, two complete attract cycles, zero
sequence errors, active video/audio, zero clipped samples, and audio maximum
delta 6,690. Synthetic Python and C video tests pass. Final normal-speed owner
validation is deliberately still open; deterministic completion and reviewed
still frames do not replace watching the motion on the target display.

The complete optimized configured matrix passes 55/56 tests. The sole failure
is the unchanged frame-3,309 sprite-reference hash: `27601b1b...` is produced
while the stored expectation remains `52e2b6bf...`. The dedicated video test,
diagnostic classifier, symbol checks, desktop/SDL smoke tests, and two-cycle
attract gate all pass, so this milestone introduces no new automated failure.

Private Version 13 was refreshed in place; no Version 14 was created. The four
previous executables are preserved under
`previous-executables/20260810-attract-before-fix`. The verified ROM, saves,
launcher settings, key bindings, recordings, and existing captures were left
in place. Updated optimized Win32, SDL, normal headless, and trace headless
hosts, generated symbol constants, capture tools, and widescreen documentation
were copied into the kit and the executable manifest hashes were regenerated.
The kit's own 120-frame record/replay plus composite/BG1/OBJ capture completed
with zero findings.

## 2026-08-13 - automatic temporal widescreen route auditor

The local Summon Night: Swordcraft Story 3 recomp was reviewed at revision
`0b8d84b71c41366fe9d88a9d717288184ba7896f` as a design reference only; no
source or assets were copied. Its adaptive-widescreen work confirms that a
rolling 256-pixel tilemap may be correct in the native center while its
off-center columns contain stale or opposite-page data. The transferable
lesson is to classify each scene/layer, prefer an exact world/map source, and
fail closed to a deliberate edge/blank policy instead of assuming that any
nonempty VRAM margin is valid. DKC2's implementation retains its own data
model and now records the chosen source directly so this condition is
measurable rather than inferred from a screenshot.

Single-frame layer isolation was useful for explaining a defect after its
frame was known, but it still required the owner to notice every pop, seam, or
wrong object. The new `scripts/audit_widescreen_route.py` runs one deterministic
route through the composite and selected isolated PPU layers, samples aligned
frames, and produces a machine-readable JSON report plus an HTML evidence
index. Raw captures remain under ignored/private output and are never added to
the source repository. `--reuse-capture` permits detector tuning without
rerunning the game or replacing the original raw evidence.

The headless host now emits opt-in `widescreen_frame=` JSON lines containing
the documented DKC2 level/camera/terrain fields, live PPU layer configuration,
world-shadow counters, placed-sprite records, and a read-only exact terrain
tile projection. This output is host-only observability. It does not write
guest WRAM/VRAM, alter input, or participate in save states.

The shared SNESRecomp shadow accounting was extended to distinguish the final
source chosen after a world-history miss: periodic fold, verified blank, or
raw wrapped VRAM. This distinction closes an important diagnostic ambiguity.
A miss followed by a verified transparent entry cannot display stale VRAM,
although it may create missing terrain; raw fallback is the direct stale-cell
hazard. A read-only world-tile lookup supports exact margin-versus-native
identity comparisons without incrementing counters or affecting rendering.

The first image-only terrain comparison produced thousands of false candidates
because palette animation, lighting, and parallax can change RGB while tile
identity remains correct. It was replaced with exact world-keyed tilemap-entry
comparison and restricted to the unique live terrain owner after a 60-frame
stable-screen warm-up. Cyclic/mirrored/clamped layers and fades are excluded.
Seam scoring remains explicitly heuristic. Placed-object checks now focus on
the widened margins and the former X=0/X=256 culling boundaries rather than
calling every intentional center-screen spawn a defect. A separate OBJ check
flags an active placed object with a nonzero graphic when no nearby isolated
OBJ pixels exist.

Eight synthetic tests cover PPM/BMP handling, edge scoring, exact
margin/native terrain disagreement, blank and raw fallback classification,
object activation/deactivation, absent OBJ pixels, and robust trace parsing.
The optimized headless target compiles with the new trace and completes a
short end-to-end capture/report smoke test.

The first real audit replayed attract frames 3,200-5,250 every 12 frames across
composite, BG1, BG2, BG3, and OBJ. It produced 171 aligned samples per layer.
Most importantly, it observed **zero raw VRAM margin fallbacks**. Therefore the
current attract output has no direct evidence of the renderer consuming stale
rolling VRAM. It did retain two verified-blank intervals: Mainbrace BG1 during
early scene fill and two Parrot BG2 samples. It also retained exact
terrain-entry disagreements, six old-boundary seam candidates, and five
boundary-relevant placed-object lifetime candidates. Review of Parrot frame
4,640 confirms a visible layer discontinuity at the old margins, demonstrating
that the detector catches a real open issue even though the underlying source
is a wrong/missing world continuation rather than raw stale VRAM.

This tool does not make owner review obsolete in the absolute sense. Without a
reference-emulator wide oracle, it cannot know artistic intent, whether an
enemy is intentionally script-triggered, or whether a dynamic BG rewrite is
correct. It changes the workflow from owner-led defect discovery to
agent-led candidate discovery and evidence ranking; manual play becomes final
validation rather than the only way to find problems.

The private diagnostic-kit template now includes `Audit-Route.ps1`, the route
auditor, and a documented coarse-pass command. The wrapper discovers the ROM
from `rom.cfg`, pairs a recording-specific starting SRAM when present, writes
to a new timestamped capture directory, and can open the completed report.
The complete Release suite passed 56 of 57 tests. The sole failure is the
unchanged frame-3,309 sprite reference hash mismatch already present before
this milestone (`27601b1b...` produced versus `52e2b6bf...` expected); the new
route-auditor test and all other private-ROM, attract, video, packaging, and
tooling checks passed.

### Version 13 `bg-02` report recovery

The owner's first complete packaged audit captured all layers successfully but
failed during analysis at BG1 frame 4,620. The file was not truncated: it had
the exact expected 229,839-byte length. Its first RGB byte was `0x20`, however,
and the PPM reader incorrectly skipped every whitespace-valued byte after the
header rather than consuming the single required separator. Because binary PPM
pixel bytes may have any value, this discarded a valid first pixel and made the
payload appear one byte short.

The reader now consumes exactly one separator (or a CRLF pair), with a
synthetic regression whose first pixel is `0x20`. Image loading also converts
actually absent or malformed samples into `capture_integrity` findings and
continues analyzing the remaining evidence. `Audit-Route.ps1` gained
`-OutputDirectory` and `-ReuseCapture`, allowing completed raw captures to be
reanalyzed without another game replay.

The original private capture at `route-bg-02-20260813-160232` was reanalyzed in
place. It completed with zero capture-integrity errors and retained 82
candidates: 59 exact terrain-entry disagreements, four native-boundary seams,
15 margin object spawns, one margin object despawn, one active margin object
without nearby OBJ pixels, and two verified-blank margin fallbacks.

The post-fix full CTest pass initially reported an SDL smoke timeout at frame
1 and a following intentional-crash drill timing failure, in addition to the
known sprite reference hash. Both unexpected tests passed immediately when
rerun independently (5.00 and 4.37 seconds respectively), along with the route
auditor test. This leaves only the pre-existing sprite hash mismatch
unresolved; the parser change is Python-only and does not affect emulation.

## 2026-08-13 - attract route source-page repair and audit closure

The owner's complete `attract-demo-01.input` report retained 46 candidates.
The raw provenance counters showed zero direct rolling-VRAM fallback, so the
terrain failures were investigated as source-coordinate errors rather than
masked with copied pixels. Bank-B5 disassembly confirms that DKC2's column
builders begin one 256-pixel page above the camera.

Two independent Y-domain defects were corrected. First, the renderer had
unwrapped every viewport row independently around the 1024-pixel PPU period.
At the half-period this could choose a different epoch from one row to the
next: Mainbrace mapped row 0 to world tile 275 and row 1 to 148. The top row
is now unwrapped once and all later rows advance continuously. Second, only
the horizontal layout selected the decompressed source page nearest
`cameraY-$0100`; the same cartridge relationship now applies to all proven
rolling layouts. Representative source rows become 179 for Mainbrace and 39
for Parrot Chute Panic instead of incorrectly following shadow rows 275/135.

Every decoded tile touching either expanded margin is now refreshed each
frame. `WsShadowForceTile` still respects a newer cartridge tilemap write, so
animated or destructible content remains authoritative. Synthetic video tests
cover the two observed rollover states, continuous row increments, source-page
selection, and partial tiles at both fine-scrolled margin boundaries. The
headless trace records complete-view and margin-only prefill counts so later
audits can distinguish a missing source from a legitimate newer game write.

The object lifetime candidates were also tested across Mainbrace frames
3,420-3,520 at one-frame resolution. None represented a real spawn/despawn
inside the wide view; the coarse 12-frame cadence had aliased changing object
slots. Lifecycle claims now require consecutive-frame input. Seam scoring now
requires persistence and is suppressed when the terrain and every enabled BG
layer have a proven wide source. The former candidates at frames 3,780, 3,900,
4,284, and 4,836 were visually inspected and are authored masts, platforms,
and hive walls that happened to cross X=43/X=299. Verified transparent
fallbacks remain in a separate safe-observation table rather than counting as
actionable defects.

The final packaged Version 13 audit replayed frames 0-5,874 every 12 frames in
composite, BG1, BG2, BG3, and OBJ. It reports zero actionable findings, two
safe transparent observations, zero capture-integrity errors, and no crash.
The report is private at
`captures/route-attract-demo-01-20260813-final-02/index.html`. Representative
images across Mainbrace Mayhem, Rickety Race, and Parrot Chute Panic were
reviewed and show continuous expanded terrain without the former honey-wall
strip or row-page discontinuity.

Both optimized desktop hosts and both headless hosts were rebuilt. Private
Version 13 was refreshed in place; its previous executables are preserved in
`previous-executables/20260813-attract-vertical-page-before-fix`, while its
ROM, saves, settings, recordings, and earlier captures were not replaced. The
complete configured matrix passes 56 of 57 tests. The only failure remains the
pre-existing frame-3,309 sprite-reference hash (`27601b1b...` produced versus
stored `52e2b6bf...`); all widescreen video, route-auditor, attract-cycle,
desktop, SDL, diagnostic, packaging, and other tests pass.

## 2026-08-13 - Mainbrace rendered-X phase correction

Owner video showed a short split at the former 4:3 boundary during Mainbrace
Mayhem's upward camera movement. This is application-rendered margin motion,
not monitor/VSync tearing. A one-frame audit of attract frames 3,700-3,960
confirmed that BG1 was the relevant world layer. It also invalidated the
auditor's previous rule that suppressed seam candidates merely because all
layers reported a source: provenance does not prove presentation alignment,
so that suppression was removed.

The frame trace exposed the timing mismatch. At the affected transition the
WRAM camera X could be one to three pixels ahead of BG1's PPU-latched hScroll.
The authentic 256-pixel center therefore rendered the older PPU phase while
the host-created margins were keyed and prefilled from the newer WRAM phase.
`Dkc2VideoTerrainShadowX` now unwraps hScroll near camera X, and that rendered
X coordinate drives terrain shadow capture, margin classification, and source
prefill consistently. A synthetic rollover/lead test covers the helper.

The optimized desktop, SDL, and headless targets and the diagnostic headless
target build successfully; `test_dkc2_video` passes. Replaying the identical
3,700-3,960 recording changes only widened-margin pixels on frames where the
two phases differ. Three retained seam candidates are unchanged authored
mast/rigging crossings, illustrating why audit candidates still require
visual classification. The full diagnostic CTest invocation was attempted,
but that build tree's integration configuration is unhealthy: 46 of 61 tests
passed while stale/missing desktop runtime outputs and headless exit-code 11
caused 15 unrelated failures. The exact targeted replay nevertheless completed
all 261 frames in composite, BG1, BG2, and BG3 without a crash. Final motion
acceptance remains an owner test in Version 13.

## 2026-08-13 - Pirate Panic Rambi fine-scroll guard

The owner recorded `pirate-panic-rambi-01.input` from a paired starting SRAM
and reported a margin-only graphical glitch after charging with Rambi while
the camera moved downward. The recording contains 6,836 frames and replays
cleanly. Camera tracing localized the rapid movement to frames 6,368-6,488.

A one-frame, five-layer audit of frames 6,320-6,520 found one relevant exact
provenance event: at frame 6,404, BG1 missed two samples in the east margin
and safely substituted its verified transparent tile. All decompressed terrain
tiles in the nominal wide span were present. The missing pixels were therefore
not stale VRAM or a bad map row; fine horizontal scroll reached the adjacent
tile just beyond the prefilled 342-pixel interval.

Terrain reconstruction now decodes one extra 8-pixel guard tile beyond each
widescreen margin. This matches the purpose of the cartridge's own streamed
guard column while remaining bounded by the existing verified source limit.
The exact frames 6,388-6,420 were replayed afterward in composite, BG1, BG2,
BG3, and OBJ: the report changed from one blank-fallback observation to zero
findings and zero safe fallback observations. This proved the fine-X guard but
did not cover or close the owner's larger visual report later in the route.

## 2026-08-14 - Pirate Panic Rambi tile-epoch correction

The complete private route was re-examined beyond the earlier narrow interval.
One-frame trace capture found three large BG1 failures at frames 6,509, 6,511,
and 6,512, each adding 1,120 verified-blank east-margin samples as Rambi's
charge ended and camera Y reversed. At frame 6,509 camera Y was `$0204` while
the rendered PPU Y was `$0004`. The fine PPU value sat exactly 512 pixels from
the camera and unwrapped to the lower epoch, while tile-aligned source prefill
selected the upper epoch. Correct tiles existed under row-128 shadow keys, but
lookup started at row zero.

`Dkc2VideoTerrainShadowY` now masks the PPU Y value to its shared 8-pixel tile
origin, unwraps that origin near camera Y, and adds the fine three-bit phase
back afterward. A synthetic regression covers the exact `$0204`/`$0004` tie
and requires the resulting row to agree with `Dkc2VideoLevelSourceTileY`.
The route auditor also retains blank bursts of at least 64 samples during
camera motion as `large_verified_blank_margin_fallback`; small bounded blank
substitutions remain safe observations.

Replaying frames 6,480-6,520 after the correction removes the 1,120-sample
bursts at all three frames. Only unrelated five-sample misses remain earlier
in the interval. A retained before/after frame 6,509 changes the stepped
turquoise blank strip into continuous authentic deck and railing. The focused
video test and all 13 route-auditor unit tests pass, and the optimized desktop
and headless targets build successfully. Final normal-speed owner validation
remains open in private Version 13.

The optimized Win32, SDL, normal headless, and diagnostic headless executables
were deployed to private Version 13 in place. The prior four executables are
preserved under
`previous-executables/20260813-rambi-tile-epoch-before-fix`; the ROM, saves,
recordings, launcher settings, key bindings, and captures were not replaced.
The packaged route auditor and widescreen diagnostic guide were refreshed to
match the new large-blank-burst classification. A replay from the installed
bundle over frames 6,480-6,520 reports zero findings and zero safe
observations. The complete Release suite passes 56 of 57 tests; the sole
failure is the pre-existing frame-3,309 sprite-reference hash (`27601b1b...`
produced versus `52e2b6bf...` expected), while the two-cycle attract test and
all other video, widescreen, desktop, SDL, diagnostic, and unit tests pass.

## 2026-08-14 - Experimental standard wasp-hive widescreen

The owner observed that Hornet Hole, Rambi Rumble, and likely King Zing Sting
remained centered in 4:3 with widescreen enabled. This was the intentional
unknown-layout gate: only Parrot Chute Panic's level `$0013` exception had
been classified under wasp-hive game sub-mode `$03`.

The statically recompiled `$80:D517` handler provides a bounded experiment.
It tests `$0AB4 & $000F`; variant five calls the alternate `$B5:B317` routine
used by Parrot, while ordinary hive variants call
`square_level_scroll_handler` at `$B5:B54A`. The adapter now assigns the
existing 48-metatile/`$60`-byte square decoder to ordinary sub-mode `$03` and
retains Parrot's 16-metatile/`$20`-byte narrow-row exception. Terrain ownership
continues to come from the live `$17B6` stream destination, so this does not
hardcode BG1 or BG2.

This is an opt-in visual experiment, not a supported-family declaration.
Synthetic C and diagnostic-classifier tests cover both the standard square
and Parrot exception. Hornet Hole and Rambi Rumble still need recorded motion
through both axes and isolated-layer inspection; King Zing additionally needs
boss-arena and behavior validation. Any bounded hive foreground/backdrop layer
that remains centered will be handled only after those captures identify it.

All four optimized Version 13 executables were refreshed in place. The prior
Rambi-corrected executables are preserved under
`previous-executables/20260814-before-experimental-hive`; ROM, saves,
recordings, settings, and captures were retained. The C geometry test and all
15 diagnostic-classifier tests pass. The complete Release suite remains 56 of
57, with only the unchanged frame-3,309 sprite-reference hash mismatch; the
two-cycle attract test and every widescreen, desktop, SDL, and diagnostic test
pass.

## 2026-08-30 - Native Mac host, exact-rate pacing, and common west boundary

The Apple-silicon port now builds as a real `DKC2Recomp.app` rather than a bare
Unix executable. CMake supplies the bundle identity and icon; `macos_host.m`
owns the AppKit Game/View menus and the Mach absolute wait; and
`build_macos.sh` bundles the Homebrew SDL2 dylib, rewrites its install name,
ad-hoc signs/verifies the complete bundle, and registers it with
LaunchServices. The menu exposes pause/settings, Assist-gated quick save/load,
fullscreen, Pixel Sharp/Smooth scaling, and live 4:3/16:10/16:9 selection.
Mutable state moved to
`~/Library/Application Support/Flat2VR/DKC2Recomp`, keeping the application
bundle and repository free of ROM, save, and generated private artifacts. The
local signature is not Developer-ID signing or notarization. The build script
also compares its requested deployment floor with SDL2's `LC_BUILD_VERSION`
minimum and raises the app target when necessary; this machine's Homebrew SDL2
requires macOS 26, so the local bundle no longer claims compatibility with an
OS on which its dependency cannot load.

The presentation geometry now has three explicit source widths: native
256x224, centered 16:10 at 308x224, and centered 16:9 at 342x224. The original
icon is source-safe project artwork rather than extracted game art. Apple GLSL
uses the OpenGL 2.1-compatible shader path, allowing the in-game overlay and
native menu to coexist in the visible bundle.

The first Mac gameplay test still showed small horizontal cadence hitches. The
game rate was not changed: it remains exactly 60.098811862 Hz. The defect was
two independent host timing gates—software exact-rate pacing followed by a
blocking OpenGL swap quantized to the display's 60/120 Hz cadence. Visible
macOS now leaves swap interval at zero, waits one absolute Mach deadline with a
short final spin before presenting, and re-anchors when a deadline is missed by
more than 2 ms rather than issuing a short catch-up frame. The compositor still
receives one complete frame atomically. `DKC2_KEEP_OPENGL_VSYNC=1` is retained
only for comparison, and the normal backend reports
`vsync=off; pacing=mach`.

### West-boundary diagnosis and generalization

The first visible frame of Pirate Panic had an ocean-only strip west of the
deck. Its ship-deck bonus `$006F` reproduced the same seam, and the owner's next
stage, vertical Mainbrace Mayhem `$000C`, reproduced it again. The common cause
was the already-proven coordinate contract: DKC2's world/camera domain starts
at X=`$0100`, while decompressed terrain begins at source tile zero. A centered
wide viewport can ask for world tiles below 32 even though no cartridge terrain
exists there. The earlier two-level allowlist was therefore too narrow.

The final rule is capability-based. Only after the live `$17B6` stream
destination identifies an enabled wide terrain layer and `$0096/$00D3`
selects a known horizontal, vertical, square, or narrow-vertical decoder may a
pre-origin tile be synthesized. World tile 31 reads source tile 0, world tile
30 reads source tile 1, and so on; reversing the tile order and toggling the
SNES entry's H-flip bit produces a reflection rather than a repeated seam. The
helper also requires that the tile touch the host-created margin. Unknown
layouts still receive the verified transparent fallback. The cartridge camera,
collision, actors, VRAM writes, save state, and authentic 256-pixel center are
never changed.

The supplied bonus state was preserved externally before replacement:
level `$006F`, game sub-mode `$0006`, camera X=`$0100`, camera Y=`$0220`,
stream destination `$7000`, snapshot SHA-256
`cff5ff417276ecb9ea5fa167aa55a17f8f6f9189a3f7956ad7e7818120975446`.
The Mainbrace exact-state branch was likewise preserved externally: level
`$000C`, NMI sub-mode `$000D`, game sub-mode `$0008`, camera
`($0104,$0B00)`, bounds `($0300,$0B20)`, map/metatile/stream
`$0000/$1600/$7800`, WRAM bank `$7F`, snapshot SHA-256
`9a4417a679e67bc819a9c2f48f85e5226e7db356bf57c6c82b4027f5b44a040c`.
These private snapshots remain outside the repository. The owner accepted
Pirate Panic and its bonus in the visible native app; an exact-state reload of
Mainbrace visibly changed the cloud-only west gutter into continuous rigging
and deck while leaving the center untouched.

The final Apple build completes all 47 configured tests, including the native
Mac app smoke, 16:10 private-ROM smoke, exact timing probe, video geometry,
widescreen diagnostics, render probes, input, rewind, audio, and runtime unit
tests. `git diff --check` also passes. This establishes the common decoded-
terrain boundary and the tested Mac host, not whole-game widescreen
certification: special/unknown handlers, bosses, transitions, and unaudited
layer compositions remain fail-closed or explicit acceptance work.

## 2026-08-30 - Ship Hold rolling-map edges and fullscreen Escape

Lockjaw's Locker remained wide, but horizontal movement exposed missing or
unrelated narrow strips at both edges. The exact-state branch was preserved
outside the repository before testing (level `$0015`, game sub-mode `$0002`,
camera `($0357,$061D)`, bounds `($0A00,$0718)`, map/metatile/stream
`$0000/$2300/$3800`, WRAM bank `$7F`, snapshot SHA-256
`f5d7a8b2b4a35fbef7b08b9decfbc14eed54f3d08bf8d84f49e25d38c9cc20c3`).
Layer-isolated movement proved two independent presentation defects: BG1 was
reading recycled pages from the 64-column VRAM ring, while bounded BG3 water
stopped at the authentic 256-pixel boundary.

The initial static-map hypothesis was rejected. The byte-exact reference's
`ship_hold_game_sub_mode` calls the scrolling path, and its NMI handler calls
both level-column and level-row DMA. Fitting the preserved WRAM source against
the native BG1 tilemap established an 80-metatile/`$A0`-byte row: the decoded
formula matched 957/957 visible cells, while tested 16-, 48-, and 96-metatile
strides matched only 60.9-65%. Sub-mode `$02` now selects that explicit
Ship Hold decoder and passes through the existing world-keyed prefill. The
water is handled separately: enabled BG3 at `$6C00` repeats the fully rendered
native scanline into the margins, preserving its HDMA phase without reading
unseen VRAM.

A deterministic 300-frame rightward replay at 308x224 completed without a
runtime failure. Every sampled frame remained wide; the terrain prefill was
complete (1,218/1,218 entries, including 319/319 margin entries), BG1 recorded
zero west/east shadow misses, and the renderer selected BG3 repeat mask `$04`.
Composite and isolated BG1/BG3 contact sheets show the formerly recycled edge
strips removed while the water spans both margins. This validates the supplied
exact state and its moving route, not every Ship Hold entrance; fresh-entry and
cross-level coverage remains open.

The same Mac follow-up restored the expected exit path from fullscreen.
Non-repeating Escape now leaves fullscreen only when the pause overlay is
closed and consumes that keypress. Windowed Escape still opens the overlay,
and Escape inside an open overlay retains its existing close/capture behavior.
The setting is persisted immediately after the transition.

### BG2 cabin-wall follow-up

A later visible capture of the same preserved Lockjaw's Locker state showed a
third independent layer defect: BG1 terrain and BG3 water filled the widened
frame, but BG2's upper cabin wall still stopped at both original 4:3 edges and
revealed the blue lower layer. Isolating BG2 proved the live signature was
Mode 1, BG1 `$3800`, BG2 `$7000`, BG3 `$6C00`, with BG1 still owning the
`$3800` terrain stream. The generic periodic fold deliberately recognizes
only proven 4/8/16-tile periods; treating the full 32-column native wall as an
automatic period would make every unknown backdrop look valid.

The first narrow fix repeated BG2's rendered native scanline. It removed the
blue columns, but a same-build policy-on/policy-off A/B found a seven-pixel
difference at the left edge of the accepted center. The cause was ordering:
the repeat bit rendered BG2 at authentic width before its hardware window was
evaluated. That candidate was initially rejected. A normal-wide isolated
source preserved the full center, but visible native-Mac QA then showed that
the clipped endpoint pixels were being copied into both margins as narrow blue
lines. The owner's follow-up explicitly identified those lines for correction.

The final policy uses two bounded facts from the exact BG2 plane. First,
the room's endpoint artifact is confined to seven columns at each native edge;
same-build A/B changed 1,791 BG2 pixels and 984 composite pixels, all within
X=0-6 or X=249-255. Second, the authored cabin wall repeats every 12 tiles/96
pixels, so the compositor sources both edge bands and both margins from the
matching interior period instead of sampling clipped native endpoints. The
corrected 308x224 BG2 center PPM SHA-256 is
`f033f78988a78020f4a9dc006153c7bb3253a483f06b105080480765d712bb72`;
the corresponding composite center is
`871d8e26a43b13bb893c4a7a05158af9e0cef416e4175deb6a28c83e4e4a2b6d`.
Cartridge VRAM, WRAM, object lifecycle, and the BG1 terrain decoder remain
unchanged.

A 300-frame rightward replay completed at both 308x224 and 342x224 with
terrain readiness retained, no blank frame, no runtime failure, and no blue
BG2 seam in the sampled composite frames 0 through 270. All 47 configured Mac
tests pass. This validates the supplied exact-state branch and moving route.
Fresh entry into Lockjaw's Locker and other Ship Hold rooms remains required
before claiming archetype-wide closure.

## 2026-08-30 - Capability-gated physical BG3 promotion

The first visual interpretation of the owner's ship-mast capture modeled the
physical Rattle Battle configuration because its missing mast/rigging shape
matched that known failure class. The center remained coherent, so the issue
was separated from terrain streaming and treated as a presentation layer-width
problem first. The byte-exact reference independently proves Rattle Battle
level `$0005`, horizontal game sub-mode `$0006`, uses the standard ship-deck
Mode-1 configuration: BG1 `$71`, BG2 `$5C`, BG3 `$79`, main/sub enables
`$17/$10`, and terrain target `$7000`. A later exact live snapshot proved that
the owner's currently loaded scene was Topsail Trouble instead; that separate
acceptance is recorded below.

The live configuration proves two independent facts. BG1 owns the decompressed
terrain stream, while BG3 is an enabled physical 64-column tilemap at `$7800`.
The host previously considered physical width only for BG1/BG2 and admitted
BG3 through a Pirate-Panic-specific level-effects-bit helper. Rattle Battle
therefore passed the terrain-readiness gate but still had BG3 clamped to the
native 256 pixels. This was the common architectural omission: physical layer
width was being mistaken for a named-level feature.

The host now derives a separate enabled physical-width mask across BG1-BG3.
It resets the PPU width latches every frame, proves the exact BG1/BG2 terrain
owner, completes the existing world-keyed prefill, and only then merges the
physical mask into the final render mask. A physical 64-column BG3 exposes the
cartridge's authentic adjacent columns; it is not decoded as terrain, repeated,
or written back to VRAM. Bounded BG3/HUD/staging screens remain clamped unless
they already have a separate source-backed rendered-scanline repeat. For the
Rattle Battle signature the final mask is BG1+BG3 (`$05`) and bounded BG2 uses
repeat mask `$02`.

Synthetic coverage locks Rattle Battle, Mainbrace, a dual-wide layout, a fully
bounded layout, null configuration, and Mode 7. The diagnostic classifier now
reports `physical_64_column` for Rattle Battle BG3 and treats its bounded BG2
as a rendered-scanline repeat. The pre-change configured Mac suite passed
47/47. After the change, the focused native-Mac/widescreen gate passed 6/6 and
the complete configured suite passed 47/47. `build_macos.sh` produced the
arm64 app at `build/macos/DKC2Recomp.app`; its packaged 4:3 and 16:10 smoke
tests passed 2/2, bundled SDL linkage resolves through `@executable_path`, the
icon is present, and strict deep ad-hoc signature verification passes. The
supported ROM SHA-256 remains
`35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`.
The packaged executable SHA-256 is
`08ed973f149b34cabc7379e632c86a852c8a60c33d517f2782c6a64f22f4a7f8`,
the icon SHA-256 is
`2a465e7e3ac15c47552226d6daac070f30dece66d77673e1cf8bc44d4cb76814`,
and the ad-hoc code-directory hash is
`c9b79e04aa231a02308f3ef78845f5c27fd46dbb`.

The physical Rattle Battle change has source-backed policy and synthetic
coverage, but no clean fresh-entry Rattle Battle route or moving on-stage
acceptance capture; whole-stage closure is deliberately not claimed.

## 2026-08-30 - Exact-state Topsail Trouble rain continuation

Before replacing the running binary, LLDB called the runtime's normal snapshot
writer and preserved the owner's live machine state outside the normal save
slots. The snapshot SHA-256 is
`da634dbc5bf3d7eef2f5c1f18bbd787cd07e90b35bee02eabc6c81023ef8c791`.
Replaying it identified the actual scene as Topsail Trouble level `$000B`,
vertical sub-mode `$0008`, with camera `(435,3846)`, terrain destination
`$7800`, `BGMODE=$09`, BG1/BG2/BG3 registers `$79/$70/$6C`, and main/sub
enables `$17/$13`. Layer isolation showed that BG1 mast terrain was already
physical-wide and BG2 already repeated; only the BG3 rain stopped at native
X=0 and X=255.

The byte-exact reference explains that distinction. Topsail's tileset selects
`ship_mast_rainy_vram_payload`, whose four `$0200` transfers place the same
rain tilemap at `$6C00`, `$6D00`, `$6E00`, and `$6F00`. That is a bounded
cyclic weather layer, not evidence for physical adjacent terrain. The host now
recognizes only level `$000B` with ready physical BG1 and enabled BG3 `$6C` as
the Topsail rain capability. It repeats the already-rendered BG3 scanline into
both margins, preserving PPU priority and line phase, while leaving gameplay,
VRAM, and the physical layer mask unchanged. The exact trace changed the repeat
mask from `$02` to `$06`; the physical mask remains `$01`.

The pre-fix isolated BG3 PPM SHA-256 is
`ca736d6a22d18e269c5bae01d69dd92c51c64a7ddd1cbbe364ee52ca20e48bcd`;
both 26-pixel margins were single-color blank strips. The post-fix BG3 PPM
SHA-256 is
`cdbab23e66d8e20a6f70dcefd2fd828665bcf711e393cf4bff2bfba92f335987`;
each margin contains the four-color rain plane, and the 256-pixel center has
zero absolute-error pixels against the pre-fix plane. The matching composite
SHA-256 is
`cd23823499ee3179e5294ac0ef6ba94faf3bbdb665876a3e5e3e1ee492a82948`.
The exact state is accepted at the reported location. A clean fresh entry and
normal-speed vertical traversal remain required for whole-stage acceptance.

The focused C video policy and Python classifier suites pass, and the complete
configured macOS suite passes 47/47 with SDL's dummy audio backend because the
owner's Mac was locked and CoreAudio would not open for the two automated app
smokes. `git diff --check` passes. The rebuilt arm64 app is strictly deep
ad-hoc signed, links bundled SDL through `@executable_path`, and has executable
SHA-256
`4b2937401eea5ed1e44f95b953351d341e984145deef2ee0e51eac3044b55051`.
Visible post-fix interaction remains pending only because Computer Use could
not access the locked desktop; the preserved-state composite and isolated
planes are the acceptance evidence for this exact location.

## 2026-08-31 - Canonical Mac bundle and native Quick State recovery

The apparent Topsail regression was a deployment identity failure. The live
process came from `build-macos-native/DKC2Recomp.app` with an executable
SHA-256 beginning `7a533bc2`, while the accepted Topsail code was present only
in the newer `build/macos/DKC2Recomp.app` (SHA-256 beginning `4b293740`). Both bundles used
`com.flat2vr.dkc2recomp` version 0.0.2, so LaunchServices could focus or reopen
the stale copy. Disassembly confirmed that the running binary lacked the exact
level `$000B` / sub-mode `$0008` repeat capability. After package verification,
the build script now replaces the former local app location with a symlink to
the newly signed canonical bundle, unregisters the diagnostic copy from
LaunchServices discovery without deleting it, and then registers only the
canonical bundle.

Quick Save/Load had an independent host-policy failure. The user's active
`launcher.cfg` had `AssistTools=0`; the AppKit menu disabled both items and the
SDL loop discarded their commands. Native Mac menu commands now bypass only
the Assist binding gate and always address Slot 1. Configurable save/load
shortcuts, overlay slots, rewind, and fast-forward remain gated. A new hidden
native-Mac regression runs with an isolated user directory, injects a platform
save at frame 30 and load at frame 60, and requires `save_load=passed`.

Before visible testing, the real Slot 1 state was preserved unchanged with
SHA-256
`f5d7a8b2b4a35fbef7b08b9decfbc14eed54f3d08bf8d84f49e25d38c9cc20c3`.
The rebuilt canonical and compatibility executables are byte-identical with
SHA-256
`b0cbccd5985b9d667fa9c8156dc5f556c5cccac8910fea05a7f6e26c9bfff1e4`
and Mach-O UUID `2C339F82-01EE-3DC3-8E40-7245132F0022`; both bundles pass
strict deep ad-hoc signature verification. In an isolated visible canonical
app with Assist Tools off, Command-L loaded the preserved Topsail state and
showed both rain margins, and Command-S replaced the isolated Slot 1 file with
a valid 299,458-byte state. The persistent user Slot 1 was not modified.

## 2026-08-31 - Ship-hold HDMA phase and alternate backdrop page

Two independent Lockjaw's Locker defects were preserved as exact frame-boundary
states without retaining either state in the user's normal save slot.

The first defect flashed lower ship planks even with no input and a fixed
camera at `(832,1597)`. BG2 was byte-identical for all 600 stationary frames,
while BG1 cycled through 11 margin images. The water effect changes BG1's live
per-scanline horizontal scroll by -1, 0, or +1 pixel, but the widescreen shadow
lookup used only the frame-latched camera. At tile boundaries those coordinate
domains selected adjacent margin cells on alternating HDMA phases. The shadow
lookup and 16x16 tile phase now use the frame anchor plus the signed 10-bit live
scroll delta. The corrected 16:10 BG1 center crop matches the 4:3 oracle with
zero differing pixels across the first 32 HDMA phases. The exact state passed
600 stationary frames and 180 hold-right frames with no route-audit findings;
the user then confirmed the visible flashing was fixed.

The second defect appeared later in the same room at fixed camera
`(1592,1469)`: the cabin wall stopped at the left native edge. The room had
switched the same 96-pixel bounded BG2 wall from tilemap base `$7000` to
`$7800`, while the repeat capability recognized only `$7000`. This produced
195,840 west-margin verified-blank samples over 16 stationary frames. The
Ship Hold capability now accepts both observed pages, retaining the existing
96-pixel period and seven-pixel endpoint repair. The corrected exact replay
has no route-audit findings, changes no pixel in native X=7..248, and visibly
continues the wall through both margins. The native endpoint repair remains the
same deliberate correction already accepted for the `$7000` page.

The complete configured macOS suite passes 48/48, both focused Python suites
pass 32/32, and `git diff --check` passes in the main and SNES runtime trees.
The rebuilt canonical app passes strict deep ad-hoc signature verification;
its executable SHA-256 is
`c120eef03c0a95a5d4c66317dced387c527db16a16e52ca533eb3ae5720fc95b`.
The user's original Slot 1 was restored byte-for-byte with SHA-256
`f5d7a8b2b4a35fbef7b08b9decfbc14eed54f3d08bf8d84f49e25d38c9cc20c3`.
A clean fresh entry and the full cross-layout matrix remain outstanding before
either shared presentation correction is claimed as whole-game acceptance.

## 2026-08-31 - Native macOS v0.0.3 fork release

The complete accumulated native-Mac and widescreen work was prepared for the
`elliotttate/DKC2Recomp` fork as the v0.0.3 alpha release. The parent repository
now pins the presentation runtime to the dedicated
`elliotttate/snesrecomp` branch
`dkc2-widescreen-presentation-v0.0.3` at commit
`f8a79309936388ff96a213783cf45e239dbd4c49`.

The release app is an arm64 macOS 26 bundle, version 0.0.3, with bundled SDL2
and a strict deep-valid ad-hoc signature. Its executable SHA-256 is
`ab49b7cf524a19b9c15e0be7ccc4c81a620d1c84b972dfedd34472c6f6067b4e`.
The ROM-free `DKC2Recomp-macos-arm64-v0.0.3.zip` archive has SHA-256
`9e232b389deb288f806acbda87e15646bb519ba3dd5699c3df59bbcf35a031c7`.
Extraction into a clean temporary directory preserved both the version and
signature, and the extracted executable was byte-identical to the packaged
build. The configured macOS suite passed 48/48 immediately before packaging.

## 2026-08-31 - Topsail lower-camera shadow capacity

The owner's active Slot 1 Quick Save was copied to an ignored immutable repro
before testing. The preserved state is 299,458 bytes with SHA-256
`fe310b4b7148b22b4ea5aad9436878cdb5c7b8295e35e372fd9193666ca97235`;
the user's live Slot 1 remained byte-identical throughout the investigation.
The exact state is Topsail Trouble level `$000B`, gameplay sub-mode `$0008`,
camera `(636,3848)`, maximum camera `(768,3848)`, Mode 1, BG1/BG2/BG3 screen
registers `$79/$70/$6C`, and terrain stream `$7800`.

At exact frame zero the terrain decoder produced 1,189 cells for each of its
four source bands, but the last two bands were reported as zero accepted
prefills. Each presentation margin then recorded 1,792 BG1 shadow misses and
1,792 verified-blank fallbacks. `Dkc2VideoLevelSourceTileY()` resolves this
camera to source tile rows 512-540, while SNESrecomp's host-only world shadow
allocated rows 0-511. Every exact cell in the second vertical epoch was
therefore rejected even though the cartridge's recycled 64-column tilemap and
native center continued to render normally.

The fix expands only `kWsShadowYTiles` from 512 to 1,024. A synthetic geometry
test locks Topsail's top row 512 and bottom row 540 inside that capacity; the
test failed against the old constant before the runtime was changed. No PPU
coordinate rule, source decoder, cartridge write, camera bound, object window,
or gameplay state changed.

The corrected exact frame accepts all four 1,189-cell prefill bands and all
three 290-cell margin bands, with zero west/east BG1 misses or raw fallbacks.
Its composite SHA-256 is
`f7c4f6926fbad4d70ff506a0615d2bb8acb85733008fef74f79bda22c2f8d983`.
The 308x224 native center matches the exact 4:3 oracle at every pixel. WRAM
remains
`ec44970b2443b7b0c52e0a4ade99d3c63bd3b78e7caffe5ec07b0c82f69186b5`
and VRAM remains
`cdc9c893ff456b381aeda1b395ccf01ac7f33b45a82fe9f569e3ec0070adb7ed`
before and after the host correction.

A 600-frame no-input replay and a 260-frame left/right input replay from the
preserved Quick Save completed without blank frames, runtime failures,
incomplete prefills, shadow misses, or raw fallbacks. Twenty-six paired 16:10
and 4:3 samples retained a pixel-identical native center. The earlier Topsail
checkpoint at camera `(435,3846)` also completed its 260-frame replay with all
13 trace samples fully populated and zero margin misses or raw fallbacks. The
rebuilt, strict-signature-valid native app was then launched through the real
Mac Game menu and Quick Load returned visibly to the preserved location with
the mast/deck continuing at left and the sail/deck continuing at right.

This closes the supplied exact-state branch only. A clean fresh entry with
normal-speed vertical traversal remains required before the whole Topsail
Trouble stage is accepted.

## 2026-08-31 - Topsail terminal-right guard containment

The owner's next active Slot 1 was again preserved unchanged before testing.
The 299,458-byte state has SHA-256
`40a2e90c62aa3517aabcb6efa74f36e8c5db5a264d9002686f3f331b4c15cede`
and reproduces level `$000B`, sub-mode `$0008`, camera `(768,3301)`, maximum
camera `(768,3848)`, BG1/BG2/BG3 `$79/$70/$6C`, and terrain stream `$7800`.
Its world shadow is healthy: all four 1,218-cell source bands and all three
290-cell margin bands are populated, with 1,792 hits and zero misses on each
BG1 side.

The marked black/white rigging was instead isolated to BG1 source tiles 96-99.
Those tiles form the cartridge streamer's one-metatile guard beyond the
terminal 256-pixel viewport. The guard is required for native fine-scroll
underrun protection, but this vertical-room example proves it is not general
side artwork. A new vertical-layout presentation predicate forces only whole
tiles at or beyond `maximumScrollX + 256` to the live verified-transparent
character when they are sampled by a host margin. Horizontal and all other
layout policies remain unchanged, and a tile that touches any native-center
pixel is never eligible.

The exact candidate changes 1,312 composite pixels, with a bounding box of
16:10 output X=282-307 and Y=9-147. Its SHA-256 is
`0fcc52448c85f67ec6ce423e06c8ea62ef2b50f7cd51b56dba8387e58381ed72`.
The complete native center matches the exact 4:3 oracle with zero differing
pixels; frame-zero WRAM remains
`9d8546484732d8030de07106e7725f09b9e752e6cfecd18d4f76e290a06adb11`
and VRAM remains
`613212ed5d17e237a607de52ce111e8ddf15148e4c9277b3a11dacd93bb2798c`.

A 600-frame stationary replay and a 260-frame left/right input replay both
complete without blank frames, incomplete prefills, shadow misses, or raw
fallbacks. Across 26 paired motion captures, the 16:10 native center remains
pixel-identical to 4:3. The rebuilt native app was then Quick Loaded through
the real Mac Game menu; the terminal east margin visibly shows the continuous
rain/cloud background instead of the disconnected guard fragment. The app was
left running for owner review. The analyzed Slot 1 remains preserved under its
recorded hash; the live Slot 1 subsequently changed again during visible
review and was deliberately left intact rather than restored or overwritten.

This accepts the supplied exact state and the vertical-layout terminal-edge
policy only. Clean fresh-entry normal-speed traversal and broader vertical
room-end coverage remain open.

## 2026-08-31 - Krow's Nest color-math margin continuity

The owner's new Slot 1 was copied to an ignored immutable repro before
testing. The 299,458-byte state has SHA-256
`ecb88165a7fde58a5575083558dcfbf2706191e7d2a708f4e388bcec7d92fb19`
and reproduces level `$0009`, gameplay sub-mode `$0008`, camera `(256,288)`,
maximum camera `(384,288)`, BG1/BG2/BG3 `$79/$70/$6C`, and terrain stream
`$7800`. Exact layer isolation showed continuous colored clouds on repeated
BG2, foreground nest/mast art on physical BG1, and a grayscale cloud-lighting
plane on bounded BG3.

The PPU registers explain the visible dark bands: main screen `$04` contains
only BG3, subscreen `$13` contains BG1, BG2, and OBJ, `CGWSEL=$02` selects
subscreen addition, and `CGADSUB=$24` enables color math for BG3 and the
backdrop. Repeating BG2 without BG3 made each host-created margin add the
colored cloud plane against black instead of against the grayscale lighting
plane used by the native center.

The presentation policy now admits a BG3 rendered-scanline repeat only when
level `$0009`, sub-mode `$0008`, Mode 1, BG3 `$6C00`, main/sub `$04/$13`,
`CGWSEL=$02`, `CGADSUB=$24`, and a ready physical BG1 terrain layer all match.
Synthetic negative cases keep the layer clamped when the scene or either
color-math register differs. No cartridge register, memory, tilemap, camera,
streamer, object, collision, or gameplay coordinate is changed.

The exact 308x224 candidate PPM has SHA-256
`0535c470a336370eaf84885b213842d4a81626105f8979275ffcb440e5da881d`.
Its complete 256-pixel center matches the exact 4:3 oracle with zero differing
pixels or channels. Frame-zero WRAM remains
`12bf6f68bc6e041662d7f193d8ecf2cb92e2f5202c7fbb74d358fc637388681a`
and VRAM remains
`3c38a16321f61b18d983ac0a1f59e9df09b1720fa7b98b86c0959225440d0ff5`.
Three independent 600-frame neutral replays are byte-deterministic, each
ending with frame hash
`b93c147d42c7927ebd8706ab96508339f3abf846a18f4b8692b07cc345c49c7a`,
WRAM hash
`ef45ce51a8eecdb91bcc1c0de6d346baf27ac607ef8dfee35fc2b391037b1177`,
and the same VRAM hash above.

The rebuilt native app passed its strict deep signature check, was relaunched,
and loaded the unchanged owner Slot 1 through Quick Load. Visible-window QA
showed the moving boss scene with continuous cloud lighting through both
16:10 margins and no darker bands. The app was left running for owner review.

This accepts the supplied exact-state branch and exact color-math signature.
A clean fresh entry and full boss behavioral closure remain open.

## 2026-08-31 - Lava-stage scanline terrain-role reconstruction

The owner's active Quick Save was copied before testing to
`.cache/repros/user-states/dkc2s0-20260831-100018.sav`; its SHA-256 is
`03c728c8e51a2546f7e0e718300fa08a6470df36843b52bf1a688139c0ff712a`.
The exact state reports level `$0007`, Mode `$09`, BG1/BG2/BG3
`$67/$79/$74`, main/sub `$17/$00`, and terrain destination `$7800`. The first
bad supplied frame is around global frame 70,445. Its frame scroll anchors are
H=`[829,414,103]`, V=`[448,487,121]`; HDMA changes the upper visible band to
H=`[414,207,103]`, V=`[487,859,121]` before restoring the lower band.

The root cause was presentation/streaming ownership, not a missing level
allowlist. DKC2 gives BG1 and BG2 different roles inside one frame, while the
host selected one terrain owner and one world-shadow coordinate set before
HDMA ran. Repeating every changed plane made ground discontinuities. Exposing
raw BG1 columns instead revealed an unauthored orange 24x16 staging block.
Hard-coding level `$0007` was rejected because the register structure itself
fully identifies the capability.

The final path detects a live two-plane phase exchange only for the proven
Mode-1 geometry. On affected lines BG2/BG3 repeat their authentic rendered
scanlines, while BG1 receives a separately keyed decoded terrain shadow. An
exact 32-or-33-column by 29-row capture from the live 64x64 BG1 tilemap then
overwrites the native viewport and straddling edge tile, preserving the center
as the oracle. The ordinary BG1 world/scroll keys are restored when the band
ends. Failed alternate decoding also restores them immediately and is attempted
only once per band, so unsupported states remain fail-closed. These are
host-only shadow and presentation changes; WRAM, VRAM, camera, collision,
objects, and the cartridge stream are untouched.

The final 308x224 image is retained privately at
`.cache/widescreen-diagnostics/lava-split-dynamic/composite-final.ppm` (with
the reviewed PNG at `composite-v4.png`). Cropping its native center and
comparing it with the 4:3 oracle produces absolute error `0`. Three independent
120-frame exact-state replays are byte-identical: framebuffer
`d5972f6f9619564a4f7ad3b740364dd2e358ac0d79ed4b96bd2e6224299a9d15`,
WRAM `6347dd246e482a6c77c0d8fae092f21475841779dd411df1026634b87a2fbf84`,
VRAM `580727e1884a47b4f9871749606bbf5b05af45d6557ff0acc11814bc6ff21e34`,
CGRAM `696b6f8fbdece2afe46e99e6af6831a70c2ad2c370fdb65e508582cb424844a8`,
and OAM/source
`3c38db7996c1aa2e53272312c14270adf27e4c4cec5f1994dad2c3c51b9228ff`.
The targeted video and PPU tests pass, as does the complete 48-test macOS
suite. The preserved owner state remained unchanged.

The owner then saved a later state at the same level's hard-left camera
boundary. It is preserved separately as
`.cache/repros/user-states/dkc2s0-20260831-103635.sav` with SHA-256
`03500e2789a2a9d514dc56f75410b9236f61bd6b087d9cdd7c75c7585fd5d8c3`.
That state has camera `(256,488)` and exposed two follow-up defects: the
temporary BG1 split prefill overwrote ordinary BG1 margin history after its
line-1-through-175 role, and the split context was prepared at the cartridge
scroll while the final renderer applied a +43-pixel endpoint presentation
bias. The latter left the requested right-margin world cells outside the
decoded range and exposed raw rolling-VRAM slices.

The renderer now snapshots only the ordinary BG1 margin cells before the
temporary role, restores their exact invalid/captured/prefill ownership when
the band ends, and keys the split capture, scroll, and decompressed prefill to
the same presented X coordinate. The snapshot is bounded to at most 640
host-only cells and is not serialized. Separately, a general endpoint policy
clamps the presented camera to `$0100 + margin` through
`$0AFC - margin`; it biases rendered BG scroll and OBJ placement together but
does not mutate the cartridge camera or any gameplay-owned memory. This is the
same architectural behavior used by DKC1: no host gutter asks for world data
before the authored start or beyond the final valid camera span.

The corrected one-frame 342x224 image is retained privately at
`.cache/widescreen-diagnostics/lava-final.ppm` with SHA-256
`4d18d3ef0c0ce49a64a5d0f8c5719223fe8e606cac9bc0ff5887997f94f1af43`.
Three independent 120-frame replays of the later exact state are byte-identical
and end with framebuffer
`ee30d539ad77f37b47c250906b52c913d2a039065493320198e88404d2190c0e`,
WRAM `fb37b62d3e98181acf07d341a77cba1cc7b89f2be1da273c46c4d5d1cb6b0c4c`,
VRAM `8da4140c954c0b712603086e8fefe163c5f71fbee813c72a4c2271925d845c83`,
CGRAM `d4e65ad4929ffd1a77ddb7982b376fc163ef4553cfc1a9aace5fb53304005d0e`,
and OAM `342377aa258cffe5906a7cc502343048168f0947ffcb1ec50929bba351e5d0bc`.
The full 48-test macOS suite and strict app signature pass. The final native
Mac window was relaunched, Quick Loaded through the real menu shortcut, and
remained visually continuous at 60 FPS while the character stood at the
boundary; the app was left running for owner review.

This accepts both supplied exact-state branches and the structural
scanline-role signature. A clean pre-entry anchor was not available in the
retained repro corpus; live normal-speed restart/traversal remains the
fresh-entry gate before the entire level can be claimed fixed.

## 2026-09-01 - Lava-stage post-split effect-role containment

The owner's newer active Quick Save was preserved before testing at
`.cache/repros/user-states/dkc2s0-20260901-075922.sav`. It is 299,458 bytes
with SHA-256
`7915b650c5610211f73337e5e918ea39dc9f50ced678c4e8ed55bf904312b1a2`.
The state remains level `$0007`, Mode `$09`, BG1/BG2/BG3 `$67/$79/$74`,
main/sub `$17/$00`, terrain destination `$7800`, and begins at camera
`(384,504)`. A deterministic neutral/left/right/left input replay first shows
a measurable lower-band margin divergence at relative frame 26; by frame 40,
camera `(331,490)`, the complete lower-left 43-pixel margin contains the
reported lava slab. The first reconstructed frame is clean, proving that the
defect is accumulated presentation history rather than corruption serialized
in the supplied state.

Isolated output identifies the slab as BG1 below the line-175 HDMA boundary.
The upper band structurally exchanges BG1/BG2 roles and correctly decodes BG1
as terrain. Below it, BG2 returns to the terrain role while BG1 is a bounded
lava/effect plane. `WsShadowFrame()` runs before HDMA and had captured BG1 as
though its frame-anchor role covered every scanline, including rows the
cartridge never displays in that role. Restoring the temporary split cells and
scroll keys therefore could still expose those unauthored BG1 history cells
as the camera phase changed.

The per-line policy now records only the already-proven structural role swap.
Inside the swapped band it retains the existing decoded BG1 terrain plus
rendered BG2/BG3 continuation. After the band restores its frame roles, BG1
continues from that line's authentic native 256-pixel rendering and BG2 stays
world-keyed. No level ID, camera bound, cartridge tilemap, VRAM write, WRAM
field, object window, collision rule, or gameplay coordinate is changed.
Synthetic video-policy tests lock the before-swap, active-swap, and
post-swap masks.

The exact 320-frame replay is retained under
`.cache/widescreen-diagnostics/lava-new-motion-candidate-all/`; its reviewed
ten-frame contact sheet is `contact-10.png`. The old left slab is absent
through both direction reversals. An independent 420-frame replay from the
earlier same-level hard-left anchor
`.cache/repros/user-states/dkc2s0-20260831-103635.sav` (SHA-256
`03500e2789a2a9d514dc56f75410b9236f61bd6b087d9cdd7c75c7585fd5d8c3`)
also remains continuous while traversing right, left, and right. This is an
earlier clean same-level branch, not a true pre-entry route.

Three independent final 320-frame replays are byte-identical. They end with
framebuffer
`26693dbfc8ba7158019b0251daea6c033c57234dec857f5ccb11721bdfb2d7d8`,
WRAM `4e1c2307c67cfe692302d8f99f115073a8f7c875d0443dd82b928f1e9cae2e18`,
VRAM `caa1b8d4ed3037605510b489cb90f34c76ce46b2601d0b5044b0d89cf167db5b`,
CGRAM `ecec43e40b3dcad087c52e3360f29f1de702ca220151db26dbed27d8444f7526`,
and OAM/source
`28d8561bbe33ead52a930f5212a9eb6a44648ce3f2d49d3fd8793e16d32ea119`.
The baseline and candidate runs have identical WRAM, VRAM, CGRAM, OAM, audio,
and event progression; only the intended host framebuffer changes. The exact
reported frame-40 native center matches the 4:3 oracle at every pixel. The
complete configured macOS suite passes 48/48 and `git diff --check` passes.
The final headless executable SHA-256 is
`b8bed8f64c39adf1b0334206d6861b34df81f72e2fc5fd605c19fc5240fa0802`;
the final strict-signature-valid native app executable SHA-256 is
`aa5202c831b142befcd0a808a4967c43097302e794105244dc38f1ec6aadcd7a`.

The signed canonical app was relaunched from `build/macos/DKC2Recomp.app`,
Quick Loaded through the native Mac shortcut, and driven left before reversing
and driving right across the supplied spot. Both widened margins remained
continuous at 60 FPS; the former lower-left lava slab did not return. The two
reviewed visible-window captures are retained privately as
`.cache/widescreen-diagnostics/lava-live-final.jpeg` and
`lava-live-final-right.jpeg`. The app was left running for owner review, and
the live Quick Save was not overwritten.

This accepts the supplied moving exact-state branch, the earlier hard-left
same-level anchor, and the structural post-split role policy. A true clean
pre-entry traversal and complete level closure remain open before the entire
stage is claimed fixed.

## 2026-09-01 - Lava-stage direction-reversal phase continuity

The same immutable owner Quick Save remains the exact reproduction:
`.cache/repros/user-states/dkc2s0-20260901-075922.sav`, 299,458 bytes,
SHA-256
`7915b650c5610211f73337e5e918ea39dc9f50ced678c4e8ed55bf904312b1a2`.
The deterministic 320-frame neutral/left/right/left route is retained at
`.cache/widescreen-diagnostics/lava-new-left-right.txt`. Its original output
is correct through relative frame 255, loses broad rectangular layer regions
in both margins at frames 256-265, and recovers at frame 266.

The first source-scoped continuity guard removed the black flash and kept BG1
stable, but the owner's next visible test showed that the margins still lacked
part of the composition. Per-plane capture isolated those remaining cutoffs to
BG2 while BG1 and BG3 stayed populated. The world-shadow trace agreed: BG2's
west/east miss counters jumped by roughly 930-1,085 lookups per frame during
the failure even though the frame-start prefill contained the margin cells.

A temporary per-scanline register trace corrected the initial diagnosis: HDMA
never stopped. At relative frame 256, for example, the frame anchor was
H=[146,585,145] and V=[468,503,125], while line 1 became H=[580,290,145]
and V=[503,867,125] before line 156 restored the frame roles. This is the same
two-plane role swap, but camera reversal and column streaming advanced the
alternate BG1 terrain phase five pixels beyond the frame's BG2 anchor. The
original structural detector allowed only four pixels, so it rejected frames
whose measured lead was five or six and incorrectly sent BG2 back through a
world-keyed lookup for the alternate role.

The horizontal phase gate now accepts the observed maximum of six pixels while
the vertical allowance remains four and every mode, screen-enable, physical-
map, two-plane-switch, and source-readiness condition remains unchanged. The
unit model covers a six-pixel accepted reversal and a seven-pixel rejected
counterexample. The existing source-scoped proof still resets on every source
or state boundary. No level ID, cartridge register, WRAM/VRAM write, camera,
collision, object, or gameplay coordinate changes.

Fine scrolling exposed a separate native-oracle detail: an 8-pixel renderer
chunk can straddle X=0 or X=256 while the world-shadow lookup is tile-granular.
Choosing the shadow tile for the whole chunk fixed the margin seam but changed
up to 539 native-edge pixels on reversal frames. The normal 4bpp renderer now
decodes that chunk from the cartridge tile for center pixels and from the
shadow tile for margin pixels. All 25 candidate frames 248-272 have absolute
error zero across the complete 256x224 center against independent 4:3 captures.

The reviewed 25-frame 16:9 reversal sequence and isolated BG1/BG2/BG3 planes
are retained under
`.cache/widescreen-diagnostics/lava-reversal-layer-complete-candidate/` and
the corresponding `-bg1`, `-bg2`, and `-bg3` directories. All three layers
remain populated through frames 248-272; BG2's cumulative west/east misses and
blank fallbacks stay fixed at 11/29 through frames 254-267 instead of jumping
during the reversal. The 16:10 sequence is retained under
`.cache/widescreen-diagnostics/lava-reversal-layer-complete-16x10/`. The
earlier same-level hard-left state was replayed for 420 frames through
right/left/right movement and remains continuous at
`.cache/widescreen-diagnostics/lava-reversal-layer-complete-earlier/`. All 25
16:9 candidate frames have absolute error zero across the complete 256x224
center against the independent 4:3 oracle. Three final 320-frame 16:9 runs in
`.cache/widescreen-diagnostics/lava-reversal-layer-complete-determinism/` are
byte-identical and end with framebuffer
`26693dbfc8ba7158019b0251daea6c033c57234dec857f5ccb11721bdfb2d7d8`,
WRAM `4e1c2307c67cfe692302d8f99f115073a8f7c875d0443dd82b928f1e9cae2e18`,
VRAM `caa1b8d4ed3037605510b489cb90f34c76ce46b2601d0b5044b0d89cf167db5b`,
CGRAM `ecec43e40b3dcad087c52e3360f29f1de702ca220151db26dbed27d8444f7526`,
and OAM/source
`28d8561bbe33ead52a930f5212a9eb6a44648ce3f2d49d3fd8793e16d32ea119`.

The complete configured macOS suite passes 48/48. The rebuilt app is validated
and signed below after the exact-state and layer checks.

This accepts the supplied exact-state reversal, the earlier same-level anchor,
16:9 and 16:10 presentation, and the bounded source-scoped phase policy. A
true clean pre-entry traversal and complete level closure remain open.

## 2026-09-01 - Lava-stage camera-independent role proof

The owner moved slightly farther through the same room and made a new Slot 0
Quick Save. It was copied unchanged to
`.cache/repros/user-states/dkc2s0-20260901-085922.sav`; the 299,458-byte state
has SHA-256
`e58bcf19aee8465709ed482622bbd6c6a6f8824276cb66f99c86bc5d17d056b6`.
At its first visible frame, the widened left and right margins both ended the
upper terrain band abruptly and exposed rectangular lava/terrain slabs below
it. Isolated BG1 and BG2 captures reproduced the discontinuities while the
native 256-pixel center remained intact.

The per-scanline trace showed the same split ownership as the preceding lava
repros but at a different camera phase. Frame H/V anchors were
`[37,18,4]`/`[458,495,123]`. Lines 1-165 changed to
`[18,521,4]`/`[495,863,123]`, then line 166 restored the anchors. Live BG1
therefore took frame BG2's exact phase and live BG2 moved to a distant effect
phase. The old structural gate nevertheless rejected every one of those 165
lines because it also required BG1 to move at least 128 pixels from its own
frame anchor; here the ordinary BG1/BG2 phases happened to be only 19 pixels
apart horizontally and 37 vertically. The test was camera-position-dependent,
not a different rendering mode.

The role detector now requires only the actual invariant: the same strict
Mode `$09`, screen-enable, 64-column BG1/BG2, and bounded BG3 signature; live
BG1 matching frame BG2 within the proven six/four-pixel tolerance; and live
BG2 making the large independent phase switch. One-plane and near-scroll
counterexamples still fail closed. A new unit vector locks the later exact
`[37,18] -> [18,521]` transition. No level ID, cartridge register, WRAM/VRAM
write, camera, collision, object, or gameplay coordinate changes.

Frames 0-5 were captured as composite, BG1, BG2, BG3, and OBJ under
`.cache/widescreen-diagnostics/lava-later-spot-fixed-*`. Both margins are
continuous in every frame; the first corrected 342x224 PPM has SHA-256
`8d68e9fdc95a595a5a9aebc097a013470f4359761de81366e0c14f180f75c015`.
All six 16:9 and 16:10 samples have absolute error zero across the complete
256x224 center against independent 4:3 renders. A 200-frame left/right motion
route repeated three times with byte-identical logs and ended with framebuffer
`f6f6731e6139594c76a8964f39d4d17b8cf778c0c352800da99b37ad4e38bc15`,
WRAM `4e851247f64f3cd4eceac39e2fae7e0ece77843f98ffe22fba5387032041bab7`,
VRAM `2a1c563e19cf60b059ef90050617c358e1025c1b8d36988f85d656cfdb468425`,
CGRAM `4bfcd203c900680b049bd473f0f4e39d2c8ac5aeacb09b8d2302f329dbf2fe15`,
and OAM/source
`44e24050a7f8476ae70421e5ec1969f110112959297c7fdac7c0f9862baa71e3`.

The preceding 320-frame reversal branch and 420-frame hard-left branch remain
pixel-identical to their accepted outputs and retain their prior final hashes.
The complete configured macOS suite passes 48/48. This accepts the new supplied
exact state, both earlier same-level motion branches, and 16:9/16:10
presentation. A true clean pre-entry traversal and complete level closure
remain open.

The final native executable has SHA-256
`0b820a299e71c0d50edf3b246d8e786a95de8a8a747862d9e368ef28ba8a540f`;
the app and bundled SDL framework pass strict code-signature verification. The
old process was closed gracefully, the signed build was launched from the
canonical app bundle, and Command-L visibly restored the untouched owner save.
Both margins remained continuous at 60 FPS at the reported spot and after a
short rightward traversal. Assist Tools were returned to their normal disabled
state, and the app was left running for owner review.

## 2026-09-01 - Lava balloon-band BG3 screen-assignment independence

The owner's next active Slot 0 Quick Save was copied unchanged to
`.cache/repros/user-states/dkc2s0-20260901-094113.sav`. The 299,458-byte state
has SHA-256
`00b36ea7dec36aec5d83450e35c34bfbf81c39be1698e6ec321f18050188b642`.
It reports level `$0008`, game sub-mode `$0012`, Mode `$09`, BG1/BG2/BG3
`$67/$79/$74`, terrain destination `$7800`, and camera `(833,432)`. Both
host-created margins were black above the lava-surface band; isolated output
showed the same cutoff in BG1 and BG2 while BG3 was blank and OBJ remained
correct.

The temporary per-scanline trace found the same terrain-role exchange as the
earlier lava states. Frame H/V anchors were `[643,833,834]` / `[442,431,175]`.
Line 1 changed BG1/BG2 to H=`[833,416]`, V=`[431,831]`, exactly proving that
BG1 took frame BG2's terrain phase while BG2 moved to a distant effect phase.
What differed was screen assignment: the frame starts at main/sub `$13/$04`,
lines 1-122 use `$13/$00`, lines 123-181 restore only BG3 on sub with
`$13/$04`, and line 182 restores the frame scrolls. The old gate required
three main-screen backgrounds and no background subscreen, so it rejected all
181 exchanged lines even though BG1/BG2 ownership was unchanged.

The structural proof now requires BG1/BG2 plus OBJ on main, no BG1/BG2 on
sub, physical 64-column BG1/BG2 maps, bounded BG3, the distant BG2 switch, and
the existing six/four-pixel BG1-to-frame-BG2 phase match. BG3 may independently
be main, sub, or disabled; the repeat result includes only bounded BG2/BG3
planes enabled on that exact line. A synthetic vector locks both `$13/$00`
and `$13/$04`, while missing BG2 and BG1/BG2 subscreen counterexamples remain
rejected. No level ID, cartridge register, WRAM/VRAM write, camera, collision,
object, or gameplay coordinate changes.

Frames 0-5 were captured as composite and isolated BG1/BG2/BG3 under
`.cache/widescreen-diagnostics/lava-balloon-spot-candidate-*`. Both margins
are continuous in every composite and in both contributing planes. The final
16:9 PPM has SHA-256
`c4c12ab65ff6cd7216a70a95a7bfb25caf96147cc7176f2725ff9c10942d9855`;
the 16:10 PPM is
`52ce966b00f2fa85fd109690d00e1b7b5bc345a6aa8e4da924e863e2bbd075bd`.
Both complete 256x224 centers have absolute error zero against the independent
4:3 render. Twelve samples across a 240-frame neutral replay remain visually
continuous at
`.cache/widescreen-diagnostics/lava-balloon-spot-candidate-temporal/`.

Three independent 240-frame replays are byte-identical and end with framebuffer
`b3feffca9a30e6eda871f8b36c12f501feda5f575c49b2ce1d2fe75d77b84cdd`,
WRAM `67bac5a60ba9d120dbb14a0d11dc16fd2a58811349bdac53c97b4f05f04c1c67`,
VRAM `0005a8e2bdaaeb95479011ee982d4f1a85f8f67065994f5410d9725eb8a6818c`,
CGRAM `71e9b678592e085406923b92255ef9e4f25445b5be54cc51eff96c4f84873f0a`,
and OAM/source
`1391e27da8e52f81064c5347d290d1b4545bd56917ee72e67343f4706dd2015b`.
The preceding 320-frame reversal and 420-frame hard-left branches remain
pixel- and state-identical to their accepted outputs. This accepts the new
exact-state branch and its neutral temporal behavior; clean entry and complete
level traversal remain open.

The complete configured macOS suite passes 48/48 and both worktrees pass
`git diff --check`. The final headless executable has SHA-256
`25e1fcabddafa0185d9c82135d7a60e923aee77d42b457175bffa57202b7ad40`;
the final native app executable is
`096871d7c91d06fb86edc287ad022239ef614f747923a00c2ebbf06cce1a7c6e`.
The app and bundled SDL framework pass strict code-signature verification.
The old process was closed gracefully, the signed canonical bundle was
launched, and Command-L visibly restored the still-byte-identical owner Slot 0.
Two live-window inspections three seconds apart showed both margins continuous
at 60 FPS with no return of either black block. The app was left running for
owner review and Quick Save was not invoked.

## 2026-09-01 - Structural widescreen policy and preserved-state corpus

Twelve journal entries between 2026-08-30 and 2026-09-01 followed the same
shape: an owner Quick Save, one isolated layer, and one policy keyed to a
level number, sub-mode, or exact register signature. Three decision points
produced that churn: a hand-written list of scenes whose bounded BG3 could
repeat, a split-scroll detector that hard-coded one swap direction, a
Mode-`$09` composition signature, three magic tolerances and a sticky flag,
and boundary tile policies whose asymmetry came from one Topsail save. This
pass replaced all three with properties of the live PPU geometry and added a
regression that judges a rule on every preserved state at once.

Rules now in force, each with the fact that justifies it:

- Every enabled bounded background repeats its rendered scanline. A
  32-column map wraps at 256 pixels on hardware, so the period-256 repeat of
  the rendered line (HDMA phase, windows, and color math included) is what a
  wider PPU would draw. Five scene signatures plus the Parrot Chute BG1 case
  collapse into `Dkc2VideoRepeatLayerMask`.
- A 64-column allocation whose extension page is another enabled layer's
  base page is bounded. Mudhole Marsh BG3 `$6D` extends from `$6C00` into
  BG1's `$7000` map; `Dkc2VideoTilemapPagesCollide` keeps it out of both wide
  masks without naming the level.
- Each repeated line continues at the period its own rendered interior
  proves (`PpuSetWidescreenLayerRepeatAutoPeriod`, DKC2 only). Lockjaw's
  Locker's wall is 96-pixel periodic in pixels on 151-171 of 224 rows but
  not in tile entries or character indices, which is why the two tile-level
  measurements tried first found nothing. Only 64-column BG1/BG2 allocations
  rebuild their seven endpoint pixels from the period.
- Rolling BG1/BG2 layers are classified per HDMA band. `runner/dkc2_hdma.c`
  dry-runs the cartridge's HDMA tables (BG offset latch, TM, TS) before
  drawing; a band whose scroll is within six/four pixels of the owner's frame
  anchor is served from the one terrain store, the other physical layer
  through the new `WsShadowSetEntryAlias` view; any other band repeats. The
  lava exchanges need no swap direction, composition signature, backup or
  restore of shadow cells, or sticky state.
- The presented viewport is biased and, for rooms narrower than two margins,
  clamped per side to the authored extent (`Dkc2VideoPresentationMargins`).
  The west-reflection and east-mask tile policies became unreachable and were
  removed.

Removed: `Dkc2VideoCanRepeatShipHoldBackdrop`, the ship-hold period and
edge-repair constants, `Dkc2VideoResolveWestBoundaryTile`,
`Dkc2VideoShouldMaskEastBoundaryTile`, the three `Dkc2VideoSplitScroll*`
functions, the 640-cell shadow backup, the split prefill and native-VRAM
capture, `WsShadowRestoreDebugCell`, the sticky role-swap flag, and the BG2
periodic-fold registration. The per-scanline loop now applies one band table.

Evidence. `scripts/check_widescreen_state_corpus.py` replayed the fourteen
distinct preserved Quick Saves under `.cache/repros`, `.cache/private-states`,
and the two live capture directories for six frames each at 4:3, 16:9, and
16:10, in composite and BG1/BG2/BG3 isolation, with the pre-change binary as
the reference. The check aligns the presented native viewport by the trace's
presentation bias, which the old binary lacked; the tool reconstructs the old
bias formula for it.

- Interior native center exact (zero differing pixels) in thirteen of
  fourteen states at both aspects, including the biased Krow's Nest
  `(256,288)` and terminal-right Topsail `(768,3301)` states.
- The hard-left lava state `(256,488)` differs by 46 pixels per frame in a
  bottom-left triangle (X=35-42, Y=215-223). The same check on the pre-change
  binary reports the same 46 pixels: the lower-band BG1 lava plane proves no
  pixel period, and the +43 bias places that 4:3 region inside the PPU's
  margin path. Recorded as an open roadmap item, not a regression.
- All lava states: composite identical to the reference; the isolated BG3
  margins now repeat on every line (2.5k-7k pixels over six frames) with no
  composite change.
- Ship-hold states: the persistent old-boundary seams that a plain 256
  repeat produced are gone; margins are within 10-19k pixels of the former
  hand-tuned output over six frames versus 55-79k for the plain repeat, and
  170 of 224 rows are identical to it at camera `(1592,1469)`. The former
  edge repair had changed 900 and 978 native center pixels in those states;
  the per-line rule changes zero, because on periodic rows the authentic
  endpoints already match the period and the old repair was rewriting the
  non-periodic picture rows.
- Neutral boot, frames 3,300-5,200 every 50: native center identical to the
  reference on all 39 sampled frames across three attract demos; 448 margin
  pixels changed in total.
- Pre-existing, unchanged: the level `$000F` sub-mode `$0009` Quick Save has
  a blank right BG1 margin, and the terminal-right Topsail state has a blank
  16:10 left BG1 margin. Both are recorded on the roadmap.

Synthetic coverage: `tests/test_dkc2_video.c` gained presentation-margin,
terrain-phase, page-collision, and HDMA dry-run vectors (the balloon-band
table with its 127+38 split entry, TM/TS, indirect, unreadable, and no-channel
cases); the engine's `tests/ppu/ppu_sprite_limit_test.c` gained the auto
period and endpoint repair cases and passes under clang;
`tests/test_check_widescreen_state_corpus.py` covers bias alignment, edge
separation, legacy bias reconstruction, visible-margin gating, boot mode, and
reference comparison; the capture tool's classifier tests follow the
structural labels. The configured macOS suite passes 49/49 in a fresh
`build-macos-refactor` directory built from this tree. No ROM, save state,
recording, generated source, or capture entered the repository.

### Follow-up: authentic 4:3 window, explained margins, streamer assessment

The three items left open above were closed the same day.

**Hard-left lava corner.** A presentation bias moves part of the authentic
4:3 viewport into the PPU's margin path: at bias +43 its first 43 columns sit
left of screen X=0. A repeated layer served those columns from its repeat or
period continuation, which is exact for a 32-column wrap but not for the
non-periodic lower lava plane on 64-column BG1. Repeated layers now render
the biased 4:3 columns from real VRAM (`PpuWidescreenRepeatAuthenticExtra`,
applied in the window calculation and the padded merge) and continue only
beyond them. The first attempt evaluated the widen-mask exclusion before the
repeat clause, so bounded layers outside the mask rendered nothing there and
the merge copied transparent columns into the 4:3 region: the corpus caught
it at once (Krow's Nest 47,460, terminal-right Topsail 25,686, and hard-left
lava 1,902 differing interior pixels over six frames) and the engine test was
extended to keep the repeated layer outside the widen mask, which it had not
done. With the clause order fixed, every one of the fourteen preserved
states has zero differing interior pixels at 16:9 and 16:10, the hard-left
lava state included; the only output change against the previous build is
that state's 564 pixels across its 48 captured images, and the neutral boot
capture is unchanged (center 0, 448 margin pixels over 39 frames). The copy
is clamped to the visible per-side margin so a camera outside the authored
range can never sample unrendered columns.

**Two blank margins.** Both are authored emptiness. The level `$000F`
sub-mode `$0009` state (camera `(813,469)`, maximum X 24,320) decodes an
empty BG1 map for world tiles 131-139 and 143-148 beyond the viewport, with
an equally empty span at tiles 116-123 inside it; every east-margin cell is
present in the store and holds the transparent character. At terminal-right
Topsail `(768,3301)`, the 16:10 left margin is world X=716-742, which the
16:9 capture shows empty inside its native view. The diagnostics doc now
says how to confirm such a warning before treating it as a defect.

**Letting the cartridge stream the wide window.** The generated streamer
was read rather than patched. `dma_level_columns` uploads one 8-pixel
column of 32 entries per camera step, latched by `$17CA` and directed by
`$17D6`; `dma_level_rows` already uploads both 32-column pages of a row.
Every column a wider stream could add would be decoded from the same
`$0098`/`$17B4` map the host decodes into the world-keyed store, so the
presented pixels could not differ from the current margins; the change
would only move the work into guest VRAM, alter save states and VRAM hashes,
and bend the engine rule that simulation stays untouched. Assessed and not
implemented; the measured geometry is in the hardware notes.

The macOS application was rebuilt from this tree with `build_macos.sh` and
its hidden smoke runs (overlay/rewind/fast-forward at 4:3, 16:10, 16:9, and
quick save/load) completed; see the changelog for the release note.

## 2026-09-01 - Level-wall edge policy

The owner's first play test of the structural build reported that leaving a
level's left wall produced "a large extreme movement": the crystal-cave
Quick Save at camera `(256,4760)` (level `$0025`, vertical sub-mode `$0C`,
copied unchanged to `.cache/repros/user-states/dkc2s0-20260901-174600-crystal.sav`)
looked right while standing at the wall, then the background jumped as soon
as the Kongs stepped right. A held-Right replay from the hard-left lava
state measured the cause. The cartridge camera holds 256 for fourteen
frames and then advances two or three pixels per frame; under the endpoint
presentation bias the presented view stayed at 256 until frame 33 while the
camera reached 298, then began scrolling at the camera's catch-up speed.
For those nineteen frames the Kongs slid across a frozen picture and every
sprite, HUD included, moved 43 pixels with the shrinking bias.

The wall presentation is now a selectable edge policy
(`Dkc2VideoEdgePolicy`; `DKC2_WIDESCREEN_EDGE=reflect|bars|shift`;
`WidescreenEdge=0|1|2` in `launcher.cfg`). `reflect`, the new default,
keeps the presented view locked to the cartridge camera and mirrors the
nearest authored terrain columns across both authored boundaries
(`Dkc2VideoResolveEdgeTile`), with a physical 64-column BG3 falling back to
its rendered-line repeat within one margin of a wall. `bars` also keeps the
view locked and clamps the visible margin to the authored extent. `shift`
is the former bias. The same replay under `reflect` shows the presented view
tracking the camera from frame 15 with no HUD motion.

The corpus, now fifteen states with the crystal cave, has zero failures and
an exact interior center everywhere under the default. The three wall
states changed, as they must. Two new heuristic seam warnings were examined:
the Topsail terminal-right BG1 warning is an authored mast edge at world 768
(the previous build carried the same 8.9-ratio discontinuity at that column,
merely not at the checked position, and the decoded margin differs from the
previously native pixels in 39 of 9,632 pixels, all animated rigging), while
the hard-left lava BG3 warning is the plane's own wrap seam. Its margins
match the 256-pixel wrap of the visible center in every pixel, and the
contrast between map columns 255 and 0 is 114.5 against about 15 for
neighboring columns; the previous build carried that seam at wide column
255 under the bias, the locked view carries it at both old 4:3 boundaries,
and the console shows it whenever the layer scrolls. Per-line period
continuation is now limited to 64-column allocations, the only case with
no hardware wrap to reproduce; that changes no corpus pixel and keeps the
ship-hold wall's continuation.

## 2026-09-01 - Glide edge policy and the pause-menu choice

The owner asked whether the view could keep its 4:3 edge pinned at a wall
and simply show more on the far side, without the stop-and-go of the old
bias. Any pinned edge must eventually hand over to the centered view, and
the handover is the relative motion between sprites and background; the
only freedom is where and how fast it happens. `glide` is that handover
spread thin: the same pins as `shift`, but the inward bias is released one
pixel per eight pixels of camera travel from each wall, so the background
scrolls at seven eighths of the camera speed for 344 pixels at 16:9 and
sprites drift over it at one eighth of their speed. At the wall the frame
is pixel-identical to `shift`. The corpus under `glide` keeps every
interior center exact through the bias-aware check, and only states within
eight margins of a wall differ from the `reflect` run. The four policies
are now chosen from the pause menu's Settings page, remembered in
`launcher.cfg`, and still overridable with `DKC2_WIDESCREEN_EDGE`.

After trying the four policies in play, the owner chose `glide` as the
default for both games; v0.0.4 ships with it. `reflect`, `bars`, and
`shift` stay selectable, and a `launcher.cfg` that already carries
`WidescreenEdge=0` from an earlier build keeps `reflect` until the pause
menu changes it.

## 2026-09-01 - Stale ring columns at the biased end of the view

Play testing v0.0.4 found a vertical strip of wrong tiles just inside the
right margin in the lava stage and again in the crystal mine, both under
the default `glide`. The preserved Quick Saves (level `$0008` at camera
414, bias 24; level `$0024` at camera 260, bias 43) showed the same shape:
the wrong columns were exactly the last `bias` columns of the PPU's
256-column window. Under a bias the cartridge's authentic VRAM window is
[-bias, 256-bias) in screen columns, so the native fast path and the
repeat-band merge, which both treated all 256 columns as authentic, were
reading the rolling ring's stale page. The old `shift` bias never showed it
because it only applied within one margin of a wall, where the ring is
freshly built for two screens.

The fix mirrors what DKC1Recomp already carried: the shadow gains a
per-layer native viewport inset from the host bias, applied to the fast
path and to the per-pixel split of a straddling chunk on both the 8x8 and
16x16 paths; the padded merge copies only the authentic window (plus a
32-column map's exact wrap), continues a 64-column ring's stale tail from
it, and detects periods and repairs endpoints on the intersection of that
window with the screen interior. At bias 0 every path is unchanged. Under
`glide` and `reflect` both states now agree on every world column, the
corpus passes both with exact centers, and the engine test gained a
64-column stale-tail case.

## 2026-09-01 - Voids beside player-held walls

A third play-test spot, the crystal mine shaft at camera 448, was a
different class from the biased-window bug. Holding Left there does not
move the camera: Squawks meets the shaft wall, `$0AFC` still reads the
level-wide maximum, and no WRAM word holds a minimum. West of world 448 the
map is wholly transparent for the whole visible height, so a wide margin
showed the BG2 crystal backdrop through a hole the console never has. The
edge policies cannot see such a wall, and the corpus cannot either: its
center was exact.

The prefill now carries the structural rule DKC1Recomp proved on Croctopus
Chase, with two additions the ship levels forced. A margin metatile column
that is empty for the entire visible height, whose first non-empty
metatile toward the native edge is fully populated and backed by another
fully populated one toward the native center, corroborated on an adjacent
row, is continued from that source; a partial metatile in between fails
closed. Without the full-height requirement the rule filled Rattle
Battle's portholes with planks, and without the thickness requirement it
stacked crates into Topsail's sky beside the mast. With both, the shaft's
solid bands (the top band and the three bottom bands) continue the rock
and its open cave bands stay open, matching the cave beside them.

Two smaller things came out of the same state. The prefill forced decoded
tiles over live history for every cell outside the presented native
window, which under a bias includes columns the 4:3 oracle shows, and an
unstaged bottom guard row then differed from the console's stale line by
eight pixels; forcing now applies only outside both the cartridge window
and the presented one. And the WsShadow inset fix was confirmed on the
mine's start (bias 43, 43 columns) as well as the lava stage.

## 2026-09-01 - Reconstruct: an upscaler experiment for the Retina panel

The owner asked for a new upscaling method to try on a modern MacBook
screen. The presenter was a fixed-function OpenGL 2.1 blit with nearest
or bilinear sampling, and on the 16-inch panel (3456x2234) the 342x224
frame is shown at about ten times its size: nearest gives uneven pixel
widths at that fractional scale and bilinear blurs. Reconstruct is a
single GLSL 1.20 fragment pass over the same texture (the GL 2.0 entry
points resolved through SDL, since the platform header only declares the
1.x API) that treats every output pixel analytically: flat inside a texel
and blended over one output pixel at its edges, so straight edges stay
crisp at any scale; a 2x2 checkerboard or one-texel line dither between
two colors decoded into the mid-tone the artists meant a CRT to show; and
an xBR-style corner test on the 21-texel footprint that rebuilds diagonal
edges along antialiased 45-degree lines, with 2:1 slopes where the edge
continues. The stages are cumulative modes and the edge blend has a
strength, so each can be judged on its own from the pause menu.

Verifying a GPU path without a visible window needed two hooks: a hidden
window's back buffer reads back empty on macOS, so the presenter draws
the capture into an EXT framebuffer object and reads that
(`DKC2_DESKTOP_SCREENSHOT`), and `DKC2_DESKTOP_TEST_LOADSTATE` starts the
run from a preserved state. Both states from today's play test were
captured under nearest, bilinear, and the four Reconstruct modes at a
2562x1440 drawable for side-by-side comparison.

After the first captures the owner liked the level-2 result but wanted it
softer. Two controls were added: softness, which widens every transition
band (edge lines and pixel boundaries) from one screen pixel to up to
three, and smooth shading, which blends a texel's flat interior toward a
bilinear gradient wherever its neighbors are close in color, so the
terraced faces of the pre-rendered Kongs and the dithered lava sky become
continuous while outlines keep their contrast; mode 4 adds 3:1 slopes.
The matrix of softness 50, softness 100, shading 60, both, and level 3
was captured on the lava and crystal-mine states; softness 50 with
shading 60 read best and is now the Reconstruct default.

## 2026-09-02 - Ship-deck rigging in the margins

The owner's Pirate Panic state showed the foreground rope in the right
margin ending at a false apex with a second strand descending from it.
Layer isolation put the rope on the physical 64-column BG3, and a
world-space mosaic built from the native windows of a held-Right replay
showed every margin column ahead of travel disagreeing with what the same
world column later displayed natively. A per-frame VRAM diff of the ring
gave the cause: `$B5:AA88` uploads one 8-pixel column exactly when the
native edge reaches it, and the row streamer `$B5:AC25` rewrites all 64
ring words of a row from a buffer in which only the 33 native columns were
rebuilt. The ring beside the view is either 512 pixels stale or a previous
row's leftovers, so no policy over the ring can be right.

The recompiled column builder (`$B5:A950`) reads a static map in ROM bank
`$F5`: 40 column-major columns of sixteen entries at `$26A7`, wrapping at
1280 by 512 pixels, into 32-byte metatile definitions at `$2087` whose
bits 14-15 mirror the definition and EOR the tile's own flip bits (the
first cut ORed them and mismatched one pre-flipped tile). A Python decode
reproduced all 33 ring columns over 32 rows, so the host now decodes that
map into a third world-keyed shadow layer. Two engine changes were needed:
the store's layer count, and a shadow hook in the 2bpp renderer (BG3 in
mode 1), which had none; the 4bpp path's per-pixel boundary split was
mirrored for it and covered by a unit test.

The gate is the decode itself: all 32 fully uploaded native columns over
the 28 fully visible rows must match the ring for the current frame, and
the streamer is recognized by its own latches. The first cut compared the
column latch with the WRAM scroll and lost the margin on every frame the
scroll crossed an 8-pixel cell: the cartridge applies the PPU scroll, the
upload, and the latch in the NMI after the frame logic, so the latches
agree with the PPU phase, one frame behind WRAM. Keyed to the rendered
phase, a 171-frame held-Right replay at 16:10 has zero margin pixels that
differ from the world mosaic (37,479 before), and the composite renders at
16:10 and 16:9 show one continuous rope.

A second state from the same level, after a Rambi run at camera X 4662,
showed a wooden block in the top right margin: BG3 ring columns holding a
row upload's leftovers. The trace showed the rigging decode not recognized
there. The signature had required `$17BC` to equal the rigging scroll, but
`$17BC` is the 5/4 camera target the scroll is moved toward by at most 8
pixels per frame; after the charge the scroll trailed it by one pixel for
good. The two real latches (`$C6`, `$17CE`) and the native verification
are the signature now, and the state decodes exactly.

With that signature the same state's held-Right replay still dropped the
margin for 37 frames while Rambi descended: six native cells disagreed
with the decode in flag bits only. Diffing the ring frame by frame showed
them written by the row upload when the camera crossed an 8-pixel cell,
and every altered cell's high byte was the previous cell's high byte,
with the last one spilling into the next row's first word: the rigging
row DMA never writes `VMAIN` and had inherited increment-on-low-byte from
an earlier upload, so the word transfer landed its high bytes one word
late. The console shows the same row. The verification now accepts
exactly that pattern (low byte exact, high byte from the previous decoded
cell, any high byte for a page's first word) through a unit-tested pure
rule, and both replays are again free of margin differences.

A third state, on the deck past the barrels at camera Y 360 and moving
on, showed stale rope pieces again in the right margin once the camera
dropped below Y 512. The rigging's PPU vertical scroll is only eight bits
wide, `(camera Y - $101) & $FF`, and its row latch `$17CE` is camera Y &
`$F8`; unwrapping the scroll as a 10-bit value picked the 1024-pixel epoch
nearest the camera, which is wrong from map Y 256 on, so the row latch
check failed and BG3 fell back to the ring. The vertical key is now
rebuilt in 256-pixel epochs from the camera (`Dkc2VideoRiggingShadowY`,
unit-tested) and the latch compared in its low byte. All three preserved
states now stay verified through their held-Right replays with zero
margin pixels differing from the world mosaic.

## 2026-09-02 - Stale mast tiles at the right wall of a vertical stage

The owner's mast-climb state (level `$000C`, camera X at its 768
maximum, glide bias -26) showed the crow's-nest cloth and platform edge
on the mast, 256 pixels above where the stage has them. A fresh headless
replay from the state was clean, so the fault was store history. A probe
comparing the store with the decode in the columns the bias slides into
view found captured entries equal to the ring's second page: in this
vertical stage the cartridge rewrites that page with the same stale 32
entries on every row upload, and the store attributes writes to the
other page by the last horizontal travel direction, which a one-pixel
camera jitter to 767 flips. Those cells were the one place the terrain
prefill still let history win over the decode ("the columns it slides
into view keep the history they had"). Every presented cell outside the
cartridge's authentic window now takes the decoded map, as the ordinary
margins always did; ForceTile still yields to a game write from the last
frame. The climb-and-detour replay drops from 1,040 mismatching cells to
zero, and the three deck-level replays are unchanged.

## 2026-09-02 - Water surface stopping at the 4:3 edges

The owner's ship-hold state (level `$0015`, camera 998x1704) showed the
water surface line ending at the native edges while the water tint below
it continued across the margins. Layer isolation put the surface on BG3,
a bounded 32-column map, and the trace showed the reason: TM is zero at
frame start and TS holds BG1, BG2, and OBJ; BG3 is switched on only inside
the water band by HDMA. The repeat policy read the frame-start enables,
so BG3 was neither wide nor repeated and the band drew its native 256
columns only. The bands the HDMA scan already records carry each band's
TM and TS, so the repeat policy now takes the union of the frame-start
enables and every band's; the surface line repeats across both margins
like any bounded layer.

## 2026-09-02 - Objects cut at the authentic margin under a positive bias

The owner's Topsail Trouble state (camera X 261, glide bias +26) showed a
Zinger beside the rope net losing its right half at exactly the cartridge
window plus one margin. The game-side culls were not the cause: the
sprite renderer's cull, widened by the margin, allows objects well past
that point. The shared PPU's nine-bit object X decode was: an X at or
beyond 256 plus the current right margin is taken as negative. Under a
positive bias the renderer shifts every object left by the bias, so the
game legitimately places objects for the presented right margin up to the
bias beyond the authentic margin, and those X values were wrapped to the
far left and dropped. The positive range now extends by a positive bias,
covered by a PPU unit test (margin 16, bias 8, X 278 presents at 270) that
fails on the previous decode. The Zinger renders whole.


## 2026-09-02 - Steam over solid rock beside the view in Red-Hot Ride

The owner's Red-Hot Ride state (level `$0008`, camera 567x432) showed a
translucent steam column rising through the rock platform in the left
margin. Layer isolation found no layer drawing it: the steam is a bounded
32x32 BG3 on the sub screen, enabled only inside the lava band, and the
isolated renders lose the color math. VRAM dumps showed a single
three-column block at map columns 3-5 that stayed put as the camera moved,
and the recompiled routines named it: `update_lava_hot_air_effect` uploads
a three-by-eighteen block from ROM animation tables for each geyser in
four WRAM slots, and `handle_lava_geyser_positioning` registers geysers
from a ROM list when they come within 12 pixels of the view and clears
them when they leave. The margin column was the map's 256-pixel wrap of
the real geyser under the balloon, and since the cartridge clears a
geyser's block as soon as it leaves the 4:3 view, a margin drawn from the
ring could show either a false column or none. The host now decodes the
geyser list and the tables into the shared BG3 shadow store and forces
every margin cell and a 24-pixel inset of each native edge each frame,
verified against every block the cartridge has fully drawn (54 of 54
cells on every traced frame, the frame-counter prediction of the
animation frame never disagreeing with the ring). Replays walking left,
right, and across the geyser at both edges show the column leaving the
presented window in place with no wrap sliver; the corpus (26 states)
keeps every center exact, and its one changed state, the older Red-Hot
Ride save at camera 833, gains the geyser standing 41 pixels left of its
view.

## 2026-09-02 - Foreground rocks cut at the 4:3 edges in Red-Hot Ride

The owner's Red-Hot Ride state on Rambi (camera 4920x469) showed the
foreground rock silhouettes ending at both native edges with the lava
behind them continuing across the margins. The band scan gave the
geometry: below the lava line BG1 shows a 64x64 map at `$6400` at twice
the camera speed, above it BG2 shows the same map at half speed (HDMA
swaps BG1SC and BG2SC at the lava line), and neither band is at the
terrain phase, so both repeated their native line at 256 pixels. VRAM
dumps at three camera positions proved the map static, its rock rows
non-periodic across all 64 columns and its spike rows periodic every 32,
so the 512-pixel wrap is the authored continuation. The HDMA scan now
records each band's tilemap base; a band whose map is 64 columns wide, is
not the terrain stream's destination, has had no VRAM write since the
camera last traveled 24 pixels (the engine now stamps every VRAM page
with its last write frame), and whose content is authored to wrap (no
populated row with a period that fails to divide 64 columns, no blank
strip at either map edge, which keeps the ship hold's 96-pixel cabin wall
and its narrower second backdrop on the repeat policy) becomes a plane
band, presented by a new engine raw band that draws the layer's own map
across the margins with the world shadow bypassed. A survey of all 26
preserved states found no non-terrain 64-column map written during 160
frames of movement, so a level starts with its pages counted as
traveled. Replays show the rocks continuing into both margins from the
first frame; the corpus keeps every 4:3 center exact, and the lava-stage
states change only in their margins and the biased columns beside them.

## 2026-09-02 - A cave pocket opening wider than the console beside a held wall

The owner's crystal mine shaft state (level `$0024`, camera 448x3421,
Squawks against the shaft wall) showed the cave pocket in the rock wall
continuing further down in the left margin than in the native view. The
decode is not misaligned: it reproduces all 928 native cells at zero
offset, and holding Left for 90 frames never moves the camera, so world
448 is where the player holds the camera and the map west of it is empty
for the whole height. That is the case the structural wall rule handled
on 2026-09-01, but the rule continues the rock only on rows whose first
authored metatile toward the native edge is full and backed by another
full one, and fails closed on the pocket's boundary rows, whose edge
metatiles are partial. Those rows now mirror the authored terrain across
the wall line once the same column has been corroborated as a wall on
any visible row (`Dkc2VideoMirrorSourceTileAcrossEdge`, the reflect
policy's geometry applied at a held wall). The pocket becomes a
symmetric hollow in the wall instead of an opening onto the backdrop,
the solid bands are unchanged, and the corpus changes only in the older
crystal-mine state at the same camera position.

## 2026-09-02 - Blue margins on a level intro

The owner's Barrel Bayou state is the level-name intro: a static
picture on a 64-column BG1 map at `$7000` with every scroll at zero, the
camera unset (`$17C0` reads `$FFFF`), and nothing at the terrain stream's
destination. The host correctly finds no terrain owner and clamps the
wide layer, but a clamped layer draws nothing in the margins and the PPU
fills them with the backdrop color, CGRAM entry 0, which this scene sets
to pure blue (the console never shows it: the picture covers the whole
screen). Whenever the world is unproven the host now paints both margins
black after the frame, the same presentation a bounded screen gets; the
picture's own map has both halves populated but nothing proves it is a
512-pixel painting authored to continue, so black is the honest margin.
Pressing Start fades the intro out; gameplay begins at camera 256x464
with the terrain proven, and the margins fill with the level as before.

## 2026-09-02 - Slime Climb's margins keyed from a phase nothing displays

The owner's Slime Climb report (a state at the level-name card; the spot
is the dock a few screens into the level) showed pillars, rafts, and
their reflections wrong at both edges. The band dump explained it: at
camera 430x5713 every one of the frame's 195 HDMA bands renders BG1 at
h 428, v 593, the camera phase, while the frame-start registers read
426 and 80. The host keyed the terrain store, decoded the prefill rows,
and classified every band from that frame-start pair, so no band was at
"terrain phase" and all of them repeated their native line into the
margins. The terrain phase is now selected per frame: the frame-start
pair when it lies within the terrain lead of the camera, otherwise the
band pair at the camera phase covering the most lines
(`Dkc2VideoSelectTerrainPhase`). With it the store serves every margin
cell of the walk from the level start to the dock, no band repeats, and
the trace's new `terrain_source.phase` shows `from_band` set for the
whole stage. The level-name card that precedes it is a static picture
with no terrain owner and keeps black margins.

## 2026-09-02 - Bramble Blast margins decoded from the wrong map rows

The owner's Bramble Blast state (level `$002D`, camera 263x2800) showed
blocks of unrelated tiles in both margins. The trace had the answer in
its prefill counters: the decode reproduced only 907 of the 1,302 native
cells, so the level map was being read wrongly. A sweep of row strides
against the native ring found 160 bytes per row (80 metatiles) at 876
of 896 cells with zero offset, where the square layout's 192 bytes gave
363; the stage runs a ship-hold-style column builder under the square
sub-mode. Instead of a per-level table, the prefill now verifies the
stride in use against the staged native window every frame and, below
90 percent, adopts the best candidate above the gate; the decode of
row-major maps takes the stride as a parameter. The state calibrates to
160 at 97 percent on its first frame, the prefill matches 1,282 of
1,302 cells, and both margins continue the vines and the walkway.

## 2026-09-02 - A barrel cannon vanishing from the right margin

At the start of Bramble Blast, with the glide bias at its full 26 pixels
against the left wall, the owner saw a barrel cannon in the right margin
disappear on a small step left. The object table showed why: the cannon
stays alive until its X is 314 pixels past the camera, the placement
radius's $120 plus one margin, but the presented window ends at the
camera plus the bias plus 256 plus the margin, 334 pixels, so the cannon
was released with 20 pixels of it still on screen. A second window, the
live sprite list's release check at `$B5:9C52`, had never been widened at
all and would have released it at 304. The override script now adapts
that routine's `$B0`/`$160` pair, and the two widening helpers subtract
the presentation bias from the left slack and add it to the right, so
every camera-relative window follows the presented window instead of the
cartridge camera. Stepping left from camera 379 to the wall at 256 now
keeps the cannon on screen throughout; the next cannon, 656 pixels in, is
released 17 pixels beyond the presented edge.

## 2026-09-02 - Margins blinking out at the top of Bramble Scramble

The owner's Squawks state at the top of Bramble Scramble (camera Y 256)
lost its margins for the frames the camera sat at the top. The trace
showed the stride verification at 81 percent and no stride passing.
Re-doing the comparison offline gave 99 percent: the first viewport row
is the guard row above the map, tile row 8191, and the verification
added rows to it without the prefill's wrap, so every row after the
first overflowed the decoder's range and only the guard row was counted
(26 of 32 cells is exactly the 81 percent reported). The rows now wrap
like the prefill's; the state is ready from its first frame at 100
percent and stays ready through the flight down.

## 2026-09-02 - Kackle cut at the edge with a twin in the far margin

The owner's Haunted Hall state showed the ghost cut at the native left
edge and a second ghost in the right margin. He is not a sprite: layer
isolation put both on BG2, a 64-column map whose left page holds his
32-column block and whose right page is blank, positioned by the layer's
scroll. The band repeated its native line into the margins: the left
margin got the line's empty right end (the cut) and the right margin its
left end (the twin). The map's own wrap has his off-screen part in the
columns beside the view and nothing beyond, so a 64-column map with one
populated page now counts as an object plane and is presented raw, with
no static gate since it is rewritten as he animates. His left part shows
in the margin, the twin is gone, and he stays whole through the ride.

## 2026-09-02 - Blank terrain margins deep in Parrot Chute Panic

The owner's Parrot Chute Panic state (camera 293x7885) showed the
honeycomb backdrop through both margins. The trace made it a one-line
diagnosis: the prefill decoded all 1,271 cells but found none of them
present in the store, and the terrain layer's shadow counters were all
misses and blanks. The stage is 13,040 pixels tall; the store kept 1,024
rows of world Y and rejected every key beyond 8,192 pixels, so from the
middle of the shaft down nothing was ever stored. The store now keeps
2,048 rows; the state serves every margin cell from the decode, and a
120-frame descent stays ready with no blank lookups.

## 2026-09-02 - Arctic Abyss presented as a bounded screen

The owner's state turned out to be the Arctic Abyss name card, and the
black-margin screenshot the level start behind it. Two things were
wrong. The stage's scroll handler, sub-mode `$19`, was not in the layout
classifier, so the host saw no wide layer and presented the level as a
bounded screen between black bars; a stride sweep against the terrain
ring showed the map stored 80 metatiles per row like a ship hold (896 of
896 cells at zero offset), and the sub-mode now maps to that layout. And
the name card itself, like every card, is a static picture the layout
path cannot extend; the three cards on hand all run NMI sub-mode 11, so
a card is now presented full width with its backgrounds mirrored at both
edges rather than centered between black bars, which reads as widescreen
without inventing or wrapping any art. The level start is ready from its
first gameplay frame with every margin cell served from the decode.

## 2026-09-02 - Name cards: black, not mirrored

The mirrored card presentation lasted one build. The owner preferred
black margins on the cards, so a card is now presented like every other
bounded screen, centered between black margins, whatever its map size;
the Arctic Abyss layout fix from the same report stands.

## 2026-09-02 - Backdrop through a wall above a continuing floor

The owner's mine state (level `$0023`, camera 256 with the Kongs held
at a wall at world 512) showed a rectangle of blue crystal backdrop
beside the view above the platform. The fill map from the prefill's
classifier explained it: past the wall the floor rows are full and the
rows above them empty, so the decode put the map's empty cells there and
the bounded backdrop layers showed through, while the structural wall
rule stood aside because it required the whole visible column to be
empty. The rule now asks only that the column be empty from the visible
top down through the row, which still refuses portholes and doorways
(wall above them) but continues a wall standing on a floor that goes on
past it. The state continues the rock above the platform and keeps the
platform itself.

The corpus flagged one other state under the relaxed rule: Topsail
Trouble's quick slot gained ten by twenty-four pixels of mast wood in
the sky at the top-left corner of a 16:9 frame. The classifier grid
showed why. The masts there are single full columns, but on one row a
sign hangs beside a mast and makes it two thick, and the rule's proving
row only asked for the empty/full pair, not the thickness. With the
whole column required empty the sail platform at the bottom of that
column had hidden the case. The proving row now has to show the wall two
thick as well; the mine's cave wall is two thick on every row that
proves it, and the Topsail corner returns to sky.

## 2026-09-02 - A wall continued from the map's adjacency

Further up the same mine shaft the owner found the rule's copy at its
worst: a scaffold tower with a panel of red lamps stands in the last two
columns of the view, and copying that edge column into the margin put the
panel three times in a row. Geometry could not separate it from the rock
walls the rule serves well (two full columns, empty map beyond, rows
proved above and below), and neither could metatile reuse: the panel's
lower half is used eleven times in the level. Where it is used is what
matters. Ten of those placements have the level's rock fill to the east,
so the map itself says what belongs beside the panel, and it is not
another panel. A continued cell now takes the fully populated metatile the
map most often places beside the previous one on the outward side, column
by column away from the wall. In the mines and crystal shafts that walks
the level's 128-pixel periodic rock fill; beside the scaffold it puts rock
where the panel copies were, and the planks carry on as planks. The unique
upper half of the panel, which the map never continues, chains from the
row below. `DKC2_TERRAIN_FILL_MAP=2` prints the fill map with each cell's
metatile id, which is how the panel's placements were found.

A few seconds further up the same shaft the crystal backdrop came back as
a block in the right margin, and the trace showed the wall rule idle
(`wall:[0,0,0]`). The fill map explained it in one glance: the gap
between the shafts is thirteen rows of void, but above it the map has a
ceiling of authored rock four columns wide, and once the camera scrolled
that ceiling into view the margin column was no longer empty from the
visible top, which is the test that keeps portholes and doorways closed.
A porthole is one row and a doorway two, so the column now also counts
as open when the row lies in an empty run at least four metatiles tall.
The corpus then showed the run test alone was too loose: in Rattle
Battle's flooded hold the water beyond the view is a run of that height
too, and the rule continued the crate at the bottom of the view into it
as a wooden patch. The water's run opens into the view on the rows above
the crate, while the mine gap is walled on all thirteen rows, so the run
must also be sealed from the view on every row (a non-empty cell no
deeper than one column inside the edge column, which keeps the crystal
shaft's pocket boundary row). The gap continues as rock under its
ceiling and the hold's water stays water.

## 2026-09-02 - Castle Crush

The owner's next stage had no widescreen at all: black bars either side
of the native picture. Its scroll handler, sub-mode `$14`, was not in
the layout classifier, so the terrain source never became ready. The
WRAM and VRAM dumps of the start settled the layout offline in one pass:
decoding the map at every candidate stride and offset and matching the
native window, only the 32-byte stride at the one-page offset reproduces
the tilemap, and it reproduces every cell. That is the narrow vertical
layout the beehive already uses: 16 metatiles per row, a 512-pixel map
under a camera that ranges 256..512. The classifier maps `$14` there,
the calibrator confirms the stride live, and the margins fill with the
castle's stone at both ends of the camera range. The corpus is unchanged.

## 2026-09-03 - Toxic Tower

The next tower, sub-mode `$1C`, had black bars too. The offline stride
search first reported no layout at all, with the best candidate matching
a quarter of the cells, and cost half an hour before the cause turned up
in the harness rather than the game: the search had fed the PPU's
vertical scroll register (748) into the map decode instead of the
camera's world position (9965), so it was decoding rows near the top of
the tower and comparing them with the ring at its base. Working back
from the screen settled it: every 4x4 block of the ring decodes to a
metatile id through the definitions, and the ids on screen sit in the
map at rows 305 to 307 with 32-byte rows, exactly where the camera says.
With the camera position the 32-byte stride matches every cell at the
one-page offset. The tower's acid is BG1 rows the game rewrites as the
level rises, which is why the calibrator's live check, not the static
classification, stays the authority for the stride. Lesson kept: the
offline matcher takes camera coordinates; the PPU scroll only picks the
ring rows.

## 2026-09-03 - Backdrop through Toxic Tower's wall

Higher up the tower the owner saw patches flicker in the left margin: a
grey block at the top corner and a green sliver lower down, both with
stepped edges. Per-layer renders put them on BG2, where the wall has
holes in the margin that it does not have in the view, and the backdrop
(the sky gradient above, the acid colour below) shows through. The band
dump explained the shapes. The stage draws its top 37 scanlines from a
second BG2 map at `$7000`, with a per-line skew of both scrolls, and
that map's wall stops short of the map edge along a slanted line; the
bands below use the `$7800` map, whose cornice strip stops nine columns
short. Both maps were presented as their own wrap, because the wrap test
counted a cell painted when its entry was non-zero, and the cells beyond
the slanted edge hold `$8000`, a flip flag over character 0. The plane
tests now judge cells by character transparency, and a band whose rows
hold a dense strip that stops short of the wrap repeats the ring
instead. The margin keeps the wall; the corpus decides what else moved.

## 2026-09-03 - The one-frame flash on Rattly

The owner then saw a texture flash in the left margin while scrubbing
time on Rattly, gone before a state could be saved. The rewind replay,
fast-forward replay, and a full-rate scan of the acid's rise all came
back clean, so the flash had to be caught by its signature instead: a
frame with acid across the centre and none in either margin. Two such
frames turned up in a scripted climb, and the band dump settled them in
one look. In each, every BG1 band carried a scroll five pixels past the
frame-start register, written by the HDMA table from line 1 as Rattly's
bounce moved the camera five pixels a frame; the register was a frame
behind. Keyed on the register, the band classifier read every band as
off-phase and put the whole layer on the repeat policy, and the margins
showed the ring's edge columns repeated for exactly that frame. The
phase selector now falls back to the scroll covering most of the
frame's lines when nothing is at the camera phase, within 32 pixels of
the camera. The scripted climb has no repeat band on BG1 in any of its
600 frames now. Kept from the hunt: the window and colour math registers
in the trace, and the lesson that a flash without a save is found by its
signature across a scripted run, not by staring at neighbours.

## 2026-09-03 - The floor that ended at the edge

Screech's Sprint's start: the Kongs on a plank platform whose first
plank sits exactly at the native edge, and the left margin showing the
bramble backdrop where the floor should be. The fill map showed the map
empty for ten columns west of the camera at every visible row, and a
walk left moved nothing: the level holds the player at its authored
edge by collision, with no camera bound and no minimum-scroll word. The
glide, told only that the map's first page is the west wall, kept both
margins and showed the void. The prefill now reports a west hold when
the columns beside the window are empty for the whole visible height,
and the glide takes the window's first column as the wall, so the frame
slides right and the platform meets its edge.

Emptiness alone was not enough. The first cut also slid Toxic Tower's
frame, where the interior is empty on BG1 beside a camera that scrolls
freely. What separates the two is the player: the camera leads a
walking player by about sixty pixels, so a player twenty pixels from the
frame's edge with the camera unmoved is one the camera failed to centre,
pinned at the level's edge. The player's world x is at `$0A2A`, and
across the held states it reads sixteen to twenty pixels in, against
sixty-four and more everywhere else. The hold enters on that pin and
persists while the void stays beside the window, so the glide releases
with travel as at any wall. Because a hold can come and go, the
presented bias now moves at most one pixel per frame toward the glide's
target, except within the first frames after a reset, where it snaps so
a level that starts at a hold opens already slid. The scripted walk
right shows the slide releasing smoothly and no void reaching the frame.

## 2026-09-03 - The Kloak that never threw

The owner reported a Kremling in Screech's Sprint that neither attacked
nor moved, and vanished when walked away from. The first pass called it
authored: the 4:3 run of the same state showed the same idle object, it
never woke with the Kongs beneath it, and the release matched the
widened window. The owner knew the game better: this Kloak throws a TNT
barrel when you come close and moves when you jump at it. So the recomp
itself was wrong, under either aspect.

The object record (type 740) named nothing, but the generated units
carry disassembly names, and `kloak_main` is a generic state handler
whose state table sits inline after its call. State 0 never advances the
state itself; it runs the sub-state dispatcher, which selects a handler
by sub-state through `JSR ($CB46,X)`. The behaviour script, in bank
`$FF` with its cursor at record offset `$50`, had stopped at `$C6C9`
with sub-state 8, the byte after the opcode that sets bit 3. The
recompiled dispatcher explained it in one read: its resolved target
table had six entries, the ROM's has sixteen, slots 8-13 mirroring 0-5
for objects with bit 3 set, and the auto-read had stopped at the null
slots 6 and 7. Index 8 took the out-of-bounds arm, which calls nothing.

The fix is a declared dispatch in `recomp/bankb3.cfg` with fourteen
entries and a decoder rule that a null slot inside a declared table is
a null entry, as the explicit target form already assumed. Regenerated,
the Kloak steps through its script, spawns the barrel, throws it left
at the Kongs, floats off, and is released off-screen. A scan of the
other 34 dispatch sites finds no other table with a null slot followed
by handlers. Lesson kept: when the owner says the game does something
this build does not, a 4:3 run of this build is not the console, and the
answer is in the recompiled dispatch, not the presentation.

## 2026-09-03 - The K. Rool Duel arena

The final fight presented at 4:3: its scroll handler, sub-mode `$17`,
was unclassified, and the trace's metatile base of 256 looked at first
like no map at all. It is the smallest map in the game, 16 by 8
metatiles in 256 bytes with the definitions right behind it, and the
offline matcher put it at the 32-byte stride with every cell matching.
Classified as narrow vertical, the deck continues into both margins and
the glide walks the arena from wall to wall with the Kongs.

## 2026-09-03 - Unlocking every level, and a save slot the game called empty

The owner asked for every level unlocked in the current save. The game's
own save routines settled the format: `update_save_buffer` at `$BB:C5E0`
packs the file at WRAM `$56CA`, `copy_save_to_sram` at `$BB:C5B8` copies
680 bytes to SRAM offsets 8, 688, and 1368 for the three files,
`calculate_checksum` at `$BB:C571` sums and exclusive-ors the words from
offset 6 to 672, and `validate_save_file` at `$BB:C526` checks both
against the header and the `$52` signature. The cleared-level flags are
sixteen words at data offset `$8D`, one bit per level number, set by
`set_current_level_as_cleared` at `$BB:8158` and read by
`is_level_cleared` at `$BB:825C`, and the level numbers that are real
levels are the entries of the pointer table at `$FD:0000` whose headers
run longer than the two-byte placeholders: 143 of 205.
`scripts/dkc2_unlock_levels.py` sets them all, recomputes the header,
and backs the save up; a boot of the patched image reaches the file
select with every file present and enters a level from it.

Two things fell out. The percentage on the file select is stored in the
file and recounted by the game at its next save, so it reads as before
until then. And the third slot showed as empty on the file select: its
SRAM copy's stored sum was one greater than the sum of its data, while
the backup save from fifteen minutes earlier held the same data with the
right sum; the two images differ in exactly that byte, SRAM offset
`$558`. A single byte incremented in SRAM with no other change smells
like a stray write landing in the SRAM bank, which is worth a watch on
that address. The tool's `--repair` recomputed the header, since the
data matched the backup byte for byte, and the quick save, which was on
that slot with four more levels than the SRAM copy, had its in-memory
flags set as well so restoring it does not write the old flags back.

## 2026-09-03 - Native macOS v0.0.5 fork release

The work since v0.0.4 was packaged for the `elliotttate/DKC2Recomp` fork
as the v0.0.5 alpha release: the sprite sub-state dispatch fix, four more
stages classified for widescreen, the map-adjacency wall continuation,
the map-derived hold, and the Toxic Tower plane and phase fixes. The
parent repository pins the presentation runtime to the
`elliotttate/snesrecomp` branch `dkc2-widescreen-presentation-v0.0.4` at
commit `3a929cd30336f2b3a077df17912719be63142299`.

The release app is an arm64 macOS 26 bundle, version 0.0.5, with bundled
SDL2 and a strict deep-valid ad-hoc signature. Its executable SHA-256 is
`1d5af11aae2b4dd893600cf00947d89fad49acb48141705a84fbff4c67a0fbd4`.
The ROM-free `DKC2Recomp-v0.0.5-macOS-arm64.zip` archive has SHA-256
`250c3e7e72dd217885085af3f6fb456967bd30a380b6cb26b0d4157b55c669bd`.
Extraction into a clean temporary directory preserved both the version and
the signature, and the extracted executable was byte-identical to the
packaged build. The configured macOS suite passed 50/50 and the 44-state
Quick Save corpus ran with no failures immediately before packaging.

## 2026-09-03 - The Lost World toll, and coins the file never keeps

Every level was cleared and the owner still could not reach the Lost
World. The kiosk explained it: Klubba's screen takes the NPC coin count
through `get_player_coin_count_npc` at `$B4:A1C6`, which reads Kremkoins
(`$08CC`) for Klubba and Banana Coins (`$08CA`) for everyone else, and
the purchase path at `$B4:9C66` subtracts the kiosk's price of 15 from
it. The file held 14. Paying never sets a shop flag: the per-NPC
handlers at `$B4:9DD2` return carry clear for Klubba, and the success
continuation writes the Swanky, Wrinkly, and Funky bytes at `$08D2`,
`$08E0`, and `$08E7` but skips NPC 8. What the kiosk tests instead, at
`$B4:91F4`, is bit `1 << world` of `$08FA`, set by the transition at
`$B4:9F53` when the Kongs go through, and the map init at `$B4:80CD`
compares `$08F9`, incremented at `$B4:B26B` when a level numbered 196 to
200 is newly cleared, with five before it opens Krocodile Kore. Both
bytes ride in the save record's mirror of `$08D2..$0901` at data offsets
`$DB` and `$DC`. Banana Coins are not in the record at all: the loader
at `$B4:800E` zeroes `$08CA` with the other counters and then fills only
the Kremkoin and DK coin bytes, so a loaded file always starts with none.

`scripts/dkc2_unlock_levels.py` grew `--lost-world`, which sets the
kiosk mask for worlds 1 to 7 and the count to five, `--kremkoins`, and
`--banana-coins`, which can only change a quick save's memory image and
refuses to pretend otherwise. Applied to all three files with 75
Kremkoins, and to the quick save with 99 Banana Coins as well. A
headless boot of the patched SRAM reaches the file select showing 75 on
every file, enters the first file onto the Flying Krock map, and its
WRAM holds 75 Kremkoins, the `$FE` mask, and the count of five; the map
init ran its one-off thirty-frame all-beaten event on that load and set
`$08FC`'s `$0C` bits, as the game does after the fifth Lost World level.
Klubba himself was not walked to in this run, so his free passage rests
on the read of `$B4:91F4`. The tool now numbers a backup instead of
overwriting one; this run overwrote the first `.before-unlock` copies
from the level unlock, so the state before any unlock survives only as
the app's own `save.srm.bak`.

## 2026-09-03 - Frame pacing on the display's own refresh

The owner reported that the game did not always feel smooth even while
the title showed 60 frames per second. The FPS counter counts emulated
frames per second, which the Mac host has always delivered; what it does
not count is how long each frame stayed on the panel. The visible Mac host
disabled OpenGL vsync and presented each frame from its own absolute Mach
deadline at 60.098811862 Hz, so the frames free-ran against the display's
refresh. On this machine's ProMotion panel that phase drifted through a
refresh every few seconds, and a pacing log written from the loop showed
what that cost during scrolling gameplay: of 1,260 frames, 23 were never
shown and 21 were shown twice, about one hiccup every half second, with the
present landing anywhere from 16.6 ms before its refresh to 10 ms after it,
and the swap itself occasionally blocking for 11 to 31 ms when the
60.0988-Hz stream ran ahead of the 60-Hz compositor.

The fix makes the display the cadence authority. `-[NSWindow
displayLinkWithTarget:selector:]` (macOS 14) delivers the window's refresh
ticks on a dedicated run-loop thread, with a preferred frame-rate range of
60 so the ProMotion panel ticks at 60 rather than 120; the ticks measured
16.667 ms apart with a 0.05 ms spread, so the request is honoured. A
portable pacer (`runner/desktop_pacer.c`) locks when one to four ticks per
frame keep the frame rate within 2% of native, covering 60, 59.94, 120, and
240 Hz and refusing 50, 75, 90, and 144, and the loop then waits for the
tick `ticks_per_frame` after the one the previous frame followed and
presents right behind it. Every one of the 1,259 locked frames in the same
gameplay run was shown for exactly one refresh, presented 13.5 ms ahead of
it with a 1.4 ms spread and a worst case of 5.9 ms. Running the game at the
display's 60.000 Hz is 0.16% slow, so the audio no longer relies on the
frame clock: `runner/desktop_audio_rate.c` resamples each frame's samples
within half a percent of unity from an average of the queue fill, aiming at
half a device pull plus two frames, which held the fill between 1,400 and
1,700 frames across the run with the ratio never past 1.0025. The device
buffer dropped from 2048 to 1024 frames on the way, halving the queue's
latency, and the same control now runs under the fallback host clock, where
it also absorbs the audio device's clock drifting from the host's.

The first measurement runs were misleading in two ways worth keeping. The
state they were meant to load never loaded, because the app changes into
its user directory at start and the state path was relative, so they timed
the title sequence; and one of them contained multi-second stalls and 512
presents of one frame with the audio reset, which was the overlay open,
not the pacer. Runs on the loaded state with the stage timings in the log
had neither. `scripts/analyze_pacing_log.py` reads the log and estimates
the refreshes each frame was shown for, which is the number that answers
the owner's report; it lives in the repository with a test so the next
pacing question starts from the same instrument.

## 2026-09-04 - CRT television display for the Retina panel

The owner asked for an optional CRT mode that looks like an older
television on the 16-inch MacBook Pro, explicitly not the kind of shader
that lays dark lines over the picture and dims it. The plan
(`docs/CRT_DISPLAY_PLAN.md`) and its build are the same day's work.

The premise is physical. Fullscreen on this panel the frame lands at 8.7
to 10 panel pixels per scanline and 10 to 12 per column, so a source pixel
is about a millimetre across, the scale of a 14-inch professional monitor.
A fixed line pattern at that fractional pitch beats against the source rows
and always takes the same light from every row; a beam does neither. The
new `desktop_crt.c` model gives every source line a Gaussian whose width
grows with its brightness per channel (0.50 to 0.18 line pitches at the
dark end from the scanlines slider, 0.50 for white), normalised so the
periodic sum averages exactly the source brightness for any width: a white
field ripples by about five percent, a dim one shows its lines. The pure C
model is unit-tested for that conservation across widths, for the mask
gain, for the fades that switch the beam and mask off in small windows,
and for the name and preset parsing; the shader is the same arithmetic in
GLSL 1.20 on the legacy 2.1 context, which a probe showed exposes float
textures, framebuffer objects, and sRGB on the M3 Max.

Five passes in half-float: lines (sRGB decode plus a horizontal Gaussian in
source pixels), beam, a 4x4 downsample and separable blurs for glow and
again for halation, and compose (cylindrical curvature with rounded
corners and a vignette, the glow added energy-neutrally, an aperture-grille
mask in window pixels with the inverse of its mean transmission as gain and
a soft knee above 0.9, sRGB encoding, a triangular dither). The presenter
was refactored so the window and the hidden capture share one render
function, since the old capture path redrew a single quad and would have
missed a multi-pass chain; `DKC2_DESKTOP_TEST_WINDOW` sizes a hidden window
in points so captures come out at the panel's 3456x2234.

Verification on preserved states (Bramble Blast's barrel start and the
castle start, a bright and a dark scene) with `scripts/crt_capture_compare.py`:
mean linear luminance 0.995 and 0.997 of the flat capture, strongest row
period 9 px for the 8.66-px pitch, and no periodic residual once the check
was done in linear light with the scene-following trend removed (the first
version compared gamma-space row means and reported the beam's own vertical
spread and the knee's highlight compression as a 20% "envelope", and the
first pitch detector picked the second harmonic because 2 x 8.66 lands
nearer an integer lag). The captures read as a tube: lines soft in the
castle's shadows, gone in the bramble sky, the mask a texture rather than a
grid at the panel's density. The living-room default was softened from
scanlines 70 to 55 after the first look at the dark scene.

What is honest about the SDR panel: at mask strength 0.3 a white field
loses a few percent to the knee, and a full-strength mask cannot be made
bright without HDR headroom that SDL's OpenGL path does not expose. The
tube bypasses the upscaler (the lines pass is the scaler); chaining
Reconstruct's dither decoding ahead of it is left for later. Pacing in
fullscreen 16:9 with the tube on (`DKC2_PACING_LOG`, 1260 display-locked
frames from the bramble state) showed every frame for exactly one refresh;
the present call's mean rose from 1.5 ms to 2.8 ms, the five passes over a
3456x1940 viewport, well inside the 16.7 ms frame.

## 2026-09-04 - Logical pause-menu placement and dragging

The SDL renderer passed the OpenGL drawable size to the pause overlay. On a
Retina Mac that size is in physical pixels, while ImGui's SDL backend reports
mouse positions and `DisplaySize` in logical window points. The pause window
was therefore centered outside the visible logical area and was clipped at
the screen edge. `Dkc2DesktopOverlayRenderOpenGl` now uses
`ImGuiIO::DisplaySize` for the dimming rectangle and the host-neutral window
layout calculation. The layout retains the existing 720 by 520 maximum and
16-point compact-window inset, with a one-pixel minimum covered by synthetic
tests.

The initial center uses `ImGuiCond_Once` instead of being forced every frame,
and the window no longer carries `ImGuiWindowFlags_NoMove`. This matches the
DKC3 behavior: the menu starts centered in logical coordinates and can then be
repositioned by its title bar without snapping back. The complete macOS suite
passed all 55 tests both before and after the change, the native app was rebuilt
and ad-hoc signed, and the owner confirmed the live menu behavior on the Retina
window.


## 2026-09-05 - Optional Donkey and Kiddy character slots

The requested Project Kongs integration adds independent Donkey/Kiddy choices
for the Diddy and Dixie slots in Pause > Characters. Default remains Original.
The local importer produces an external pack; the host renders its decoded
sprites inside native OBJ composition. Original DKC2 abilities are retained,
including the Dixie slot's helicopter spin. The repository and app bundle
contain no imported graphics. Contributor and author-reported MIT provenance
is recorded under `third_party/project_kongs/`.

The existing CRT/menu working-tree changes were preserved. Baseline validation
passed all 55 configured CTest cases. The importer and runtime add synthetic
coverage for split DMA tile layouts, visual calls/loops/carry commands, binary
bounds and transactionality, OAM identity, taller replacement bounds, temporary
palette restoration, and disabled no-op behavior.

The first moving-state audit exposed current WRAM being ahead of OAM. Complete
committed-layout matching replaced the initial current-pose assumption. A
36-state, 120-frame-per-state comparison then completed with zero unmatched
visible layouts and identical WRAM/VRAM/CGRAM/OAM/audio fingerprints between
original and Donkey/Kiddy runs. Native-rendered contact sheets were inspected
for ship deck, water, rope, hook, bramble and boss scenes. Full-game completion
and every special animation are not established by those state checks.


The moving audit added walking, jumping, rolling, direction changes, team
swapping, climbing and swimming input for 700 frames from every preserved
state. It exposed clipped compound layouts and a one-frame 97-pixel follower
reattachment; matching all submitted pieces of a known layout fixed those
cases without guessing sprite ownership. Loading the user's current live
save additionally exposed the separate animal rider at `$006C`, which now
uses the chosen Kong while retaining the original animal sprite and behavior.
The final 37-state 16:9 replay passed with zero unmatched visible layouts and
zero changed WRAM/VRAM/CGRAM/OAM/audio fingerprints. This is 25,900 frames per
configuration, compared against the same input with original characters.

The native Characters tab was exercised through keyboard navigation: the
646-frame pack was detected, Donkey + Kiddy changed both slot selectors, and
Original pair restored Diddy/Dixie. The selected pair was then restored to
Donkey + Kiddy and persisted across closing the app. The actual shared PPU
submodule remains clean; CMake's build-local adapter has unique-anchor drift
checks. Source/game-data separation and contributor provenance are documented
in `docs/PROJECT_KONGS.md` and `third_party/project_kongs/`.

Final validation: all 57 configured CTest cases passed (55 baseline plus the
two character suites), the canonical macOS app rebuilt with `MACOS_BUILD_OK`,
and deep/strict ad-hoc signature verification passed. The reversed Kiddy/Donkey
pair also passed the 37-state, 700-frame input audit at native 4:3 with zero
machine differences or unmatched visible layouts. Full-game completion,
original Donkey/Kiddy mechanics and exhaustive rare-pose coverage remain
unverified/outside the implemented presentation feature.

The final signed canonical app was relaunched and the user's existing Quick
Load state was loaded through the native Game menu. Live rendering showed
Kiddy riding Squitter with the replacement retained. Donkey/Kiddy choices
survived restart; the app was left paused on Characters with that pair selected.

## 2026-09-05 - Correct mounted Donkey/Kiddy presentation

The owner reported incorrect animal-rider animation. Reproduction in the live
Squitter save showed Kiddy looping the crouched mount frames. The first adapter
also reused the original Kong's attachment and omitted the animal-driven
compound frame selection. The prior state hashes and OAM-ownership checks did
not establish visual correctness.

Pack version 2 imports five per-character attachment points, dedicated mounted
idle/movement sequences, and animal/rider pairs from commands `$85/$86`, including
explicit offsets. Host rendering reads the live leader's animal animation and
graphic to select jump/landing poses; holding an animal frame holds its rider
pose. Attachment deltas preserve native bobbing and mirror with committed OAM
facing. Host-only rider clocks reset on movement changes and frame-counter
discontinuities and do not advance on repeated presentation of the same frame.
Unmounted rendering and original game state remain unchanged.

Kiddy's seated sequence replaces the Squitter crouch callback loop. His
unlabelled movement tail is separated from idle, and the reference's borrowed
Diddy/Rattly frame is adapted to Kiddy's seated art with a seven-pixel frame
bottom correction. Version 1 packs remain loadable for unmounted characters;
the menu asks for re-import to enable corrected riders. The installed private
pack now contains 655 frames, 218 sequences and 172 compound pose records.

Validation: all 57 configured CTest cases passed before and after this fix.
Synthetic additions exercise signed attachments for both slots and all five
mount types, mirroring, animation phase/reset/hold behavior, compound offsets,
branch extraction, and truncated/malformed version 2 packs. Two 37-state,
700-frame-per-state input audits (Donkey/Kiddy at 16:9 and the reversed pair at
4:3) reported zero machine-state differences and zero unmatched visible
layouts. Private contact sheets compared both replacements against original
Squitter, Rambi and Squawks poses, including Rambi jumps/landings and facing
changes. Rattly and Enguarde live gameplay remain unverified by this corpus.

The canonical macOS bundle rebuilt with `MACOS_BUILD_OK` and passed deep/strict
signature verification. It was quit and relaunched through its exact bundle
path; the existing quick save was loaded through Game > Quick Load State.
Live inspection verified seated Kiddy and Donkey on Squitter, selected through
the actual Characters menu, which reported 655 loaded frames. The Donkey/Kiddy
pair was restored and the rebuilt game left running. Changes remain uncommitted;
all imported data and capture artifacts remain outside Git.

## 2026-09-05 — Pause, ground attacks and barrel attachment follow-up

The user clarified that the ground attacks are for Donkey and Kiddy, leaving
original Diddy unchanged, and requested each character's original throw style.
The supplied screenshot showed Donkey's hands raised above a barrel still
positioned against his chest. Presentation-only substitution retained the
original slot's carry offsets and callback timing; native Start pause also
continued the host frame clock used for the replacement animation.

Pack v3 adds private carry offsets and attack sequences (685 frames, 221
sequences, 172 mounted records, 63 hand attachments). The animation clock now
freezes on $08C2 bit $0040. Six ROM-verified simulation hooks connect hand
positions and throw timing to the existing object lifecycle, and add Down + Y
Donkey hand slap / Kiddy body slam. Kiddy uses his existing somersault and
landing poses; downward enemy contact retains the defeat and suppresses the
ordinary bounce so the slam continues to the ground.

Initial integration testing exposed two issues before delivery: reference
address comments differed from the supported ROM, and compiled direct calls
bypassed pre-opcode hooks. Actual ROM signatures established the correct
addresses. A checked adapter now routes four short optional callbacks through
the existing paired interpreter interface; shared runtime files remain clean.
A separate test caught the animation-cursor convention while shortening the
Dixie-slot windup. No external assembly scripts were executed.

The baseline complete CTest suite passed 57/57. Synthetic cases cover malformed
v3 packs, signed attachments, freeze/resume, hand-slap impacts, body-slam
landing and stomp continuation, damage/mount cancellation, both facings, and
throw callback timing. A 37-state private no-input comparison passed with zero
WRAM/VRAM/OAM/CGRAM/audio differences and zero unmatched visible layouts.
Fresh-boot Pirate Panic replays exercise real pickup, overhead/underhand
release, and enemy contact; private captures and logs are under
`/tmp/dkc2-kongs/boot-route`. Final bundle verification is recorded below.

The first packaged ground-attack/barrel build passed 57/57 CTest checks and
`MACOS_BUILD_OK`. Before final delivery, the user reported inherited helicopter
flight and missing tag gestures. Flight now exits after the shared run-speed
update, with a separate recovery path for an already-gliding save/menu change.
Pack v4 (707 frames / 225 sequences) supplies both handoff roles. The native
paired commands leave the outgoing animation ID at idle; a shared clock now
drives both replacement poses until the native 44/26-frame transfer.

The user additionally supplied DKC1Recomp and DKC3Recomp as references. Their
animation registry/character dispatch, plus the DKC3 disassembly's distinct
glide constants, corroborate the separation of character behavior from slots.
No further source or game assets were vendored. This is not a complete
transplant: movement constants, ordinary collision sizes and DKC3 water-skip,
team/floor-breaking mechanics remain explicit roadmap work.

Focused synthetic checks pass for both handoff roles, both original slots,
pause/resume, v4 truncation, original-character flight preservation and
mid-glide recovery. A real Toxic Tower replay recorded 83 original Dixie
glide frames versus zero for either replacement; both replacements landed
normally. Two-way tags passed in both pair orders and facings with zero
unmatched sprites. The 37-state preservation corpus also passed in both
pair orders (74 comparisons, zero machine or visible-layout mismatches).

Final v4 packaging emitted `MACOS_BUILD_OK`; all 57 CTest checks passed, and
deep/strict bundle-signature verification passed. Relaunched the exact
`build/macos/DKC2Recomp.app`, verified Pause > Characters reports 707 frames
and the saved Donkey/Kiddy pair, and restored the user's quick-save into
60 FPS gameplay with Kiddy riding Squitter. Source changes remain uncommitted;
the pack, captures, recordings and executables remain private/untracked.


### September 5: paired team pickup and throws

The next user report concerned one replacement picking up and throwing the
other. Deterministic Toxic Tower replays reproduced independent follower
animation and attachment. The source's Donkey top idle still points to Diddy;
its Kiddy bottom throw omits the original DKC3 paired contact operands.
Pack v5 now imports 715 frames / 233 sequences, including four top-role states
per replacement. DK adapts seated/tumbling art; Kiddy uses dedicated team art.
The renderer anchors the top to the displayed carrier after validating each
OAM owner. Two additional simulation callbacks prepare/release the native
partner at phases 15/18, preserving the native collision/velocity path and
setting the actual launch position to the matching hands. The callback seeker
now understands ten-byte paired commands and consumes preparation once.

Baseline CTest passed 57/57. Synthetic tests cover v5 truncation, top movement
with unchanged follower animation ID, displayed-origin attachment, mirroring,
pause/drop, both original slots and carrier choices, callback ordering and
recovery. Eighteen private gameplay replays cover both choices/slots/facings,
forward/upward throws and mid-throw Start pause: all completed, each released
once at 15/18, and no visible layout was unmatched. Captures were inspected
under /tmp/dkc2-kongs/team-matrix. Both 37-state no-input preservation runs
passed (74 comparisons, zero machine or layout mismatches).

Mixed original/replacement partner placement and DKC3 floor-breaking mechanics
remain separate work. No assets, saves, captures or binaries were added to
Git; the shared snesrecomp runtime was not modified. Final packaging and live
app verification follow below.

Final v5 packaging emitted `MACOS_BUILD_OK`; the complete suite passed 57/57
and deep/strict codesign verification passed. Installed pack SHA-256:
`9238d533b8be62c6afc9ef76cb58b48ab563f040279614481ea7df2faf2c943c`.
The exact rebuilt bundle was launched, Characters showed 715 loaded frames
with Donkey/Kiddy selected, gamepad input was restored, and the user's
quick-save resumed at 60 FPS with Kiddy on Squitter. Live app checks establish
pack/menu loading and save restoration; detailed team motion was verified
from the deterministic gameplay render captures above. No commits were made.


### September 5: Kiddy post-throw jitter correction

The user reported visible twitching after Donkey threw Kiddy. Captured the
current native snapshot and restored the previous quick-save file immediately.
The exact state reproduces a stationary follower in state $21, semantic 40,
with $D7A zero. The source borrows Diddy's team-stunned animation; the importer's
hurt fallback alternated Kiddy upright/horizontal hit poses every six ticks.
This survived the previous release/layout checks, which did not establish a
correct sustained recovery pose.

The importer now isolates Kiddy’s four grounded sit-up frames and holds the
seated final frame. Death/cry callbacks and frames are excluded. Ordinary
hurt mapping and native throw/rejoin physics are unchanged. Pack format and
counts remain v5, 715 frames / 233 sequences; earlier packs need regeneration.
The trace includes displayed origins to distinguish translation jitter from
pose substitution. Synthetic tests cover the isolated sequence, held recovery,
pause, long waits and native following resumption.

The exact snapshot's 180-frame replay now progresses to one seated pose and
holds it, with a constant displayed origin (90,128). A catch-up/jump replay
exits semantic 40, resumes native follow animations, and matches the earlier
pack's WRAM, VRAM, CGRAM, OAM, source OAM and audio fingerprints. Eighteen
team-throw replay cases still each release exactly once with zero unmatched
visible actors. Private evidence is under /tmp/dkc2-kongs/throw-jitter.
Baseline full CTest passed 57/57; final packaging/live verification follows.

Final macOS packaging emitted `MACOS_BUILD_OK`, all 57 CTest tests passed,
and deep/strict bundle signature verification passed. Installed the regenerated
private pack with SHA-256
`5f5e8e0d452225ad3a7f730b429b8acb0ada19265e5f3e9d659f56bbcf9cb0b4`.
The rebuilt app loaded the exact reported snapshot: screenshots several seconds
apart show Kiddy holding the seated pose, and the live trace confirms semantic
40 / pose $3EE0 at the stable origin (90,128). Then relaunched the bundle
normally and restored the user's original quick-save on Squitter; both player
input sources remain gamepad. No commits were made.

## 2026-09-12 - Native macOS v0.0.6 fork release

The work since v0.0.5 was packaged for the `elliotttate/DKC2Recomp` fork
as the v0.0.6 release: the CRT television display, the logical pause-menu
placement on Retina windows, and the optional Donkey Kong and Kiddy Kong
character slots with their mounted, carry, throw, ground-attack, tag and
recovery presentation. The three pieces of work had accumulated in one
working tree and were split into separate commits before the release,
each compiled and unit-tested on its own; the parent repository still pins
the presentation runtime to `elliotttate/snesrecomp` at commit
`3a929cd30336f2b3a077df17912719be63142299`. No Windows build was
refreshed for this release; the `v0.0.5-r2` Windows archive remains the
most recent one and predates these features.

The release app is an arm64 macOS bundle, version 0.0.6, with bundled
SDL2 and a strict deep-valid ad-hoc signature. Its executable SHA-256 is
`068370009adcccec5feceeb7a904f97c3667527380202ef66d503055d2f5be53`.
The ROM-free `DKC2Recomp-v0.0.6-macOS-arm64.zip` archive has SHA-256
`20947ee9ee4bc64df13b2aeae68440288811f8e6b5b263bc8c4c8258352a6094`.
Extraction into a clean temporary directory preserved both the version and
the signature, the extracted executable was byte-identical to the packaged
build, and it completed a hidden 180-frame run at 16:9 with the CRT
display on. The configured macOS suite passed 57/57 immediately before
packaging, including the CRT smoke run, the CRT model, the overlay layout,
the Project Kongs importer, and the Project Kongs runtime checks. The
archive contains no ROM, no character pack, and no configuration file.

## 2026-09-12 - Simultaneous two-player co-op

The cartridge's "2 PLAYER TEAM" mode alternates: the input dispatch at
`$80:8A00` fills one global "active" input word each frame from exactly
one controller chosen by the active-Kong selector `$08A2`, and the Kong
action gate (`$B8:B9C7`, in generated function `CODE_B8BAA8`) lets only
the active Kong (`$0593`) read it. The other Kong follows by AI, and a
human waits for a hit, a thrown-partner landing, or a DK barrel to get
control back. Both disassemblies agree on this design; the p4plus2 and
Yoshifanatic1 references were used read-only.

The new simultaneous policy makes TEAM mode truly two-player: the Kong
in slot `$0DE2` reads controller 1 and the Kong in slot `$0E40` reads
controller 2, every frame, in any shared action state. The patch is two
expression wraps in the generated gate, applied by the new source-owned
`scripts/apply_dkc2_coop_overrides.py` immediately after the widescreen
adapter in both generators:

- the gate compare's `$0593` read is wrapped with
  `Dkc2CoopGateActiveValue`, which substitutes `current_sprite` under the
  simultaneous policy, so both Kong slots take the input path (the
  fall-through CLC path for screen fades is untouched); and
- the input path's `$050E`/`$0510` reads are wrapped with
  `Dkc2CoopSelectHeldWord`/`Dkc2CoopSelectPressedWord`, which substitute
  the per-controller words (`$0502`/`$0504` held, `$0506`/`$0508`
  pressed) keyed by Kong slot. That binding is the cartridge's own:
  `set_active_kong` (`$80:883B`) ties slot `$0DE2` to controller 1 and
  slot `$0E40` to controller 2.

Hurt, death, barrel-respawn, and swap machinery are deliberately stock:
every one of those paths is slot-consistent, so a respawned Kong re-enters
its own slot and therefore its own player's controller, and the classic
"control passes on hit" flip still only retargets the camera. 1 PLAYER and
2 PLAYER CONTEST are never altered, the classic policy returns cartridge
values byte-for-byte, and attract-mode input (demos inject the active
words directly) is untouched because the wraps only change the gate.

The runtime policy lives in `runner/dkc2_coop.{c,h}` beside the video
adapter. It defaults to simultaneous, is persisted as `CoopMode` in
`launcher.cfg` (the `WidescreenEdge` pattern, no shared-launcher ABI
change), is switchable live in the overlay's Settings page ("2P Team
mode"), and has a process-local `DKC2_COOP=simultaneous|classic`
diagnostic override accepted by the Win32, SDL, and headless hosts.

Anchors are instruction-shape based (the `current_sprite` direct-page
read feeding the `$0593` compare with its `Z`-flag branch; the
`$0981`/`$0983` stores after the word loads), with exactly-one-match
counts across the whole generated tree and idempotent re-application, so
revision-dependent PC drift fails closed at regeneration instead of
guessing. `tests/test_dkc2_coop_emitter_contract.py` drives the pinned v2
emitter on a gate-shaped synthetic ROM and runs the real output through
the adapter, so an emitter idiom change is caught source-only.

Verified here (2026-09-12, private ROM available): the adapter applies to the
real v1.0 Python-backend generation (3,323 roots / 3,474 exact variants) and
is idempotent there. The real v1.0 gate lives inside
`process_player_action_M0X0` at trace label `L_B8F3` — one byte and one whole
function boundary away from both reference disassemblies — which vindicates
the instruction-shape anchors: PC-based labels would have failed closed on
the owner's very first regeneration.

Real 2P TEAM validation followed. A crafted 2 KiB SRAM image
(`file_signature = $0152`, TEAM, zero payload, zero checksums — the
`validate_save_file` algorithm was rebuilt from the reference) boots straight
into TEAM mode via `DKC2_SRAM_INPUT`, and a 4,122-frame deterministic input
route walks title → player select → map → Pirate Panic. The route's three
control windows, replayed under both policies with the `DKC2_COOP_TRACE`
WRAM probe, prove simultaneous control:

```text
DKC2_COOP=simultaneous
  W1 P1 Right+B:  slot A x 304->691   slot B x 304->304 state $13 (idle)
  W2 P2 Right+B:  slot A x 707->707   slot B x 660->679 state $13->$00->$2A
  W3 both Right+B: slot A x 708->877  slot B x 679->851 (both at once)
DKC2_COOP=classic (identical recording)
  every window:   slot A responds to P1 only; slot B x 304->304 state $13
```

The decisive find on real hardware: the follower never reaches the input
gate — it lives in behavior-script states (level start is state `$13`) that
bypass the gate entirely, so gate-only patching admits nobody. The policy
therefore also nudges: when the follower's own controller presses while it
sits in its passive wait state, the helper transitions its sprite to the
idle input state (`$2E <- $00`), and from the next frame the opened gate
feeds it its own controller's words. Scripted states (hurt `$59`, barrel
`$3E`, carried `$18`) are never redirected. The cartridge's follower
catch-up teleport still rejoins a separated Kong to the leader.

Known limitation: the dispatch's `$0B02` bit-$10 active-word remap (the
level-end forced-walk input neutralization) applies to the single active
word, which the simultaneous policy bypasses by reading the raw
per-controller words; during the short scripted outro both pads therefore
stay live where stock neutralizes one. The forced walk itself is driven by
`$091B`, not the gate, so the outro choreography is unchanged.

Environment note from the visible launch: on this workstation's Parsec
virtual display, the default OpenGL presenter hangs its swap wait
(`wglSwapIntervalEXT(1)` never completes without a real vsync), leaving a
black window and a stalled loop; the hidden smoke tests are unaffected
because they deliberately request no swap interval. `DKC2_DESKTOP_FORCE_GDI=1`
(or the launcher Renderer setting) presents normally at 60 FPS with the
title screen rendering correctly.

## 2026-09-13 - Windows dropdowns, GDI ImGui, and TEAM control retention

Added the DKC3-style dark Game/View/Input menu to the Win32 and Windows SDL
hosts, with MIT provenance under third_party/dkc3_menu. Commands are queued
from the native callback and applied by the host loop. Display, player-source
and co-op choices persist through the shared launcher settings. Quick Save/Load
work with Assist shortcuts off. Fullscreen hides/restores the menu bar.

The reported missing Escape menu reproduced with Renderer=0 (GDI): the old
host had no renderer for ImGui on that path. The GDI presenter now exposes a
32-bit top-down offscreen DIB, flushes completed GDI drawing, composites ImGui
with the bundled SDLRenderer2 software backend, and presents with one BitBlt.
The software surface/renderer and textures follow window resize. A visible
GDI launch showed the full Escape menu and accepted a click into Controls.

Win32 scancode-to-VK mapping no longer depends on SDL's keycode table surviving
launcher shutdown. Both hosts retain short keydown edges through one poll.
Native menu/focus transitions clear pending input; held menu keys must release
before gameplay resumes. Start+Back remains consumed through partial release.
A visible recorded Enter reached the guest Start bit and advanced the game.
Native Quick Save with AssistTools=0 wrote a private Slot 1 snapshot.

The previous TEAM validation above was insufficient: the old new-game route's
Down input could land on file selection before the TEAM selector. The new
check_coop_route.py waits through each screen and asserts TEAM mode plus
in-level state before measuring movement. It starts with blank SRAM, requires
no saved-state seed, and writes all temporary input/WRAM traces outside Git.
This is private-ROM runtime evidence, not a physical-controller playtest.

That route reproduced a second issue: after joining, P2 landed in state $22
and replayed the leader's coordinates instead of retaining its own input.
The supported ROM's $B8:A095 handler calls $D8B6, which copies position history
from $7FA532/$7FA572. Dkc2CoopSelectStateWord now intercepts the state read in
kong_state_handler, before follower execution: waiting state $13 joins on its
player's input, and follow-history state $22 becomes normal player control
including on neutral input. The active-input gate is now read-only. Other
states retain their handlers. The dispatcher sets DB=$B8, so policy WRAM reads
use bank $00. Existing nested private gate/input wrappers were normalized to
single calls; the adapter now rejects duplicate or misplaced existing calls.

Fresh 3,518-frame TEAM replays: P1-only moved slot A x304->557 while B stayed
x304; P2-only moved B x540->756 while A stayed x583, including continued
movement after landing; both then moved A x583->837 and B x753->976. Classic
mode kept the inactive B slot at x304 on the same input route. These are
bounded opening-level checks; they do not establish complete death/respawn,
animal, bonus, end-of-level or full-game behavior. The camera remains shared.

Validation: Release builds succeeded for Win32, SDL and headless. Baseline
had 61 tests; the PowerShell packaging test initially lacked Get-FileHash due
to inherited PSModulePath. Prepending the Windows PowerShell system module
path fixed the test environment. The final 66-test suite passed, including
native menu structure/commands/checkmarks, keyboard lifecycle and short taps,
software/GDI composition, visible-overlay smoke, co-op unit/emitter contracts,
and classic/simultaneous private replay. Logs: build/menu-coop-final-build.log
and build/menu-coop-final-tests.log. Private live artifacts are under the
external temporary DKC2-menu-20260913 directory. No ROM, save, game capture or
generated game binary was added to Git. Physical gamepad and non-Windows
acceptance, plus complete co-op gameplay coverage, remain unverified.

Final visible acceptance also confirmed Alt+Enter entering fullscreen, Escape restoring the window/menu bar, and Escape reopening ImGui after the resize. The rebuilt Release executable was reopened from build/Release for the user.

## 2026-09-13 - TEAM second-player combat and follower colors

The owner's report reproduced after the movement repair: Player 2 could move
but could not damage enemies and retained the dim follower palette. A fresh
baseline complete suite passed 66/66 tests (`build/coop-combat-baseline-tests.log`),
showing that movement-only acceptance did not cover combat.

Private observation of the supported ROM identified independent follower
restrictions. The inactive sprite kept interaction mask zero at `+$30` and
render order `$D8` at `+$02`; `get_active_kong_clipping` skipped its normal
action states. The source-owned policy now promotes only an ordinary inactive
Kong's zero mask to `$1E` and back order to `$E4`, preserving nonzero masks
and special action states. The generation adapter redirects follower clipping
to the game's existing separate hitbox path at `$BC:FB2C`, preserving the
caller's guest return frame.

The initial stomp replay exposed a second bug: the enemy died, but the active
leader entered bounce state `$16` while the attacking second player stayed
in jump state `$06`. At accepted reaction queue time, `$0A84` identifies the
enemy and `$6A` identifies the colliding Kong. The policy records both then,
replaces the record when a higher-priority reaction is accepted, and consumes
it once in the stomp reaction to select the correct stock Kong metadata.
This avoids using a collision pointer that later enemy checks can overwrite.

The normal palette selector at `$BB:8B66` adds `$1E` bytes to choose a dim
follower palette. A narrow immediate-value wrapper suppresses that offset
only in simultaneous TEAM. Status-effect branches remain in place. A fresh
private capture visibly confirmed bright Dixie colors, and the replay checker
compares all 15 nontransparent colors of each Kong against the normal palette
in the external ROM. No palette bytes were copied into source.

`scripts/check_coop_combat.py` reuses blank-SRAM TEAM navigation and adds four
source-owned input recipes. P2 roll defeats the first enemy at frame 3450;
P2 stomp defeats it at 3456, puts P2 in bounce state and moves P2 upward while
P1 remains grounded over 100 pixels away. Ordinary P2 contact enters the hurt
path at frame 3456 with its collision mask disabled, the enemy alive and P1
unaffected. P1's own roll still defeats the enemy at frame 3304. The original
binary failed the P2 roll case on the identical input recipe. Diagnostics are
observation-only; `DKC2_COOP_TRACE_SPRITES` adds sprite state and Kong CGRAM
colors to private traces. Temporary replay and trace files stay outside Git.

Synthetic policy and real-emitter adapter contracts cover the new hooks,
idempotence, missing/ambiguous anchors, preserved special masks, classic/solo/
contest palette behavior, queued stomp ownership and reaction replacement.
The complete Release build succeeded for headless and both Windows hosts.
The final complete suite passed 67/67 tests in 51.40 seconds, including the
new private combat test and prior classic/simultaneous movement and GDI ImGui
checks. Logs: `build/coop-combat-final-build.log`,
`build/coop-combat-final-tests.log`, and `build/coop-combat-route.log`.

The rebuilt `build/Release/DKC2Recomp.exe` was launched visibly and Escape
opened the full ImGui pause menu. Existing keyboard-P1/gamepad-P2 settings
were retained. No ROM, save, screenshot or generated game binary was added to
Git. Physical gamepad acceptance, full-game enemies/bosses, carry/barrel
interactions and complete death/respawn behavior remain unverified. Palette
changes after a live policy switch take effect on the game's next palette
refresh.

## 2026-09-13 - TEAM barrel ownership, recovery and saved damage lifecycle

The owner reported barrels launching from P1, P2 locking after enemy kills,
and a saved ledges scene with intangible combat and hurt Kongs returning to
play. Baseline complete Release suite: 67/67 passed in 32.92 seconds
(`build/coop-barrel-baseline-tests.log`), exposing missing lifecycle coverage.

Private before/after replays showed roll-stop `$04` and stomp-bounce `$16`
remaining set because `CODE_B9D705` skips state clearing for the follower.
The source-owned adapter wraps only its entry follower compare; controlled
Kongs finish the cartridge's ordinary recovery in those states and throw
finish `$3F`. The later compare distinguishing a carried Kong stays stock.
P2 can now walk more than 150 pixels back and jump after either first-enemy kill.

The accepted pickup store records the object's slot/id and actual Kong.
Attachment and four animation command owner reads use this ownership; carrying
guest states reconstruct it after loading, and the other player's idle recovery
does not adopt the global held object. A regular barrel test at different
platform heights exposed a separate release teleport: `CODE_B8D4AE` swept from
P1 and clamped the object back to P1. All five owner reads in that named terrain
sweep now consult the thrower. Private tests cover DK throws left/right and
regular throws across heights, plus control after release. The adapter checks
exact counts, rejects ambiguity and avoids rewriting unchanged generated units.

The owner's slot 0 hash was
`5cd521b68ff3daaca043c4e7624c1bd8ca211cd072f580a438ef303e3ff51ee3`.
P2 was the active Kong there; P1 retained follower mask `$06`. Idle promotion
now handles that exact mask as well as zero, preserving invincibility masks.
Both controller roles pass contact and roll-kill replays from this snapshot.
The save itself and all trace/ROM data remain outside Git.

Initial join-on-input also revived Kongs after hurt/runaway `$24/$25` reached
waiting `$13`. A two-bit loss mask now blocks that until an accepted DK-barrel
rescue `$3E`. It records ordinary death `$05` and animal death `$59` too, although
full animal behavior is not accepted by these tests. Two bytes of original
zeroed host-snapshot tail padding preserve marker/loss metadata without changing
legacy field offsets or total size. Legacy absent waiting states that already
left the initial follower order are treated as lost; original dormant legacy
states remain ambiguous. Transient interaction/pickup history is cleared on load.
TEAM's survivor wait `$6F` resumes its existing jump and damage grace period,
clearing the turn pause and wait blink. Joined players set the cartridge's
presence bit so a DK barrel cannot rescue an already present P2.

Validation: full Win32/SDL/headless Release build succeeded; all 69 configured
tests passed in 52.88 seconds (`build/coop-barrel-final-build.log`,
`build/coop-barrel-final-tests.log`). Four additional controller replays from
the exact owner snapshot passed (`build/coop-user-slot0-check.log`). New barrel
and lifecycle CTests cover saved loss, held inputs, DK revival and repeat loss;
synthetic tests cover mode guards, recovery states, restored ownership,
loss persistence and the real emitter's adapter contract.

The original slot remains unchanged and keyboard P1/gamepad P2 settings are
retained. Full-game, bosses, animal mounts, all throwable types and physical
gamepad acceptance remain unverified; these are bounded Pirate Panic checks.

Visible acceptance: the rebuilt `build/Release/DKC2Recomp.exe` (11:00:55 build)
launched, the Game dropdown loaded the unchanged owner slot through Quick Load
with Assist disabled, and Escape displayed the full ImGui pause menu over the
restored ledges scene. The game was left paused at that save for the owner.


## 2026-09-13: Animal mounting, death handoff and widescreen movement

Reproduced the animal problem from the external quick slot with SHA-256
`3f5a2b019b68fef226fd7b994ab31a566e7b9aec5e33a7a46a1ba01e6fb21307`.
The mount collision started with only the leader. Simply admitting the other
collider was insufficient when a stationary partner masked a valid landing.
The adapter now uses the existing two-Kong collision path for an eligible
follower, checks that collider's mount state, and records the accepted mount's
source/owner. The rider becomes camera leader while retaining its controller.
Named Kong movement/reaction/animation operands give the on-foot partner normal
physics. Barrel callers of the shared state-flags helper remain unchanged.
Stock animal lifecycle and saved guest selectors own mounting/dismounting;
only accepted-mount attribution is transient and resets on load.

The death report reproduced a turn-freeze leak: skipping TEAM wait $6F left
$0A36 == 7 active. Clearing that specific freeze and timer resumes actual
enemies. State $27 now skips follower repositioning and completes handoff at
the survivor's existing X/Y, retaining stock invincibility and recovery jump.
The older ledges route now lets the survivor break the animal crate and land
on Rambi. Its acceptance recognizes that valid mounted state while still
requiring subsequent movement/jumping and a resumed world.

The widescreen report reproduced identical movement limits at every aspect:
the Kong clamp retained camera-relative X [16,240] despite rendered margins.
Its two operands now use confirmed terrain width and presentation bias for
simultaneous TEAM Kongs. Centered 16:10/16:9 gain 26/43 playable columns per side.
Shifted views allocate that reach on the appropriate side. Slack is capped by
outer level scroll limits, preserving the native inset. Classic, solo/contest,
non-Kong and unavailable-terrain paths remain stock.

Validation: baseline full suite 69/69 passed. After the complete Win32, SDL and
headless Release build, all 72 configured checks passed in 50.81 seconds
(build/coop-final-build.log, build/coop-final-tests.log). Added blank-SRAM
supplied_rom_coop_handoff and optional supplied_rom_coop_animals /
supplied_rom_coop_widescreen, enabled by external DKC2_COOP_ANIMAL_STATE.
Both riders pass mounted save/load, on-foot partner controls, riding/jumping,
dismount/remount and P1-to-P2 exchange. Both slots reach both edges in all three
aspects; Glide/Shift/Bars/Reflect entrance checks preserve the west wall.
Synthetic policy and real-emitter tests cover guards, ownership, freeze reasons
and operand placement. Four additional replays from the earlier combat save
passed (build/coop-old-ledges-regression.log). Additional shared-state barrel
guards passed the rebuilt C policy test after the full suite.

Coverage remains bounded to these Pirate Panic routes. Other animal types,
transformations, animal damage handoffs, full-game/boss behavior and physical
gamepad acceptance remain unverified. ROM, private states and traces remain
outside Git. The original quick-slot hash and keyboard-P1/gamepad-P2 settings
are preserved.

Visible acceptance: the rebuilt desktop executable launched, Game > Quick Load
State restored the unchanged Slot 1 in 16:9, and Escape opened the full ImGui
pause menu. The game is left paused at the owner's saved spot with keyboard P1
and gamepad P2 retained.


## 2026-09-13: Mounted TEAM banana collection

The owner reported Player 2 missing bananas in the mounted ledges scene.
Preserved the newer external slot with SHA-256
`4123ce28e54f2eaf7cda66e14374d530bf845b605550f5797d091bd66682e421`.
Baseline complete suite passed 72/72 in 166.20 seconds
(`build/coop-bananas-baseline-tests.log`). The real replay exposed the
collector's two-rectangle limit: leader and animal occupied both slots,
excluding the independently controlled follower. With P2 riding in this save,
P1's jump through the trail kept the count at 30 before the fix.

The source-owned adapter wraps the second-width read in `bank_B5_F776` and
its completed-call continuation. The policy temporarily uses the second
scratch rectangle for the omitted follower, restores the original rectangle,
and repeats the same guest call before leader processing. It preserves
separate geometry, guest return frames and original collection-bit/reward
handling. A transient four-word backup is consumed within each group, with
no new save fields. Mode, presence and saved-loss guards preserve stock paths.

Private acceptance now observes P1 on foot collecting 30 -> 38 while P2 rides;
a controller-driven rider exchange and fresh-process load proves P2 on foot
collecting 30 -> 35. The rider reaches 43 both on the original trail and after
revisiting P1's collected bananas, proving no double awards for overlap or
load. Dismounted collection and idle separation also pass. Synthetic policy
tests cover scratch restoration, both slots, lost/absent colliders and mode
guards. Synthetic-emitter and missing-anchor tests cover the generated hooks.


The unchanged classic TEAM replay produced identical full-WRAM SHA-256
`6509aeb0f523b4696d390a9fce5d3bad3366f8b814e7f5762f42112275223c45`
before and after the banana adaptation. The owner's quick slot remained
unchanged throughout private testing.

## 2026-09-13: Reconstruct host mismatch investigation

At the owner's explicit request, a subagent inspected reconstruction mode.
The shader exists only in `desktop_present_sdl.c`; native GDI/OpenGL never
consume its settings. The shared pause Settings page nevertheless offered
Reconstruct and its sliders. The live native game saved `Renderer=0` and
`Upscaler=2` on normal close, confirming a request the host could not apply.

The overlay now gates shader selection by host capability, shows the effective
nearest/bilinear sampler and points to `DKC2RecompSDL.exe`. It preserves the
saved Reconstruct preference until the user explicitly selects another mode.
The pre-boot launcher and native dropdown already expose only implemented
capabilities. A portable model helper and synthetic tests cover truthful
fallback display and preserved SDL selection. No native shader port or
renderer change was made in this bounded repair.

The subagent's isolated SDL frame-300 comparison produced identical raw guest
PPM SHA-256 `53f19481e7f67ef55dcb9c7c1b770d7699ff4d76227851c9111adbcd7d862b7c`
with Nearest and Reconstruct. The presented 1281x720 images differed at 37,850
pixels and Reconstruct added 7,995 blended colors; both processes exited 0 on
NVIDIA OpenGL 4.6. This proves shader application, independent of animation.
Evidence remains in the private DKC2-reconstruct temporary directory. It is
boot-image evidence: the SDL test-load-state hook is Mac-guarded and ignored
on Windows, so the supplied gameplay save was not used for this GPU comparison.
No live renderer/config/save was changed by the subagent.

Combined final validation rebuilt the Win32, SDL and headless Release targets
successfully (`build/coop-bananas-final-build.log`). The complete available
suite passed 73/73 in 168.13 seconds, including the new private banana replay
and existing animal, combat, handoff and widescreen regressions
(`build/coop-bananas-final-tests.log`). The rebuilt native executable launched,
loaded the owner's Slot 1 through the Game dropdown, and opened the full pause
overlay with Escape. Visible Settings verification showed the effective
Nearest sampler, disabled Reconstruct entry, and SDL explanation. The game
was left paused at the supplied scene; the quick-slot hash and saved
`Upscaler=2` preference remained unchanged. Physical gamepad play and other
animal/level banana cases were not verified by this milestone.

## 2026-09-13: DKC3 display and controller parity

Compared DKC3 revision `3a033f19801a2bd3abf784d4b29c4462495d19de`
against DKC2's desktop hosts. The screen LUT and SDL reconstruction shader
were already equivalent after project-name normalization. All source CRT
choices are Raw, CRT, Composite and Trinitron color responses; DKC3 has no
additional scanline, curvature or bloom controls. The full option inventory
and remaining platform limits are in `DKC3_FEATURE_PARITY.md`; MIT provenance
and local adaptations are recorded under `third_party/dkc3_menu/`.

Native Windows OpenGL now compiles the existing reconstruction shader from
a header shared with SDL. The presenter applies all five modes and the
strength, softness and smooth-shading controls. Both menu systems expose
actual compiled capability; GDI retains its truthful fallback and preserves
the saved shader preference. Live inspection caught an accidental reset of
the extra preferences in overlay creation; that reset was removed, leaving
it only in the explicit Restore All Settings action. A private integration
check exercises pause and two launches of isolated WGL/GDI/SDL copies with
non-default settings, including disabled rumble and classic co-op.

Added 21:9 as 446 source columns with 95 pixels per side, within the signed
9-bit sprite-coordinate budget. Frame allocations, menus and launcher bounds
use the larger maximum. Synthetic and private replay checks cover both
players at both edges in all four aspects. The owner's mounted Pirate Panic
snapshot also passed a 21:9 scene check with zero center/edge mismatch and
zero warnings; one scene is not full-game ultrawide validation.

Added persistent stomp rumble and per-player test pulses through XInput and
SDL. Accepted DKC2 stomp reactions emit a consumable host-only owner mask;
no DKC3 WRAM addresses are used and no guest/save layout changes are needed.
Keyboard P1/gamepad P2 routes the first connected pad to P2. Host focus,
disablement and shutdown stop pulses. Synthetic tests check routing and
event consumption; the private combat replay verifies P2-only stomp feedback
and no rolling/jump-only feedback. Physical motor sensation remains unverified.

The baseline full suite passed 73/73 in 167.82 seconds. New GPU validation
compares 40 WGL/SDL combinations using synthetic dithers, slopes and gradients,
including every color model, reconstruction mode and tuning endpoint. All
passed on NVIDIA OpenGL 4.6.0 / 616.56; a few default-framebuffer versus FBO
channels differ by one 8-bit rounding step, below the explicit 0.1% tolerance.
Independent tuning endpoints must change output. The Windows SDL test-load
hook now loads the supplied gameplay snapshot; an isolated ten-frame run
exited 0 and captured the mounted scene with CRT and Reconstruct active.
All private snapshots, ROMs and captured game pixels remain outside Git.

Final Release builds of native, SDL and headless targets succeeded. The
complete suite passed 75/75 in 193.91 seconds, including the new restart
check (six launches across WGL/GDI/SDL), GPU matrix and existing combat,
barrel, recovery, handoff, animal and banana regressions. Evidence:
`build/display-parity-final-build.log`, `build/display-parity-final-tests.log`
and `build/Testing/Temporary/LastTest.log`. The native executable was restarted
with OpenGL, CRT and Reconstruct enabled. Live Escape Settings verification
showed all five reconstruction modes and three sliders, all four screen
presets, keyboard P1 with no gamepad, and P2 assigned to XInput gamepad 1.
P2's test-pulse button was invoked; motor feel is not inferred from the UI.
The game was left paused at the owner's mounted scene. The original quick
snapshot retained SHA-256
`4123ce28e54f2eaf7cda66e14374d530bf845b605550f5797d091bd66682e421`.
The DKC3 checkout remained unchanged. macOS builds/Metal, physical motor feel
and full-game 21:9 acceptance remain outside the proven coverage.


## 2026-09-13 - Characters, complete CRT controls and MSU-1

Selectively integrated CRT revision `38a30177556b3f8177196b95e4d597ca5f8e3127`
and Project Kongs revision `69820103df1414fe022d2262fda916fa2fc6f88d` from
the MIT DKC2 project, preserving local co-op/input/widescreen changes.
Unrelated upstream save-unlock/macOS pacing changes were excluded. The full
75/75 baseline and pre-milestone dirty sources were backed up externally.

WGL/SDL share CRT and reconstruction rendering, real capability gating,
environment parsing and settings persistence. Expanded GPU checks caught
NVIDIA rejecting reserved GLSL identifier `flat`; corrected it. The matrix
covers 40 flat combinations and 13 CRT variations. Imported Kong move state
needed per-slot storage for simultaneous TEAM dispatch; an interleaved
partner-update regression checks that a throw remains active.

The external pinned character pack imports 715 frames, 233 animations, 172
mounted poses and 63 carry offsets. A 240-frame owner-snapshot comparison
rendered replacement characters with no unmatched visible layouts and
unchanged machine hashes. Assets remain outside the repository.

MSU-1 adapts licensed sibling PCM/policy helpers; exact provenance and license
texts are under third_party/dkc_msu1. Verified DKC2 scheduler/command addresses
and song-bank aliases against the supported external image. Added menus,
headless mixing, synthetic tests and scripts/check_msu1_pack.py. The supplied
102-track restoration pack passed header checks, unchanged gameplay/video,
changed music, nonzero stock SFX at zero music volume, and exact original audio
when the replacement track is absent. An initial complete suite passed 80/80
in 192.16 seconds; final validation after per-slot move changes follows below.

State/rewind restarts the restored song, not its exact PCM position. Disabling
mid-song resumes the suspended SNES sequence. Full-game transitions,
replacement-character coverage, macOS/Linux builds and physical audio/rumble
remain unverified.




The combined Donkey/Kiddy TEAM replay then exposed a second integration gap:
replacement callbacks bypassed the generated co-op barrel-owner read. The
hook now resolves the same per-sprite owner and held-object view, restores it
before guest execution, and preserves the owner in the short interpreted
attachment routine. The original pair retains its compiled path. The extended
`check_coop_barrels.py --kongs-pack PACK` replay passed DK-barrel left/right
and ordinary-barrel throws, checking correct origin, direction, independent
P2 movement and no mistaken rescue of an already present partner.

Final Release build succeeded and the complete suite passed 80/80 in 193.97
seconds (`build/characters-crt-msu1-final-tests.log`). The native executable
launched and Escape opened/closed the pause menu. Live inspection verified
the complete CRT controls, saved Donkey/Kiddy selection, and both replacement
characters together, including Kiddy mounted on Rambi. The Audio checkbox
switched between original SNES music and MSU-1 track 6 and persisted the
supplied restoration folder at 100% music volume. The game was left paused
with replacement music enabled. This establishes UI and rendered-state
behavior, not a physical listening or rumble assessment.

The owner's quick snapshot remained unchanged (SHA-256
`4123ce28e54f2eaf7cda66e14374d530bf845b605550f5797d091bd66682e421`).
The DKC3 checkout remained clean, and `git diff --check` passed. Private ROM,
snapshot, character pack, music and acceptance outputs remain outside Git.

## 2026-09-13 - v0.0.7 Windows release and carried Mac artifact

Committed the feature work and merged origin/main v0.0.6 (`c9ce402`) without
discarding either history. Conflict resolution retains per-slot Kong moves,
co-op owner callbacks, shared WGL/SDL CRT and strict environment parsing,
MSU-1 and haptics, while bringing forward SDL audio rate control, Mac pacing,
Retina layout and save tools. Rebuilt as 0.0.7 and passed all 84 configured
CTest checks in 194.47 seconds (`build/v0.0.7-tests.log`).

The release packager now supports MSVC app-local dependencies, both playable
hosts, the importer and setup guides, all new license/provenance texts and
archive checksums. Synthetic fixtures cover MSVC/MinGW packages, private-file
exclusion and invalid version paths. Fresh extracted executables matched the
build and passed four 180-frame CRT/Reconstruct runs; SDL restored the mounted
scene with MSU-1 track 6. A separate clean extraction opened the ROM picker.
The owner's settings were restored after tests and the original snapshot
retained its recorded hash.

The new release carries the unchanged v0.0.6 Mac ZIP and checksum. Both match
the source GitHub asset digests; Info.plist remains 0.0.6 and the executable
matches the original published hash. The Mac binary was not rebuilt or run
on this Windows host and does not include the new co-op/MSU-1 integration.
See RELEASE-NOTES-v0.0.7.md for public coverage and limits.


## 2026-09-13 - Nearby partner carrying in simultaneous TEAM

The original team-up gate accepted only AI follower states, rejecting an
independently controlled partner. Adapted its partner selector and eligibility
operand to admit the other fixed player slot only within 24 horizontal and
16 vertical pixels, with both Kongs present, on foot and empty-handed.
The carrier becomes the guest leader without teleporting either player;
controller ownership remains fixed. Preserved native pickup/drop/throw flow,
including the pickup animation's waiting state. Grounded thrown partners
resume independent control instead of waiting for contact with the leader.

Baseline ran all 84 checks; 83 passed. The pre-existing Windows snapshot test
incorrectly required a dirty working tree even on a clean checkout. Corrected
its assertion to match the actual source status. Added synthetic carry policy
coverage and real-emitter fixtures, plus a blank-SRAM input replay registered
as supplied_rom_coop_carry. Diddy/Dixie and the owner's imported Donkey/Kiddy
pack both passed pickup by either player, carrying while moving, passenger
inputs, saved carrying ownership, drop/throw, independent movement and jumping,
distance/height rejection, walking up to retry and simultaneous A presses.

Both Release Windows hosts rebuilt. Native Windows UI verification loaded an
external nearby-player snapshot and confirmed keyboard X pickup, the completed
Diddy/Dixie and Donkey/Kiddy carry poses, put-down and the Escape menu. Full-game team-throw
terrain/enemy interactions, unusual animal transitions and physical controller
acceptance for this change remain unverified. Private assets and replay
snapshots stay outside Git; the owner's quick snapshot is unchanged.

Final validation covers all 85 configured tests: the complete run passed 83;
the emitter fixture needed the new team-up operands, and the snapshot test
encountered a transient Windows staging-directory rename denial. After the
fixture correction, targeted reruns passed both tests and the expanded carry
replay (including jumping while carrying). Logs are
`build/coop-carry-tests.log` and `build/coop-carry-final-checks.log`; the combined
coverage has no outstanding test failures. Donkey/Kiddy also passed the
expanded replay separately. No runtime source changed after the full build.

## 2026-09-13 - Independent rope climbing, animation and junctions

The reported Topsail Trouble snapshot exposed rope reactions selecting the
camera leader's work pair even when Player 2 contacted the rope. The adapter
now uses the guest-serialized interaction source to select the grabbing Kong,
without changing camera ownership. The first complete baseline passed all 85
checks in 203.24 seconds (`build/coop-rope-baseline.log`). Synthetic policy,
fail-closed generator and real-emitter tests cover ownership and mode guards.

The follow-up animation report exposed single-rope callbacks skipping the
independent follower. Their existing Up/Down selection and frame-speed paths
now run for either player. The owner confirmed climbing and its animation
worked in the rebuilt native game with P1 keyboard and P2 gamepad. A later
checkpoint was already parked at the terminal wait of the single/double-rope
transition. Enabled its native completion and double-rope callbacks, with a
validated one-time script rewind for older saves that missed completion.
Unfinished transitions retain their native timing and no actor is teleported.

The owner then confirmed the middle junction worked but reported restricted
sideways movement. Input replay reproduced P2 reaching the middle column and
looping its turn. The shared facing callback also skipped the follower; the
rope-only policy now lets it turn and cross to both outer ropes. Horizontal
rope animation callbacks use the same policy, scoped to states $35-$38.

The private regression uses only controller input, with the supported ROM
hash and external snapshot hashes:

- Original rope: `725807dad843865b979cc6f8c14a98eccd0adfd3549c63897ca257c38c3497d2`.
- Stuck junction: `d3fa341c257601fab83ef8ba1ec2adc53516b2766b83585f4a92bb0e9e65f245`.

`DKC2_COOP_ROPE_STATE` registers `supplied_rom_coop_ropes`; optional
`DKC2_COOP_ROPE_JUNCTION_STATE` extends it. Both Diddy/Dixie and the imported
Donkey/Kiddy pack pass grab, animated Up/Down, opposing controls, save/load,
jump/regrab, stuck-save recovery, double-rope Up/Down, transition save/load
and full right-to-left net traversal. Rich headless traces now include native
animation and displayed-graphic IDs so movement with frozen frames fails.

Windows native, SDL and headless Release targets rebuilt. The rebuilt native
executable was reopened with the external junction save and the owner's
keyboard/gamepad assignments. Intermediate complete 86-test runs passed
after the single-rope animation and junction fixes; final sideways validation
is recorded below. True horizontal-rope gameplay, other stages, unusual animal
transitions and Mac/Linux builds remain unverified. Private snapshots, ROM,
graphics and imported characters remain outside Git.

Final validation passed all 86 configured tests in 237.50 seconds
(`build/coop-rope-sideways-tests.log`), plus the expanded Donkey/Kiddy rope
replay separately. `git diff --check` passed. The native testing copy matches
the rebuilt executable (SHA-256
`739a3db2fa62aa9d10d81b1343a0ec059d8dd22dbbca141a935a8c436542f437`).
Build-directory settings match their pre-test backups and the original
Pirate Panic snapshot retains hash
`4123ce28e54f2eaf7cda66e14374d530bf845b605550f5797d091bd66682e421`.
One live window inspection briefly reported Not Responding; it subsequently
responded without a restart, and five additional input-only jump routes
completed. The cause of that transient UI stall remains unverified.

## 2026-09-13 - v0.0.8 release preparation

The owner confirmed the final rope behavior works and requested commit, push
and a new release. Set the project and Windows download version to 0.0.8 and
recorded carrying, rope animation, junction recovery and full net traversal
in the release notes. The feature baseline is the complete 86-test pass above.
GitHub inspection found no newer Mac artifact than v0.0.6; the release carries
that original archive and checksum, with its older feature coverage explicitly
labelled. Source-only changes are committed; release archives stay outside Git.

## 2026-10-18 - Isolate partner run/pick from held items

Reproduced the reported cross-player release with real input: while one Kong
held a barrel, the empty-handed partner's jump entered carry state $0E and
then a throw state, allowing its run/pick input to release the owner's item.
The shared $0D7A item pointer was still visible to actor-local movement and
animation decisions. Grounded run/pick alone did not reproduce the tested case.

Extended the existing owner-filtered item policy to jump/fall, slope,
animal/item and honey animation selection, plus jump/landing/movement animation
callbacks and their emitted tail paths. Fifteen bounded operands are validated
before any generated file is written. Global attachment and pickup state remain
unchanged. Ownership reconstruction now includes guest carry states $0D/$0E,
so loading during a jump does not require the transient host pickup record.

Added synthetic ground/air ownership checks for both fixed slots, missing and
duplicate operand rejection, and real-emitter coverage. The new
`scripts/check_coop_item_input.py` replay uses blank SRAM and controller inputs,
saves/restores ground and airborne carrying, checks partner run/pick and jump,
and verifies the owner can still release. It failed against the old executable
and passes with either player carrying after the fix. Supplying DKC2_ROM
registers it as `supplied_rom_coop_item_input`.

The complete configured 55-test suite ran before and after: 53 passed both
times; `create_windows_version` and `make_release` still fail because this
machine's Windows PowerShell cannot find Get-FileHash. Final private item-input,
barrel, nearby partner-carry and combat replays all pass using the external
supported USA v1.0 ROM (SHA-256
`35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`).
Native Windows desktop and headless Release targets rebuilt, and the desktop
completed a hidden 65-frame OpenGL/audio startup with a clean exit.

Evidence is ignored under `build-tools-vscode/`: `coop-baseline-tests.txt`,
`coop-input-before.txt`, `coop-input-final.txt`, `coop-barrels-final.txt`,
`coop-carry-final.txt`, `coop-combat-final.txt`,
`coop-complete-final-tests.txt`, `coop-desktop-smoke-final.txt` and
`build-coop-final.log`. No private ROM, snapshots, traces or generated code are
added to Git. SDL, macOS/Linux, physical two-player control, imported characters,
honey-floor gameplay and full-game throwable interactions were not verified
for this change.

## 2026-10-18 - Three-second Select catch-up in simultaneous TEAM

Added a frame-based co-op rescue: holding Select for 181 NTSC console frames
(three seconds rounded up) moves the requesting living on-foot Kong 16 pixels
beside a grounded teammate. It clears position fractions, movement velocity
and stale terrain-contact flags while preserving the actor's action/animation,
controller binding and camera ownership. The moved actor's held item receives
the same position delta. Lost/dead Kongs, mounted and team-carry combinations,
ropes, barrels and scripted transitions are not interrupted. If both requests
become ready together, the follower joins the camera leader once.

Select is reserved for catch-up in simultaneous TEAM: held/pressed action
wrappers mask its guest bit, preventing a native tag while charging. Classic
TEAM, single-player and contest retain their cartridge input words. The timer
uses actual controller port state rather than delayed guest input polls, so
a one-frame release is observed even during a lag frame. Pending charges reset
on release, guest/overlay pause, scene/level changes and mode/session/snapshot
reset. An already-fired hold stays latched through pause until released.
The new lightweight `dkc2_coop_assist.h` exposes the frame/pause interface;
desktop and SDL overlay loops cancel charges when pausing. Save layouts and
hardware controller registers are unchanged.

Synthetic coverage checks both player slots, the 180/181-frame boundary,
one-shot/rearm behavior, both-held priority, stale guest input, ground gating,
falling momentum, carried-item translation, pause/load/level resets and
ineligible modes/actors. `check_coop_rescue.py` drives real inputs from blank
SRAM and passes timing, destination, no native tag, brief-release and snapshot
reset for both players, plus simultaneous holds and guest pause. Its idle
seed isolates timer behavior from level traversal/death; one-shot latching
and held-item translation are covered synthetically. With DKC2_ROM configured,
the replay is registered as `supplied_rom_coop_rescue`.

The complete configured suite ran before and after: 53/55 passed both times;
the same Windows packaging tests cannot find Get-FileHash in Windows PowerShell.
Native Windows desktop and headless Release targets rebuilt. The desktop
completed a hidden 65-frame OpenGL/audio run and clean shutdown. Evidence is
ignored under `build-tools-vscode/`: `teleport-baseline-tests.txt`,
`rescue-port-final-tests.txt`, `rescue-verified-input.txt`,
`rescue-desktop-smoke.txt`, `build-rescue-ports.txt` and
`rescue-port-unit-build.txt`. Private replays use the owner's external verified
USA v1.0 ROM and temporary snapshots/traces, never tracked content.
The previous item-input and nearby partner-carry regressions also pass after
the final controller-port change (`rescue-final-item_input.txt` and
`rescue-final-carry.txt`). `git diff --check` passed.

Physical two-player, imported-character, SDL/macOS/Linux and full-game
terrain/platform-edge acceptance remain unverified. This assist is not a
revival mechanism and requires a normal grounded destination.

## 2026-10-18 - Rescue the reported offscreen rope save

Read-only replay of a private copy of the owner's quicksave (SHA-256
`7f493a2393bd973897864f3f9b9ac5a108fe1a1ac0b5d5fcb007e60c7a408c33`)
found P1 standing at (753,1229), while live P2 was hanging in rope state $35
at (947,2659). P2 still climbed on Up. Both-present $4000 explains why the
DK barrel did not revive anyone. Select rescue had two blockers: the rope
state exclusion and a stale serialized lost-mask bit for the live P1.

The requesting live Kong may now leave rope states $35-$38, including rope
junction transitions. Its destination remains a grounded normal on-foot Kong.
The move clears rope state/action and initializes its own idle animation from
the ROM descriptors used by $B9:D0C6, discarding the old rope callback. Normal
guest dispatch owns subsequent graphics, gravity and terrain. Ordinary
on-foot rescues still preserve their action/animation and held-item ownership.
State dispatch clears stale lost bits only for a valid standing/rope Kong
with live collision and guest both-present evidence. Dead, absent and waiting
actors retain their lost status. No revival bypass or save format change.

Synthetic tests cover either slot in all four rope states, animation context,
timing, rope-destination rejection, clearing only the confirmed live actor's
lost bit and retaining it when live evidence is missing. The optional --state
argument to check_coop_rescue.py replays the reported external snapshot using
only real controller inputs. P2 detaches at frame 181, reaches (769,1229),
walks and jumps normally, and remains usable after save/load; P1 position/state
and camera ownership stay unchanged. Both-player blank-SRAM rescue timing,
pause/load/release checks and the death/DK-barrel/repeat-loss lifecycle pass.

The complete available suite ran before and after: 53/55 pass, with the same
two pre-existing Windows packaging failures from unavailable Get-FileHash.
Windows desktop, headless and co-op unit Release targets rebuilt successfully.
A hidden 65-frame desktop startup/audio/presentation smoke completed cleanly.
The user's quicksave was not modified. Ignored evidence includes
build-tools-vscode/rope-rescue-{baseline-tests,final-tests,private,lifecycle,
final-build,desktop-smoke}.txt and limbo-inspection/. git diff --check passed.

Physical controllers, imported characters, other platforms and full-game
rope/terrain combinations remain unverified. The requester must still be
alive and the destination must be grounded; barrel/mount/team-carry and hurt
transitions remain excluded.

## 2026-10-18 - Water rescue and independent swimming facing

Inspected a private copy of the new reported quicksave, SHA-256
`4eec319313989fedf2f53c683ce0e78760dfc9853c6cce65c3076a3cc9178d20`.
P1 was alive in swimming state $3D, around (758,1905) after settling; P2 was
standing at (838,1665). Neither Select hold worked because the requester and
destination eligibility excluded swimming. Both players remained present.

Rescue now accepts a live swimmer at either endpoint. Swimming destinations
do not require ground contact. Crossing the water boundary restarts the moved
Kong's idle/swim animation and state; entering water also initializes its own
work-block gravity and terminal speed from its $FF property record, matching
$B8:B5DA. This is necessary: state $3D alone retains dry-land physics, while
normal global water entry can trigger an unrelated follower transition.
Only the requesting actor/work block changes. Same-water rescue preserves
its existing swimming animation/physics. Missing water-entry work metadata
fails closed. Stale loss reconciliation also recognizes confirmed live swim.

The user additionally reported movement without a matching sprite turn.
The native swim callback CODE_B9DB6C at $B9:DB6E skips $0597 before updating
animation speed/facing. Added a checked operand override using the existing
rope-animation policy, now also admitting independently controlled state $3D.
Native motion determines facing; other modes retain their original operand.
The generated adapter was reapplied without storing generated code in Git.

Unit coverage checks both slots, water entry/exit and same-water movement,
ungrounded swim destinations, own-character animation/physics, missing work
metadata, native swim facing eligibility and saved loss evidence. The private
check_coop_rescue.py --water-state replay passes timing, both directions,
walking/jumping on land, swimming strokes, leader/follower facing, unchanged
teammate/camera ownership and save/load. The old reported rope snapshot and
both-player blank-SRAM timing/pause/load tests still pass. The separate native
death/DK-barrel/repeat-loss replay passes.

The complete available suite ran before and after: 53/55 pass, with the same
two pre-existing Windows packaging Get-FileHash failures. Release co-op unit,
headless and native desktop targets rebuilt. A hidden 65-frame desktop smoke
completed with a clean exit. The original quicksave remains byte-for-byte
unchanged. Ignored evidence: build-tools-vscode/water-rescue-{baseline-tests,
final-tests,private,lifecycle,facing-build,desktop-smoke}.txt and water-inspection/.
git diff --check passed. Physical controls, imported characters, other hosts
and full-game terrain/water-boundary combinations remain unverified.

## 2026-10-18 - Recover the live waiting partner beside Rambi

Inspected the reported quicksave, SHA-256
`67734f95f3bca7c6de10bede4338545d902601f95b6ff332c0b55adb36a5853a`.
P1 was riding Rambi at (6673,497), with P2 in waiting state $13 at
(6590,2084), live collision flags $1E, both-present $4000 and lost mask zero.
Input moved P2 briefly farther below the level without restoring normal play.
The Select assist excluded both waiting requesters and all mounted contexts.

A live, present, non-lost waiting requester can now recover with the existing
three-second Select hold. Its state/action/animation reset to ordinary
movement so the old waiting callback does not reassert the limbo state.
Grounded Rambi riders qualify as destinations only after validating the
active rider's normal mount state, collision/ground flags, animal type and
aligned live animal pointer. The on-foot partner moves; rider, Rambi and
global mount/controller selectors remain unchanged. Rider requests, airborne
riders, other animals and dead/lost requesters remain excluded. This restores
catch-up access without automatically reviving a lost Kong or moving a mount.

Synthetic coverage exercises both fixed player slots, timing, live waiting
recovery, cleared callback, rider exclusion, dead/lost requesters, airborne
riders, other animal types and invalid mount records. The --mount-state
private replay passes the exact reported save: at frame 181 P2 reaches
(6657,497), walks and jumps independently, and remains usable after save/load.
Both-held requests recover the follower; rider-only Select preserves the
mount. The animal comparison uses matched neutral replay frames because its
normal idle motion changes Y by a pixel. No guest memory is patched by tests.
The earlier reported rope/water saves, both-player timer/pause/load cases and
the native death/DK-barrel/repeat-loss lifecycle also pass.

The full available suite ran before and after: 53/55 pass, with the same
pre-existing Windows packaging Get-FileHash failures. Windows desktop,
headless and co-op unit Release targets rebuilt. A hidden 65-frame desktop
smoke completed cleanly. The user's quicksave is byte-for-byte unchanged.
Ignored evidence: build-tools-vscode/mount-rescue-{baseline-tests,final-tests,
build,unit-build,private,lifecycle,desktop-smoke}.txt and current-alive-inspection/.
git diff --check passed. Other animals, imported characters, physical
controllers, other platforms and full-game mount/terrain coverage remain
unverified; this mounted destination exception is deliberately Rambi-specific.

## 2026-10-18 - Either independent Kong can finish at the goal

Inspected the reported Pirate Panic goal quicksave, SHA-256
`7555d52d615e02820a1b862aaf7af21b040e124cbadad8ddfb05d6687de44434`.
Both Kongs were alive, P1/camera leader at (7131,465), P2 at (7112,401), with
the native goal idle nearby. Before the fix, P1's jump completed the level;
P2's jump reached the goal but left its main sprite idle. The native goal
only checked the active Kong's landing eligibility and collision.

In simultaneous unmounted TEAM, the original goal gate now admits either
live, non-lost airborne descending Kong. The existing native two-Kong collision
path checks both exact hitboxes. A goal-scoped flags operand filters the
original landing eligibility separately for each collider, so a grounded or
ascending leader cannot mask the follower or trigger an invalid goal contact.
Only accepted native collision carry promotes the actual colliding follower
to ending leader. Guest active/inactive work selectors and cached 24-byte
collision records swap together. Positions, fixed controller ports, goal
rewards and native scene progression remain unchanged. No host-only pending
goal owner, guessed collision success or save format change is involved.

The generated adapter checks the goal gate, two accepted-block source reads,
native collision flags operand and two-Kong call site. Synthetic fixtures and
real-emitter contracts cover these additions and fail closed for missing
handlers. Lightweight declarations stay in dkc2_coop_assist.h, included only
by affected generated units. Unit tests cover both fixed slots, per-collider
ground/ascending/lost rejection, native carry gating, winner/work/cache swaps,
unchanged positions and classic mode. Generated private C remains ignored.

check_coop_goal.py passes both players from the exact private save, requiring
native goal state progression, the correct winner, ending animation and actual
level exit. It also verifies walking into the goal does not bypass the landing
requirement, and P2 completion after a mid-approach snapshot reload. Configure
DKC2_ROM and DKC2_COOP_GOAL_STATE to register supplied_rom_coop_goal. The shared
private replay helper permits an explicitly requested scene-changing route;
existing callers retain their gameplay-submode assertions.

The full available suite ran before and after: 53/55 pass, with the same two
pre-existing Windows packaging Get-FileHash failures. Windows desktop,
headless and unit Release targets rebuilt. The earlier rope/water/Rambi rescue
snapshots, both-player timing checks and native death/DK-barrel lifecycle all
pass. A hidden 65-frame desktop smoke completed cleanly; the original quicksave
is byte-for-byte unchanged. Ignored evidence: build-tools-vscode/goal-{baseline-
tests,final-tests,build,private,rescue-regressions,lifecycle,desktop-smoke}.txt,
goal-config-check.txt, goal-adapter-tests.txt and goal-emitter-tests.txt, plus
goal-inspection/. git diff --check passed. Mounted goals, other goal variants,
full-game ending/reward combinations, imported characters and other platforms
remain unverified.


## 2026-10-18 - Midpoint co-op camera and independent transport cannon

Inspected the reported Topsail Trouble transport-cannon snapshot, SHA-256
`7f13848c76f258a8ce233cf0013cc5c3ece04159b8be2b3a87a1bed45aa7a03e`.
P2 was active at (363,1037), holding a barrel; P1 was at (295,1037) beside
transport cannon $013C at (280,1033). Baseline replay repeatedly pulled P1
through $2F/$31 follower recruitment while P2 entered $30. The follower
callback could also enter the old leader instead of the Kong touching it.

The adapter skips that native follower recruitment in simultaneous TEAM,
then executes the native owner entry continuation. Accepted follower entry
promotes the inactive Kong with the work selectors and collision caches;
its original reaction is already cleared, and later collision scans can
change $6A. Flight now targets the source cannon in $0A84, rather than the
other player's position. The active-entry path suppresses its two-Kong
recruitment flag too. No host pending owner or save layout changes.

The native camera target operands now average both eligible Kongs' complete
16.16 X/Y positions using 64-bit sums. Integer and overlapping fraction reads
are covered. Native room bounds, scroll smoothing and framing remain active.
Lost, missing, waiting or scripted actors retain the native fallback target.
Camera reads do not change player positions or controller ownership.

Synthetic unit tests cover integer/fraction midpoint, overflow, either leader,
occupied-cannon positions, classic/solo/lost/missing/scripted fallbacks,
transport ownership and native target fallback. Adapter fixtures and real
emitter contracts pass with checked entry/continuation anchors. Private
check_coop_camera_cannon.py passes both cannon owners, partner walk/jump,
occupied-flight save/load, old-loop recovery, water/land midpoint on both
axes and camera save/load. The water snapshot SHA-256 is
`4eec319313989fedf2f53c683ce0e78760dfc9853c6cce65c3076a3cc9178d20`;
settled targets are exactly (798,1785) for Kongs at (758,1905)/(838,1665).
Set DKC2_COOP_CANNON_STATE and optionally DKC2_COOP_CAMERA_STATE alongside
DKC2_ROM to register supplied_rom_coop_camera_cannon. Private inputs, traces,
saves and generated code remain outside Git.

Native horizontal screen clamping changes a stationary Kong's position when
midpoint scrolling moves the screen edge past it. Rope and idle rescue
checks now compare the untouched teammate to an input-neutral replay rather
than assuming a fixed initial position. Assertions still verify timing,
destination, leader ownership, independent controls and reload. Midpoint
scrolling also changes enemy timing in the lifecycle route: P1 waits during
P2's first contact, rolls past the surviving enemy to the DK barrel, and the
revived P2 jumps into the next enemy for repeat damage. Hurt, native revival,
held-input rejection, movement and both saved loss checks remain required.

Before/after full configured suite: 53/55 pass, with the same pre-existing
create_windows_version/make_release Get-FileHash failures. Both-player goal
completion, rope/water/Rambi rescue, Select timing/pause/load checks and the
revised native death/DK-barrel/repeat-loss route pass. Desktop, headless and
co-op unit Release targets rebuilt. A hidden 65-frame desktop smoke passed;
the external quicksave is byte-for-byte unchanged. Ignored evidence:
build-tools-vscode/camera-cannon-{baseline-tests,final-tests,final-build,
private,desktop-smoke}.txt, camera-{goal-regression,rescue-final,lifecycle-final}.txt,
and camera-cannon-inspection/. Full-game framing, other cannon variants,
concurrent cannon occupants, imported characters and other platforms remain
unverified.


## 2026-10-18 - Bluetooth controller discovery and native gameplay input

Windows reported two present HID game controllers. Probing the actual bundled
SDL2 runtime found Xbox One S Controller mapped (GUID
030044f05e040000e002000000007200), and Bluetooth Wireless Controller unmapped
(03009761c82d00000161000000000000, VID 2DC8/PID 6101). The launcher's mapped-only
filter excluded the second device. Native Win32 gameplay separately polled
only XInput, so even launcher recognition did not guarantee Bluetooth input.

Replaced native XInput polling/rumble with desktop_gamepads.c. It pumps SDL
on the host thread, retains mapped handles across frames, handles hot-plug,
closes detached instances, and converts standardized buttons, sticks and
triggers into the existing Dkc2GamepadState. Connected instance ownership
survives another pad disconnecting. Explicit launcher GUID choices reserve
devices ahead of automatic choices; an unavailable explicit choice emits a
neutral slot rather than shifting the other player's input. Focus gating and
the existing two-port packing remain intact. SDL initializes after launcher
teardown, and rumble uses timed SDL pulses on the selected device.

Launcher GUID selections previously were neither persisted by launcher.cfg
nor used by the native host. Player1Device/Player2Device now load validated
32-digit GUIDs and save them alongside source/deadzone/binding settings.
Native player binding capture consults the assigned pad, and the Controls
page identifies its device. Assist/menu navigation retain their existing
first-controller behavior. Portable SDL-host device routing remains a
separate implementation and has not been migrated in this change.

Added one upstream Windows SN30 Pro Bluetooth DInput mapping, registered
before launcher and gameplay SDL initialization. Source revision
c1d5289a1f713b30a2c121e9fe6529d39360be0f, the original mapping line, C-string
wrapper adaptation and zlib license are recorded in
third_party/sdl_gamecontrollerdb. Both attached physical devices are now
recognized by the production backend and reported by test_desktop_gamepads
--list. No unknown controller layouts are invented and no third-party
framework source is modified.

The new SDL virtual-controller test attaches two devices after startup and
verifies button/axis/trigger conversion, two independent packed ports, saved
choices reversing enumeration order, disconnect isolation, reconnect,
automatic-assignment stability, explicit selection priority, keyboard+gamepad
routing, the supplemental mapping lookup and reinitialization after SDL_Quit.
The existing private check_desktop_settings.py now verifies both device GUIDs
through pause and two launches and permits native-only checking when the
portable target is absent. WGL and GDI both pass four isolated 65-frame
launches; the user's executable-side configuration and saves are untouched.

Full suite before: 53/55 pass. After: 54/56 pass, including the new controller
test; the same create_windows_version/make_release Get-FileHash failures remain.
Native desktop and controller test Release targets rebuilt. git diff --check
passes. Ignored evidence: build-tools-vscode/controller-{baseline-tests,
final-tests,final-build,physical-enumeration,virtual,desktop-settings}.txt.
Physical button/rumble feel, Bluetooth latency, controller-mode changes,
identical-GUID devices, unrelated unmapped models and other platforms remain
owner acceptance; enumeration and synthetic input success do not claim those
hardware interactions were tested.


## 2026-10-19 - Bonus arrival and independent shared tail hooks

Copied the supplied saves before every investigation; all replay checkpoints,
traces and private renders remain in ignored bonus-hang-inspection/. The
Topsail Trouble teleporter copy SHA-256 is
085976ea50e0f6d0f2db0da842433aaaa42edefd4a526db76b47ede1b94221fe.
The later Gangplank Galley fixed-hook copy SHA-256 is
7e3b7aa8a55474e9213746dd48d6a87b07f2acd78e361564262dd88b4d4d9ba6.
The owner clarified that the hanging report concerns fixed tail hooks, not
climbable ropes, and requested simultaneous use of the same prop.

Reproduced bonus entry into room $79: its native entrance barrel at (464,647)
launches the active Kong onto (464,589), while the independent partner falls
below the platform and remains in live wait $13. A bounded state-dispatch
repair now initializes the live waiting partner beside the grounded entrant
without input. It requires the actual bonus entrance barrel/spawn coordinates,
nearby actors, both-present and no lost/death evidence. Native intro and
ballistic flight remain intact. Older waiting snapshots with the same guest
evidence recover too; no host pending flag or snapshot-format change.

Reproduced P2-first fixed hook contact attaching P1 through the cartridge's
follower recruitment path. The accepted follower hook callback now transfers
selectors/collision-cache ownership to the touching Kong and skips recruitment.
Its native entry animation targets the recorded prop instead of the other
player. To support shared fixed-hook occupancy, the hook dispatcher reuses
native collision checks while occupied; a scoped per-collider flags operand
excludes already-hanging actors, and the original busy-leader rejection is
relaxed only for a hanging owner of the fixed hook. Occupancy uses live $3C
states at the exact anchor, rather than adding global hardware state.

Hook parameter $4E has low-byte data ($D8 in this save); only its high-byte
script/camera flags exclude shared handling. A release still clears $0D82
and runs the native cooldown, preventing immediate recapture while the other
Kong retains its own hanging pose. Hanging actors now contribute to midpoint
camera focus. Script/camera hook variants and other levels remain unverified.

Synthetic policy tests cover either bonus partner, beside-platform placement,
unchanged entrant, lost-player and ordinary-wait rejection, correct hook owner,
unchanged other actor, occupied/free/special/classic hook cases, collision
filtering, leader flags, native cooldown and hanging camera focus. Adapter
fixtures and real-emitter contracts pass and fail closed for missing hook
anchors. Private generated C remains ignored.

check_coop_bonus_hooks.py passes both teleporter entrants, automatic neutral
arrivals, independent partner movement, save/load, an older waiting snapshot,
either first hook grab, a freely moving other player, shared occupancy,
independent jump-off in both directions and shared save/load. A held run/pick
arrival replay also keeps both players present. Configure DKC2_ROM plus
DKC2_COOP_BONUS_STATE and/or DKC2_COOP_HOOK_STATE to register the private test.
Rich traces now identify actual level $00D3 and bonus mode $0515, since the
existing $0529 stage field does not change for this bonus room.

Full suite before and after: 54/56 pass, with the same pre-existing Windows
packaging Get-FileHash failures (create_windows_version and make_release).
Goal, camera/cannon, old-loop recovery, rope/water/Rambi rescue, Select timing
and native death/DK-barrel lifecycle regressions pass. Unit, headless and
native desktop Release targets rebuilt. An isolated hidden 65-frame desktop
smoke loaded the shared-hook snapshot and completed cleanly; the current
original quicksave remains byte-for-byte unchanged. git diff --check passes.
Ignored evidence: bonus-hang-{baseline-tests,final-tests,private,desktop-build,
desktop-smoke,goal-regression,camera-regression,rescue-regression,lifecycle-
regression}.txt and bonus-hang-inspection/ under build-tools-vscode. Full-game
bonus variants, animal bonus entries, scripted hooks, imported characters,
physical two-player play and other platforms remain unverified.

## 2026-10-19: Sequential hook departures and team-throw barrel softlock

Copied both live quicksaves before inspection. Hook snapshot SHA-256:
434d7d715e54b67435eee72e05b252df7b38db2d40e583dabb9c27a9aa88adea.
Elevated-barrel snapshot SHA-256:
40e941f539c2ee1e4ca782fc6c9cd0f80745d6a62621e25e07a8ec0dc396fa4c.
Original files remain external/ignored and unchanged.

Reproduced the last hanger's first jump being cancelled after the first
departure's cooldown expired. Re-arm matching fixed-hook state 1 at accepted
jump block $B8:C621; native $0D82 clearing then restarts its own cooldown.
Adapter fixtures and the real emitter contract cover the new accepted-action
anchor, missing-anchor rejection and idempotence. Synthetic policy tests cover
expired cooldown, unrelated actors and special-hook rejection. Private hook
acceptance now tests sequential single presses in both departure/grab orders,
including save/load between departures; first-jump ascent continues normally.
Later contact after cooldown remains a valid native re-grab.

Reproduced both carrier roles throwing their partner into Gangplank's barrel
at (3120,444). The independent cannon callback had intercepted a shared native
reaction whose source was the thrown Kong, leaving states $30/$2B indefinitely.
Bound the shortcut to actual cannon props, retaining native team-throw reunion.
The native launcher subsequently leaves a waiting passenger below the upper
platform. A bounded type-$0140/parameter-$0040 arrival check restores its own
movement/animation at the grounded leader's coordinate. An offset would fall
off this platform's edge. Synthetic tests and input-only private acceptance
cover both carrier roles, both arrivals at y=401, reload and independent jumps.

Unit/headless/desktop Release targets rebuilt. Full available suite before
and after: 54/56 pass, retaining the same two packaging failures involving
Get-FileHash (create_windows_version, make_release). Private bonus entrances,
old waiting snapshot and shared-hook acceptance pass. Evidence is ignored under
build-tools-vscode/hook-release-* and carried-barrel-build*. Other launcher
parameters, scripted/moving hooks, full-game and physical controller acceptance
remain unverified.

Follow-up validation for sequential departures/team barrel: goal, camera/cannon
and old-loop recovery, rope/water/Rambi rescue and native lifecycle regressions
all pass. Isolated native desktop smoke completes 65 frames with a clean exit;
the live quicksave retains SHA-256 40e941f539c2ee1e4ca782fc6c9cd0f80745d6a62621e25e07a8ec0dc396fa4c.
git diff --check passes.



## 2026-10-19: Preserve the teammate after a pit fall

Copied the reported quicksave into ignored pit-inspection/original.sav before
inspection. SHA-256:
7e9adac4f4dad150496621b61f13bd9e889e82c375f174368b9a6f9f84a8a49c.
Input-only reproduction confirmed active P2's fall started global level loss
while P1 stood safely nearby; a follower fall also lacked saved lost evidence.

Added a bounded policy at native accepted-pit blocks $B8:B481/$B8:B499. Promote
a verified live teammate with its selectors/collision caches, mark only the
fallen fixed slot lost, remove its collision flags and both-present flag, then
use native inactive parking $B8:B4CA. Survivor position and input port remain
unchanged. Existing saved loss and DK-barrel revival remain authoritative.
No eligible teammate, classic mode or bonus failure retains native behavior.

Synthetic tests cover either fallen leader, survivor promotion/preservation,
loss flags, input rejection, native revival and classic/dead/pit/below-screen/
lost/bonus rejection. Adapter fixtures and actual-emitter contracts cover both
accepted blocks, destination and idempotence, failing closed on missing anchors.
check_coop_pits.py passes both leaders and both fallers (native team-up/put-down
swaps leadership), survivor jump/control, saved loss with held input, final
player death and both falling. Configure DKC2_ROM and external DKC2_COOP_PIT_STATE
to register supplied_rom_coop_pits. No ROM or snapshot enters Git.

Unit/headless/native desktop Release builds pass. Complete suite before and
after: 54/56 pass, with the same existing Windows packaging Get-FileHash
failures (create_windows_version, make_release). Private pit, bonus/shared-hook/
team-barrel, goal, camera/cannon, old cannon loop, rope/water/Rambi rescue and
native hurt/DK-barrel lifecycle checks pass. Isolated native desktop completes
120 frames with clean shutdown. The live save remains byte-for-byte unchanged;
git diff --check passes. Ignored evidence: build-tools-vscode/pit-*.txt and
pit-inspection/. Mounted pit handoff, bonus failures, full-game terrain variants,
physical controller play and other platforms remain unverified.

## 2026-10-19: Rattly hanger contact and offscreen partner rescue

Copied the current save before inspection. SHA-256:
370f391d23a4817cfc3cf0d94b951289667a1ab671463113b0da8e4e70a02a14.
All working/replay snapshots remain ignored outside tracked source.

Confirmed global mounted-animal and busy-leader gates rejected P2's native
hook contact. Scoped those gates to permit the on-foot collider, exclude the
rider and select native direct attachment with follower work. Rider selectors
and mount context remain unchanged. A flight-only adaptation reproduced a
state-$30 loop; the final implementation uses native $B3:9914 instead.

Rattly's superjump caused safe grounded P2 to cross the camera+$120 cutoff and
park in live wait $13. Use native maximum camera Y only for that independent
partner's cutoff, keeping native terrain-pit handling and actual camera intact.
Permit the verified Rattly rider as Select destination during riding/jump/
charge, including airborne frames; preserve grounded-only Rambi behavior.
A stale saved loss bit on the confirmed live rider also blocked all rescue;
reconcile live mounted states with native mount/type/presence evidence.

Synthetic policy tests cover both rider roles, hook candidate/owner/callback,
camera cutoff, stale rider loss, airborne Rattly rescue and real pit loss.
Adapter fixtures and real-emitter contracts pass. check_coop_snake_hooks.py
passes native attachment/release/reload, independent rider movement, safe
offscreen standing/jumping, exact three-second Select timing, rescued controls
and recovery from the older parked snapshot. Configure external DKC2_ROM and
DKC2_COOP_SNAKE_STATE for supplied_rom_coop_snake_hooks.

The Rambi regression exposed Select waking an older live offscreen wait into
normal pit processing before its charge completed. Mask Select from waiting
control decisions, leaving the actor parked until host rescue; other movement
input remains unchanged. This preserves previous Rambi rescue and the new
Rattly path. No temporary debug logging remains.

Release unit/headless/native desktop builds pass. Full available suite before
and after: 54/56 pass, retaining the same pre-existing Windows packaging
Get-FileHash failures (create_windows_version and make_release). Private snake/
hook, old-wait rescue, pit survival, bonus/shared-hook/team-barrel, goal, camera/
cannon, rope/water/Rambi rescue and native hurt/DK-barrel lifecycle regressions
all pass. Other animal types, imported characters, mounted rider death,
full-game hook variants and physical controller play remain unverified.
Ignored evidence: build-tools-vscode/snake-hang-*.txt and snake-hang-inspection/.

Isolated native desktop smoke completes 521 frames through Rattly charge,
superjump and Select rescue with a clean exit. The live quicksave remains
byte-for-byte unchanged at SHA-256 370f391d23a4817cfc3cf0d94b951289667a1ab671463113b0da8e4e70a02a14.
git diff --check passes.

## 2026-10-20: Shared transport, Squawks handoff and Rattly rider pit loss

Preserved external checkpoint copies before testing. The latest both-alive
Squawks copy is SHA-256
304a5a52acfdc64c85f1505e26a668f5cfeb9694c0bbef9d186702311a61d727;
the earlier already-lost checkpoint remains preserved separately. No original
save was overwritten, no guest state patched, and no private game data added
to Git.

Rattly rider loss reproduced stale shared mount and palette-index ownership.
Before survivor promotion, perform the native dismount palette-index exchange,
restore unmounted animal idle/order, and clear shared selectors/flag. Native
terrain/deletion removes the fallen animal. Older saves with a lost inactive
rider and on-foot active survivor receive the same guarded cleanup. Synthetic
checks cover both owners and Rattly/Rambi; private Rattly replay confirms
animal despawn, normal survivor colors, on-foot jumping and reload recovery.

The requested barrel behavior supersedes independent partner movement during
transport flight. Valid sources $013C/$0174/$0178 retain native recruitment
and paired launch when both players are present and neither is lost. Preserve
native follower $22 during flight; a mounted animal excludes stale barrel
source reuse. The carried-Kong reaction remains native. Either entrant in the
reported cannon carries both through bonus arrival and restores separate
controls; the older looping checkpoint recovers. Bonus acceptance waits for
the completed native introduction before sending confirm to both ports.

Squawks type $0198 uses native shared snapping/hanging for the live partner;
ordinary partner input cannot cancel follower $22 while its rider uses $10.
Native damage already transfers leadership and palette ownership, but the old
simultaneous turn-prompt bypass resumed ordinary jump $06 from $6F. Resume
Squawks $10 for the verified active survivor instead. The copied checkpoint
passes paired movement/reload, a real hit losing P2, and P1 steering/flapping
after reload; lost P2 input cannot control the bird or revive P2. Both rider
roles have synthetic handoff coverage.

Added scripts/check_coop_shared_mounts.py with ROM hash validation, temporary
checkpoint copies and source preservation checks. Optional external CMake
DKC2_COOP_PARROT_STATE plus DKC2_COOP_MOUNT_PIT_STATE register
supplied_rom_coop_shared_mounts when DKC2_ROM is supplied. Updated cannon
acceptance to require paired flight, both arrivals and independent controls.

Release unit/headless/native desktop builds pass. The complete available
suite before and after is 54/56 passing; the same existing packaging tests
create_windows_version and make_release fail because Get-FileHash is missing
in their spawned Windows PowerShell. Private shared mount, camera/cannon,
Rattly/hook, pit, bonus/shared-hook/team-barrel, rescue, goal and lifecycle
checks pass. Isolated desktop smoke completes 980 frames through shared bird
flight, hit and survivor flight with a clean exit. The live quicksave remains
byte-for-byte unchanged at the both-alive SHA-256 above. git diff --check passes.

Full-game barrel variants, input acceptance for $0174/$0178, reverse-role bird
hit replay, other birds, Rambi pit replay and physical controller play remain
unverified. Evidence is ignored under build-tools-vscode/, including
barrel-parrot-inspection/, rattly-death-inspection/, check_coop_shared_mounts-
latest.txt, check_coop_camera_cannon-latest.txt, shared-regression-*.txt,
barrel-parrot-release-build.txt, barrel-parrot-full-tests.txt and
barrel-parrot-desktop-smoke.txt.


## 2026-10-21: Manual barrels recruit both players and accept either shooter

Copied the live checkpoint to ignored shared-barrel-inspection/original.sav
before inspection. SHA-256:
ab95401f5f3f5df057b633ad7546fd86a398d656edce988c35f3ad5ea5754379.
The save has two living Kongs beside manual barrel type $0140 at (472,6336)
in level $24. The previous shared transport predicate covered $013C/$0174/
$0178 but omitted this type, so P2 entered alone. Add $0140 to the shared
source predicate and the lost-partner shortcut. Native recruitment brings
both Kongs inside, using partner $31 and active occupant $11.

Allow either live controller to fire: the existing pressed-word wrapper
merges only the other port's newly pressed B bit $8000 while the current
sprite is the active barrel occupant in $11, with a verified shared barrel
source and no animal mount or lost players. Native actions still own timing,
rotation, direction, flight and landing. Other buttons and on-foot input are
unchanged. No generated adapters, headers or save formats were changed.

Synthetic tests cover either leader/controller, on-foot and flight rejection,
lost partner exclusion, non-barrel props and classic mode. New source-only
check_coop_barrel_sharing.py copies its external checkpoint into a temporary
directory and verifies both entrants crossed with both shooters, inside
reload, native paired flight, both upper-platform landings and each player's
separate jump after landing/reload. DKC2_COOP_MANUAL_BARREL_STATE with the
external DKC2_ROM registers supplied_rom_coop_barrel_sharing in CMake.

Restored native shared firing in the bonus-room entrance barrel can now land
both Kongs at the same X, instead of the previous fallback's 16-pixel spacing.
The bonus check accepts zero-to-16-pixel spacing while retaining live flags,
standing/platform, save/load and separate controller assertions. Both native
arrivals and independent jumping pass; no forced separation was introduced.

Release unit/headless/native desktop builds pass. All four private manual
barrel combinations pass; isolated desktop routes pass P1-entry/P2-fire at
261 frames and P2-entry/P1-fire at 301 frames with clean exits. The original
live quicksave remains byte-for-byte unchanged at the SHA-256 above.
Full available suite before/after: 54/56 pass, with the same existing
Get-FileHash packaging failures in create_windows_version and make_release.
Private shared mounts, transport camera/cannon, Rattly hooks, pits, bonus/
shared hooks/team barrel, rescue, goal and lifecycle regressions pass.
Full-game moving/rotating barrel variants and physical controllers remain
unverified. Ignored evidence: shared-barrel-inspection/, shared-barrel-*.txt,
check_coop_shared_mounts-latest.txt, check_coop_camera_cannon-latest.txt and
shared-regression-*.txt under build-tools-vscode/. git diff --check passes.


## 2026-10-21: Passenger air control after shared manual-barrel launch

Copied the current save before inspection to ignored
build-tools-vscode/barrel-air-inspection/original.sav. SHA-256:
f6d98587741aa6a5194cdd3fd8f15bf508ce6ec14992ae5738a9824308b6ce3e.
Both Kongs are inside the level $24 manual barrel at (962,5264), with P2
active $11 and P1 passenger $31. After shooting, P2's $12 state unlocks air
steering around trace frame 25, while P1 remains in follower $22 until about
frame 37 (or later when following a steering leader). This reproduces the
reported asymmetric controls.

Preserve the native initial paired launch lock. When the active occupant's
+$42 timer is zero and signed +$44 is nonpositive, left/right on the passenger's
own port releases $22 to native airborne $01. Retain its current coordinates
and copy only the shot's current X/Y/target velocities (+$20/$24/$26). Clear
stale ground contact/action gate and restore normal collision mask $1E. The
native falling state then handles this Kong's air input, physics and landing.
The other Kong retains $12 and its own controller. Neutral inputs preserve
paired flight. Require shared manual source $0140, no lost players/mount/held
object and no scripted +$48 exit. No generated adapter, public header or save
format changed.

Synthetic checks cover both ownership roles, launch/action lock timing,
scripted-exit rejection, exact momentum inheritance, unchanged coordinates and
leader selection, collision/contact restoration, other transport sources and
classic mode. Extended check_coop_barrel_sharing.py with optional --air-state
and CMake DKC2_COOP_BARREL_AIR_STATE. Private input replay verifies the first
25 trace frames remain identical to neutral flight; P1 left/right separates
by over 15 pixels by frame 35 without changing P2's trajectory. P2 retains
its own left/right response, opposing controllers separate, and P1 can reverse
in the air after loading a detached-flight save. Original entrant/shooter,
landing and separate landing controls still pass for all four combinations.

Release unit/headless/native desktop builds pass. Isolated rebuilt desktop
runs complete both opposing-control routes at 46 frames with clean exits.
The live quicksave remains byte-for-byte unchanged at the SHA-256 above.
Complete available suite before/after remains 54/56 passing; the same existing
Get-FileHash packaging failures affect create_windows_version/make_release.
Private shared mounts, camera/cannon, snake/hooks, pits, bonus/shared hooks/
team barrel, rescue, goal and lifecycle checks pass. Full-game manual barrel
variants, reversed-owner private replay and physical controllers remain
unverified. Ignored evidence: barrel-air-inspection/, barrel-air-*.txt and
shared-regression-*.txt under build-tools-vscode/. git diff --check passes.


## 2026-10-21: Equal upward barrel force and passenger landing animation

Copied the new save before inspection to ignored
build-tools-vscode/barrel-momentum-inspection/original.sav. SHA-256:
4ba7dbe4545bc39bd240fbf635c1e5517e8ebe8835f9ab6562b0b32fbb2f7c2c.
Both Kongs stand below a manual barrel at (368,6012) in level $24. Replaying
upward-right firing showed that the prior air-control fix changed the
passenger into ordinary fall $01: by frame 45 it was roughly 75 pixels below
the occupant and retained barrel animation $40. Copying velocity alone did
not preserve shot gravity or the animation transition.

Replace that falling-state release with independent native shot $12 for both
players. On the passenger's first shot dispatch, inherit the occupant's
16.16 position, action/contact/collision fields, X/Y/target velocities and
complete shot parameters (+$42 through +$4E). Both native updates enforce
the same launch/action locks and gravity while reading their own controllers.
Identity, palette and character animation context remain owned by each Kong.
Preserve scripted +$48 exits, held-object cases, other transport sources,
lost-player exclusion, animal mounts and classic play.

Neutral landing then exposed a separate native follower recovery gate: the
passenger landed but kept $12/spin while the occupant returned to idle. Extend
the existing recovery operand helper to permit $12's native active idle and
animation transition, just as controlled roll/bounce recovery does. No new
generated adapter, public header or snapshot format is needed.

Synthetic tests cover both owners, exact copied lock/shot parameters, preserved
palette/animation context and leader, scripted/transport/loss/classic rejection,
and native landing recovery. Updated barrel-sharing acceptance to expect two
native $12 actors and retain its original input-lock, independent control,
inside/flight/landing reload and all four entrant/shooter checks.
New check_coop_barrel_momentum.py uses native team-up/put-down to obtain P1
leadership, then verifies both leaders crossed with both shooting controllers:
equal upward apex (within one pixel), rightward reach, at most one native
update of initial ordering difference, native-shot persistence and flight
reload. Both normal landing transitions and leftward walking after landing/
reload pass with advancing sprite graphics and no retained spin animation.
DKC2_COOP_BARREL_MOMENTUM_STATE with external DKC2_ROM registers the private
supplied_rom_coop_barrel_momentum test. No checkpoint memory was patched.

Release unit/headless/native desktop builds pass. Rebuilt isolated desktop
checks complete upward-right, land/left and reverse-leader upward routes at
196, 291 and 388 frames respectively, with clean exits. The original live
quicksave remains byte-for-byte unchanged at the SHA-256 above. Full available
suite before/after: 54/56 pass, retaining the same existing Get-FileHash
packaging failures in create_windows_version and make_release. Private barrel
momentum/sharing/air, mounted loss/shared Squawks, camera/cannon, Rattly hooks,
pits, bonus/shared hooks/team barrel, rescue, goal and lifecycle regressions
pass. Full-game barrel variants and physical controllers remain unverified.
Evidence is ignored under build-tools-vscode/: barrel-momentum-inspection/,
barrel-momentum-*.txt, barrel-air-acceptance.txt, check_coop_*-latest.txt and
shared-regression-*.txt. git diff --check passes.


## 2026-10-21: Keep partner safe during Kleever's scripted intro

Copied the selected-level map save before inspection to ignored
build-tools-vscode/boss-intro-inspection/original.sav. SHA-256:
f13ae0c9d9a0f9b8acbc41e8486ef426564820521efb17de11737dc17d64558b.
Entered the selected level $21 (Kleever's Kiln). The partner completes native
DK-barrel arrival into follower $22 at frame 206, but independent dispatch
previously released it at frame 207 while the leader was still scripted $75.
Ordinary grounded physics left vertical velocity $0300 on the partner. When
the script placed both in $75 at frames 277-317, velocity-only movement sank
that partner to Y=777 instead of the floor at Y=657.

A classic-mode reference replay keeps native follow during the intro and both
stay above the floor. Preserve inactive $13/$22 while a verified live leader
is scripted $74 (hold) or $75 (walk). Native follow owns its velocity before
the script takes both actors; normal independent dispatch resumes once the
leader leaves the intro. No position copying, arbitrary velocity clearing,
intro timer changes, revival or combat immunity was introduced. Held input
cannot activate the initial waiting partner before native DK-barrel arrival.

Synthetic tests cover both actor roles, both scripted states, waiting/following,
held input, independent release, lost-player exclusion and classic behavior.
Added source-only check_coop_boss_intro.py with temporary checkpoint copies,
ROM hash validation and source preservation check. Private acceptance covers
selected level entry, complete hold/walk sequence, neutral and held-input
paths, mid-intro reload and both independent post-intro jumps. The native
DK-barrel rescue arcs upward to Y=606; the test correctly permits that native
motion while requiring no intro sinking below Y=657. Native ending positions
are (996,657)/(950,657). External DKC2_COOP_BOSS_INTRO_STATE plus DKC2_ROM
register supplied_rom_coop_boss_intro.

Release unit/headless/native desktop builds pass. Two isolated rebuilt desktop
routes complete 450 frames each through level entry, intro and post-intro
jump/held-input release with clean exits. The live quicksave remains byte-for-
byte unchanged at the original hash above. Full available suite before/after
remains 54/56 passing, with the same existing Get-FileHash packaging failures
in create_windows_version/make_release. Private boss intro, barrel momentum/
sharing/air, mounted loss/shared Squawks, camera/cannon, snake/hooks, pits,
bonus/shared hooks/team barrel, rescue, goal and lifecycle regressions pass.
Other boss/cinematic variants, a complete Kleever fight and physical two-player
play remain unverified. Native combat damage after the intro is unchanged.
Ignored evidence: boss-intro-inspection/, boss-intro-*.txt and existing private
regression logs under build-tools-vscode/. git diff --check passes.


## 2026-10-21: Share Kleever dropping hooks in either grab order

Copied the current save before inspection to ignored
build-tools-vscode/hanger-orders-inspection/original.sav. SHA-256:
1bf52eafd8e88fdca7bc5303fc3b3b47c8ff552004d4cc5b89c1027adf0d8a7e.
P1 starts (960,657), P2 (955,657), level $21. These are Kleever's generic
$0024 dropping-hook actors, animation $02AB and parameter $E000, not the fixed
$014C props covered earlier. P2-first reproduced native follower recruitment:
P1 became the hanger occupant and P2 fell away. Independent promotion also
needed the actual generic hook target in native entry flight, avoiding $30
loops aimed at the other Kong.

Recognize only the complete boss hook identity in aligned slots and reuse
independent grab ownership, entry-target routing, busy-leader relaxation and
already-hanging collider exclusion. Their actual occupied handler is
CODE_B6E21D ($B6:E2BC), not hook_main_state_01. Add a guarded native collision
scan at its continuation $B6:E2CD after CODE_B398A7 has applied camera/framing.
Call the same CODE_B39982 used by its free handler, with a paired JSL frame,
PB restoration, M/X interpreter fallback and non-local return propagation.
Continue the original boss animation/lifetime update afterward. Fixed hooks
retain their existing dispatch. Re-arm the native boss occupied transition
for either departure, preserving its 30-frame cooldown and last-person release.
No permanent invincibility, player copying or host attachment timer was added.

Synthetic policy checks cover both owners, second collision eligibility,
proper generic target, post-camera selection, native release re-arming and
animation/level/parameter/classic rejection. Adapter fixtures pass 17 tests,
including strict missing-camera-anchor rejection without writes and existing
idempotence checks; the real-emitter contract passes the new guarded call.
Generated private sources were regenerated from the source adapter.

Added check_coop_kleever_hooks.py. Both clean grab orders succeed from the
unmodified copied snapshot, at (1008,559), without displacing the first player.
Camera flags $0AB8 & $6000 and framing remain native. Shared reload, either
first departure and either final single-press departure after cooldown pass.
The user's authorized invincibility is used only on temporary copied shared
checkpoints for extended acceptance: native work+$16 counters and mask $9E,
with no position, velocity, input or hook-state patches. Initial grab proof
always uses unmodified save bytes. DKC2_COOP_KLEEVER_HOOK_STATE plus external
DKC2_ROM registers supplied_rom_coop_kleever_hooks with that isolated test mode.
The original and all diagnostic saves remain preserved outside tracked files.

Release unit/headless/native desktop builds pass. Rebuilt isolated desktop
routes complete both clean grab orders (134 frames each) and independent
releases from a copied protected checkpoint (76 frames), with clean exits.
The live quicksave remains byte-for-byte unchanged at the SHA-256 above.
Full available suite before/after remains 54/56 passing; the same pre-existing
Get-FileHash packaging failures affect create_windows_version/make_release.
Private Kleever hooks, boss intro, barrel momentum/sharing/air, mounted loss/
shared Squawks, camera/cannon, Rattly hooks, pits, bonus/shared fixed hooks/
team barrel, rescue, goal and lifecycle regressions all pass. Other dynamic
hooks, full boss-fight acceptance and physical co-op play remain unverified.
Ignored evidence: hanger-orders-inspection/, hanger-orders-*.txt and existing
private regression logs under build-tools-vscode/. git diff --check passes.


## 2026-10-21 - Shared manual barrel rotation

Preserved the reported save before inspection at the ignored external-content
path build-tools-vscode/barrel-rotation-inspection/original.sav, SHA-256
be1d6ee8f39cfbb76a058bfd8a24f6a8a64cc91f6a4865c23148dc78758b5511.
Native input-only entry with either Kong joins both at the level-$28 manual
barrel $0EFC/type $140/parameter $0400. Active P2 becomes $11, waiting P1 $31.
Before the fix P1 held directions leave the angle unchanged while P2 rotates.

The held-input selector now combines only directions for a verified shared
manual barrel with both occupants arrived. Opposing axes cancel independently;
native rotation speed and the rest of fixed-port controls stay intact. Synthetic
tests exercise either active role, pre-arrival, other transports, lost partners,
on-foot/flight and classic guards. Private check_coop_barrel_rotation.py copies
the snapshot into a temporary directory; either entrant and either aim/fire
port produce equal native angles/paired trajectories after reload. Twelve
frames yield $DC00/$2400 for left/right, zero opposed, $2400 matching. Optional
CMake test uses DKC2_ROM and DKC2_COOP_BARREL_ROTATION_STATE external paths.

Validation: Release unit/headless/native desktop builds pass. The rebuilt
desktop completes two isolated 120-frame entry/partner aim/fire routes with
clean exits. The live quicksave remains byte-for-byte identical to the hash
above. Full available suite before/after: 54/56; unchanged pre-existing
create_windows_version and make_release failures cannot find Get-FileHash in
spawned PowerShell. Native private barrel rotation, sharing/air, momentum,
Kleever hooks, boss intro, mounted loss/Squawks, camera/cannon, Rattly hooks,
pits, bonus/fixed hooks/team barrel, rescue, goal and lifecycle checks pass.
CMake configuration and git diff --check pass. Evidence remains ignored under
build-tools-vscode/barrel-rotation-*.txt and barrel-rotation-inspection/.
Other manual barrel variants and physical two-controller play are unverified.


## 2026-10-21 - Forgiving shared stomp and compiled co-op configuration

Copied the current quicksave before inspection to the ignored path
build-tools-vscode/shared-stomp-inspection/original.sav, SHA-256
572580601b777db548fc70a2e82e80bce8f604d02146678d251f6e04f71284be.
Reproduced the requested failure using inputs alone: level $28 enemy $0220,
joint landing at frame 56 gives P2 a native $16 bounce while P1 stays $06 and
falls. Both descending Kongs were only 8 pixels apart horizontally and 6
vertically. A spatial window is enough to forgive the update-order difference
without inventing extra enemy hits or moving either player.

At the accepted $1B stomp ownership selector, capture a nearby airborne,
descending partner near the same source enemy. Default separation is 32 X / 24
Y pixels. Reject grounded/rising/scripted actors, lost/absent players, mounts
and non-simultaneous modes. The strict generated adapter hooks the completed
on-foot impulse at $B8:87F4. Consume/revalidate once and copy only native vertical
velocity, gravity and terminal speed into the partner's state-$16 bounce;
retain its own jump animation and all independent horizontal/identity data.
Feedback covers both ports, with no second native enemy reaction.

User additionally requested compiled tuning. Added config/coop_tuning.cfg with
Select hold milliseconds (default 3000) and horizontal/vertical shared-stomp
pixels (32/24). C-preprocessor range checks reject invalid values. Header
dependency tracking rebuilds consumers; defaults retain 181 console-frame
ticks. Parameterized rescue unit timing. A temporary copied source project,
without touching the real config, builds and passes all co-op unit checks with
750 ms hold and a 16x12 window. Both-role public tests cover exact/outside
boundaries, eligibility, unchanged animation/horizontal speed and consumption.
Strict adapter missing-anchor/idempotence and real-emitter tests pass.

Private check_coop_shared_stomp.py uses copied saves and input only: both native
first-stomper orders bounce together, rise with comparable lift, steer
independently, survive reload identically and land on the next platform Y=645.
Single stomps do not recruit grounded/distant players; slightly staggered
descending contact is forgiven. Optional CMake integration uses external
DKC2_COOP_SHARED_STOMP_STATE plus DKC2_ROM and default tuning. No private save,
ROM or generated binary is added to Git.

Final Release unit/headless/desktop builds pass. Rebuilt isolated desktop
stomp routes complete 102/110 frames with clean exits; headless traces verify
both shared flights and next-platform arrivals. Original live save remains
byte-for-byte unchanged at the hash above. Final shared-stomp and manual barrel
rotation checks pass. Existing barrel sharing/air/momentum, Kleever hooks,
boss intro, shared mounts/palette cleanup, camera/cannon, Rattly hooks, pits,
bonus/fixed hooks/team barrel, rescue, goal and lifecycle checks pass.

The full available standard suite remains 54/56 before/after. Existing
create_windows_version/make_release packaging failures cannot find Get-FileHash.
The optional old blank-SRAM combat script also fails its P2-roll setup isolation
assertion (89-pixel gap versus its required >100), with no stomp events in that
route. A temporary build disabling shared stomp reproduces the same failure;
the production source is restored and rebuilt afterward. A diagnostic relaxed
isolation precondition additionally exposed that its old P2-stomp route misses
the first enemy. The original assertions were retained; this old route's full
acceptance remains unverified. No gameplay success is inferred from it.

Copied-source 750ms/16x12 configuration unit checks pass with final guards.
Strict adapter/emitter checks and git diff --check pass. Evidence is ignored
under build-tools-vscode/shared-stomp-*.txt, shared_stomp-final-acceptance.txt
and shared-stomp-inspection/. Exhaustive enemy variants and physical paired
controller play remain unverified.

The user edited Select hold to 2000 ms while work was running. Preserved that
change; final production builds use 121 ticks and 32x24 stomp pixels. Earlier
private rescue/mount regression routes used the original 3000 ms build; their
hardcoded timing is not acceptance for custom settings. Final parameterized
rescue unit checks cover the 2000 ms production setting. Repeated the isolated
custom-config check with explicit replacement/validation of all three macros,
proving 750 ms (46 ticks) and 16x12 pixels without modifying the user's file.


## 2026-10-21 - Enemy push independence and actor-pole climbing

Preserved both user checkpoints separately before inspection:
build-tools-vscode/enemy-push-inspection/original.sav, SHA-256
72fb6ea6cf8b32c7ca77bc30226b8d38aa2e9319aba9e4606321d7778e6fbedd;
build-tools-vscode/pole-hang-inspection/original.sav, SHA-256
96f23b32dc3b0653c21188941421e47043968235d9d69704c65260fbb987deed.
The second report arrived while completing the first; retain and fix both.

Reproduced neutral Klobber contact: accepted reaction $1D for inactive P1 from
enemy $0EFC enters AI $40 then pickup states $18/$17 without a button press.
Record the actual accepted push owner/source and redirect its $1D handler to
the cartridge's normal $1E knockback routine CODE_B88936, selecting that Kong's
work once. Preserve priority, sound, impulse, invulnerability and recovery.
Scope the held-object operand to its owner. Both-owner synthetic guards,
replacement/consumption/reset and classic behavior pass. Private
check_coop_enemy_push.py proves neutral/each/both-port contacts use $15 without
forced carrying; recoil reload matches uninterrupted movement and explicit
pickup still works from both ports.

Reproduced pole failure: type-$2D8 actor X=1704, P2 repeatedly alternates $45/$01
and sinks while P1 cannot enter normally. The native actor contact scan uses
the leader and global $0BA0/$0BA2/$0A86, so one Kong clears the other's anchor.
Provide per-current-Kong spatial candidate/anchor operands using native runtime
clipping geometry, and route accepted $0A work to its real source Kong. Keep
native entry, pose, movement and eligibility. Synthetic bounds are invented
test data; no extracted level/clipping tables are stored in the repository.

Testing caught two further ownership details: native CODE_B8B4F7 clamps the
leader's Y even when another Kong is climbing; route its target to the current
$45 Kong. Native global attachment also suppresses jump regrabs until contact
ends; retain the action-local per-Kong anchor at +$4C through state $06, clear
after leaving contact, and prefer a valid existing anchor when poles overlap.
Verified native pole/jump code does not otherwise use that word in this context.
No host-only latch or save-format change is needed. The private first-airborne
frame reload test verifies this association while the jumper is still aligned.

check_coop_poles.py passes simultaneous entry and both first-grabber orders,
stable $45 hanging, independent up/down motion and native limits, either/last
single-press release and byte-matching jump trajectories after reload. The
original saves remain preserved; integration data and diagnostic images are
ignored. Added optional external CMake snapshot flags for both regressions.
The user's cfg now uses Select=2000 ms and stomp=32x32; preserve those edits.

Constrain new pole discovery to initialized regular $2D8 states $05/$06.
The same native dispatcher also has ghost-rope hidden/appearance states;
do not synthesize contacts for those. Unsupported native leader contacts remain
native, and must not be reused as a distant partner's contact. Synthetic checks
cover that initialization guard. Other ghost/moving-pole acceptance is not
inferred from the regular-pole results.

Final validation: Release unit/headless/desktop builds pass with the preserved
2000 ms / 32x32 config. Synthetic co-op units and 19 adapter tests plus the
real-emitter contract pass. Full standard suite remains 54/56 before/after;
the same existing create_windows_version/make_release packaging failures
cannot find Get-FileHash. Final copied-save enemy push, poles, shared stomp
and manual rotation regressions pass. Existing barrel sharing/air/momentum,
Kleever hooks, boss intro, mounted loss/shared Squawks, camera/cannon, pits,
bonus/fixed hooks/team barrel, goal and lifecycle checks pass. Earlier
timing-specific private rescue routes still assume 181 ticks; parameterized
unit coverage verifies the current 121-tick setting instead.

An isolated copied-source build also passes all co-op unit checks at 750 ms
and 16x12 pixels. The rebuilt desktop completes neutral push (80 frames),
both pole entry/first-and-last-release orders (140 frames each), with clean
exits. A private rendered pole checkpoint was visually inspected. The live
quicksave remains byte-for-byte unchanged, and both original copied saves
remain preserved. git diff --check passes. Ignored evidence: enemy-push-*,
pole-hang-*, push-pole-* logs and their inspection directories under
build-tools-vscode/. The prior optional blank-SRAM combat-route setup failure
and broader/physical-controller limitations recorded above remain unverified.

## 2026-10-21 — Independent actor-pole climbing animation

Preserved the supplied checkpoint as an ignored inspection copy (SHA-256
96f23b32dc3b0653c21188941421e47043968235d9d69704c65260fbb987deed).
Reproduced the inactive Kong moving along the pole while graphic $1B30
remained frozen. Added pole state $45 to the existing fixed-Kong animation
callback eligibility; native scripts retain direction, timing and idle.
Synthetic coverage checks both inactive slot identities. The private pole
regression now checks actual graphic changes in both directions and native
idle after settling, in addition to movement, limits, releases and reloads.
All three grab orders pass. Saves and ROM data remain outside tracked source.

Validation: Release unit/headless/desktop builds succeed; 19 adapter tests and
one real-emitter contract test pass. Complete CTest remains 54/56 before and
after, with the same two packaging failures (Get-FileHash unavailable in the
spawned PowerShell). A 225-frame hidden desktop pole approach/climb/idle smoke
exits cleanly, preserving the live quicksave byte-for-byte. Physical controller
play and other pole families remain unverified.

## 2026-10-22 — Midpoint camera during actor-pole climbing

Copied the live quicksave before inspection; SHA-256 remains
96f23b32dc3b0653c21188941421e47043968235d9d69704c65260fbb987deed.
The reported scene reproduced single-player camera targeting on poles: at
Kong Y=584/696 the target was 696 instead of midpoint 640. The existing
CoopCameraPosition eligibility excluded pole state $45, restoring the original
operand. Added only that state to camera eligibility; native fixed-point
midpoint, scrolling/level limits and lost/dead/non-co-op guards remain intact.
Synthetic coverage checks either/both pole states and either camera selector
on X, Y and fractional Y. The copied-save pole test now verifies midpoint
targets with each sole climber, mixed grounded/airborne partner, reload, shared
climbing in all three entry orders and both-port directional movement/idle.
Integer traces allow one pixel for omitted native fractional positions.

Validation: Release unit/headless/desktop builds succeed. All copied-save pole
checks, existing water midpoint/cannon routes and loop recovery pass. Native
trace confirms target Y=640 for player Y=584/696. The rebuilt desktop completes
a 225-frame shared pole approach/climb/idle smoke with a clean exit and the
live quicksave unchanged. Synthetic co-op, 19 adapter and one emitter-contract
test pass; full CTest remains 54/56 before/after, with the same two packaging
failures caused by unavailable Get-FileHash in the spawned PowerShell.
Physical controller play and broader pole families remain unverified.

## 2026-10-22 — Midpoint camera after enemy stomps

Preserved the live checkpoint in an ignored inspection copy, SHA-256
8dc4652f40f8c3f745383315aa2b781763b07f0593e86d1a397c0ad9b6b1850b.
Input-only reproduction reaches a shared stomp at frame 42: Kong Y=626/612
but camera target Y=612. Native bounce state $16 was absent from the camera
eligibility list. Added that state only to camera eligibility, yielding Y=619
without changing rescue policies, bounce physics, camera limits or lost guards.
Synthetic tests cover either/both bouncing slots, either camera selector and
X/Y/fractional-Y math. Existing private stomp tests now assert camera midpoint
during both first-stomper orders, flight, reload and the live P2-only route.
The old P1-only fixture loses its partner to a pit before impact, so it remains
a stomp exclusion test rather than a two-live-player midpoint test. Optional
--camera-state exercises the new reported scene with a living grounded partner
and a shared bounce, including matching player/camera state across reloads.

Validation: Release unit/headless/desktop builds, synthetic co-op checks,
19 adapter tests and the real-emitter contract pass. Full CTest remains 54/56
before/after with the existing Get-FileHash packaging failures. Copied-save
shared-stomp acceptance (both first-stomper orders, lift/steering/platform,
exclusions and staggered descent), new single/shared camera reload checks and
all pole animation/camera routes pass. Rebuilt desktop single/shared stomps
complete 95 frames each with clean exits and the live quicksave unchanged.
Physical-controller play and broader enemy/level variants remain unverified.

## 2026-10-22 — Pole survivor after teammate pit death

Copied the current quicksave to ignored pole-death-inspection/original.sav,
SHA-256 4541790218ba5f17a029f1828cf5441ee030c2b37826a46f9b02efceb31dd38d.
Reproduced native level loss at frame 274 after the active Kong jumped right
off the pole into a pit, with the other Kong still hanging alive at Y=567.
The pit-survivor eligibility list omitted actor-pole state $45. Added that
state only: existing accepted native pit-death handling parks the fallen Kong,
promotes the live climber, and preserves state/position/animation/anchor.
Synthetic checks cover both fixed fallers and either leader, retained anchor
and animation cursor, lost mask and collision; pole-specific negative cases
retain classic/dead/lost/bonus/offscreen death handling.
New input-only check_coop_pole_survival.py uses the external snapshot to test
either faller, preserved climbing, own-controller up/down and first jump,
parked-player input/reload, matching midfall reload trajectories and native
level loss when the remaining player falls. No private data is tracked.

Validation: Release unit/headless/desktop builds pass, alongside synthetic
co-op checks, 19 adapter tests and one real-emitter contract. Full CTest is
54/56 before/after, retaining the two existing Get-FileHash packaging failures.
New reported pole-survival routes pass for both fallers, including final loss
and midfall/lost reloads. Existing ordinary pit routes (both leaders/fallers,
controls, simultaneous/final loss) and all pole animation/camera routes pass.
Rebuilt desktop completes 340-frame pit/survivor-climb routes for each faller
with clean exits and the live quicksave byte-for-byte unchanged. Physical
controller play and enemy-hurt handoff while hanging remain unverified; this
change is confined to accepted native pit deaths. git diff --check passes.

## 2026-10-22 — DK-barrel return without pit drift

Preserved the live quicksave in ignored respawn-placement-inspection/original.sav,
SHA-256 8bc7704bacebbcba6185a9a3adad727d8771c20cfdee58bdb1badb98722767ee.
The copied scene has P1 alive beside a DK barrel and P2 lost. Input-only pickup
and throw revives P2: it reaches X=3670/Y=685, then retains entrance X momentum
when independent movement replaces follower tracking. It slides to X=3614 and
is lost again by frame 128. On the final native $3E tick (+$42 <= 0 before
decrement), refresh its finish point/fractions from the live teammate and clear
entrance velocity/acceleration. Native animation and the $3E->$22 transition
remain intact; no additional native-source adapter or host latch is needed.
Synthetic tests cover either fixed player, early/final/expired timer, current
target subpixels and velocity reset, untouched target/timer and classic/solo/
dead/lost-target guards. New check_coop_respawn_placement.py confirms exact
arrival at the survivor, stable platform standing, no unauthorized revival,
matching mid-entrance reload, stable landed reload and independent jump input.

Validation: Release headless/unit/desktop builds and all synthetic co-op
checks pass. Full CTest remains 54/56 before/after, with the two existing
Get-FileHash packaging failures; 19 adapter tests and the real-emitter contract
pass. New copied-save placement/entrance-reload/landed-reload/control checks
and the broader TEAM lifecycle route (damage, loss, held input, survivor,
DK revival, returned-player control and repeat loss) pass. Rebuilt desktop
completes the 241-frame barrel/platform/jump smoke with a clean exit and the
live quicksave byte-for-byte unchanged. git diff --check passes. Physical
controller play and broader revival terrain/mounted/hanging variants remain
unverified; this is a native DK-barrel arrival correction.

## 2026-10-22 — Barrel passenger gravity and terminal speed

Copied the live checkpoint to ignored barrel-gravity-inspection/original.sav,
SHA-256 6588d1873db40437aa992a80396a72c96c259dc340ac3edc94bdca628453d370.
It already has P2 grounded with work+$0A=$0100 while P1/native properties
use $0800. The shared $22->$12 shot conversion copied sprite context but
not gravity/terminal speed in per-Kong work. Copy work+$08/+$0A after checking
the distinct native work pair. Grounded old saves repair their own terminal
speed from runtime $FF properties +$02; retain native gravity so queued jump
initialization is preserved. Shot updates retain native property +$4A gravity.
Synthetic tests cover either leader and work mapping, copied impulse/gravity/
terminal speed, unchanged owner, grounded repair and classic/airborne/mounted/
missing-property/invalid-work guards. New check_coop_barrel_gravity.py reads
physics from private v7 snapshots and ROM properties, verifies both native
leader orders via input-only team-up, equal peak and descent (within one native
update), matching flight reload and normal-jump recovery from the old save.
Existing momentum tests also verify held-jump approach, both owners/shooters,
upward launch, right steering, left walking animation and landing reload.

Validation: Release unit/headless/desktop builds and synthetic co-op checks
pass. Full CTest remains 54/56 before/after with the same two Get-FileHash
packaging failures; 19 adapter tests and the real-emitter contract pass.
New copied-save gravity/terminal-speed checks pass for both leadership orders.
Existing barrel momentum/steering/landing, DK respawn placement and all pole
movement/animation/camera regressions pass. The rebuilt desktop completes a
175-frame shared barrel ascent/descent smoke with clean exit, preserving the
live quicksave byte-for-byte. git diff --check passes. Physical controller
play and broader barrel variants remain unverified. Already-airborne old
snapshots are not claimed repaired until a new launch or normal landing.

## 2026-10-22 — Mounted bonus return ownership

Copied the live quicksave to ignored bonus-rider-inspection/original.sav,
SHA-256 4147f3d14a04a8c6e6d5a1cacbc3837f35d34eca7f0f6335b6a347683f134c99.
The Rambi bonus times out from level $8B to $29. Restoration recreates the
animal correctly, but co-op releases the dormant follower over a second
barrel. A conflicting independent cannon reaction leaves the rider in $30
and drifting without input, eventually dismounting. A copied classic-mode
comparison retains native mounted $11->$12->$09 and lands safely.
Preserve complete native cannon setup with a validated animal selector and
keep only dormant zero-mask $22/$2A return followers until their live mounted
owner reaches ground in $09/$0B. Release to independent play at the rider's
fixed-point coordinate with flight momentum cleared. Ordinary active partners
and shared bird flight remain independent under their existing policies.
Synthetic tests cover either fixed rider, air/barrel states, idle follower
arrival, preserved mount ownership and collision, invalid animal pointers,
already-controlled partner, bird/classic exclusions and safe grounded release.
New check_coop_bonus_rider.py verifies full timeout return, native barrel
flight, stable mounted/on-foot actors, separate movement/jumps, palette
stability, matching mid-return reload and stable landed reload using inputs
only. All private comparison data stays ignored; no save format change.

Validation: Release unit/headless/desktop builds and synthetic co-op checks
pass. Full CTest remains 54/56 before/after with the two existing Get-FileHash
packaging failures; 19 adapter tests and the real-emitter contract pass.
Reported mounted timeout/return/reload/control tests, existing shared Squawks
loss/handoff and Rattly pit/palette cleanup, and barrel gravity regressions
pass. The rebuilt desktop completes the full 1140-frame timeout, return, rider
movement and partner jump smoke with clean exit and the live quicksave
byte-for-byte unchanged. Removed a local-variable shadow warning without
changing behavior. git diff --check passes. Successful bonus completion, other
mounted return families and physical controllers remain unverified.

## 2026-10-22 — Animal-only sections and damage turns

Preserved the live checkpoint in ignored snake-health-inspection/original.sav,
SHA-256 80466dbb99531eb60afff64a092abb1a07d933790146a508a76559f24065d87e.
The entry transforms to Rattly, then advances into a section with $6C=0,
$6E=$194 and mounted flag $0200. The inactive Kong waits in $13 but previously
joined on any input. Suppress its state promotion/input/rescue while retaining
the single native animal actor. Route active controls through the existing
guest controller words rather than physical Kong identity in this mode only.
This scopes all five native animal types without relying on level IDs or the
entry-only $0515=2 flag. Ordinary mounted companions retain independent play.

A copied real-hit route confirmed the surviving animal damage continuation
$B8:8DCB. Capture accepted $23 source/owner and consume once there to switch
controller ownership, leaving native health/recoil/invincibility intact. Reset
and replacement clear pending candidates; duplicate callback entry cannot
switch twice. Strict source adapter validation pins the anchor, with missing
anchor, idempotence and real-emitter coverage. Synthetic tests cover four
consecutive switches for each animal type/either physical owner, held/pressed
routing, hidden partner, native health preservation and mode/mount/reset guards.
Private check_coop_animal_turns.py drives transformation/section entry, each
inactive button, a real hit, old/new controller steering, pre-hit reload,
DK-barrel healing without spawning and native final loss. Current turn uses
existing guest words and requires no snapshot format extension.

Validation: Release unit/headless/desktop builds pass without project-source
warnings (existing generated-source warnings remain). Full CTest remains
54/56 before/after with the same two Get-FileHash packaging failures. All
synthetic co-op checks, 20 strict adapter tests and the real-emitter contract
pass. The new copied-save Rattly route verifies actual damage, changed control
after reload, native DK health restoration without Kong spawning and final
loss. Existing ordinary Rattly pit/palette, shared Squawks handoff and mounted
bonus-return routes pass. The rebuilt desktop completes 930 frames through
transformation, section entry, damage handoff and new-controller movement
with clean exit and the live quicksave byte-for-byte unchanged.
Repeated hits and all five animal types have synthetic coverage; later private
animal-section routes and physical paired controllers remain unverified.
git diff --check passes.

## 2026-10-22 — Water entry respects held-object ownership

Preserved the live checkpoint in ignored water-held-inspection/original.sav,
SHA-256 cb9d7a899af74ede575c1e641286aef18a8dd358b7b326183dbc20bb5a47d76d.
A native beetle pickup followed by partner water entry reproduced clearing
$0D7A at the first swimming frame while the carrier stayed on dry ground.
The generic water-entry drop paths read the shared item directly. Added
actor-local Dkc2CoopHeldObjectValue operands for drop_sprite_if_holding and
drop_follower_kong_if_teamed_up, retaining native drop behavior for the owner
and non-Kong cleanup. The fix is item-type independent and reuses guest-state
ownership reconstruction after load. No runtime policy or snapshot changes.
Synthetic checks cover either carrier, beetle/barrel/crate categories, falling
and swimming partner, shared-slot/carrier preservation, owner drop, cleanup
and classic behavior. Strict source and real-emitter tests now require 18
held-object policy operands including both drop functions; malformed/missing
operands remain fail-closed and adaptation stays idempotent.
New check_coop_water_held.py drives both real beetle pickups and water entries
on opposite platform edges, matching water-entry reloads, explicit dry-ground
throws and owner-water drops. The dry throw checkpoint avoids the nearby
water enemy's unrelated damage handoff interrupting the throw animation.

Validation: Release unit/headless/desktop builds and synthetic co-op tests
pass. Complete CTest remains 54/56 before/after with the two existing
Get-FileHash packaging failures. All 20 strict adapter tests and the
real-emitter contract pass. Both copied-save beetle carriers retain their
item through teammate water entry and reload; deliberate dry throws and
carrier water drops pass. Rebuilt desktop completes both pickup/water routes
(158 and 136 frames) with clean exits and the live quicksave unchanged.
An additional blank-SRAM check_coop_item_input.py replay fails its P1 pickup
setup (no held item); it is not counted as a pass or classified as a proven
regression. Other grabbable water routes and physical controller play remain
unverified, with other item categories covered synthetically. Existing
generated unreachable-code warnings remain; git diff --check passes.

### 2026-10-22 — Thorn damage and native death sequence
Preserved the live quicksave as ignored thorn-death-inspection/original.sav
(SHA-256 e2766c8174c29cc487b21fcf56b074554edc617e4012ae25030798e8a50f4097).
Reproduced immediate $13 parking for either Kong at thorn contact. The native
pit helper also accepts onscreen terrain damage: distinguish native +$10
bit $0100 with no +$1E instant-pit bit $0080 below the screen cutoff. Retain
interaction $23 and promote the touching actor before it, fixing inactive
contact hurting the safe leader. Existing instant/offscreen pit removal stays
intact. No generated-source edits or guessed death animation parameters.
Synthetic public checks cover either touching actor and either leader, native
damage continuation, correct target, teammate collision and no premature loss.
New check_coop_thorns.py verifies both native hurt/death arcs, survivor jumping,
mid-death reload matching, lost-player persistence and final native level loss.
Release unit/headless/desktop builds pass. Full CTest is 54/56 before and after;
only existing create_windows_version/make_release Get-FileHash packaging failures.
Private ordinary pit and pole survival regressions pass. Rebuilt desktop runs
both 330-frame thorn sequences cleanly; live save remains unchanged. Other
thorn layouts and physical controller play remain unverified.
Additional shared Squawks and Rattly pit cleanup/reload regressions pass.

### 2026-10-25 — Share stomp bounce after barrel launch
Copied the current quicksave to ignored barrel-stomp-inspection/original.sav;
SHA-256 32e5ca9d99d78e466a550309d9d317a8ce9b81532c6bf7131c7b9cfb8b016828.
Input-only replay reproduced a sole bounce while the nearby partner remained
in shot state $12. CoopStompAirborne accepted jumps and bounces but omitted
native barrel flight, excluding both owner and partner from shared selection.
Added $12 to this one eligibility predicate; existing accepted native impulse,
gravity/terminal copying, distance settings and descending/ground/lost/mount
checks remain intact. No source adapter or generated-code changes.
Expanded synthetic eligibility/guard checks for both owners across all four
ordinary-jump/barrel-flight combinations. New check_coop_barrel_stomp.py uses
two timings that select opposite native stomp owners and either firing port;
all four routes verify shared bounce, equal lift, separate steering and exact
pre-impact/mid-bounce reload trajectories, camera targets and event masks.
Release unit/headless/desktop builds pass. Full CTest remains 54/56 before and
after, with only the same Get-FileHash create_windows_version/make_release
packaging failures. Ordinary shared/single/staggered stomps, next-platform
arrival and midpoint-camera/reload regressions pass. Rebuilt desktop completes
both reported barrel routes (79/133 frames) and leaves the live save unchanged.
Other barrel/enemy layouts and physical paired controllers remain unverified.

### 2026-10-25 — Independent Kudgel ground-shake stun
Preserved the live quicksave as ignored ground-shake-inspection/original.sav,
SHA-256 26b59a774b3c396578355cd89e79f4faf10f4743efcb60ca51ea2dd1e02e7ded.
The boss landing issuer checked only the active Kong. Added native candidate
fallback to a grounded live partner, native reaction $0B work selection and
bounded repetition for the second eligible Kong. Both use the cartridge's
stun $63 initialization, character animation and physics. No new host latch.
Independent follower recovery needs both CODE_B9DFE5's completion decision and
the existing final idle-recovery decision to accept $63. The shared $091F
counter must decrement only once per native clock phase when both are stunned;
the adapter skips the follower's kong_state_63 decrement at $B8:AFAA. A lone
stunned follower retains the native clock. Timer duration and animation values
remain cartridge-owned, with no snapshot format change.
Synthetic checks cover either leader, grounded/airborne selection, classic,
animal/lost/dead/invincible/absent exclusions, bounded repeat, native recovery
and clock ownership. Strict source tests cover missing reaction/timer anchors,
missing policy functions, fail-closed writes and idempotence; all 21 pass.
The real-emitter contract also passes. New check_coop_ground_shake.py dodges
Kudgel's body using input only, then verifies both grounded, either jumping
and both jumping, full-duration paired recovery, each player's post-stun jump
and exact mid-stun reload. The copied source save remains unchanged.
Release unit/headless/desktop builds pass. Full CTest remains 54/56 before and
after, with the same Get-FileHash create_windows_version/make_release packaging
failures. All four previous barrel-stomp routes, equal lift, independent
steering and pre-impact/mid-bounce reload regressions pass. Other boss quake
variants, animal stun interactions and physical paired controllers are unverified.
Rebuilt desktop both-grounded and airborne-leader routes complete for 540
frames each with clean exits and the live quicksave byte-for-byte unchanged.
git diff --check passes; existing generated unreachable/unused warnings remain.

### 2026-10-25 — Honey-floor contact stays with its player
Copied the live quicksave to ignored honey-movement-inspection/original.sav;
SHA-256 aad638ae8dcb5eda4f3a37a60478c90600f378edcb414080ac9ae20617fddc5e.
Input-only replay showed P2 honey contact applying sticky $43 to the active
P1 while P2 remained $00. Added player_interaction_07 to the existing terrain
work-context redirect using queued-Kong $0A84 ownership. Wrapped its held-item
operand with Dkc2CoopHeldObjectValue so teammate inventory cannot select $7A.
Native honey sticking and jumping remain intact; no new runtime helper or
snapshot fields. The adapter now requires four terrain reaction redirects
and 19 actor-local held-object reads. Synthetic cases cover either toucher
and leader, teammate item isolation and classic mode. Strict fail-closed
adapter tests (21) and the real-emitter contract pass.
New check_coop_honey.py verifies both contact owners, the dry teammate walking,
each player's escape jump and matching mid-jump reload, both Kongs sharing the
honey patch, independent escapes and repeated native hops. The supplied save
is copied before use and remains unchanged. Release unit/headless/desktop
builds pass. Full CTest remains 54/56 before/after with only the same two
Get-FileHash packaging failures. All four ground-shake eligibility/recovery
cases and native duration/reload checks pass as a regression. Honey-wall and
carrying private routes and physical paired controllers remain unverified.
Rebuilt desktop completes both single-owner honey routes (190 frames each)
and the shared-honey escape route (260 frames), with clean exits and live
quicksave unchanged. git diff --check passes.

### 2026-10-25 — Sticky-wall attachment follows the touching Kong
Preserved the live checkpoint as ignored honey-wall-inspection/original.sav,
SHA-256 95eeff972533610bf14f10475e24498f383136da57c9a29ffb4a3bbf4898c2b2.
P2 right-jump contact reproduced attaching untouched P1 while P2 fell away.
Reaction $08 had the same unconditional active work-context selection as the
previous honey-floor reaction. Added $08 to the existing terrain redirect;
queued $0A84 chooses the actual touching Kong without swapping leadership.
Native attachment $41, wall jump $42, character animation, drop behavior and
physics remain intact. No runtime helper or snapshot changes. Strict adapter
and real-emitter tests now require five terrain redirects. Synthetic honey
ownership coverage includes both reactions $07/$08, either leader/toucher,
item isolation and classic behavior.
New check_coop_honey_wall.py verifies either actor attaching to the reported
right wall, stable hanging after reload, own wall jump with an untouched
teammate, matching mid-jump trajectories and both sharing the same wall while
jumping away independently. Existing honey-floor contact/movement/jump/reload
and repeated-hop acceptance passes. Release unit/headless/desktop builds pass.
Full CTest remains 54/56 before/after with only the same create_windows_version
and make_release Get-FileHash packaging failures. All 21 strict adapter tests
and the real-emitter contract pass. Rebuilt desktop completes both individual
wall routes (115 frames each) and the shared wall route (150 frames) with clean
exits and live quicksave unchanged. Other wall orientations, wall-slide state
$44, carrying private routes and physical controllers remain unverified.

### 2026-10-25 — Sticky-wall midpoint camera
Copied the current quicksave to ignored honey-wall-camera-inspection/original.sav,
SHA-256 ab817c217bfe89082bfb201d5fcf5e426eede685886e4c95b8030bcbc81c7a2f.
Reproduced P2-only targeting while both live Kongs hang on opposite walls.
CoopCameraPosition excluded honey states. Added $41..$44 and $7A to its live
eligibility list without changing fixed-point midpoint math or native/dead/
lost/scripted guards. No adapter, generated-source or snapshot changes.
Synthetic camera tests cover all 36 combinations of ordinary/honey states,
either native selector and integer/fractional coordinates on both axes.
Extended check_coop_honey_wall.py with midpoint assertions for original
attachment/jump routes and an optional --camera-state checkpoint. The reported
opposite-wall save passes neutral hanging, 50-frame live windows of either/
both wall jumps, and exact post-reload player/camera target matching. Longer
falls hit native harmful terrain or pit death and are outside these live-camera
assertions. Existing honey-floor contact, independent controls, repeated jumps
and reload checks pass. Private wall-slide/carry variants and physical paired
controllers remain unverified, with those states covered synthetically.
Release unit/headless/desktop builds pass. Full CTest remains 54/56 before and
after, with the same create_windows_version/make_release Get-FileHash packaging
failures. Source saves remain unchanged.
Rebuilt desktop opposite-wall idle and three jump routes complete (30/50/50/50
frames) with clean exits and live quicksave unchanged. git diff --check passes.

### 2026-10-25 — Developer config and F8 beat-level command
Added config/developer.cfg (ENABLED=1, beat-level key F8 by default), portable
runner/dkc2_developer.c/h and non-repeating Windows/SDL function-key dispatch.
The user selected F8. Settings are compiled like coop_tuning.cfg and require
rebuilds. The key is independent of Assist bindings and blocked by the overlay.
The command uses the native $08A8 map-level completion bitmap and ROM mask
table, then the normal exit states/fade; it never invokes generated functions
out of context or mutates CPU registers/stack. It refuses bonus/boss/map/title,
invalid/dead ownership and duplicate/death exits before changing guest state.
An exploratory boss clear did not complete the special victory/map sequence;
the shipped guard explicitly rejects boss kind 3 without changing progress.
Bonus/boss/endgame victory commands remain future work.
Synthetic test_developer_enabled/disabled cover all 256 bitmap indices, both
leaders, existing progress preservation, exit state/fade, CPU continuation,
unknown commands and invalid contexts, including boss/map and disabled builds.
New scripts/check_developer.py verifies the supported HiROM completion mask,
normal exit to map, subsequent map rejection and source-save preservation.
The copied source is developer-inspection/original.sav (ignored), SHA-256
81d5fa4246639dffd1d05fb292347febc099b5823ab5bd19db55b551eb768108.
During F8 acceptance, found older Windows hidden smoke tests did not consume
DKC2_SAVESTATE_INPUT: they exercised boot/input and clean exit, not the claimed
checkpoint routes. Added exact checkpoint loading only in hidden Windows
acceptance runs. The new F8 smoke confirms a real key-down event executes the
command after loading the copied gameplay state; live saves remain unchanged.
Prior headless checkpoint gameplay verification is unaffected.
Enabled/disabled tests, Windows/headless/SDL Release builds and private normal
completion/map rejection pass. Physical SDL-keyboard input and special animal/
endgame transitions remain unverified. Generated unused/unreachable warnings
remain unchanged. Developer commands are not controller-recording events.
Complete CTest is 56/58 after adding two developer tests (baseline 54/56);
only the existing Get-FileHash create_windows_version/make_release packaging
failures remain. The loaded-state Windows F8 smoke completes 180 frames with
clean exit and live save unchanged. git diff --check passes.

### 2026-10-25 - F8 completes boss fights and bonus rooms
Extended the compiled developer command to room kinds 1/3. Bonus/boss
rewards use native room-indexed $7E:59B2, set collection signal $08C4 bit 2,
and increment $08CC only for a new reward, capped at the native 75. Bonuses
do not clear the parent map node; the native exit restores its animal form.
Boss skips mark the map node and select the normal destination exit before
fading, bypassing kind-3 parent-sublevel restoration. CPU state stays intact.
Bonus $059D=1 is a valid exit selector. The command now also requires the
gameplay continuation $0020=$8608, rejecting map states with stale boss and
camera fields. Rendered map inspection corrected the earlier mistaken
conclusion that the boss experiment had not reached a map: room kind alone
is insufficient evidence. The shipped path preserves $059D=0 for bosses.
Copied original checkpoints before use into ignored developer-special-inspection:
boss SHA-256 26b59a774b3c396578355cd89e79f4faf10f4743efcb60ca51ea2dd1e02e7ded;
bonus SHA-256 4147f3d14a04a8c6e6d5a1cacbc3837f35d34eca7f0f6335b6a347683f134c99.
Input-only ready copies let Kudgel finish the intro and enter the bonus loop.
Expanded enabled/disabled synthetic tests cover all 256 room reward indices,
both leaders, duplicate rewards, the 75-coin cap, bonus parent preservation,
boss map credit, map continuation rejection and unchanged CPU/disabled memory.
Extended check_developer.py verifies copied normal, Kudgel and Rambi bonus
states, progress/reward bits and coin count, native map/parent return,
preserved animal form, map rejection and unchanged input saves. All pass.
Rebuilt Release headless, Windows and SDL hosts. Real Windows F8 WM_KEYDOWN
smoke tests load all three copied checkpoints, execute the command, and exit
cleanly after 180 frames each; the live quicksave remains unchanged.
Complete CTest before/after is 56/58, with only existing Get-FileHash failures
in create_windows_version/make_release. git diff --check passes. Other boss
encounters, final ending cinematics and physical SDL-keyboard input remain
unverified; the skip does not reproduce each boss defeat animation.

### 2026-10-25 - Thrower freezes after passenger grabs a hanger
Copied the live quicksave to ignored throw-hanger-inspection/original.sav
before probing; SHA-256 a9c50f815808b64175b00ef5b26aa5a6f5c46791c9731dc4ba01f04ecbc54f30.
Input-only Up+Run through the throw animation reproduces passenger $2B ->
$30 -> $3C while P2 remains in throw-wait $2C. Jumping first freezes the
thrower in midair after hook ownership promotes the passenger.
The existing dispatch policy now releases $20/$2C only for an independently
controlled live pair, empty hands and a partner confirmed at a recognized
hanger anchor in state $3C. Restore normal movement/interaction, preserving
position, velocity, native gravity record and the passenger. No generated
code changes or transient host tracking are needed; old frozen saves recover.
Synthetic tests cover both throwers, both wait states and rejection during
flight, invalid props/anchors, carrying, mounts, lost partners and classic mode.
New check_coop_throw_hanger.py copies input saves and verifies ground/jumping
throws, walking/jumping after reload, stable passenger attachment, old frozen
state recovery and ordinary horizontal throw regression. All pass. Existing
shared-hook acceptance also passes both first-grab orders, free-partner input,
shared occupancy, independent first-press release and reload. An initial run
used the wrong archived checkpoint (no fixed hook); rerun with the preserved
Gangplank hang.sav passes. Release headless/Windows rebuilt; full CTest before
and after remains 56/58, only the existing Get-FileHash packaging failures.
Release SDL build also passes. Rebuilt Windows hidden copied-state smoke
routes cover jumping throw plus movement and old frozen-state recovery, with
clean exits and the live quicksave hash unchanged. git diff --check passes.
Physical controller play and other hanger families are not privately replayed
in this milestone; synthetic coverage includes either fixed controller owner.

### 2026-10-25 - Select rescue blocked on sticky walls
Copied the current save before testing to ignored honey-teleport-inspection/
original.sav, SHA-256 5af5e680fe55e1196ba933400e0919b576b64206e608b67f290a40a421bb4cf9.
P1 is attached to the left wall and P2 is grounded below. Both directions were
blocked by the rescue movement whitelist; wall destinations additionally
failed the ground requirement. Extend only CoopRescueMove's honey handling,
leaving the general movement predicate's other callers unchanged.
Live honey sources $41..$44/$7A can leave; stable $41 wall destinations share
the proven anchor with native wall state/own-character animation and facing.
Grounded honey-floor targets qualify too. Detaching resets animation callbacks
and restores own normal gravity; jumps in flight are still not destinations.
Synthetic tests cover both players and all five honey source states, normal/
wall destinations, configured timing, once-per-hold behavior, own animations/
gravity, facing-only attribute changes, target preservation, and jumping/dead/
classic destination rejection. New check_coop_honey_teleport.py copies saves,
uses input-only reverse-role setup and checks both players in both directions,
pre-hold timing, stable wall attachment, independent jumps and snapshot reload.
All pass. Existing honey-floor and shared-wall ownership/jump/reload private
checks pass unchanged. Headless/Windows/SDL Release builds pass. Real Windows
hidden checkpoint routes replay both Select directions and own jumps with
clean exits; live quicksave hash is unchanged. Full CTest before/after remains
56/58 with only existing Get-FileHash create_windows_version/make_release
packaging failures. git diff --check passes. Physical controllers and a rescue
while carrying an item onto a wall were not separately replayed.

### 2026-10-25 - P1 pit game over while honey-attached P2 is alive
Copied the live save to ignored pit-honey-inspection/original.sav first;
SHA-256 120f99bd83052a33a2067a22801329b5960f9e5fd21efd4ddd67c2a5bbc3e8c9.
Input-only P1 left jump reproduces level loss while P2 remains attached in
$41. P2 can jump onto the honey floor and fall left; native inactive parking
then leaves it recoverable because honey-floor $43 was also ineligible for
the host survival path. Extend Dkc2CoopPitSurvivor's local survivor whitelist
to live honey states $41..$44/$7A, keeping the existing harmful-terrain, lost,
collision and screen cutoff guards. Both falls now promote the living honey
teammate without altering pose/animation/position, and mark the faller lost
consistently. The existing policy requires DK-barrel revival; Select cannot
revive either lost faller. Asked whether the user instead wants recoverable
pit falls; no policy change was inferred while awaiting clarification.
Synthetic tests cover all five honey survivor states, both leader/faller
identities and retained attachment/animation. New check_coop_honey_pits.py
copies the checkpoint and verifies both input-only falls, surviving controller
jumps, level continuity and lost-state persistence across reload and Select.
The established Gangplank pit acceptance also passes all leader/faller pairs,
last-survivor death and simultaneous loss. Enabled unit tests and Release
headless/Windows/SDL builds pass. Full CTest before/after remains 56/58; only
existing Get-FileHash packaging failures remain.
Windows hidden copied-state pit/survivor-input smoke routes both exit cleanly.
The live quicksave hash is unchanged; git diff --check passes. Physical
controller play and a recoverable-pit gameplay policy remain unverified.

### 2026-10-25 - Spider rider pit corrupts the survivor state
Copied the current save first to ignored spider-pit-inspection/original.sav;
SHA-256 09818b709cf57f4332e6f6bc81c9882e2e5a143908faf5df6cd7647febca8df6.
P1 rides Squitter ($190) on a small Hornet Hole platform. Input-only left or
right movement reproduces the rider parked lost with the old mount selectors
still active, because existing mounted cleanup only accepted Rattly/Rambi.
Include Squitter in live mounted-rider and lost-mount validation; also accept
airborne riding state $0A. At an accepted active-player pit loss, release the
validated mount record regardless of a pose transition. Restore palette-index
ownership, let the spider use its native unmounted lifecycle, then clear
mount selectors/flag before promoting the survivor. Older lost-rider snapshot
repair now recognizes Squitter too. No generated source changes are needed.
Extended synthetic mounted-pit tests cover Squitter/Rattly/Rambi, both riders,
all three riding poses, palette preservation, cleanup and old-save repair.
New check_coop_spider_pits.py preserves external inputs and creates P2 riding
through actual X dismount and jump inputs. It verifies both riders falling
from both edges, no level exit, normal survivor controls, correct palette
ownership, lost-rider Select rejection, exact midfall resume and old broken
snapshot repair. All pass; dead players still require a DK barrel, following
the user's confirmed pit rule. Headless/Windows/SDL Release builds pass.
Complete CTest before/after remains 56/58, only existing Get-FileHash failures
in create_windows_version/make_release. Other animal families are unchanged
and were not privately replayed in this milestone.
Windows hidden checkpoint smoke routes cover either rider loss plus survivor
jump and old-state recovery, all with clean exits. The live quicksave remains
unchanged and git diff --check passes. Physical-controller testing is unverified.

### 2026-10-25 - Camera midpoint was not centered in the viewport
Copied the live save first to ignored spider-camera-inspection/original.sav,
SHA-256 ed47d3c5c8bb9795eda3560436b0c809b2a450f3438150b4ff989b1ecedc59fc.
P1 rides Squitter while P2 hangs above. Traces show the existing target already
correctly averaged both players, but native single-player camera offsets left
the actual view near one edge. A private offset-field experiment did not fix
the placement because native movement rewrites these profiles. No such profile
patches are shipped. Instead, reuse the live-pair eligibility predicate and
center native desired 24.8 scroll at the midpoint minus (128,112), immediately
before native camera bounds/region/streaming processing at $B5:E549. Preserve
CPU/actor/profile state; ineligible pairs keep the original desired scroll.
Source-adapter validation requires exactly one continuation and rejects
missing/misplaced adaptations. Added synthetic fixture and real-emitter checks.
Synthetic camera tests cover either selector, ordinary/riding/hanger/honey/pole
poses, precision, underflow, preserved profiles/CPU and native fallback for
classic/lost/dead/scripted/invalid contexts. New check_coop_camera_center.py
copies the source, verifies actual viewport center after smoothing, both
players' jump routes, target precision and exact settled/mid-jump reload.
Corrected the acceptance expectation to include native Squitter jump $0B.
All camera checks pass; settled scroll is (301,1644) for midpoint (429,1756).
Private rendered-frame inspection shows both players around the viewport
center. Spider-pit and sticky-wall ownership/jump/reload regressions pass.
Release headless/Windows/SDL builds pass; Windows hidden copied-state camera
routes exit cleanly. Live quicksave remains unchanged. Full CTest before/after
is 56/58 with only existing Get-FileHash packaging failures. Native regional
bounds still constrain final framing; physical controller camera play and
all stage-specific camera locks remain unverified.

### 2026-10-26 - Pole cannot be grabbed after a death or earlier pole jump
Preserved the initial failure as ignored pole-dead-inspection/original.sav,
SHA-256 27d4b97910380bd145eccba6e7c2458d144b5a58e5fdfe0bb486c441e62b86cb.
It records both Kongs lost although P2 is visibly alive and moving, with the
both-present flag clear. Reconcile a live active actor's established gameplay
pose independently of both-present; waiting/dead actors remain lost.
The user then supplied a second failure with both alive, preserved as
both-alive.sav, SHA-256 0b8e18142c6808cfb38fe3405bc339210fd474d5f708704c1884af0d2012217a.
Both +$4C words retain $0EFC from a previous pole. The actual pole now occupies
that reused slot at X=3024. Input-only approach traces enter valid X/Y contact
but the old reference suppresses attachment. Temporary diagnostic logging
confirmed valid geometry and no lost bit; it was removed after diagnosis.
Native pending no-contact skips the current-pole callback, where the old
expiry lived. Expire the jump reference in the pending pass on departure or
non-jump/pole movement, preserving suppression while still touching the pole.
Synthetic tests cover the native no-contact scheduling path, either player's
real pole entry after stale loss, all live restoration poses and dead/inactive
retention. check_coop_respawn_lost.py verifies source metadata recovery and
controlled stale-metadata replay on an actual pole with either dead teammate.
New check_coop_pole_regrab.py verifies both players' clean first grabs, climb
controls, release, latch expiry and repeat grabs from the newer reported save.
The nearby wasp damages repeat attempts independently of pole contact; the
repeat-only check uses native invincibility on private copies, with no actor
position/velocity/geometry changes. Initial grab/release proofs are unmodified.
The earlier shared-pole death/survivor/last-loss regression still passes.
The first barrel-rope save has no recognized actor pole at that location and
classic mode also rejects its jump; exact first-location attachment remains
unverified without a working checkpoint. The second supplied climbable pole
is verified fixed. No ROM/assets/saves/binaries are tracked.
Release headless/SDL and a separate Windows output build pass. Windows copied-
state routes for either player's grab/climb/release exit cleanly; live save
remains unchanged. Complete CTest before/after is 56/58, only existing
Get-FileHash create_windows_version/make_release failures. The initially
running game blocked linking the usual executable; the tested alternate
output kept gameplay undisturbed while the user supplied the second save.
After the game closed, the normal Windows Release target rebuilt successfully
into build-game-vscode/Release/DKC2Recomp.exe. git diff --check passes.

### 2026-10-26 - Independent rollercoaster jumps and rider loss
Copied the current save before inspection to ignored coaster-inspection
artifacts; SHA-256
8068d9a4c62e4a92cd4f4ab8027780cc83367bde1cdaeee68fa3753589828070.
Both players enter the native $0158 cart from barrel flight, then occupy
$5B/$5C. Native paired jump $16 launches only the active rider and overwrites
the other with follower flight. Each seated player's pressed B now starts
its own native $67 air update using its own character's ROM jump physics.
Cart seat placement retains airborne Y and restores its seated pose on
descending contact, keeping native cart motion, slopes and sparks.

Private replays exposed two related hurt failures: the active hitbox included
the cart even during a jump, and accepted inactive-player damage was applied
to the leader. Skip the cart-body clipping union for independent riders and
capture accepted damage ownership before later collision checks overwrite it.
Native hurt initialization still runs; its follower freeze/confirmation is
replaced with a moving cart survivor and native damage grace. The native
cart-specific $21 collision also retains the other rider, while the lost
rider takes its original $6B death arc. Dead actors remain unavailable to
input/Select. Accepted pit loss recognizes a live cart rider as a survivor.

Synthetic policy tests cover either port, own impulse/seats, airborne placement,
landing, accepted damage ownership, survivor/lost-mask handoff, grace, final
loss and classic fallback. Adapter fixtures fail closed on missing coaster
anchors and labels; all 23 adapter tests and the real emitter contract pass.
New scripts/check_coop_coaster.py uses copied saves and real controller input
for either jump, simultaneous/staggered jumps, exact midair reload, either
rider hurt, own survivor jump, uninterrupted cart movement, dead Select,
cart-specific collision and exactly one team life spent after final loss.
A separately identified controlled copy changes only the spare-life word
$08BE from 4 to 0: first hurt preserves the survivor and zero counter; final
loss reaches $FFFF/native $FA80. Rendered output was inspected: GAME OVER.
No position, velocity or geometry edits are used in the acceptance routes.

Release unit, headless, Windows desktop and SDL builds pass. Windows hidden
smokes load the copied checkpoint for staggered jumps (207 frames) and damage
through final loss (1050 frames), both exit desktop_completed. Live slot-0
bytes/hash remain unchanged. Baseline and final complete CTest: 56/58, only
existing create_windows_version/make_release Get-FileHash packaging failures.
The normal build-game-vscode/Release/DKC2Recomp.exe is rebuilt in place.
Other cart actor types and complete coaster-level playthroughs remain
unverified; the adapter is deliberately scoped to the observed $0158 cart.
No ROM, save, graphics, screenshots or generated binaries are tracked.


### 2026-10-26 - Coaster stomp bounce and independent gap survival
Preserved both supplied checkpoints under ignored coaster-stomp-inspection
artifacts before inspection. Stomp checkpoint SHA-256:
3993e5b4b083ba0915a20b967f3a9acce1d556ff4c685f4898400281433477c5.
Later gap checkpoint SHA-256:
4dda3037cb39727c1e27c4b8964ec597a0a7b3d4fc3862fbbb9e8b68bd64ca4f.

Native enemy collision used the global cart pointer as a cart-body attack,
so airborne riders killed enemies without receiving their native bounce.
Scope the collision override to the actual live airborne collider and retain
native coaster bounce state $68, including shared stomps. Native airborne
work accesses now select the current rider's own work area. Accepted cart
departure clears only that rider's seat and inherits cart horizontal speed.

Private replays also found inactive follower translation at $BE:D88B,
premature cart recruitment, and stale old-cart seat ownership snapping a
survivor off the next cart. Independent riders skip follower translation but
retain native collision, reject rising/same-cart recruitment, and validate
exact cart ownership before placement. Offscreen falling carts evaluate each
rider independently: actual fallers stay lost, safe airborne/new-cart riders
continue with their own controls, and final loss keeps native team-life and
Game Over handling. Lost riders cannot return through Select.

Synthetic tests cover either rider, native $67/$68 flight, own work/seat
ownership, momentum, entry guards, stale-cart placement, individual pit loss,
and native final-loss fallback. Adapter fixtures and real-emitter contract
pass. New check_coop_coaster_stomp.py replays both supplied checkpoints using
controller input: either/shared upward stomp bounce, own seat landing, either
gap survivor, dead Select rejection, no premature team life loss, and exact
mid-gap save/reload. Both missing the gap spends one team life. A separately
identified copy changes only spare-life word $08BE to zero and reaches native
Game Over after final loss. Acceptance routes do not alter geometry, physics,
positions or invincibility. The previous coaster regression also passes.

Release unit, headless, Windows desktop and SDL builds pass. Complete baseline
and final CTest: 56/58; existing create_windows_version and make_release fail
because their spawned PowerShell cannot find Get-FileHash. Normal desktop
executable rebuilt in place. Complete coaster-level playthroughs and other
cart actor types remain unverified; these policies target observed $0158
carts. No ROM, save, graphics, screenshots or generated binaries are tracked.
Windows hidden desktop smokes also complete: shared stomp (180 frames),
either gap survivor (370 frames each). Live slot-0 bytes remain unchanged
with SHA-256 matching the preserved later gap checkpoint. git diff --check
passes.

### 2026-10-26 - Lone survivor next-cart landing
Copied live slot 0 before inspection; SHA-256:
d6291c1b38a8b7f20f11961678bc959f5461b4ab8b0a4692cada4e8c9d83003b.
The extra origin-height check rejected native accepted descending foot contact
with the next cart. Remove that arbitrary threshold while retaining lost,
rising and own-cart reunion guards. Finish accepted new-cart vertical seating
before native cart validation can eject its new rider. Include detached live
coaster flight in the own-active-hitbox policy. Native airborne horizontal
steering is retained. New check_coop_coaster_landing.py uses copied external
saves and controller input across four jump timings, verifies the next-cart
attachment, dead teammate, unchanged lives and exact midair reload. Synthetic
tests cover accepted high foot contact/seating and detached collision policy.
Validation: new landing acceptance and both previous coaster acceptance
scripts pass, including either-player gap survival, stomp bounce, own jump,
hurt handoff, exact reload and native final loss/Game Over. Release unit,
headless, Windows desktop and SDL builds pass. Desktop copied-state replay
completes 170 frames; live slot-0 bytes/hash remain unchanged. Complete
baseline and final CTest: 56/58, only existing create_windows_version and
make_release Get-FileHash packaging failures. git diff --check passes.
Full-level playthrough remains unverified. No private assets are tracked.

### 2026-10-26 - Paired coaster landing tolerance
Copied live slot 0 before inspection; SHA-256:
82a05e4841000c0404549458d40a5dc85fb821f965c189de09d23b85a6ce934a.
Reproduced the clarified report: P2 lands while P1 narrowly misses the rear
seat after a simultaneous jump. Character arcs and seat spacing leave a
descending partner close to the cart but outside native overlap. Other jump
timings leave a rear-first rider waiting for a missed front recruitment.
Add bounded partner landing at the existing state dispatcher: a live $67/$68
descender may fill a nearby empty seat only after a live teammate has landed
in that cart. Use native slope/character offsets and +/-32 X, -32..+24 Y.
Keep own-cart arcs, rising actors, non-jumpers, lost actors and distant pit
fallers excluded. Clear old seat ownership, initialize the new pose/animation
and own movement correction, and advance native waiting -> moving only when
the front seat fills. Native track motion and independent jump input remain.
Synthetic tests cover either Kong, either seat, bounds, lost/rising/occupied/
classic rejection, accepted ownership and native start state. New private
check_coop_coaster_pair_landing.py replays eleven simultaneous/staggered timing
pairs, either own jump after landing, a genuine missed-pit loss, and exact
reloading between/after arrivals. All three previous coaster acceptance
scripts pass, including one-survivor landing, either pit survivor, stomps,
hurt/control handoff and native final loss/Game Over. Full-level playthrough
remains unverified. An exploratory frame-100 reload kept both riders/camera
identical but left the unoccupied sinking old cart one update behind; exact
all-sprite reload checks pass at frame 108. No private assets are tracked.
Release unit/headless/Windows desktop/SDL builds pass. Hidden desktop
replays complete 150 frames for both the front-first and rear-first cases.
Live save bytes/hash remain unchanged. Complete baseline and final CTest:
56/58; only the existing create_windows_version and make_release packaging
failures remain (spawned PowerShell cannot find Get-FileHash). git diff
--check passes. Normal playable executable rebuilt in place.

### 2026-10-26 - Verify and repair detached shared coaster stomp
Preserved current checkpoint before inspection; SHA-256:
1307e675b78d0a3ab8a7a21abfd9413ae95f2ab65f36813f03eb2f4b293e2a1a.
Input replays reproduced P1 bouncing while nearby descending P2 did not
after both cart pointers had cleared. The pre-contact policy allowed cart
flight but post-bounce validation required attached CartRider ownership.
Use live $67/$68 flight identity for the accepted owner, retain $68 for the
partner even when detached, and classify detached airborne collider attacks
independently of shared cart ownership. Existing proximity/status gates stay.
Synthetic shared-stomp cases now cover attached, owner-detached, partner-
detached and both-detached combinations for either owner and $67/$68.
New check_coop_coaster_detached_stomp.py uses only controller input and copied
saves: seven simultaneous/staggered timings prove both +$42 pointers are zero
before contact, both receive identical upward impulses and ascend more than
45 pixels, and both land on the next cart without losing a rider/life. A
separate outside-threshold route preserves a solo bounce. Mid-bounce exact
save/reload passes. Full-level playthrough remains unverified.
All four prior coaster acceptance scripts pass. Release unit, headless,
Windows desktop and SDL builds pass; hidden desktop shared-stomp replay
completes 220 frames. Live slot-0 bytes/hash remain unchanged. Full baseline
and final CTest: 56/58; only existing create_windows_version/make_release
Get-FileHash packaging failures. git diff --check passes. No private assets
are tracked; normal playable executable rebuilt in place.

### 2026-10-26 - Early cannon landing falsely killed by old coaster
Copied current save before inspection; SHA-256:
3d3931add0643994abf8b6c2bb854c49e4b5aacbc344947358298e2744d95667.
Reproduced an actual early barrel contact with controller input: Jump+Right+
Run at frames 66-70 captures on frame 112, old-cart reaction $15 changes $11
back to $5B on frame 113, and the falling cart starts death on frame 119.
An earlier timing without enough horizontal motion genuinely misses instead;
acceptance explicitly checks both real contact and that negative case.
Filter cannon occupant $11/$31 before native cart collision can recruit it;
retain a reaction guard against already captured occupants. Capture dispatch
releases only that Kong from old cart seats/pointer and clears global $0D5E
when no live rider remains. Without the latter cleanup, actor-slot reuse
still causes a false remote-cart landing and death mid-shot. Native shot $12
keeps destination-cart eligibility. Synthetic tests cover either Kong and
occupant state, queued entry rejection, independent remaining rider, native
shot eligibility, other sources and classic fallback.
New check_coop_coaster_cannon_capture.py uses eight early/later input routes,
accepted cannon dwell, native shot and genuine next-cart landing. No death or
lost-teammate revival occurs. Exact in-cannon/mid-shot reload passes. Actual
missed entry still spends one native team life, measured after banana-awarded
extra lives. Full-level playthrough remains unverified.
All five previous coaster acceptance scripts pass. Release unit, headless,
Windows desktop and SDL builds pass. Hidden desktop early-cannon route
completes 300 frames; live save bytes/hash remain unchanged. Complete baseline
and final CTest: 56/58, only existing create_windows_version/make_release
Get-FileHash packaging failures. git diff --check passes. Normal playable
executable rebuilt in place. No ROM, save, screenshot or binary is tracked.

### 2026-10-26 - Coaster scripted exit strands its rider
Copied current checkpoint before inspection; SHA-256:
3ad569ae7cdee48ac190974509aa51bfb0aaba1b81b8697fc0df4cf89c8808cb.
Reproduced the survivor freezing near X=17210 in native state $70 while the
cart continued. Native $B8:ADF1 requires cart placement to carry the rider
past the viewport before starting its fade. The independent seat policy
recognized gameplay states but omitted that scripted action. Carry live,
non-lost, correctly owned $70 riders with their existing native seat offsets,
preserving the state and ending sequence. Keep that exception local to seat
placement, with no new gameplay rider or jump eligibility.
Synthetic tests cover either Kong/front/rear seat, continued movement, native
exit-state preservation, jump rejection, missing/lost/mismatched ownership
and classic fallback. New check_coop_coaster_exit.py replays the supplied
save with neutral or jump input, verifies continued $70 movement, native
$66 fade and room $1E arrival, then triggers the actual $0160 goal and returns
to the map (room $2E/submode $10). Exact mid-exit save/reload passes. No
position/physics edits or developer shortcut are used. Full-level playthrough
from its start remains unverified.
All six previous coaster acceptance scripts pass. Release unit/headless/
Windows desktop/SDL builds pass. Hidden desktop exit-and-goal replay completes
870 frames; live save bytes/hash remain unchanged. Full baseline and final
CTest: 56/58, only existing create_windows_version/make_release Get-FileHash
packaging failures. git diff --check passes. Normal playable executable
rebuilt in place. No ROM, save, screenshot or binary is tracked.

### 2026-10-27 - Ceiling thorn assigns damage to the teammate
Copied live slot 0 before inspection; SHA-256:
f73747c1137cc1db821028a54011895ac285aecb17c23e91b63fbd5fef228568.
Reproduced P1 jumping into the ceiling while active P2 stays below: native
$23 source is P1 but hurt selects active P2, so P2 dies. The prior floor-
terrain owner promotion does not cover this ceiling/side movement path.
Record a live non-lost self-sourced Kong at accepted $23 and promote that
recorded victim in the existing native hurt-ownership continuation. Validate
source identity, ignore later $6A changes, consume once, and keep existing
cart ownership/floor handling. Native animations, survivor handoff and last-
player loss remain responsible for their original behavior.
Synthetic tests cover either fixed Kong, active/inactive victim, overwritten
reaction/collider, invalid source, wrong reaction, absent/lost victim, reset,
classic fallback and one-shot consumption. New check_coop_ceiling_thorns.py
uses copied checkpoint/controller inputs for either ceiling victim, native
$24/$25 death, live survivor control, no premature life loss, exact mid-hurt
reload, dead Select rejection and native final loss. Existing floor-thorn
acceptance remains intact. Other ceiling layouts and a full level playthrough
remain unverified. No private assets are tracked.
Existing cart hurt/survivor and detached shared-stomp acceptances also pass.
Release unit, headless, Windows desktop and SDL builds pass. Hidden desktop
replays complete 230 frames for each ceiling victim; live save bytes/hash
remain unchanged. Complete baseline and final CTest: 56/58, only existing
create_windows_version/make_release Get-FileHash packaging failures. git
diff --check passes. Normal playable executable rebuilt in place.

### 2026-10-27 - First-boarder shared kart control
Preserved live slot 0 before inspection, SHA-256
c1681e65484e0a5a42fe2b144ea1b77f4a380ffa0ec24b0c18fb50d991795f2e.
Mode-$02 kart must jump with its riders rather than eject either Kong.
Bypass independent seated launch only for this variant, route native jump
work to the front rider and queue their own port's Jump edge. Native
whole-cart jump now carries both seat positions through its original arc.
The recruitment owner read and accepted front reaction preserve the first
boarder regardless of fixed controller port; later passengers cannot take
over. No new save metadata, ROM assets or binary changes are tracked.
Synthetic policy and emitter tests cover first-boarder ownership, either
controller, passenger rejection, seated states and classic/coaster fallbacks.
Private check_coop_shared_kart.py covers original P1 seating and controlled
P2-first seating, native cart height/landing, passenger input and exact
mid-jump reload. Existing coaster jump/hurt/life accounting and ceiling
thorn ownership acceptance pass. Full kart-race completion is unverified.
Release unit, headless, Windows desktop and SDL builds pass. Full baseline
and final CTest: 56/58; only existing create_windows_version/make_release
Get-FileHash packaging failures remain. Hidden desktop copied-save jump
replay completes 75 frames. Original copy and live slot 0 are unchanged.
First-contact ownership is covered synthetically; the provided checkpoint
already has both riders seated, so a fresh input-only boarding approach
from the level entrance remains unverified. git diff --check passes.

### 2026-10-27 - Sideways throws and shared framed hooks
Preserved live slot 0 before testing, SHA-256
e530eef3bd9b9ed1218277664a59bf4ce6f50b0fcf2f2ec745ea0bb0f9ec82d8.
The room-$2C hooks use +$4E high-byte camera parameters, excluded by the
old fixed-hook sharing filter. Preserve native framing and scan the second
collider after its camera continuation; allow native release cooldown for
these hooks too. A horizontal thrown Kong lacked the grab mask and was
blocked by its thrower's busy wait. Scope eligibility to live non-lost
$1F hook contact and the matching $20 thrower. Use direct native attachment
for the accepted collider instead of the AI-reunion follower callback,
which can stall the new independent contact in state $30. No throw impulse
or hitbox dimensions are changed. Classic behavior retains native policy.
Synthetic policy, adapter and emitter tests pass. New input-only
check_coop_thrown_hooks.py verifies real sideways contact, misses below
the hook, free thrower controls, shared framed occupancy, exact reload,
either departure order with one Jump and later regrab. Existing upward
team throws, ordinary shared hooks in both orders and Kleever camera/hooks
pass. Mounted-Rattly grab/reload/release checks pass; that older script
later fails its unrelated hard-coded three-second rescue assertion because
compiled Select tuning is now two seconds. Every hook layout and full
level completion remain unverified.
Release unit, headless, Windows desktop and SDL builds pass. Hidden desktop
replay completes 253 frames; desktop trace/output-state environment knobs
are unsupported, so actor assertions use the headless replay. Baseline and
final CTest: 56/58 with the same create_windows_version/make_release
Get-FileHash packaging failures. Live slot 0 and preserved original are
byte-identical. No ROM, save or generated game data is tracked.

### 2026-10-27 - Thrown player cannot use the next hook
Preserved live slot 0 before inspection; SHA-256
f7ca235a65ee7c00c2234a4da43dd24f4f7f4b8a81244e5e8337bc3951d052d0.
P1 hangs on hook $1016 while P2 hangs on $0FB8 with mask $0016. The previous
direct capture fix skipped grounded throw recovery, leaving P2 without
interaction bit $0008 on its next jump. Restore $0018 for live non-lost
hanging actors in state dispatch, including older snapshots. Also let a
live hanging leader yield the busy-state veto for a different hook; this
was previously restricted to a leader occupying the target hook. Preserve
other busy-state gates and original contact, framing, arcs and cooldowns.
Synthetic tests exercise either fixed player, saved mask restoration,
different-hook ownership, stationary occupant, unrelated busy state and
classic fallback. New check_coop_next_hooks.py uses only controller input
from the original copied checkpoint: P2 shares P1's next hook; either actor
then reaches the third while the teammate stays put, and the teammate can
join in either order. Exact mid-jump reload is covered. No private data
is tracked; every hook layout and full level completion remain unverified.
Existing sideways throw/shared framed-hook, ordinary hook and Kleever
acceptances pass, including both grab/release orders. Release unit,
headless, Windows desktop and SDL builds pass. Hidden desktop replay
completes the 260-frame next/third-hook route. Baseline and final full
CTest: 56/58, with the same create_windows_version/make_release
Get-FileHash packaging failures. git diff --check passes. Live slot 0 and
preserved original remain byte-identical; playable builds rebuilt in place.

### 2026-10-27 - Cat grabs the distant teammate
Preserved initial live checkpoint (SHA-256
0f3658c378f850abf845dcce6be72355243a17bc2d35873559030986dc00d455),
which had already deleted the enemy. User supplied a paused pre-contact
save, copied separately with SHA-256
67c11bbd21bd0e69ad9935e5418a81b8aac904c074a016717c423ef569f74787.
Unpaused using P2 Start. Reproduced P2 touching type-$0204 Cat O' Nine
Tails and native $13 forcing distant P1 (over 200 pixels away) to $2F,
then duplicating its spin position. Capture records the accepted collider
and selects its work; skip the original follower recruitment/positioning.
Find the captured owner from native state $48 during spinning so unrelated
active-owner changes do not steal its pose. Preserve original cat physics.
Removing paired movement exposed the native cat-only camera pushing the
free teammate to its edge. Route the cat camera target through its actual
captured Kong and include $48 in midpoint eligibility for valid cats.
No ROM, save or generated assets are tracked. Every enemy/level layout and
concurrent captures by multiple cats remain unverified.
The native cat release also uses common cannon launch code, which would
still hide/recruit the free actor. Skip that follower launch and flag
mutation only for cat sources, and promote the live captured owner before
release work selection. Ordinary shared barrels keep their existing path.
Synthetic tests cover either collider, stale later $6A, reset, wrong
reaction/source type, one-shot ownership, classic fallback, surviving
owner selection, capture camera midpoint and release owner promotion.
New check_coop_cat_grab.py drives the unmodified copied save through P2
Start, capture and native launch: P1 stays exactly at its original idle
position throughout; P1 can jump without releasing P2; mid-spin save/load
reproduces the same release. A controlled copy changes only active/work
selectors to prove an inactive P2 collider owns the grab too. Existing
shared barrel entry/either-port firing/landing and successive-hook checks
pass. No live checkpoint was modified.
Release unit, headless, Windows desktop and SDL builds pass. Hidden desktop
replay completes 156 frames through unpause, capture and launch; actor
assertions use the headless acceptance. Baseline and final CTest: 56/58,
only existing create_windows_version/make_release Get-FileHash packaging
failures. Live save and both preserved originals are unchanged. Playable
executables rebuilt in place; git diff --check passes.

### 2026-10-29 - External preservation and commit evidence
User authorized preservation, fresh validation, evidence documentation and
commit preparation. Copied and SHA-256 verified 585 checkpoint/SRAM files
outside the repository; the inventory includes original checkpoints and
derived diagnostic copies, not 585 independent regression scenarios.
Fresh complete CTest before/after: 56/58, with the same two packaging
Get-FileHash environment failures. Forty-four of 45 targeted private checks
pass using the external copies. The blank-SRAM item-input replay still
fails its P1 pickup setup, as already documented on 2026-10-22; no claim
of passing ownership assertions is made for that route.
Initial broad validation exposed four additional setup/assertion issues:
the normal developer check was given a non-gameplay checkpoint; corrected
to the actual normal-level checkpoint. Water approach stopped five ticks
before swimming while the carrier retained its beetle; now use a bounded
real-water-contact probe followed by the full per-frame ownership replay.
Staggered pole routes need the same run-up as the sole-climber routes;
add that approach while retaining all shared-climb/animation/release checks.
Rattly mount cleanup intentionally leaves the restored animal in native
unmounted idle; test this state, cleared mount references, survivor palette
and controls instead of requiring deletion. These are replay maintenance,
not gameplay changes. Later respawn-loss pole verification also required
its documented shared-pole-death checkpoint rather than the different
regrab checkpoint. Corrected rechecks pass; preserve initial failure logs.
See docs/evidence/coop-validation-2026-10-29.md and its JSON/CSV metadata
for source fingerprint, commands, hashes, outcomes and unverified scope.

### 2026-10-29 - Host-specific packaging module resolution
Resolved both packaging failures: explicitly import bundled Utility/Archive
modules from the executing PowerShell PSHOME rather than relying on the
inherited cross-edition PSModulePath. Synthetic tests verify known executable
hashes and independently calculate ZIP/manifest checksums. Full CTest before:
56/58; after: 58/58. No gameplay changes. Historical failures remain in the
original evidence; see docs/evidence/packaging-validation-2026-10-29.md.

### 2026-10-29 - Blank-SRAM item replay setup repair
P1 reached the enemy during roll recovery; P2's old route released Run and
threw the picked-up chest. Use actor-specific approaches, restart P1's roll,
keep Run held during settling, and assert the held sprite is a native chest.
All original ownership assertions now pass for both fixed slots, including
partner inputs, ground/air snapshot loading and intentional owner release.
No gameplay change. Complete CTest before/after this milestone: 58/58.
The previous 44 private successes are retained; only the missing item case
was rerun. See docs/evidence/item-input-validation-2026-10-29.md.
