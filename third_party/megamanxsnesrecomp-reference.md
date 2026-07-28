# MegaManXSNESRecomp widescreen research reference

- Project: <https://github.com/mstan/MegaManXSNESRecomp>
- Inspected revision: `f0cba24dfd6ce80b3e06b11871fac7658b0921d1`
- Inspection date: 2026-07-28
- License status at that revision: no root license file or explicit repository
  license metadata was found.
- Use in DKC2Recomp: reference-only architectural research.

No source, comments, generated game code, ROM-derived data, or assets were
copied. The DKC2 implementation was written independently against
SNESrecomp's public PPU interfaces and DKC2's separately documented runtime
boundaries.

Useful concepts confirmed by inspection were a wider internal SNES
framebuffer, per-game PPU margin policy, game-specific object boundaries, and
presentation policies for screens whose original tilemaps do not contain
off-screen scenery. The reference project itself documents unresolved
widescreen enemy-boundary and background-margin issues at the inspected
revision, so it is not treated as correctness evidence for DKC2.
