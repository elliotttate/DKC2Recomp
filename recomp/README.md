# DKC2 recompiler configuration

This directory contains only hand-authored structural hints for
[`snesrecomp`](../snesrecomp/README.md). It must not contain ROM bytes,
extracted assets, generated C, or text copied from an unlicensed disassembly.

The initial configuration deliberately starts from the architectural interrupt
vectors and lets the framework's LLE interpreter remain the correctness floor.
Add a function boundary or dispatch hint only after a private trace proves it is
required, and record that evidence in `docs/IMPLEMENTATION_JOURNAL.md`.

Generated C belongs in ignored `generated/snesrecomp/` during bring-up. A
future build may use ignored `src/gen/` if that becomes necessary to match the
upstream game-runner convention.
