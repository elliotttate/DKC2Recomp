# Changelog

## Unreleased

- Pinned `mstan/snesrecomp` at the fetched 2026-07-16 `main` tip and added
  source-only HiROM analysis, mapper-aware runtime routing, and private DKC2
  generation/host integration.
- Corrected the DKC2 non-returning NMI continuation contract, producing
  advancing intro state, nonzero CGRAM, and rendered native frames.
- Fixed compiler-dependent `BRA`/`BRL` relative addressing in the shared 65816
  interpreter and added focused CPU regressions. The old `BRL` behavior froze
  DKC2 at host frame 3,048 under MSVC.
- Completed a 12,000-frame neutral-input native soak and added aggregate video
  and audio activity telemetry plus opt-in private PPM capture.
- Fixed the DKC2 frame adapter's missing VBlank OAM-port reload, which had
  rotated each otherwise-correct 544-byte sprite DMA through stale OAM.
- Added OAM-source integrity checks and a private aligned-frame regression whose
  VRAM, CGRAM, OAM, and normalized RGB pixels exactly match Snes9x.
- Added a master-clock frame deadline and real-PC interrupt-frame entry so long
  interpreted loading work yields and resumes across host frames.
- Added semantic title/demo telemetry and a deterministic 12,000-frame gate
  that completes two ordered attract cycles with six starts, six ends, and no
  sequence errors.
- Replaced fixed 534-frame audio pulls with a fractional 32,040 Hz / 60.0988 Hz
  accumulator; added clipping, discontinuity, silence, and stream-fingerprint
  telemetry plus private raw PCM capture.
- Added a source-only native/Snes9x PCM comparison and three synthetic tests;
  the checked full-cycle streams pass duration, level, peak, discontinuity, and
  seven-region silence-envelope gates with zero clipping.
- Added an interactive Windows `snesrecomp` host with resizable 4:3 video,
  high-resolution 60.0988 Hz pacing, exact-rate queued waveOut stereo,
  keyboard controls, and hot-pluggable XInput controls.
- Converted the desktop host to a normal Windows GUI application. Double-click
  now opens an `.smc`/`.sfc` picker instead of flashing a command prompt, while
  explicit ROM arguments remain available for scripts and automated tests.
- Centralized exact-ROM loading for both native hosts, added a synthetic
  gamepad-mapping regression and a hidden 180-frame desktop startup test, and
  documented the complete manual testing workflow.
- Fixed the shared 65816 interpreter clearing program-bank bit 7 on long
  jumps/calls/returns. DKC2 now executes its `$80/$B5/$BB` FastROM banks with
  correct timing; the former 54-frame first-cycle lag is six frames early, and
  native frame 3,575 exactly matches Snes9x frame 3,578.
- Removed intermittent one-frame black flashes caused by publishing a GDI
  clear and scaled frame as two visible operations. The desktop host now
  composes off-screen and presents once; a synthetic resize/letterbox test and
  corrected recording found no isolated black frames.
- Added fixed 3x fast-forward (`2`/right trigger) and approximately 15 seconds
  of fixed 3x in-memory rewind (`1`/left trigger), with bounded-history,
  trigger-threshold, and real snapshot-restore tests.
- Added DKC2's normal 2 KiB battery-SRAM persistence beside the executable,
  automatic `save.srm.bak` rotation, ROM-picker working-directory protection,
  and explicit automated-test isolation from user saves.

## 0.7.0 - 2026-07-15

- Added an opt-in 512x224 headless RGB renderer for tiled PPU modes 0, 1, 3,
  and 5, including 2/4/8-bpp tile decoding, high-resolution Mode 5, flips,
  layer priority, and forced blank/brightness.
- Added sprites for every OBSEL size pair, OAM addressing/write latching,
  priority rotation, and the 32-object/34-sliver scanline limits.
- Added main/subscreen composition, fixed color, add/subtract/half color math,
  background scroll latches, and observed PPU mode/feature telemetry.
- Added deterministic framebuffer SHA-256 output and opt-in private PPM export;
  the 2,000,000-instruction private regression pins a complete-frame hash.
- Added explicit frame/global limitation masks for unsupported PPU state rather
  than presenting partial output as fully implemented.
- Added a synthetic renderer suite and a private render integration test,
  bringing the configured clean build to 20 passing tests.
- Ran the renderer for the existing 20,000,000-instruction checkpoint with no
  new execution barrier; the final published frame has no declared per-frame
  limitation, while earlier Mode-7 frames remain explicitly unsupported.

## 0.6.0 - 2026-07-15

- Implemented the shared Mode-7 write latch for `$211B-$2120` and signed
  24-bit multiplication reads at `$2134-$2136`.
- Implemented delayed unsigned CPU multiplication and division, including
  operand capture, result registers, and divide-by-zero behavior.
- Implemented the `$2180-$2183` WRAM data/address ports, 17-bit wraparound,
  and open-bus handling for the unused remainder of the B-bus register range.
- Added command-line controller masks and SHA-256 snapshots of WRAM, SRAM,
  VRAM, CGRAM, OAM, and ARAM to timed probes.
- Advanced the private-ROM regression to 20,000,000 instructions with no
  unsupported-hardware barrier and pinned its deterministic VRAM hash.
- Isolated imported APU warnings from strict project warnings, added the timing
  suite to the Make build, and made the PowerShell runner stop on build errors.

## 0.5.0 - 2026-07-15

- Added an opt-in master-cycle scheduler with NTSC scanline/frame progression
  and a documented provisional eight-master-cycles-per-A-bus-access adapter.
- Moved APU execution onto the timed path at a nominal 21:1 master/SPC ratio
  while retaining the version-0.4 port-access checkpoint.
- Implemented `$4200`, `$4207-$420A`, `$4210-$4212`, NMI/TIMEUP latches,
  H/V timer IRQs, CPU interrupt entry, and timed `WAI` resumption.
- Implemented direct/indirect HDMA, all transfer patterns, line descriptors,
  table termination, and register write-back.
- Added two-controller serial input plus timed autojoy and `$4218-$421F`.
- Added synthetic timing/interrupt/HDMA/controller tests and a private
  `--with-timing` integration checkpoint.
- Advanced the real ROM through repeated NMI, 133 general-DMA transfers, and
  1,071 HDMA line transfers to the next explicit boundary: `$2135` Mode-7
  multiplication output.

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
