# DKC2 Native Port

This repository is a clean, source-only foundation for a native PC port of the
SNES release of *Donkey Kong Country 2: Diddy's Kong Quest*.

Version 0.4 provides:

- exact identification of the supported USA v1.0 ROM, including copier-header
  detection, CRC32, SHA-256, internal metadata, and vectors;
- tested 4 MiB HiROM mapping plus WRAM, SRAM, ROM, I/O, and open-bus routing;
- descriptions and mode-aware decoding for all 256 W65C816 opcodes;
- a portable interpreter for all 256 opcodes, including native/emulation
  modes, 8/16-bit widths, decimal arithmetic, interrupts, block moves, and
  documented boundary behavior;
- 5,080,000 passing external instruction-state comparisons across native and
  emulation modes;
- conservative static control-flow analysis, optional private labels, and
  Graphviz DOT export;
- a bring-up SNES I/O model with PPU register storage, VRAM/CGRAM/OAM memory
  ports, and all eight A-bus-to-B-bus general-DMA transfer patterns;
- an executing SPC700/S-DSP core, S-SMP timers, the four bidirectional CPU/APU
  ports, and a synthetic IPL upload regression; and
- a real-ROM boot probe that executes reset code, completes DKC2's 64 KiB VRAM
  clear, optionally completes the audio upload path, and stops explicitly at
  the next missing `$4211` IRQ/timing behavior.

This is meaningful executable progress, but it is not yet a playable port. The
CPU core is instruction-state accurate rather than cycle accurate. APU
scheduling is still approximate, and PPU rendering, host audio, HDMA, NMI/IRQ
timing, controllers, native-C emission, and a desktop host are still required.

## ROM policy

You must supply your own lawfully obtained ROM. Do not commit, upload, or
redistribute ROMs, extracted graphics, music, level data, or save files.

The supported baseline is a headerless North American v1.0 image:

| Property | Expected value |
| --- | --- |
| Size | `4,194,304` bytes |
| CRC32 | `006364DB` |
| SHA-256 | `35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633` |
| Internal name | `DIDDY'S KONG QUEST` |
| Map mode | `31` — HiROM/FastROM |

The filename and `.smc`/`.sfc` extension do not matter.

## Build on Windows

Install a C compiler and CMake, then run from PowerShell:

```powershell
cmake -S . -B build -DDKC2_ROM="C:\private\dkc2.smc"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

`scripts/test.ps1 -Rom "C:\private\dkc2.smc"` performs the same sequence and
also finds the CMake bundled with Visual Studio 2022 Community when `cmake` is
not on `PATH`.

The ROM path is stored only in the private CMake build directory, which Git
ignores. Useful commands are:

```powershell
.\build\Release\dkc2_verify.exe "C:\private\dkc2.smc"
.\build\Release\dkc2_analyze.exe "C:\private\dkc2.smc" 100000 --follow-calls
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc"
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 5000000 --with-apu
```

## Build with Make

```sh
make test
make verify-rom ROM="/private/path/dkc2.smc"
make boot-rom ROM="/private/path/dkc2.smc"
```

Successful ROM verification ends with:

```text
Result:        supported DKC2 USA v1.0 baseline
```

The default boot probe preserves the version-0.3 deliberate boundary:

```text
DMA:           1 transfer(s), 65536 bytes
VRAM clear:    confirmed
Outcome:       APU/SPC700 communication required
Trigger:       $002140 (value $00) from $B5821A
```

The exact instruction and I/O counts are regression evidence, not a timing
claim. General DMA runs synchronously inside one interpreted CPU instruction.
The optional APU continuation reaches this next boundary:

```text
Instructions:  1359156
DMA:           13 transfer(s), 157448 bytes
VRAM clear:    confirmed
APU cycles:    960481 (port-access scheduler)
ARAM SHA-256:  49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f
Outcome:       unsupported I/O read
Trigger:       $804211 (value $00) from $809360
Checkpoint:    APU upload path complete; IRQ/timing model required
```

The ARAM hash is a deterministic private regression value, not yet a claim of
cycle accuracy; it still needs comparison against an accurate reference dump.

## CPU conformance

The project includes a standard-library Python runner for the external
SingleStepTests 65816 vectors. The multi-gigabyte corpus and compiled shared
library remain outside this repository and are never required by end users.

The clean checkpoint is 5,080,000 passing comparisons: every vector for 254
opcodes in both modes. MVP and MVN are excluded because those two corpus files
stop after 100 bus cycles in the middle of a logical block-move instruction,
while this interpreter completes the logical instruction in one step.

See [docs/CPU_CONFORMANCE.md](docs/CPU_CONFORMANCE.md) for the exact command,
scope, and limitations.

## Analyze control flow

The analyzer defaults to the reset vector and 128 decoded instructions:

```sh
./build/dkc2_analyze "/private/path/dkc2.smc"
```

A wider pass can follow direct calls and write a machine-readable Graphviz
graph. The graph contains addresses, width states, labels when supplied, and
typed edges; it contains no ROM bytes or game assets.

```sh
./build/dkc2_analyze "/private/path/dkc2.smc" 100000 \
    --follow-calls \
    --dot generated/reset.dot
```

Native interrupt entry points and private symbol overlays are also supported:

```sh
./build/dkc2_analyze "/private/path/dkc2.smc" --entry nmi
./build/dkc2_analyze "/private/path/dkc2.smc" --entry irq
./build/dkc2_analyze "/private/path/dkc2.smc" 100000 \
    --follow-calls \
    --symbols "/private/path/dkc2-v0.sym"
```

See [docs/REFERENCE_WORKFLOW.md](docs/REFERENCE_WORKFLOW.md) for the private
revision-validation and symbol-generation workflow.

## Project direction

The intended first playable version translates 65816 game logic to native C
while retaining a compact SNES hardware layer for PPU, APU/DSP, DMA/HDMA,
controllers, SRAM, timing, and open-bus behavior. The interpreter is the
correctness fallback and bootstrapping oracle for that translator. Generated
routines can then be replaced incrementally with readable C implementations.

The ongoing plain-language record is
[docs/IMPLEMENTATION_JOURNAL.md](docs/IMPLEMENTATION_JOURNAL.md). The technical
design and task order live in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and
[docs/ROADMAP.md](docs/ROADMAP.md). External-source evaluations and reuse
decisions live in [docs/RESEARCH_NOTES.md](docs/RESEARCH_NOTES.md).

## Source and dependency boundary

Project-owned code is MIT-licensed. The SPC700/S-DSP subset under
`third_party/lakesnes_apu` retains LakeSnes's separate MIT notice, exact source
revision, and adaptation record. Unlicensed disassemblies remain private
research references and are not copied. No source license grants rights to
Nintendo or Rare content.
