# Upstream reconciliation

## Scope

Continuing development is based on branch `codex/reconcile-local-work`, whose
parent is public DKC2Recomp commit
`1c923cf7a9a1e3c1bab2508f6c0f707760b23e6d` (`v0.0.1`). Its submodules remain
at the revisions selected by that repository:

| Component | Revision |
| --- | --- |
| `snesrecomp` | `cfa8e567d5927a89523f50b6d29ccdb9b02eceb8` |
| `recomp-ui` | `7c18edda01a86fc27231cb58ed7d189e13483626` |

No ROM, generated game C, save, capture, or private icon is part of this
reconciliation.

## Preservation

Before changing bases, every tracked and untracked source change was committed
to local branch `codex/pre-dkc2recomp-reconciliation`:

| Working tree | Preservation commit |
| --- | --- |
| Parent repository | `347868a` |
| Former `snesrecomp` submodule | `8516437` |
| Former `recomp-ui` submodule | `5c834f0` |

These commits are recovery references, not the new development base.

## Disposition

The public repository already supersedes the former parent implementation for
the launcher, exact ROM verification, SRAM and save-state persistence,
fast-forward/rewind, atomic GDI presentation, static generation, release
packaging, and the two-cycle attract regression. Those older copies were not
replayed.

The current `snesrecomp` pin is 29 upstream commits beyond the former local
base and includes the relevant mapper, interpreter, PPU, static-coverage, and
block-move work. Replacing it with the older local fork would discard shared
engine fixes, so no old submodule patch was carried forward.

The former `recomp-ui` patch added DKC2-specific masks, a locked audio rate,
and hidden-launcher automation to an older API. The current shared UI has been
substantially reorganized and already supplies persistent launcher settings.
Those exact changes are neither copied nor declared complete; any still-useful
behavior must be redesigned as a generic upstream-compatible capability rather
than restoring the old fork.

Player 2 was subsequently exposed through the current public ABI by setting
DKC2's `num_players` metadata to two. The parent host now consumes both source
selectors and packs two inputs into the shared runtime's existing controller
ports. This required no `recomp-ui` fork or submodule modification.

Four self-contained host improvements remained applicable and were replayed
against the public source:

- a once-per-second FPS value in the gameplay window title;
- opt-in phase telemetry with an explicit GDI/no-GPU-timestamp boundary;
- Release speed flags (`-O3`, or MSVC `/O2`); and
- an optional external `.ico` build input.

Synthetic tests cover the FPS and telemetry calculations. These additions do
not modify either submodule or emulated SNES behavior.

## Verification

The verified private North American v1.0 ROM was generated locally with the
Python fallback because the Rust analyzer was unavailable in this environment.
That fallback emitted 3,464 exact AOT variants and three LLE variants, rather
than the release profile's conservative 3,425/42 split. Generated files and
the generated declaration header are build artifacts and are not committed.
This difference is recorded so the local build is not misrepresented as a
byte-for-byte reproduction of the 0.0.1 release profile.

The MinGW Release build completed with `-O3`. All 32 configured tests passed,
including the desktop smoke test, sprite reference hashes, and 12,000-frame
two-attract-cycle gate. A hidden 180-frame telemetry run reported 59.22 and
61.00 presented FPS. Main-thread active time was 72.4-78.5%, with 11.64-13.15
ms/frame spent in emulation and only about 0.005-0.006 ms/frame in the measured
GDI presentation call. This particular sample is therefore CPU/emulation
limited; it is not a general claim about every machine or scene.
