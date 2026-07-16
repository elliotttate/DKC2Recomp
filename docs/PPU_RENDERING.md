# Headless PPU rendering

## Purpose and boundary

Version 0.7 adds a deterministic framebuffer checkpoint before any desktop
window is introduced. It converts the PPU memory/register model into RGB pixels
so visual state can be tested, hashed, and inspected without making the boot
runner interactive.

This is a partial, independently written SNES renderer. A recognizable frame
does not establish console accuracy. The interpreter timing adapter is still
provisional, several PPU features are deliberately unsupported, and no output
has yet been compared with an accurate emulator at the same logical event.

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
| Background modes | 0, 1, 3, and 5 |
| Tile formats | SNES planar 2-bpp, 4-bpp, and 8-bpp |
| Background layout | 8x8/16x16 tiles and 32/64-tile map dimensions |
| Addressing | BG map bases, character bases, H/V scroll, map wrapping |
| Tile attributes | palette, priority, horizontal flip, vertical flip |
| Layer composition | per-mode BG priority, main screen, subscreen, backdrop |
| High resolution | native 512-wide Mode 5 |
| Objects | all OBSEL size pairs, name select, palette, flip, priority |
| Object evaluation | priority rotation, 32-object and 34-sliver line limits |
| Color math | fixed/subscreen source, add/subtract, and half result |

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
| `$01` | unsupported background mode (currently 2, 4, 6, or 7) |
| `$02` | object-rendering condition outside the supported model |
| `$04` | color or object windows enabled |
| `$08` | direct-color mode enabled |
| `$10` | mosaic enabled |
| `$20` | pseudo-hires state outside implemented Mode 5 behavior |
| `$40` | Mode-7 EXTBG enabled |
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

That long run records 7,307 object range-over and 5,053 object time-over
scanlines. Its global limitation mask is `$01` because early intro frames use
Mode 7, whose pixels are not yet implemented. These values are local regression
evidence, not reference-emulator or hardware validation.

## Next validation task

Implement Mode-7 pixel generation, then capture a reference-emulator image and
state snapshot at the same logical event as a native frame. Compare the RGB
buffer, PPU registers, VRAM, CGRAM, and OAM rather than relying on visual
similarity. Only after the mismatch is understood should the result be used as
a rendering-accuracy checkpoint. Windows and the remaining tiled modes follow
the same reference-driven process.
