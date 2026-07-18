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

Generated C belongs in ignored `generated/snesrecomp/` during bring-up. A
future build may use ignored `src/gen/` if that becomes necessary to match the
upstream game-runner convention.
