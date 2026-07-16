# Architecture

## Distribution boundary

The public source tree contains original tools, runtime code, tests, and
documentation. It never contains the game ROM, extracted game content, or
generated copyrighted data. Private products live beneath ignored `build`,
`private`, or `generated` directories.

## Target execution model

The intended first playable implementation combines translation with a tested
fallback:

1. Verify the user's ROM locally.
2. Decode reachable W65C816 code and build control-flow graphs.
3. Execute uncertain paths in the portable interpreter.
4. Emit native C for proven direct control flow and known calls.
5. Link both to a portable SNES hardware layer.
6. Replace translated routines with readable C subsystem by subsystem.

The interpreter is now functional and is the behavioral bootstrap for the
translator. It is not the final performance strategy.

## Static analysis layer

`dkc2_analyze` tracks processor mode and M/X operand widths, follows direct
branches and jumps, and can record or traverse direct calls. It recognizes
DKC2's `PEA`/`RTS` startup trampoline using a small abstract return-word stack.
Indirect jumps and unknown stack effects end a path rather than inviting a
guess.

An external WLA symbol map may be overlaid and a Graphviz DOT graph exported.
Neither is needed at runtime or included in releases.

## CPU execution layer

`dkc2_cpu_step` executes one complete logical W65C816 instruction against
generic 24-bit read/write callbacks. The register file includes A, X, Y, S, D,
PC, DBR, PBR, P, E, wait/stop state, and an instruction counter. Reset, NMI,
and IRQ entry points use the same callback boundary.

Every opcode and addressing mode is implemented, including decimal arithmetic,
native/emulation transitions, interrupt frames, long pointers, stack
exceptions, and complete block moves. The core passed 5,080,000 external
instruction-state comparisons. It is not cycle accurate: one step does not
expose dummy accesses or internal block-move iterations.

## Address-space layer

`dkc2_hirom_snes_to_rom` recognizes ROM windows only:

- `$40-$7D:0000-FFFF`
- `$C0-$FF:0000-FFFF`
- `$00-$3F:8000-FFFF`
- `$80-$BF:8000-FFFF`

`dkc2_bus` separately routes full WRAM, its low mirrors, DKC2's 2 KiB SRAM
mirrors, I/O callbacks, ROM, and unmapped/open-bus accesses. This keeps mapper
logic from silently treating hardware registers as ROM.

## Bring-up hardware layer

`dkc2_snes_io` currently provides:

- CPU and DMA register storage;
- PPU register storage;
- VRAM address/remap/increment and low/high data ports;
- CGRAM data ports and the OAM word-address/write-latch behavior used by the
  renderer;
- shared background scroll-offset latching and PPU-mode telemetry;
- the `$2180-$2183` 17-bit WRAM address and auto-incrementing data port;
- the shared `$211B-$2120` Mode-7 write latch and signed multiply result;
- delayed CPU multiplication/division and `$4214-$4217` results;
- all eight general-DMA B-bus offset patterns;
- fixed, incrementing, and decrementing A-bus sources;
- the four CPU/APU communication ports;
- an opt-in master-cycle event timeline with NTSC H/V counters;
- NMI/TIMEUP status, interrupt latches, and `WAI` wake-up support;
- direct and indirect HDMA for all eight transfer patterns;
- two serial controllers and timed automatic polling; and
- explicit barriers for unsupported I/O and B-to-A DMA.

This is enough to execute DKC2's reset initialization and exact 65,536-byte
fixed-source VRAM clear. The default probe can still stop at the first SPC700
IPL handshake for regression compatibility; `--with-apu` continues with the
executing APU. Richer PPU reads, access restrictions, several display modes,
and exact CPU/bus cycles remain future components.

## Headless PPU rendering layer

`dkc2_ppu_renderer` is an optional observer of `dkc2_snes_io`. At visible
HBlank it snapshots the just-completed scanline before that line's HDMA
updates. At the end of the visible region it publishes a complete 512x224 RGB
frame and a deterministic SHA-256 fingerprint.

The renderer implements tiled backgrounds for modes 0, 1, 3, and 5; Mode-7
BG1 and EXTBG affine sampling; 2/4/8-bpp planar tiles; map, tile, and Mode-7
screen flips; layer and tile priority; low- and high-resolution output; all
object-size pairs; object priority rotation and scanline range/time limits;
and main/subscreen color math. Unsupported state is recorded in a feature mask
instead of silently claiming a fully supported frame. The current unsupported
set includes modes 2/4/6, windows, direct color, mosaic, pseudo-hires, and
interlace.

