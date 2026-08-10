# Widescreen diagnosis workflow

## Purpose

`scripts/capture_widescreen_diagnostics.py` converts one deterministic DKC2
frame into evidence that separates five different failure points:

1. level or object data exists in game memory;
2. the required tile/graphic data exists in VRAM;
3. a game sprite is active inside the extended camera boundary;
4. the game submits tiles or OAM for that layer;
5. the PPU produces pixels in the 43-column widescreen margins.

This is a developer tool, not part of the playable release. It does not modify
the ROM, WRAM, VRAM, generated game code, or save data.

## Build the trace runner

Keep this separate from the normal optimized play build. On Windows:

```powershell
cmake -S . -B build-snesrecomp-diagnostics `
  -DDKC2_BUILD_SNESRECOMP=ON `
  -DSNESRECOMP_ENABLE_TRACE=ON `
  -DDKC2_ROM="C:\private\dkc2.smc"
cmake --build build-snesrecomp-diagnostics --config Release `
  --target dkc2_snesrecomp_headless --parallel
```

The trace runtime reserves large history buffers and should not replace
`build-snesrecomp` for normal play or performance measurement.

## Capture a frame

Use a deterministic input recording so each isolated run reaches exactly the
same game state:

```powershell
python .\scripts\capture_widescreen_diagnostics.py `
  --executable .\build-snesrecomp-diagnostics\Release\dkc2_snesrecomp_headless.exe `
  --rom "C:\private\dkc2.smc" `
  --input-recording .\recordings\pirate-panic-entrance-to-goal.input `
  --frame 5499 `
  --output-dir .\.cache\widescreen-diagnostics\frame-005499
