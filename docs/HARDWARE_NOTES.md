# Hardware notes

## DKC2 cartridge map

The supported cartridge is a 4 MiB FastROM HiROM image with 2 KiB battery-backed
SRAM. Its internal header reports map mode `$31` and SRAM size code `$01`.

The baseline map follows the standard SNES system areas and the known
SHVC-1J1M board map used by North American DKC2 cartridges:

| SNES address | Runtime destination |
| --- | --- |
| `$7E-$7F:0000-FFFF` | 128 KiB WRAM |
| `$00-$3F,$80-$BF:0000-1FFF` | first 8 KiB WRAM mirror |
| `$00-$3F,$80-$BF:2000-5FFF` | I/O callback or open bus |
| `$20-$3F,$A0-$BF:6000-7FFF` | 2 KiB SRAM mirrors |
| `$00-$3F,$80-$BF:8000-FFFF` | HiROM upper-half mirrors |
| `$40-$7D,$C0-$FF:0000-FFFF` | full-bank HiROM windows |

References:

- [SNESdev memory map](https://snes.nesdev.org/wiki/Memory_map)
- [SHVC-1J1M-20 board map](https://snescentral.com/pcbboards.php?chip=SHVC-1J1M-20)
- [fullsnes hardware specification](https://problemkaputt.de/fullsnes.htm)

## Open bus

An address with no responding device does not simply read as zero on a SNES.
The baseline runtime retains the most recent A-bus data byte and returns it for
unmapped or unhandled I/O reads. DKC2 is known to depend on open-bus behavior,
so this is required functionality, not an optional accuracy tweak.

The final model must be more detailed. PPU1, PPU2, and CPU-side registers can
have distinct latches or mixed driven/open bits, and DMA affects which values
remain on the bus. Those rules will live behind the I/O callbacks and will be
validated with reference traces.

## Current general-DMA model

`dkc2_snes_io` implements synchronous A-bus-to-B-bus general DMA for channels
0 through 7. It supports all eight B-bus transfer patterns, fixed/incrementing/
decrementing A-bus addresses, a transfer size of zero meaning 65,536 bytes, and
register write-back after completion.

The model currently rejects B-bus-to-A-bus transfers explicitly. It also does
not consume CPU cycles, interleave refresh, arbitrate HDMA, or expose bus pins.
Those are timing-layer responsibilities.

The first real-ROM checkpoint configures channel 0 as mode 1 with a fixed ROM
source and writes alternating bytes to `$2118/$2119`. The runtime transfers
65,536 zero bytes and confirms the complete 64 KiB VRAM array is clear.

## Current PPU memory ports

The bring-up model stores PPU register writes and implements:

- `$2115-$2119` VRAM increment, remap, address, and data behavior;
- the shared background horizontal/vertical scroll write latch used by
  `$210D-$2114`, while retaining the Mode-7 H/V offsets;
- the shared low/high write latch for Mode-7 matrix/center registers
  `$211B-$2120`;
- signed `M7A * high_byte(M7B)` output through `$2134-$2136`;
- `$2121-$2122` CGRAM address and paired data writes; and
- `$2102-$2104` OAM word addressing, paired low-table writes, high-table
  mapping, and write-address progression.

PPU read buffers, VRAM/OAM/CGRAM access restrictions, and dot-level behavior
remain to be implemented and checked against traces.

## Current headless PPU renderer

The opt-in renderer turns the stored PPU state into a 512x224 RGB frame. It
implements tiled modes 0, 1, 3, and 5; Mode-7 BG1 and EXTBG with signed affine
coordinates, screen flips, repeat modes, and interleaved VRAM; 2/4/8-bpp
planar tiles; 8x8 and 16x16 background tiles; map/tile flips and priorities;
sprites in every OBSEL size pair; priority rotation; the 32-object/34-sliver
scanline limits; and main/subscreen fixed-color addition/subtraction.
Low-resolution pixels are doubled; Mode 5 retains separate 512-wide pixels.

A visible scanline is captured when the scheduler reaches HBlank, before that
line's HDMA changes register state. A complete frame is published after line
224. Each frame records which modes it used and any unsupported features it
encountered. Global counters separately retain limitations from earlier
frames.

Modes 2, 4, and 6, windows, direct color, mosaic, pseudo-hires, and interlace
are explicitly marked unsupported. This keeps a recognizable image from being
mistaken for a complete PPU implementation. See
`PPU_RENDERING.md` for the detailed priority, hashing, export, and verification
contract.

## WRAM data port and CPU arithmetic

`$2181-$2183` select a 17-bit WRAM address. Reads or writes at `$2180` transfer
one byte and increment that address modulo 128 KiB. The unused `$2184-$21FF`
B-bus range follows the current main open-bus model. This matters because
DKC2 uses a 16-bit store that writes the high byte immediately after `$2183`.

Writing `$4203` captures two unsigned 8-bit operands; `$4216/$4217` receive
the product after 48 master cycles. Writing `$4206` captures a 16-bit dividend
and 8-bit divisor; `$4214/$4215` receive the quotient and `$4216/$4217` the
remainder after 96 master cycles. Division by zero produces quotient `$FFFF`
and returns the dividend as the remainder. The delays run on the shared master
timeline, so their real-console alignment is limited by the provisional CPU
clock adapter.

## Current APU model

The default boot probe retains the version-0.3 barrier at the SPC700 IPL-ready
comparison at `$B5:821A`. This makes the older 74,262-instruction checkpoint
directly reproducible.

With `--with-apu`, `$2140-$2143` connect to four separate one-way values in
each direction. The imported SPC700 executes the real IPL, publishes `$AA/$BB`,
accepts `$CC`, echoes transfer counters, writes ARAM, and starts the uploaded
program. A synthetic test independently transfers two bytes to `$0200/$0201`.

DKC2's 16-bit store of `$01CC` writes CPU port 0 and port 1 on consecutive
65816 bus accesses. Advancing the APU by 64 cycles between those writes made
the IPL observe the old zero in port 1 and jump instead of entering transfer
mode. The temporary scheduler therefore advances one complete SPC opcode per
APUIO access, the smallest unit exposed by the core. This is correct enough for
the deterministic bootstrap but is not cycle-accurate.

The optional probe leaves the audio loader, performs later DMA/decompression,
and stops at a `$4211` TIMEUP read. Its private 64 KiB ARAM SHA-256 is
`49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f`.
The hash must still be compared with an accurate emulator at the same point.

## Current CPU timing boundary

The opt-in timing path now models `$4200`, `$4207-$420A`, and `$4210-$4212`,
including clear-on-read NMI/TIMEUP latches, H/V timer comparisons, interrupt
delivery, scanline progression, and `WAI` wake-up. It uses 262 scanlines of
1,364 master cycles, HBlank at 1,096, and non-overscan VBlank at line 225.

The hardware timeline consumes master cycles, but the current CPU adapter
estimates eight master cycles per visible A-bus byte access. Internal cycles,
dummy accesses, and address-dependent bus speeds are missing. See
`TIMING_AND_INTERRUPTS.md`; reported frame and beam positions are provisional.

## Current HDMA and controller model

HDMA initializes enabled channels at frame start and runs at visible HBlank.
All transfer patterns, direct/indirect tables, repeat/write-once line counts,
and register write-back are implemented. Transfers currently occur as atomic
HBlank events without consuming their bus duration.

Two controllers expose manual `$4016/$4017` serial reads. Autojoy begins at
VBlank when enabled, reports busy for 4,224 master cycles, and publishes
results at `$4218-$421B`; controllers 3 and 4 are zero. The private probe uses
neutral input by default. `--controller1=<mask>` and `--controller2=<mask>`
allow deterministic held-button probes; masks use the standard 16-bit autojoy
layout (`$1000` is Start).

## Current long-run boundary

The former `$2135` boundary is implemented and covered by signed-product and
shared-latch tests. Subsequent real-ROM probes exposed and then crossed CPU
math reads at `$4216` and WRAM-port traffic at `$2181/$2184`. The neutral-input
integration test now runs to its 20,000,000-instruction limit with no explicit
hardware barrier. That is strong deterministic progress, but it does not prove
that the emulated state matches a real console or accurate emulator; exact
reference comparison is still required. The optional renderer now runs for the
same full probe and publishes deterministic frames. One Mode-7 Rareware-logo
frame is an exact RGB match to a Snes9x 1.63 capture, and its VRAM, CGRAM, and
OAM match an adjacent save state. Incomplete mode coverage, non-beam-aligned
registers, and provisional timing still prevent a console-accuracy claim.
