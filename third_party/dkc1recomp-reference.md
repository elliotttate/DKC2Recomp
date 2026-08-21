# Current SNES recomp reference projects

Inspected on 2026-08-20 as behavior and workflow references only:

- `mstan/SuperMarioWorldRecomp` at
  `10d0ea1f23b4fdb8c2233747ff15e626e3c44a47`;
- `mstan/ZeldaAlttPSNESRecomp` at
  `3c2f4d7a68458ccbb167be61b1ddcdb8a29db736`; and
- `elliotttate/DKC1Recomp` at
  `b404cb708460829998315d44a9fa0ab39984a96b`.

DKC1Recomp identifies its interactive host as the **Visible Widescreen
Debugger** (`dkc1_desktop_tools.exe`). Useful concepts for a future DKC2-native
implementation are layer isolation, pause/exact-step controls, pixel-source
provenance, rolling input plus snapshot anchors, explicit repro export, and
clean-history replay for separating cartridge state from stale host state.

DKC1Recomp is MIT-licensed at the inspected revision. No source, comments,
ROM-derived data, screenshots, or game assets were copied. DKC2 will implement
only independently expressed, game-appropriate diagnostics and retain its own
tests and evidence schemas.

On 2026-08-20 DKC2 independently implemented the first subset of those ideas
as `DKC2VisibleDebugger.exe`: host-side layer isolation, exact pause/step,
BG1/BG2 margin provenance, and a single-frame evidence export. DKC2's code is
based on its existing shadow-tile and diagnostic interfaces. The DKC1 rolling
repro recorder, game-specific state decoding, and assets were not copied.
