# SNESrecomp provenance

## Repositories

- Authoritative upstream: <https://github.com/mstan/snesrecomp>
- DKC2 integration fork: <https://github.com/Nicktendonick/snesrecomp>
- Submodule path: `snesrecomp/`
- Current authoritative base:
  `fe6045c22bb023e15d825ec40bfc25387ec9253c`
- Current integration revision:
  `884abcbe13e277f0f541536e5ec4c284c758b02e`

The fork exists so DKC2-specific framework changes can be fetched by ordinary
submodule commands and proposed upstream without requiring direct write access
to `mstan/snesrecomp`.

## 2026-08-20 authoritative refresh

The parent repository first refreshed to official `mstan/snesrecomp` and then
pins the tested integration commit above. The former DKC2 fork state remains
available on local backup branch `codex/backup-dkc2-engine-20260820`; only the
remaining adaptations were replayed on the current upstream base.

## 2026-08-20 visible-debugger instrumentation

Integration commit `884abcb` adds a small, host-disabled-by-default observation
API to the shared world-shadow renderer. It records the selected source class
for widened BG margin pixels and supplies that class to DKC2's private live
debugger. The normal tile result and guest state are unchanged when capture is
off. `ppu.c` passes scanline/pixel-span context to this API; `ws_shadow.c` owns
the transient provenance surface and distinguishes source-map-prefilled cells
from live captures.

These framework changes are not part of authoritative revision `fe6045c` yet.
They are published on the DKC2 integration fork so ordinary recursive clone and
submodule commands reproduce the tested tree while upstream review is pending.

## 2026-07-27 rebase

- Upstream base: `1d0f2e0ba19d60f68122a79451e48f278b7fed41`
- Rebased DKC2 branch: `codex/dkc2-static-runtime-support`
- Rebased DKC2 revision: `a4ec65d78e82a9260d422976db14d7997a67356e`
- Current DKC2 integration revision:
  `d2d4cc7c0ed2bdad0aba088a39e699324038751e`
- Immutable pre-rebase backup branch:
  `codex/backup-pre-upstream-rebase-20260727`
- Pre-rebase backup revision:
  `19ac90e7863d6dd3cef51bbee653760172959995`

The rebase replayed two DKC2 commits. One additive conflict occurred in
`tests/v2/test_cfg_loader.py`: current upstream and the DKC2 commit had inserted
different parser tests at the same location. The resolution retained all four
independent tests. SNESrecomp's 364-test Python v2 suite and 50 Rust analyzer
tests passed after reconciliation.

The second rebased commit remains explicitly work-in-progress. It seeds
configured forced variants as native-analyzer roots while the Pirate Panic
`$BA:B33F` dispatch-mode problem is investigated; it is not a completed
correctness fix.

The follow-up integration commit adds two narrowly scoped upstream
compatibility corrections:

- generated game targets may define
  `SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS` so MSVC does not link the legacy
  fallback guard table beside the generated strong table; and
- the standalone interpreter-bridge harness supplies the neutral Cx4 IRQ fake
  required by the current bridge.

Later DKC2 integration commits preserve the forced-variant experiment, repair
trace-enabled APU lock declarations, and propagate a clean interpreted guest
non-local return through the compiled call bridge. The latter is covered by a
synthetic M=0 `PLA; RTL` bridge case and is required by Swanky's Bonus Bonanza.

## License status

SNESrecomp's original code is licensed under PolyForm Noncommercial 1.0.0 at
the current pin. The exact notice is preserved as
`third_party/licenses/SNESRecomp-PolyForm-Noncommercial-1.0.0.txt`.
SNESrecomp's `THIRD_PARTY_ATTRIBUTION.md` and `third_party/` directory retain
the separate notices for bundled components.
