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
