# Proposed snesrecomp upstream pull request

## Status

Do not publish this pull request yet. The focused tests and DKC2 native rebuild
pass, but an existing LoROM integration gate and final commit split/review are
still required. The submodule work is isolated on branch `dkc2/hirom-mapping` at upstream base
`c1ce97ec8ae3743b4b1dce092903bebcefd58896`.

## Proposed title

Add HiROM routing and correct 65816 control-flow semantics

## Problem

The analysis pipeline and several runtime ROM-pointer helpers assume LoROM.
That selects the wrong architectural vectors and physical offsets for HiROM
games. Separately, the CPU-state helper recognizes the LoROM and HiROM battery
RAM windows simultaneously. Addresses such as `$F0:09FE` are therefore read
from SRAM even when the active cartridge is HiROM and the address is ROM.

## Proposed changes

- Detect conventional LoROM versus HiROM headers conservatively, preserving
  LoROM on an ambiguous score.
- Select the mapper's vector-table location and CPU-to-ROM offset conversion in
  the v2 analyzer/emitter.
- Route runtime fixed, dynamic, and block-move ROM pointers through `Cart` so
  one mapper owns mirroring and bounds handling.
- Preserve the `$80` execution mirror in the convenience ROM-pointer helper.
- Select only the active cartridge's SRAM window in `cpu_state.c`.
- Add synthetic mapper tests and an overlapping-address runtime regression.
- Fetch `BRA`/`BRL` operands before updating the interpreter PC, so relative
  targets are based on the end of the operand on every C compiler.
- Add positive-`BRA` and negative-`BRL` regression cases.
- Preserve all eight program-bank bits across `JSL`, `JML`, `RTL`, bridge RTL,
  and `JMP [abs]`; clearing bit 7 changes FastROM execution into its SlowROM
  mirror even when the fetched bytes are identical.
- Preserve an explicit suspended 24-bit PC when constructing a hardware
  interrupt frame, while retaining the existing generated-host wrapper.
- Make OAM scanline evaluation follow the hardware's reverse sliver-fetch
  order and 34-sliver limit, with a focused synthetic PPU regression.
- Keep DMA/OAM state transitions routed through the shared hardware helpers.
- Add memory-backed snapshot save/load entry points using the existing
  versioned `SaveLoadInfo` format and game-specific extension hooks. This is
  framework-only support required by rewind; the bounded history and DKC2
  continuation payload remain in the game repository.

## Files intended for the upstream change

- `recompiler/snes65816.py`
- `recompiler/v2/cfg_loader.py`
- `recompiler/v2/program_emit.py`
- `tools/v2_analyze.py`
- `runner/src/common_rtl.c`
- `runner/src/common_rtl.h`
- `runner/src/cpu_state.c`
- `runner/src/cpu_state.h`
- `runner/src/snes/cart.c`
- `runner/src/snes/cart.h`
- `runner/src/snes/dma.c`
- `runner/src/snes/ppu.c`
- `tests/v2/test_rom_mapping.py`
- `tests/v2/run_tests.py`
- `tests/runtime_dispatch/known_lle_entry_test.c`
- `tests/runtime_dispatch/run.ps1`
- `runner/src/snes/interp816.c`
- `tests/interp816/interp816_test.c`
- `tests/interp816/bridge_test.c`
- `tests/ppu/ppu_sprite_limit_test.c`
- `tests/ppu/run.ps1`

No DKC2 ROM hash, game configuration, symbols, generated code, or runner glue
belongs in the upstream pull request.

## Required verification before publication

1. Run `tests/v2/run_tests.py`; verified 295/295 under bundled Python.
2. Run the interpreter and runtime-dispatch focused suites; verified under
   MSVC: interpreter core 23/23 and bridge harness 52/52, with GCC still
   required before publication. The focused PPU sprite-limit regression also
   passes under MSVC.
3. Rebuild the DKC2 native runner from clean generated output; verified.
4. Run the 600-frame private smoke gate; verified with nonzero palette, video,
   and audio samples.
5. Re-run at least one existing LoROM game runner or the closest upstream
   LoROM integration gate to guard compatibility.
6. Review the final submodule diff for framework-only content, then split it
   into logical commits before pushing a fork branch and opening a draft PR.

The upstream top-level launcher currently reports four width-lint failures in
`v2/codegen.py` on an untouched checkout of the same base commit. Record those
as baseline failures in the PR rather than modifying unrelated code.

## Draft description

This change makes ROM analysis and runtime pointer routing honor the active
SNES cartridge mapper. It adds conservative LoROM/HiROM header detection,
uses the corresponding vector and physical-address mapping in v2 analysis,
and centralizes runtime ROM-pointer resolution in `Cart`.

It also fixes an overlapping SRAM-window bug: the CPU helper previously
treated LoROM and HiROM SRAM regions as active at the same time. The regression
case uses `$F0:09FE`, which is LoROM SRAM but HiROM ROM, and verifies both
cartridge types select the correct storage.

The behavior remains LoROM-compatible on ambiguous headers. Synthetic mapper
coverage is included; no game ROM or generated game source is part of the
change.
