# Changelog

## 0.4.0 - 2026-07-15

- Imported the MIT-licensed LakeSnes SPC700/S-DSP subset at a pinned commit,
  with its license, provenance, and local adaptation record.
- Added a project-owned APU API for reset, execution, CPU/APU ports, ARAM
  inspection, and cycle accounting.
- Added a synthetic IPL regression that observes `$AA/$BB`, performs the
  `$CC` transfer handshake, uploads two bytes, and verifies their ARAM values.
- Connected `$2140-$2143` to the executing APU while retaining the version-0.3
  stop barrier as the default diagnostic mode.
- Added `dkc2_boot --with-apu`, which advances the exact ROM through its audio
  upload and stops at the next unsupported register, `$4211`.
- Recorded the private ARAM SHA-256 checkpoint
  `49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f`.
- Added repository guidance and a research/reuse decision log.

## 0.3.0 — 2026-07-14

- Added a complete instruction-state W65C816 interpreter for all 256 opcodes.
- Added deterministic reset, NMI, IRQ, stack, decimal, width, and block-move
  behavior with focused boundary regressions.
- Added an external conformance runner and passed 5,080,000 comparable
  SingleStepTests state vectors with zero failures.
- Added PPU register storage and basic VRAM, CGRAM, and OAM data-port behavior.
- Added synchronous A-bus-to-B-bus general DMA for all eight transfer modes,
  including DKC2's fixed-source 64 KiB VRAM clear.
- Added `dkc2_boot`, which runs the exact private ROM from reset through VRAM
  initialization and stops explicitly at SPC700/APU communication.
- Added a separate implementation journal, CPU conformance guide, DMA tests,
  and a private real-ROM boot integration test.

## 0.2.0 — 2026-07-14

- Added a safe, read-only verified ROM image API.
- Described and decoded all 256 W65C816 opcodes with M/X width tracking.
- Added reset, native NMI, and native IRQ control-flow analysis.
- Added direct-call traversal and DKC2 startup stack-trampoline recognition.
- Added optional Asar WLA symbol-map overlays and Graphviz DOT export.
- Validated the supported revision against a byte-identical private rebuild.
- Added the first runtime bus: WRAM, SRAM, ROM, I/O hooks, save-data helpers,
  and a main open-bus latch.
- Expanded unit and private integration coverage.

## 0.1.0 — 2026-07-14

- Added exact USA v1.0 ROM verification and copier-header detection.
- Added internal-header/vector parsing, CRC32, SHA-256, and HiROM mapping.
- Added portable CMake and Make builds with synthetic unit tests.
