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
- `$2121-$2122` CGRAM address and paired data writes; and
- basic `$2102-$2104` OAM address/data writes.

This is storage behavior, not a renderer. OAM's internal write latch, PPU read
buffers, beam timing, access restrictions, and pixel generation remain to be
implemented and checked against traces.

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

`$4211` is not yet modeled. The next timing layer must implement clear-on-read
TIMEUP behavior, `$4200` NMI/IRQ enable bits, H/V timer comparators, interrupt
delivery, scanline progression, and HDMA scheduling. Returning a guessed zero
would hide that missing behavior, so the probe stops explicitly.
