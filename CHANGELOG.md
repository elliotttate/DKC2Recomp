# Changelog

## Unreleased

- Rebased the DKC2 SNESrecomp integration onto upstream `1d0f2e0`, published
  immutable dated backup branches first, and moved the submodule fetch URL to
  `Nicktendonick/snesrecomp` so the rebased DKC2 commits remain available.
- Restored MSVC builds after the rebase by making generated RAM-routine guard
  ownership explicit and adding the current Cx4 dependency to the synthetic
  interpreter-bridge harness. All three DKC2 hosts build; 44/45 configured
  tests pass, with the former frame-3,309 reference hash intentionally left
  failing pending renewed event-aligned comparison.
- Added deterministic two-player input recording/replay, a synthetic parser
  test, Pirate Panic route telemetry, and a private entrance-to-goal gate that
  rejects clipped audio, interpreter caps, and unresolved dispatches.
- Captured the first private 11,275-frame Pirate Panic route. It reaches the
  normal goal path but exposes an unresolved `$BA:B33F` dispatch at frame
  5,522, so the route remains a documented failing fixture rather than a
  completed correctness milestone.
- Added append-only `Version NN` Windows snapshots. The packager automatically
  chooses the next number, refuses overwrites, records executable hashes and
  source provenance, and excludes private/runtime artifacts.
- Standardized `build-snesrecomp/` as the routine Windows compiler workspace
  and documented every legacy build tree, generated/private output, and
  numbered test-handoff location in `docs/BUILD_HYGIENE.md`.
- Added a confirmed Restore Defaults button to the shared launcher Settings
  footer. DKC2 resets its complete launcher configuration from the same
  authoritative defaults used on first run, without changing the selected ROM,
  SRAM, or save states.
- Added rolling per-launch crash reports, privacy-allowlisted automatic
  diagnostic bundles, Windows minidumps, and clean/fatal/exception integration
  drills for both playable hosts. Mod-aware save isolation remains deferred
  until an actual versioned mod manifest and loader exist.
- Renamed new slot-0 state writes from `saves/dkc20.sav` to
  `saves/dkc2s0.sav` while retaining load-only compatibility with the former
  filename.
- Added a shared SDL2 gameplay host for Windows, Linux, and macOS source builds
  with accelerated video, keyboard/two-controller input, exact-rate queued
  audio, SRAM, states, rewind/fast-forward, filters, and FPS title reporting.
- Added a portable Python private-source generator, a pinned SDL 2.30.9 CMake
  fallback, host-neutral launcher/cache and viewport modules, and synthetic
  portable-tooling/viewport tests.
- Fixed recomp-ui's GCC-only compiler barrier to use the MSVC equivalent when
  building the same Dear ImGui launcher source on Windows.
- Kept Linux/macOS support explicitly provisional until native build,
  controller/audio, visible-attract, and packaging acceptance is completed.
- Added an OpenGL gameplay presenter with automatic atomic-GDI fallback and
  launcher-selectable nearest/bilinear scaling.
- Added opt-in Raw/CRT/Composite/Trinitron screen-color models by adapting the
  pinned PSXRecomp lookup-table implementation; Raw remains the byte-exact
  default and the selected model works through either presenter.
- Added synthetic raw/CRT/filter-metadata coverage plus hidden private-ROM
  OpenGL-required and forced-GDI CRT smoke gates, with complete upstream
  revision and license provenance under `third_party/`.
- Reached 100% demanded static 65816 coverage: both whole-program analyzers
  identify 3,468 exact variants, all 3,468 emit as native C, and none are
  classified as LLE-only code nodes.
- Modeled DKC2's WRAM-clearing stack reset as a proven static continuation and
  added a generic `noreturn_jsr` contract for two documented original-game
  crash calls into data while preserving their real interpreter fault path.
- Added Python and Rust parser/analysis/code-generation regressions; 357 shared
  Python tests, 44 Rust tests, and all 32 optimized DKC2 tests pass.
- Rebased continuing development on the public `mstan/DKC2Recomp` 0.0.1
  source baseline and its exact `snesrecomp`/`recomp-ui` submodule pins;
  preserved the former working trees on named backup branches.
- Added an FPS readout to the game-window title and opt-in per-phase
  main-thread telemetry in `performance.log`.
- Enabled speed optimization for Release host builds (`-O3` for GCC/Clang,
  `/O2` for MSVC) and added an optional private Windows icon build input.
- Added synthetic FPS and telemetry tests and repeated the complete 32-test
  suite, including the two-cycle attract gate, on the optimized build.
- Exposed Player 2 in the shared ImGui launcher and routed selected keyboard
  or XInput sources to both native SNES controller ports, with persistent
  source and deadzone settings.
- Restored inherited Windows permissions after private source generation so
  atomic output replacement cannot leave `generated/` inaccessible to the
  interactive user.

## 0.0.1 - 2026-07-19

- Published the first playable Windows preview with the shared Dear ImGui
  `recomp-ui` launcher, North American retail cover art, and strict
  external-ROM verification.
- Shows the native host's Player 1 keyboard/controller slot in the launcher;
  unsupported per-button rebinding remains hidden.
- Added resizable 4:3 presentation, exact-rate stereo audio, persistent SRAM
  with backup rotation, rewind, fast-forward, keyboard controls, and XInput.
- Promoted 3,425 of 3,467 exact DKC2 CPU-mode variants to static C (98.79%);
  the remaining 42 variants use the shared interpreter fallback.
- Fixed direct-page bank selection, indexed address carry, computed-return
  ownership, BRR decoding, and echo FIR semantics in the shared framework.
- Completed a 12,000-frame/two-attract-cycle DKC2 soak with zero sequence
  errors, inspected clean title and in-level frames, and active non-clipping
  audio.
- Fixed the Pirate Panic death/restart black screen by making static MVN/MVP
  transfers preserve 65816 count, index wrapping, data-bank, per-byte timing,
  and legal frame-deadline continuation semantics.
- Added F5/F9 slot-0 save states and deterministic headless state/machine
  capture inputs used to reproduce and close the death regression.
- Completed 12,000-frame attract soaks and manual gameplay checks on the same
  framework revision for DKC2, Super Mario World, A Link to the Past, Mega Man
  X, and Super Metroid.
- Fixed generated monolithic translation units to declare every referenced
  cross-bank exact variant, matching the existing sharded-unit contract.
- Added reproducible Rust-analyzer generation and a release packager that
  refuses ROMs, generated code, saves, screenshots, and audio captures.
