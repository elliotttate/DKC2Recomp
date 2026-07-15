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
- basic CGRAM and OAM data ports;
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
executing APU. Mode-7 multiplication results, CPU multiplication/division
registers, PPU rendering, accurate OAM behavior, and exact CPU/bus cycles
remain future components.

## Timing and event layer

`dkc2_snes_io_advance_master_cycles` is the common clock input for beam
progression, NMI/IRQ latches, HDMA, autojoy, and the APU. The current boot
adapter counts all host-visible A-bus byte accesses and assigns eight master
cycles to each. That adapter is intentionally replaceable: when the CPU core
later reports exact cycles, the hardware event API does not need to change.

The compatibility modes are layered. The default stops at the original APU
barrier, `--with-apu` retains the port-access scheduler and `$4211` checkpoint,
and `--with-timing` selects the new event path. See
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
repeated VBlank NMI, performs general DMA and intro HDMA, and stops at the
unsupported `$2135` Mode-7 multiplication-result read. The checkpoint is
1,619,491 instructions, 133 general-DMA transfers, and 1,071 HDMA line
transfers. Its reported frame/beam position is tied to the provisional
eight-master-cycles-per-access adapter and is not a hardware timing oracle.

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
