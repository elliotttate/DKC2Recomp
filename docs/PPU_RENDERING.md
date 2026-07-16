# Headless PPU rendering

## Purpose and boundary

Version 0.8 extends the deterministic framebuffer checkpoint with Mode-7 BG1
and EXTBG rendering plus a private reference-image comparison workflow. It
converts the PPU memory/register model into RGB pixels so visual state can be
tested, hashed, and inspected without making the boot runner interactive.

This is a partial, independently written SNES renderer. The Rareware-logo
frame now matches Snes9x 1.63 pixel for pixel, but that one image does not
establish complete console accuracy. The interpreter timing adapter is still
provisional and several PPU features are deliberately unsupported. VRAM,
CGRAM, and OAM now match an adjacent Snes9x state exactly; beam-aligned display
registers have not yet been compared at the same deterministic event.

## Running it

The renderer is opt-in and implies the timed APU path:

```powershell
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 2000000 --with-render
```

To inspect the final completed frame locally, provide an explicit PPM path:

```powershell
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 2000000 `
    --frame-output="build\private-frame.ppm"
```

The PPM contains graphics derived from the ROM. Keep it beneath an ignored
`build`, `private`, or `generated` directory; never add it to Git or distribute
it. No image is written unless `--frame-output` is present.

## Frame contract

The framebuffer is always 512x224 pixels with three bytes per pixel in RGB
order. A low-resolution SNES pixel occupies two adjacent output pixels. Mode 5
keeps its separate 512-wide pixels. The public dimensions and renderer state
are declared in `include/dkc2/ppu_render.h`.

Visible scanlines 1 through 224 are rendered at their HBlank event, before the
same event applies HDMA register writes. After scanline 224, the working buffer
is published atomically as the completed frame. The boot runner hashes only
this published buffer, so an incomplete next frame cannot change the result.

Brightness is applied while converting 15-bit CGRAM colors to 8-bit RGB.
Forced blank produces black pixels.

## Implemented PPU state

The tiled renderer currently supports:

| Area | Implemented behavior |
| --- | --- |
| Background modes | 0, 1, 3, 5, and 7 (including EXTBG) |
| Tile formats | SNES planar 2-bpp, 4-bpp, and 8-bpp |
| Background layout | 8x8/16x16 tiles and 32/64-tile map dimensions |
| Addressing | BG map bases, character bases, H/V scroll, map wrapping |
| Tile attributes | palette, priority, horizontal flip, vertical flip |
| Layer composition | per-mode BG priority, main screen, subscreen, backdrop |
| High resolution | native 512-wide Mode 5 |
| Objects | all OBSEL size pairs, name select, palette, flip, priority |
| Object evaluation | priority rotation, 32-object and 34-sliver line limits |
| Color math | fixed/subscreen source, add/subtract, and half result |

Mode 7 adds the SNES interleaved VRAM map/pixel layout, signed 13-bit offsets
and centers, 8.8 matrix arithmetic, horizontal/vertical screen flips, 1024x1024
wrapping, transparent and tile-zero outside behavior, and EXTBG palette and
priority selection. Mode-7 BG1 uses the full 8-bit palette index. With EXTBG
enabled, BG2 uses seven palette bits and the high source bit as priority.

OAM writes implement the paired low-table latch, high-table addressing, word
address, and priority-rotation starting object required by the renderer.
Background scroll writes use the shared offset latch exposed by `$210D-$2114`.
Fixed color is reconstructed from component-select writes to `$2132`.

## Explicit limitations

Every scanline is inspected before rendering. Unsupported state sets a bit in
the working frame; publication retains both the completed-frame mask and a
global mask of all features seen during the run.

| Bit | Meaning |
| --- | --- |
| `$01` | unsupported background mode (currently 2, 4, or 6) |
| `$02` | object-rendering condition outside the supported model |
| `$04` | color or object windows enabled |
| `$08` | direct-color mode enabled |
| `$10` | mosaic enabled |
| `$20` | pseudo-hires state outside implemented Mode 5 behavior |
| `$40` | reserved legacy EXTBG marker; implemented state no longer raises it |
| `$80` | interlace/object-interlace enabled |

The renderer also reports the background-mode mask, limited scanline count,
forced-blank count, and separate hardware range-over/time-over object counters.
Object range/time overflow is modeled behavior and is not itself an
unsupported-frame condition.

## Verification

The synthetic `ppu_render` suite constructs VRAM, CGRAM, OAM, and register
state without any game data. It verifies:

- forced blank and brightness conversion;
- Mode-1 layer and tile priority;
- background flips;
- main/subscreen addition;
- object palette and OAM positioning;
- distinct Mode-5 high-resolution pixels;
- Mode-7 identity and screen-flip addressing;
- Mode-7 wrapping, transparent-outside, and tile-zero-outside behavior;
- Mode-7 interleaved VRAM and EXTBG priority;
- the 33rd-object range-over condition; and
- completed-frame publication and mask reset.

The private 2,000,000-instruction integration check publishes 478 frames. Its
last frame uses modes 1 and 5, reports zero limited scanlines, and hashes to:

```text
fd62d5bea3f0961e286bd4ae266ff1c09a30be9260da820003dc06b26d307b8d
```

The private 20,000,000-instruction run publishes 4,445 frames. The last frame
uses Mode 1, reports no per-frame limitation, and hashes to:

```text
bbf512419991ea943dd5e61aa61096c043feeae94c43de0d37bf9d18ebe941ad
```

The Mode-7-specific integration check stops at 1,700,000 instructions and pins
the completed 512x224 frame hash:

```text
ce5c1873327e39ba4d77c33e101ce9956ee86554c889855b8e3531b330923c2f
```

It publishes 342 frames, reports Mode 7 (`$80`), and has zero limited
scanlines. The 2,000,000-instruction run now has zero limited scanlines over
all 107,072 rendered scanlines; its pre-existing final-frame hash is unchanged.
The 20,000,000-instruction final hash is also unchanged. Its object-limit
counters remain separate modeled hardware behavior.

## Reference-image comparison

The official Snes9x 1.63 Windows release was run locally with the user's ROM.
Screenshots were captured across the Rareware-logo animation and retained only
outside the repository. The 1,700,000-instruction native PPM was reduced from
512x224 to the SNES low-resolution 256x224 image by verifying and collapsing
each duplicated horizontal pixel pair. Two consecutive reference screenshots
contained the same sustained frame. Both comparisons produced:

```text
Candidate SHA-256: 57b5636a6eee0295ff395771453092d8560de5e643208e2fb69cecae190d627f
Reference SHA-256: 57b5636a6eee0295ff395771453092d8560de5e643208e2fb69cecae190d627f
Differing pixels: 0 / 57344
Differing channels: 0 / 172032
Maximum channel error: 0
Result: exact RGB match
```

`scripts/compare_frames.py` reproduces the measurement using only the Python
standard library. It reads non-interlaced 8-bit RGB/RGBA PNG and binary PPM,
automatically collapses a doubled-width candidate, verifies the horizontal
pairs, reports image hashes and error statistics, and can emit an optional PPM
difference image:

```powershell
python scripts\compare_frames.py build\private-mode7.ppm `
    "C:\private\snes9x-mode7.png" `
    --diff build\private-mode7-diff.ppm
