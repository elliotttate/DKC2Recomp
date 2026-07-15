# Research sources and reuse decisions

This file records what each external source contributes, whether code may be
reused, and how it fits the port. It is an engineering record, not legal
advice.

## DKC2-disassembly

[H4v0c21/DKC2-disassembly](https://github.com/H4v0c21/DKC2-disassembly) is
the strongest revision-specific map. Its North American revision-0 build at
commit `59a3a8aef88d074f488d25d0d0623fcb37fa3791` was privately assembled and
matched the supported ROM byte for byte. It identifies routines, data
structures, compression streams, the SPC700 program, and hardware accesses.

No explicit license was present at the validated revision. The project uses it
only as a private address/behavior reference and copies none of its source,
comments, labels, or game data into distributable code.

## Donkey Kong hacking development documents

The [DKC2 development documents](https://donkeyhacks.zouri.jp/html/En-Us/dkc2/index.html)
cover sprite variables, WRAM, SRAM, ARAM, animation commands, Rareware music
sequence data, compression, and selected disassembly. These are valuable for
giving names and formats to behavior found in traces.

The site prohibits unauthorized reproduction. Facts may guide independent
implementations, but its prose, tables, and code are not copied into this
repository.

## dkcomp

[Kingizor/dkcomp](https://github.com/Kingizor/dkcomp) is an MIT-licensed C
library and command-line tool. Its `Big Data` format covers DKC2/DKC3 SNES
tilesets, tilemaps, and metatiles. It is a good candidate for a future private
asset-validation tool or for a properly attributed runtime decompressor.

It is not required for the current reset/APU milestone. Before integrating it,
add its license and a synthetic round-trip test; never commit its ROM-derived
outputs.

## snesrecomp

[mstan/snesrecomp](https://github.com/mstan/snesrecomp) demonstrates the same
high-level model selected here: translate reachable 65816 code to C, retain an
emulated SNES hardware layer, and compare against a reference emulator. Its
current public README describes an alpha framework with game-specific runner
repositories and several games at varying playability.

The repository also states that it has no declared overall license and no
stable public API. It is therefore an architecture and testing reference only;
its code is not copied or linked here.

## LakeSnes APU core

[angelo-wf/LakeSnes](https://github.com/angelo-wf/LakeSnes) is an MIT-licensed
SNES emulator written in C. Version 0.4.0 imports only its SPC700, S-DSP,
S-SMP timer/port, and save-state support from commit
`9db90b86e46a377609305e298dd92d71cd1d4c8a`.

The subset, license, provenance, and local changes live in
`third_party/lakesnes_apu`. No LakeSnes 65816, PPU, cartridge, input, SDL, or
frontend code is included.

## Snes9x hardware behavior reference

The official [Snes9x source repository](https://github.com/snes9xgit/snes9x)
was consulted to cross-check the Mode-7 shared write latch, the signed
`M7A * high_byte(M7B)` product exposed at `$2134-$2136`, and the delayed CPU
arithmetic register behavior. It is a behavior reference only: no Snes9x PPU,
CPU, platform, or frontend source is copied or linked into this project.

The project-owned implementation is small and independently expressed, and
its externally observable behavior is retained in synthetic tests. Hardware
details must ultimately be confirmed by differential traces, because agreement
with one emulator is not by itself proof of console accuracy.

## Hardware and conformance references

- [SNESdev: Booting the SPC700](https://snes.nesdev.org/wiki/Booting_the_SPC700)
  documents the `$AA/$BB`, `$CC`, byte-echo, and execute phases used by the
  synthetic IPL test.
- [SNESdev: S-SMP](https://snes.nesdev.org/wiki/S-SMP) documents the four
  independent CPU/APU port directions, 64 KiB ARAM, timers, DSP registers, and
  IPL mapping.
- The WDC W65C816S data sheet and Tom Harte/SingleStepTests corpus remain the
  CPU semantics and instruction-state conformance references.

## Architecture decision

Continue the existing interpreter-first foundation and add static C emission
incrementally. Do not restart on another framework. The current code already
has a revision-verified ROM loader, complete 65816 semantics, deterministic
boot oracle, memory bus, DMA, and now an APU core. Replacing those pieces would
discard verified evidence without solving PPU/timing or game-logic translation.
