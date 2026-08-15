# DKC2 recompiler configuration

This directory contains source-only structural input for
[`snesrecomp`](../snesrecomp/README.md). It must not contain ROM bytes,
extracted assets, generated C, assembly source, comments, or game data.

The checked-in CFG files were mechanically derived from the public H4v0c21
DKC2 disassembly and symbol line map at commit
`59a3a8aef88d074f488d25d0d0623fcb37fa3791`. They contain function addresses,
labels, bounded ranges, data ranges, and finite indirect-dispatch contracts.
The referenced repository had no explicit license at the validated revision;
its assembly source, comments, and data are therefore not redistributed here.
Any public redistribution of these derived labels/contracts requires a
separate provenance and legal review.

The shared LLE interpreter remains the correctness oracle for unproven paths.
Add or change a boundary or dispatch contract only when disassembly structure
and differential runtime evidence support it, and record material decisions in
`docs/IMPLEMENTATION_JOURNAL.md` or `ISSUES.md`.

`funcs.h` is a mechanically generated declaration index for these CFG entries.
`scripts/generate_snesrecomp.ps1` refreshes it before each private-ROM emit so a
clean checkout cannot silently build against a stale function ABI surface.

For private naming research, `scripts/promote_snesrecomp_symbols.py` can expand
a residual `CODE_BBXXXX` entry from an ignored WLA overlay. It defaults to a
dry run and accepts only one context-qualified alias that retains the same full
generic identity. Never promote by raw range-start address: conditional
revision differences and dense dispatch tables make that unsafe. Review the
reported set, apply explicitly, then run the normal generator. The overlay is
not a build dependency and must remain outside Git.

Curated knowledge discovered after that import belongs in `symbols.toml` and
`layouts.toml`. Function records use the actual supported USA v1.0 CFG entry
as their stable key; aliases may preserve descriptive labels containing an
address from another conditional-assembly layout. Layout records identify WRAM
objects and typed structure fields used by diagnostics. Do not hand-edit the
generated `scripts/dkc2_symbols_generated.py` or `docs/SYMBOL_DATABASE.md`.

```powershell
python scripts\build_dkc2_symbol_database.py --apply-cfg
python scripts\build_dkc2_symbol_database.py --check
```

The first command validates and applies exact-boundary semantic names before
regenerating tracked projections. The second is the non-writing CI check.

Generated C belongs in ignored `generated/snesrecomp/` during bring-up. A
future build may use ignored `src/gen/` if that becomes necessary to match the
upstream game-runner convention.
