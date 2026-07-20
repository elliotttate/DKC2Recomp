# Changelog

## Unreleased

## 0.0.1 - 2026-07-19

- Published the first playable Windows preview with the shared Dear ImGui
  `recomp-ui` launcher, North American retail cover art, and strict
  external-ROM verification.
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