```

Every input and optional diff image can contain ROM-derived pixels. Keep them
outside source control. The script exits 0 only for an exact match, 1 for a
valid mismatch, and 2 for an unreadable or unsupported image.

The comparison establishes exact output for this visible Mode-7 frame. It does
not prove that the provisional scheduler reached it at the same console time.

## Reference display-memory comparison

Snes9x's own save-slot command captured private snapshot-format-v12 states
beside the sustained matching frame. The official format stores raw VRAM and
serialized CGRAM/OAM. After converting the saved 16-bit CGRAM words to the
runtime's byte order, the native and reference hashes are identical:

```text
VRAM SHA-256:  011580629bf3007e8acd599b872173a08a6156c4f896ef9e6fbf35023e99cb7e
CGRAM SHA-256: bb867c40f4978157de0e761f13d2ed05fc4f697f8c23597ea110b3a26a01df2e
OAM SHA-256:   44ddd2f478477ebd1c1cd5b99400af48cd46033c59173195f48870e608cec810
```

`scripts/inspect_snes9x_snapshot.py` reproduces this extraction using only the
Python standard library. It validates the Snes9x v12 gzip/block structure,
prints VRAM/CGRAM/OAM and raw `$2100-$2133` write-register hashes, accepts
optional expected hashes, and returns a failing status for a mismatch.

The native runner now prints the same display-memory hashes, a raw PPU-write
fingerprint, and decoded Mode-7 register values. The raw write-register image
is deliberately reported separately: write-only data/address ports and
per-scanline HDMA state are beam-dependent and are not a canonical PPU state.
The published native frame and Snes9x screen can match while each runner has
already advanced those current registers into a different following scanline.

## Next validation task

Create a deterministic reference-emulator breakpoint or trace event at an
agreed beam position for the matched Rareware-logo frame, then compare the
decoded display registers alongside the already-matched RGB, VRAM, CGRAM, and
OAM. Repeat at the title screen. Windows and the remaining tiled modes follow
the same reference-driven process.
