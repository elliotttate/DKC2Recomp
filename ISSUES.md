# DKC2Recomp — Issues & State

Status as of 2026-07-18. This repo is the snesrecomp-integrated native port of
Donkey Kong Country 2 (USA v1.0, 4 MiB HiROM). It was stood up from a
contributor's archive; the recompiler engine is a pinned `snesrecomp` submodule.

Supported ROM SHA-256: `35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`
(headerless USA v1.0). The ROM, disassembly, and `.sym` overlays stay under
ignored `private/` — never committed.

---

## Current checkpoint — 99.91% exact static variants

The checked-in structural import now describes 3,309 bounded function entries,
610 finite runtime-pointer sites, 38 terminal inline-table calls, and 313 exact
data regions. Generation produces 3,467 exact CPU-mode variants: **3,464 AOT
eligible and three deliberate LLE fallbacks**. This supersedes the old 13-node
bootstrap and 1,665-node seed experiments retained below as debugging history.

The latest analyzer work models recursive exit sets, declared boundaries,
DKC2's indirect return idioms, and caller-crossing nonlocal returns. A
12,000-frame full-AOT run completes two attract cycles with no sequence errors
or runtime bailouts. Inspected title and in-level captures render cleanly.
Manual testing still exposes game issues, so static coverage and the attract
regression are not playability sign-off.

The remaining fallback set is `$80:85E4` (the `clear_full_wram` stack-reset
continuation), `$B3:BC0D` (a source-documented game-crashing branch into
invalid code), and `$BA:F305` (an all-zero/`BRK` crash entry). Each must be
closed by a game-agnostic analyzer improvement or explicitly retained in
LLE—never by editing generated C, adding HLE, or inventing CFG contracts.

The Rust analyzer now matches the Python emission contract exactly and emits
byte-identical C across all 103 translation units. The B5 closure generation
measured 25.3 seconds with Rust versus 401.0 seconds with Python; the Release C
compile, about 3 minutes 38 seconds from a cold generated tree, is now the
dominant rebuild cost.

Reproduce the checked-in static build with:

```powershell
.\scripts\generate_snesrecomp.ps1 -Rom "C:\private\dkc2.sfc"
```

The interpreter remains the byte-level oracle. Visible fixes require an
inspected screenshot plus the relevant full-AOT/full-interpreter memory or port
gate; the pacing-sensitive audio fingerprint is not a standalone correctness
gate.

---

## RESOLVED #1 — full disasm seed exposed two codegen mis-compilations

**Summary.** DKC2 boots and runs LLE-first (interpreter). Seeding AOT function
roots from the H4v0c21 disassembly (1,335 function entries, `--cfg-roots`) grows
static coverage from **13 → 1,665 AOT nodes** and builds/links cleanly, but the
initial attract-mode gate **diverged from the interpreter baseline**. The
interpreter was masking two latent engine codegen bugs; promoting to AOT exposed
them. Both generator/runtime class bugs were fixed on 2026-07-17.

**Root causes and fixes.**

1. `SegKind.DIRECT` emitted bank `$7E`, but 65816 direct-page addressing always
   uses bank `$00`. DKC2's `set_ppu_registers` uses `STA $00,x` with X holding a
   `$21xx` PPU-register address; AOT therefore wrote WRAM instead of the PPU.
   The generator now emits bank `$00` for all direct-page operands.
2. DKC2's decompressor uses three `PEI; RTS` computed-dispatch sites. When a
   data-driven target was absent from the static dispatch table, generic RTS
   miss handling treated it as a normal caller return and exited with PHB/PHY
   locals still on the guest stack. The generator now recognizes an RTS that
   popped a synthetic frame deeper than the function entry and tiers an unknown
   target into the interpreter at the exact ROM continuation.

The fixes are in the recompiler/runtime, not generated C. A synthetic regression
test covers the computed-RTS case, and the full engine suites pass (297 v2 and 73
runner tests).

**Closure gates.** The direct-page fix restores `video_active_frames=10862` and
`max_consecutive_blank=143` (interpreter-class behavior). The decompressor fix
makes full AOT VRAM byte-identical to the targeted interpreter workaround that
isolated `$BB8D9E`; the remaining full-AOT/full-interpreter frame-3400 delta is
745/65536 bytes (1.13678%), unchanged by that workaround and attributable to
the already-known frame-timing residual. VRAM-address-high writes are restored
to `$20,$50,$70,$60,$6C...` instead of `$00`, and an inspected frame-3400 demo
screenshot has a clean level background rather than dense tile noise.

**Evidence (12,000-frame headless attract gate):**

| metric                | baseline (interp, 13 nodes) | full seed (1,665 nodes) |
|-----------------------|-----------------------------|-------------------------|
| video_active_frames   | 10,609                      | 7,202                   |
| max_consecutive_blank | 155                         | 2,236 (~37 s stall)     |
| audio_fnv1a           | `73f8981735837ce1`          | `af96add222b3237c`      |
| attract_cycles        | 2                           | 2                       |
| sequence_errors       | 0                           | 0                       |
| interp bail hits      | 0                           | 0                       |

The high-level attract sequence still completes (2 cycles, 0 sequence errors, 0
bail), so it is not catastrophic — it is a specific AOT function that stalls the
render (~2,236 consecutive blank frames) and shifts audio. **WRONG-value bug**
(an AOT body produces different output than the interpreter for the same input),
not MISSING-behavior. The interpreter is the oracle here (baseline is correct).

