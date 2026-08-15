# PSXRecomp screen-color LUT

This directory records provenance and complete license texts for the
present-time screen-color lookup table adapted from `mstan/psxrecomp` revision
`d7815862e18ef939e5e6e5c6947f8c29667982d5`, the exact framework revision
pinned by `mstan/MegaManX6Recomp` when this component was imported on
2026-07-21. The same two files were byte-identical at PSXRecomp revision
`d2006e02a3001495b1eedf2c1cc965d23c0de38f`, the revision pinned by
`mstan/Tomba2Recomp` at that time.

Upstream paths:

- `runtime/include/color_lut.h`
- `runtime/src/color_lut.c`
- `THIRD_PARTY_ATTRIBUTION.md`

The implementation extends SNESRecomp's shared
`runner/src/snes/color_lut.{c,h}` module instead of carrying a duplicate in
project-owned code. Local adaptations add a programmatic model-selection API,
the Composite option, explicit invalid-screen rejection, and DKC2 integration.
The colorimetric constants and model math remain aligned with the cited
PSXRecomp implementation. DKC2 converts its already-rendered BGRX8888 frame
back to the SNES-native five-bit channel indices before consulting this table.
Raw presentation bypasses the conversion completely, so the default verified
frame remains byte-identical.

The C color-science implementation derives from JRickey/gba-recomp
`crates/screen/src/{color,profile,lut}.rs`; the locally inspected upstream
revision was `de4edf59b872d887046d6a3b005e2df551b6d44c`. That work is licensed
MIT OR Apache-2.0. The PSXRecomp C reimplementation and CRT/composite models
are distributed under PolyForm Noncommercial 1.0.0. The applicable license
texts are stored beside this file. This third-party component is not
relicensed by the repository's root MIT license.

No ROM data, extracted game assets, shaders, or generated binaries are stored
here.