Rendering is opt-in so the existing CPU, APU, and timing checkpoints retain
their cost and behavior. `--frame-output=<private.ppm>` is also opt-in and
writes only to the caller's path; no ROM-derived image belongs in source
control. See `docs/PPU_RENDERING.md` for supported state and validation rules.

## Timing and event layer

`dkc2_snes_io_advance_master_cycles` is the common clock input for beam
progression, NMI/IRQ latches, HDMA, autojoy, delayed CPU math, and the APU. The
current boot adapter counts all host-visible A-bus byte accesses and assigns
eight master cycles to each. That adapter is intentionally replaceable: when
the CPU core later reports exact cycles, the hardware event API does not need
to change.

The compatibility modes are layered. The default stops at the original APU
barrier, `--with-apu` retains the port-access scheduler and `$4211` checkpoint,
`--with-timing` selects the event path, and `--with-render` adds scanline
capture and framebuffer publication to it. See
`docs/TIMING_AND_INTERRUPTS.md` for the complete contract and limitations.

## APU execution layer

`dkc2_apu` wraps the MIT-licensed LakeSnes SPC700/S-DSP subset. The wrapper
owns reset/execution, CPU-side port access, ARAM inspection, and cycle counts;
LakeSnes types do not escape the wrapper API. S-SMP registers, IPL ROM, timers,
DSP registers, BRR decoding, and sample generation are present.

The compatibility scheduler advances one complete SPC opcode per 65816 APUIO
access in `--with-apu` mode. The timed continuation instead derives SPC cycles
from the master timeline at a nominal 21:1 ratio and carries whole-instruction
overshoot as debt. CPU-to-master timing is still provisional, so audio timing
and race behavior cannot yet be called accurate.

## Current boot sequence

With zeroed host WRAM/SRAM and the NTSC status bit selected, `dkc2_boot`:

1. enters the ROM reset vector;
2. completes DKC2's RAM, SRAM, region, and startup checks;
3. initializes PPU, CPU, and DMA registers;
4. executes channel-0 DMA and verifies all 64 KiB of VRAM are zero;
5. clears both WRAM banks through two logical MVN instructions; and
6. reaches the SPC700 IPL-ready comparison at `$B5:821A`.

The measured checkpoint is 74,262 interpreted instructions, one 65,536-byte
DMA, and an explicit APU barrier. This is a deterministic bring-up regression,
not a console timing trace.

With `--with-apu`, the same probe executes both CPUs through the IPL transfer,
later DMA, and decompression work. It reaches an unsupported `$4211` TIMEUP
read after 1,359,156 65816 instructions. The accompanying deterministic ARAM
hash is `49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f`.
This value is a local regression checkpoint until compared with an accurate
emulator dump.

With `--with-timing`, the runner passes `$4211`, reaches `WAI`, delivers
repeated VBlank NMI, and performs general DMA, HDMA, controller polling,
Mode-7 multiplication, CPU math, and WRAM-port traffic. The current regression
runs 20,000,000 instructions without an unsupported-hardware barrier, with
5,619 general-DMA transfers and 4,005 HDMA line transfers. It prints SHA-256
fingerprints for WRAM, SRAM, VRAM, CGRAM, OAM, and ARAM. Its reported
frame/beam position remains tied to the provisional
eight-master-cycles-per-access adapter and is not a hardware timing oracle.

With `--with-render`, the same real-ROM execution also proves that the
renderer can consume changing PPU state for thousands of frames without
creating a new execution barrier. The 1,700,000-instruction private regression
pins a Mode-7 frame, while the 2,000,000-instruction regression preserves the
later modes-1/5 hash. After low-resolution normalization, the Mode-7 image is
an exact RGB match to an official Snes9x 1.63 screenshot. VRAM, CGRAM, and OAM
also match an adjacent private Snes9x state byte for byte. Beam-aligned display
registers and provisional timing still need event-aligned comparison, and the
executable is not a playable desktop build.

## Verification strategy

Each milestone should combine synthetic unit tests, CPU state conformance, and
private real-ROM integration. Future differential checkpoints will compare:

- CPU registers and mode flags;
- WRAM and SRAM;
- VRAM, CGRAM, and OAM hashes;
- DMA/HDMA channel state;
- APU communication and ARAM/DSP state;
- rendered frame hashes; and
- deterministic input playback.

The user's ROM running in an accurate emulator remains the behavioral oracle;
it is never distributed with the project.
