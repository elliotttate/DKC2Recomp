# DKC2Recomp — Issues & State

Status as of 2026-07-17. This repo is the snesrecomp-integrated native port of
Donkey Kong Country 2 (USA v1.0, 4 MiB HiROM). It was stood up from a
contributor's archive; the recompiler engine is the `snesrecomp` submodule on
branch `dkc2/hirom-mapping`.

Supported ROM SHA-256: `35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`
(headerless USA v1.0). The ROM, disassembly, and `.sym` overlays stay under
ignored `private/` — never committed.

---

## OPEN #1 — full disasm-seed exposes a codegen mis-compilation (PRIORITY)

**Summary.** DKC2 boots and runs LLE-first (interpreter). Seeding AOT function
roots from the H4v0c21 disassembly (1,335 function entries, `--cfg-roots`) grows
static coverage from **13 → 1,665 AOT nodes** and builds/links cleanly, but the
attract-mode gate **diverges from the interpreter baseline** — proving at least
one of the ~630 newly-compiled AOT functions is mis-compiled. The interpreter
was masking a latent engine codegen bug; promoting to AOT exposed it.

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

**Next steps (recompiler-discipline: root-cause the generator, no cfg band-aids,
no editing generated C):**
1. Bisect banks to isolate the mis-compiled function (binary-search the cfg-dir:
   regen with half the banks, gate, narrow). Each step = regen (~33 s) + build +
   12k gate.
2. Once isolated to one function/variant: disassemble it (literal oracle), find
   the exact instruction/M-X width/idiom the codegen mis-emits, diff AOT-vs-interp
   execution at first divergence.
3. Fix the **generator** (`snesrecomp/recompiler/...` or runtime), regen, rebuild,
   re-gate. Expect possibly several distinct bugs.
4. Alternatively / meanwhile (doctrine-aligned): commit only the gate-clean subset
   (bank `$B3` is proven clean) so static coverage grows safely today, with the
   interpreter as the failsafe for the rest; burn the buggy functions down as each
   is root-caused.

### Reproduce the divergence

```powershell
# from repo root; native python (msys2 python mangles Windows paths — do not use)
$py  = "C:\Users\Matthew\AppData\Local\Programs\Python\Python312\python.exe"
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

## OPEN #2 — DKC2 is heavily interpreted (LLE-first, expected)

DKC2 boots almost entirely through the interpreter: bank00.cfg is a minimal
`auto_vectors` + `tier_down_stubs` seed (`--no-hle --no-host-root-scan`), so
static analysis reaches only 13 AOT nodes before hitting the first unresolved
indirect (the NMI dispatcher's `JMP` at `$00:F3A3`). This is the correct
LLE-first baseline. Growing static coverage is OPEN #1 (disasm seed) plus the
always-on tier-2 coverage-feedback loop (below).

The engine now emits a **tier-2 interp-coverage manifest** on every exit for
every game (`SNESRECOMP_TIER2_MANIFEST`, default CWD `tier2_coverage.json`;
schema `snesrecomp tier2 coverage v1`), which `snesrecomp/tools/tier2_ingest.py`
folds back into the cfg to promote interp gaps to AOT. A 1,200-frame run names
125 gap sites incl. the `$00:F3A3` dispatcher targets — the promotion worklist.

---

## Attribution PENDING

The original DKC2 port author is GitHub **Nicktendonick** (id 140297302). Their
work is committed under the placeholder identity `Codex <codex@local>`. A final
history pass should re-author those commits (mailmap / filter-repo) to
`Nicktendonick <140297302+Nicktendonick@users.noreply.github.com>`. Recipe and
commit list: `../_dkc2_review/ATTRIBUTION_PENDING.md`. Not yet applied.

---

## Regression status (engine branch vs upstream main)

Branch `dkc2/hirom-mapping` = snesrecomp `main` (9fdba7d) + Nicktendonick's 20-file
HiROM delta (47d11e6) + the every-game coverage-manifest hoist (b9c5308).
Differential analysis (`../_dkc2_review/REGRESSION_ANALYSIS.md`): **no codegen/logic
regression to MMX/SMW/ALttP/SM** — codegen byte-identical for all four (all detect
LoROM; new HiROM paths inert), runtime changes inert/bugfix, with two accuracy-only
deltas (ppu sprite-limit overflow, interp RTL bank). Empirical per-game frame A/B
still optional. NOTE: Super Metroid separately fails on plain `main` (widescreen-
override drift) — a pre-existing SM-vs-main issue, unrelated to this branch.

---

## Build / toolchain notes

- Native Windows python only (`...\Python312\python.exe`); the msys2/devkitPro
  `python` on PATH mangles Windows path args.
- Generated banks use `file(GLOB CONFIGURE_DEPENDS)`; after a regen that adds or
  removes `bank*_v2.c`, run `cmake -S . -B build-snesrecomp` (reconfigure) so the
  new files are globbed in, else you get spurious link errors for the new banks.
- mingw gcc builds need Windows-style `TMP`/`TEMP` (else it falls back to
  `C:\Windows` and permission-fails).
