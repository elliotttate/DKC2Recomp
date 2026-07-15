# LakeSnes APU subset

This directory contains the S-SMP/SPC700, S-DSP, timer, and state-handler
subset of [angelo-wf/LakeSnes](https://github.com/angelo-wf/LakeSnes), imported
from commit `9db90b86e46a377609305e298dd92d71cd1d4c8a` on 2026-07-15.

LakeSnes is MIT-licensed. The required copyright and permission notice is in
`LICENSE.txt` and applies to the files in this directory.

Local adaptations are intentionally small:

- removed the owning `Snes` frontend pointer from `Apu`;
- made `apu_init` standalone;
- added allocation-failure checks; and
- fixed the unused sample-resampler source count to the NTSC baseline; and
- exposed the subset through the project-owned `dkc2/apu.h` wrapper.

The original save-state helpers are retained because the imported CPU and DSP
state functions reference them. No LakeSnes PPU, 65816, cartridge, input,
frontend, or SDL code is included.
