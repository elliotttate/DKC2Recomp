# SNESrecomp provenance

## Repositories

- Authoritative upstream: <https://github.com/mstan/snesrecomp>
- DKC2 integration fork: <https://github.com/Nicktendonick/snesrecomp>
- Submodule path: `snesrecomp/`

The fork exists so DKC2-specific framework changes can be fetched by ordinary
submodule commands and proposed upstream without requiring direct write access
to `mstan/snesrecomp`.

## 2026-07-27 rebase

- Upstream base: `1d0f2e0ba19d60f68122a79451e48f278b7fed41`
- Rebased DKC2 branch: `codex/dkc2-static-runtime-support`
- Rebased DKC2 revision: `a4ec65d78e82a9260d422976db14d7997a67356e`
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

## License status

SNESrecomp does not currently declare a project-wide license or provide a
root license file. Its upstream README states under `License`: "Not yet
declared." Code must therefore not be copied out of the submodule or assumed
to carry a permissive license. The submodule's
`THIRD_PARTY_ATTRIBUTION.md` and `third_party/` directory preserve the
separate notices and license texts for components whose licenses are known.