**Isolation so far.** The 179-entry bank-`$B3`-only PoC gated **byte-identical**
(13 → 227 nodes, audio_fnv1a `73f8981735837ce1`). The full seed adds banks
`$80, $B4, $B5, $B6, $B8, $B9, $BA, $BB, $BC, $BE`. So the culprit is a non-`$B3`
bank, or a `$B3` entry not in the PoC subset.

The first-divergence workflow remains the doctrine for any later seeded-AOT bug:
interpreter as oracle, no cfg/HLE band-aid, and fixes made at the generator or
runtime class level.

### Reproduce the divergence

```powershell
# from repo root; use a native Windows Python (MSYS2 path conversion can mangle
# Windows arguments)
$py  = "python"
$rom = "<private DKC2 USA v1.0 .sfc>"

# full seed regen (private cfg-dir already built, 1335 entries, 11 banks)
& $py snesrecomp/tools/v2_emit.py --rom "$rom" --cfg-dir private/cfg_full `
    --out-dir generated/snesrecomp --no-host-root-scan --no-hle --cfg-roots `
    --max-insns 500000 --max-nodes 100000
# IMPORTANT: reconfigure so CONFIGURE_DEPENDS re-globs new bank*_v2.c
& cmake -S . -B build-snesrecomp -DDKC2_BUILD_SNESRECOMP=ON
& cmake --build build-snesrecomp --config Release --target dkc2_snesrecomp_headless
.\build-snesrecomp\Release\dkc2_snesrecomp_headless.exe "$rom" 12000   # compare stats to table

# restore baseline (correct, 13 nodes)
& $py snesrecomp/tools/v2_emit.py --rom "$rom" --cfg-dir recomp `
    --out-dir generated/snesrecomp --no-host-root-scan --no-hle `
    --max-insns 500000 --max-nodes 100000
```

### Seed pipeline artifacts (all under ignored `private/` or `_dkc2_review/`)

- `_dkc2_review/tools_asar/asar.exe` — Asar 1.91.
- `_dkc2_review/disasm_h4v0c21/` — H4v0c21 disassembly (assembles byte-exact to
  the ROM; no license → private research reference, not redistributed).
- `private/dkc2-h4v0c21.sym` — full WLA symbol map (26,963 labels) from
  `asar -Dversion=0 --symbols=wla all.asm`.
- `private/dkc2-h4v0c21-entries.sym` — 1,335 JSL/JSR call-target (function-entry)
  subset. Correct granularity: seed entries, not every basic-block label; the
  analyzer discovers interior blocks itself and aliases interior labels into their
  parent.
- `private/cfg_full/` — per-bank cfg `func` directives generated from the entries.
- Scratchpad tools: `sym_to_cfg.py` (.sym → cfg funcs), `resolve_call_targets.py`
  (JSL/JSR operands → entry addresses).

To regenerate the seed from scratch: assemble H4v0c21 with Asar `--symbols=wla`,
run `resolve_call_targets.py` then `sym_to_cfg.py`.

---

## SUPERSEDED #2 — original 13-node LLE-first bootstrap

The original checkpoint booted almost entirely through the interpreter:
`bank00.cfg` was a minimal
`auto_vectors` + `tier_down_stubs` seed (`--no-hle --no-host-root-scan`), so
static analysis reaches only 13 AOT nodes before hitting the first unresolved
indirect (the NMI dispatcher's `JMP` at `$00:F3A3`). This is the correct
LLE-first baseline. It is retained as historical context; the current checked-in
configuration emits 3,464 AOT variants and three LLE variants.

The engine now emits a **tier-2 interp-coverage manifest** on every exit for
every game (`SNESRECOMP_TIER2_MANIFEST`, default CWD `tier2_coverage.json`;
schema `snesrecomp tier2 coverage v1`), which `snesrecomp/tools/tier2_ingest.py`
folds back into the cfg to promote interp gaps to AOT. A 1,200-frame run names
125 gap sites incl. the `$00:F3A3` dispatcher targets — the promotion worklist.

---

## Attribution applied

The original DKC2 port author is GitHub **Nicktendonick** (id 140297302). Their
placeholder `Codex <codex@local>` authorship was rewritten on 2026-07-17 to
`Nicktendonick <140297302+Nicktendonick@users.noreply.github.com>` in both the
engine branch and game repository. Matthew Stanley remains committer. The
review recipe and original commit list remain in
`../_dkc2_review/ATTRIBUTION_PENDING.md` as an audit record.

---

## Regression status (engine branch vs upstream main)

Branch `dkc2/hirom-mapping` = snesrecomp `main` (9fdba7d) + Nicktendonick's 20-file
HiROM delta (47d11e6) + the every-game coverage-manifest hoist (b9c5308).
Differential analysis (`../_dkc2_review/REGRESSION_ANALYSIS.md`): **no codegen/logic
regression to MMX/SMW/ALttP/SM**. After the direct-page and computed-RTS fixes,
all four were regenerated and compiled in detached validation worktrees, then
runtime-smoked through real frame dumps: SMW 200 frames, ALttP 317, MMX 281, and
SM 150, with every process still live when stopped. Super Metroid required the
pre-existing widescreen-override check to be disabled in the scratch build
because its current profile lacks the optional override symbols; no game main
worktree was changed.

---

## Build / toolchain notes

- Native Windows python only (`...\Python312\python.exe`); the msys2/devkitPro
  `python` on PATH mangles Windows path args.
- Generated banks use `file(GLOB CONFIGURE_DEPENDS)`; after a regen that adds or
  removes `bank*_v2.c`, run `cmake -S . -B build-snesrecomp` (reconfigure) so the
  new files are globbed in, else you get spurious link errors for the new banks.
- mingw gcc builds need Windows-style `TMP`/`TEMP` (else it falls back to
  `C:\Windows` and permission-fails).