```

Optional `--sram` and `--savestate` arguments reproduce a private desktop
state. `--layers composite,bg1,obj` can shorten a focused rerun. The complete
default is composite, BG1, BG2, BG3, and OBJ; BG4 is available explicitly.
Add `--scan-oam-margins` to retain all route-wide margin OAM samples. Add
`--function-watch CODE_NAME` with a trace runner to capture the first exact
static-function hit and its call stack. Function watches prove execution, not
visual correctness.

The tool starts a fresh process for each layer using
`SNESRECOMP_LAYER_MASK`. This is intentional: it prevents previous draw state
from contaminating an isolated capture. It refuses to overwrite a non-empty
evidence directory. It writes:

- `index.html`, a side-by-side human-readable view;
- `report.json`, the machine-readable evidence and automatic findings;
- one BMP and stdout/stderr log per layer;
- composite-only WRAM and VRAM snapshots;
- decoded camera and 25-slot DKC2 game-sprite state;
- a live `screen_profile` with level configuration, terrain owner,
  horizontal/vertical map layout, and BG3 policy;
- the newest 128-slot render-consumed OAM inventory; and
- widescreen-shadow and PPU register state.

All output belongs in ignored `.cache/` or another private diagnostic
directory. WRAM/VRAM snapshots, screenshots, ROMs, recordings, saves, and
generated game binaries must never be committed.

## Start with terrain

For a floor or wall defect, inspect `screen_profile.terrain_owner` before
investigating objects. Terrain can be BG1 or BG2:

1. Choose a frame immediately before the bad edge becomes visible.
2. Open `index.html` and inspect the reported terrain layer alone.
3. If the floor/wall is absent there, examine the matching WRAM map source,
   VRAM tilemap, camera coordinate, and widescreen-shadow counters.
4. If BG1 contains the correct terrain but the composite does not, examine
   PPU priority, color math, clipping windows, and presentation.
5. Repeat one or more frames after the defect. Do not infer temporal behavior
   from a single still image.

BG2 normally identifies Pirate Panic's sky/ocean parallax, but is the terrain
owner in Mudhole Marsh. BG3 can contain HUD
or staging material and remains intentionally conservative. The label is an
evidence category, not a promise that every DKC2 room uses a layer identically.

## Diagnose an object

For a barrel, banana, enemy, or collectible that appears late, capture a frame
just before pop-in and another immediately after it. Compare the same sprite
slot and placement number:

| Game sprite in margin | OAM in margin | OBJ pixels in margin | Likely stage |
| --- | --- | --- | --- |
| no | no | no | placement/spawn/load boundary |
| yes | no | no | object render cull, render table, or display mode |
| yes | yes | no | OBJ tile availability, PPU clipping/window, or presenter |
| yes | yes | yes | object exists; investigate position, priority, or timing |

The decoded game-sprite record includes its slot, type pointer, world
coordinates, camera-relative coordinates, graphic, display mode, state,
placement number/parameter, and despawn counters. OAM records are deliberately
separate. A compound DKC2 game sprite can emit several OAM tiles, so matching
by screen position and frame is a candidate correlation, not an object ID.

`screen_x` is the game sprite origin relative to `$17BA`, not the bounds of
every tile in its compound graphic. A sprite just outside the margin can still
contribute a tile inside it, and the inverse is possible.

Bananas require one extra branch in this workflow. They use a dedicated list
and direct OAM writer, so absence from the 25-slot game-sprite report does not
prove that banana data is unloaded. First inspect the banana-list WRAM record,
then the render-consumed OAM tiles and OBJ isolation. If the expected X is at
least 256 but the reported OAM X equals only its low byte, investigate the
renderer-specific ninth-X-bit packing rather than placement activation.
If right-margin bananas are complete but left samples stop near X=-14, inspect
the tile emitter's separate `$000F` negative-X cutoff. For a 43-pixel margin,
the widened limit is `$003A`; changing the nearby `$0167` value is incorrect
because that value belongs to the vertical span.

## Interpreting automatic findings

The report only makes narrow evidence-based classifications:

- `screen_policy_unclassified`: live Mode-1 state does not identify one proven
  terrain owner, so the screen must not be widened by analogy;
- `background_load_or_render`: the classified terrain owner or explicitly
  repeated BG3 has native-view detail but no non-backdrop pixels on one side;
- `object_render_or_cull`: an active game sprite projects into a margin but no
  render-consumed OAM entry does;
- `object_ppu_or_presenter`: margin OAM exists but OBJ isolation produces no
  margin pixels; and
- `runtime_integrity`: an unresolved abandon, interpreter cap, or fatal event
  invalidated the run.

No finding does not prove the image is correct. Position errors, wrong but
non-empty tiles, priority errors, and animation errors still require visual
comparison. A logged `UNRESOLVED-STUB TRAP HIT` is counted in runtime events
for investigation but is not classified as blocking unless the run abandons,
hits the interpreter cap, or reports a fatal error.

Every image report includes `upper_left_margin`, the logical top-down rectangle
`x=[0,43), y=[0,64)`. This region was added for the owner-recorded late Pirate
Panic regression; the original bad frame contained 224 non-backdrop pixels in
a 28x8 strip, while the corrected frame contains zero. BMP storage order is
normalized, so the same coordinates apply to positive-height Windows BMPs.

Trace builds configured with external `DKC2_ROM`, `DKC2_PIRATE_PANIC_INPUT`,
and `DKC2_PIRATE_PANIC_SRAM` paths register
`supplied_rom_widescreen_bg1_route`. The test replays frame 6,750, verifies the
expected camera, rejects blocking runtime events, requires active native BG1,
and asserts the logical upper-left region is empty. The ROM, input, SRAM,
screenshots, WRAM, and VRAM remain external or temporary.

The later private Pirate Panic route is intentionally separate because it
reaches a different late-screen margin state. Configure a trace build with
`DKC2_PIRATE_PANIC_LATE_INPUT` and `DKC2_PIRATE_PANIC_LATE_SRAM`; CTest adds
`supplied_rom_widescreen_pirate_panic_late_route`. It captures frame 14,400,
requires camera `(5673, 419)`, and requires zero non-backdrop pixels in the
same upper-left BG1 rectangle. Its source map was calibrated against all 896
native cells in the inspected area, so the test specifically protects against
stale shadow history in map cells proven transparent rather than judging a
generic image similarity score.

Frame 15,900 is intentionally **not** registered as a passing private test.
Although its source-page calibration improves native-map agreement, owner
review found remaining margin artifacts. Retain frames 12,900, 13,800, and
15,900 as diagnosis checkpoints until their transition, hard room-edge, and
dynamic-layer policies are individually proven. Do not bless their current
image hashes merely because a deterministic replay is stable.

For the private Bramble regression, configure the trace build with
`DKC2_BRAMBLE_INPUT` and `DKC2_BRAMBLE_SRAM`. CTest then registers
`supplied_rom_widescreen_bramble_route`, which verifies the square-layout
profile and both BG1 margins at frame 1,600. `bramble-01` is a partial-stage
fixture; a later entrance-to-goal recording is still required.

## Repeatable order for each new defect

1. Record the exact route and first visibly bad frame.
2. Capture the frame before and after the transition.
3. Identify the layer with isolation, beginning with BG1 terrain.
4. For objects, follow game sprite → OAM → OBJ pixels in that order.
5. Trace the relevant VRAM address only after the failing stage is known.
6. Implement one narrowly scoped policy.
7. Recapture the same frames and compare the evidence.
8. Preserve the confirmed address/field/layer flow in the implementation
   journal and add a synthetic test before moving to the next object class.

This avoids applying a general widescreen guess to unrelated screen types.
