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
