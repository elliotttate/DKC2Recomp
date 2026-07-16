# DKC2 Native Port

This repository is a clean, source-only foundation for a native PC port of the
SNES release of *Donkey Kong Country 2: Diddy's Kong Quest*.

Version 0.8 provides:

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
  ports, and a synthetic IPL upload regression;
- an opt-in master-cycle timeline with NMI/IRQ status, scanline events, HDMA,
  serial/automatic controller input, and timed SPC700 execution;
- Mode-7 matrix write latching and signed multiplication output, delayed CPU
  multiplication/division, and the `$2180-$2183` WRAM data/address ports;
- a deterministic real-ROM timing probe that crosses the former `$2135`,
  `$4216`, and `$2181/$2184` boundaries and runs for 20,000,000 instructions
  while fingerprinting every major writable memory region;
- an opt-in, headless 512x224 RGB renderer for modes 0, 1, 3, 5, and 7,
  including Mode-7 affine transforms and EXTBG, sprites, priority, color math,
  deterministic frame hashes, and private PPM export; and
- an exact 256x224 RGB match between the private Mode-7 Rareware-logo frame
  and an official Snes9x 1.63 capture, plus byte-exact VRAM, CGRAM, and OAM
  matches against an adjacent private save state and reusable comparison tools.

This is meaningful executable progress, but it is not yet a playable port. The
CPU core is instruction-state accurate rather than cycle accurate. The new
CPU-to-master-clock adapter is deliberately provisional. Several PPU features
remain unsupported, and host audio, native-C emission, input integration, and
a desktop host are still required.

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
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 20000000 --with-timing
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 20000000 --with-timing --controller1=0x1000
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 2000000 --with-render
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 1700000 --frame-output="build\private-mode7.ppm"
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 2000000 --frame-output="build\private-frame.ppm"
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

The timed continuation preserves that checkpoint and now advances without an
unsupported-hardware barrier through a 20-million-instruction regression:

```text
Instructions:  20000000
DMA:           5619 transfer(s), 3111598 bytes
HDMA:          4005 line transfer(s), 4005 bytes
Timing:        1588559648 provisional master cycles, frame 4445 beam 43:236
WRAM SHA-256:  e10559dffe4381d912c93d3c7548dd0056a90e490dd8b204020029ba7db4db2c
VRAM SHA-256:  fa7fa5b8d66b584757bbe01fa5e35906263791318c356e4080a91b2730274cdc
APU cycles:    75645698 (provisional master scheduler)
ARAM SHA-256:  c9e3dd1d8e7c5b0d5152457f87d543374f8a89d5b4a5c0fc8e06c5ceec4bbeda
Outcome:       instruction limit reached
Checkpoint:    timed hardware path remained barrier-free to requested limit
```

The scheduler consumes real master-cycle units, but the CPU currently supplies
eight master cycles per visible A-bus byte access. The reported frame and beam
are therefore deterministic regression values, not console-accurate timing.
The complete output also fingerprints SRAM, CGRAM, and OAM. Controller masks
use the standard 16-bit SNES autojoy layout; for example, `0x1000` holds Start.
See [docs/TIMING_AND_INTERRUPTS.md](docs/TIMING_AND_INTERRUPTS.md) for the
register behavior, HDMA/controller model, tests, and known limitations.

## Headless rendering checkpoint

`--with-render` enables the timing/APU path and renders completed visible
scanlines into an internal 512x224 RGB framebuffer. Low-resolution pixels are
doubled horizontally; Mode 5 uses the full 512-pixel width. The renderer covers
modes 0, 1, 3, 5, and 7; planar 2/4/8-bpp backgrounds; Mode-7 matrix
transforms, screen flips, repeat behavior, and EXTBG; tile flips and
priorities; all SNES object-size pairs; scanline object limits; main/subscreen
composition; fixed color; and add/subtract color math.

The private 2,000,000-instruction regression publishes a frame using modes 1
and 5 with no declared per-frame limitation and pins this framebuffer hash:

```text
Frame SHA-256: fd62d5bea3f0961e286bd4ae266ff1c09a30be9260da820003dc06b26d307b8d
```

`--frame-output=<path>` writes that frame as a binary PPM for local inspection.
The image is derived from the user's ROM: keep it in an ignored private or
build directory and never commit or redistribute it.

The private 1,700,000-instruction checkpoint publishes a Mode-7 Rareware-logo
frame with hash:

```text
Frame SHA-256: ce5c1873327e39ba4d77c33e101ce9956ee86554c889855b8e3531b330923c2f
```

After collapsing each duplicated low-resolution pixel, all 57,344 pixels and
172,032 RGB channels match an official Snes9x 1.63 screenshot exactly. The
normalized image hash on both sides is
`57b5636a6eee0295ff395771453092d8560de5e643208e2fb69cecae190d627f`.
An adjacent Snes9x state also matches all three display memories byte for byte:

```text
VRAM SHA-256:  011580629bf3007e8acd599b872173a08a6156c4f896ef9e6fbf35023e99cb7e
CGRAM SHA-256: bb867c40f4978157de0e761f13d2ed05fc4f697f8c23597ea110b3a26a01df2e
OAM SHA-256:   44ddd2f478477ebd1c1cd5b99400af48cd46033c59173195f48870e608cec810
```

You can reproduce both comparisons without third-party Python packages:

```powershell
python scripts\compare_frames.py build\private-mode7.ppm `
    "C:\private\snes9x-mode7.png"
python scripts\inspect_snes9x_snapshot.py "C:\private\snes9x-mode7.009" `
    --expect-vram 011580629bf3007e8acd599b872173a08a6156c4f896ef9e6fbf35023e99cb7e `
    --expect-cgram bb867c40f4978157de0e761f13d2ed05fc4f697f8c23597ea110b3a26a01df2e `
    --expect-oam 44ddd2f478477ebd1c1cd5b99400af48cd46033c59173195f48870e608cec810
```

This validates those pixels, not the provisional CPU timing or the complete
PPU. Beam-aligned display-register state, modes 2/4/6, windows, mosaic, direct
color, pseudo-hires, and interlace remain outstanding. See
[docs/PPU_RENDERING.md](docs/PPU_RENDERING.md) for the exact contract and
capture workflow.

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
