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
owner in Mudhole Marsh. The per-layer policy labels mirror the runtime's
structural rules: `world_keyed_terrain` is the live stream owner,
`rendered_scanline_repeat` is any enabled bounded map (32 columns, or a
64-column allocation whose extension page is another enabled layer's base
page), `phase_classified_64_column` is a 64-column BG1/BG2 that is served
from the terrain store in HDMA bands at the terrain phase and repeats
elsewhere, `physical_64_column` is a 64-column BG3 with pages of its own,
and `bounded_unclassified` is any layer on a screen without a proven terrain
owner. The label is an evidence category, not a promise that every DKC2 room
uses a layer identically.

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

## Automatic route audit

`scripts/audit_widescreen_route.py` turns a deterministic recording into a
temporal audit instead of asking a tester to notice and describe every defect.
It runs the same route once per selected layer, samples aligned frames, and
combines the images with a host-only per-frame trace of camera/PPU state,
world-keyed terrain entries, widescreen lookup provenance, and placed-object
lifetimes. ROM, SRAM, recordings, images, and traces remain under the chosen
ignored/private output directory.

For the neutral attract sequence:

```powershell
python .\scripts\audit_widescreen_route.py `
  --executable .\build-snesrecomp\Release\dkc2_snesrecomp_headless.exe `
  --rom ".\build-snesrecomp\Release\Donkey Kong Country 2 - Diddy's Kong Quest (U) (V1.0).smc" `
  --output-dir .\.cache\widescreen-route-audit-attract `
  --start 3200 --end 5250 --step 4
```

For an owner recording, omit `--end` to use the recording's complete length
and supply the paired starting SRAM:

```powershell
python .\scripts\audit_widescreen_route.py `
  --executable .\build-snesrecomp\Release\dkc2_snesrecomp_headless.exe `
  --rom "C:\private\dkc2.smc" `
  --input-recording "C:\private\route.input" `
  --sram "C:\private\route.start.srm" `
  --output-dir .\.cache\widescreen-route-audit-route `
  --step 4
```

`--step 1` examines every frame and is the exhaustive setting. It also emits
roughly 1.15 MiB of raw PPM data per emulated frame when all five default
layers are selected. Use a coarser pass such as 4/8/12 to locate intervals,
then repeat a short interval at step 1. `--layers` can reduce work. A captured
route can be reclassified without rerunning the game by repeating the command
with `--reuse-capture`; raw evidence is preserved and only derived reports are
refreshed.

Placed-object spawn/despawn claims are deliberately disabled for coarse
sampling. At `--step 4`, `8`, or `12`, a moving object's slot can fall between
samples and imitate a lifecycle edge. Rerun the suspect interval at
`--step 1`; only immediately adjacent frames support that conclusion.

The audit currently emits these detector classes:

| Finding | Meaning |
| --- | --- |
| `raw_vram_margin_fallback` | exact: a margin used the rolling VRAM entry after every safer source missed; this can directly expose stale VRAM |
| `verified_blank_margin_fallback` | safe observation: a small bounded source miss used verified transparency rather than stale VRAM; retained for possible missing/pop-in investigation |
| `large_verified_blank_margin_fallback` | actionable: at least 64 verified-blank margin samples appeared in one frame; retained even during camera motion because a burst can expose a tile-epoch or streaming failure |
| `margin_world_tile_mismatch` | the exact terrain entry served in a margin disagreed with the same world cell when it later/earlier entered the native view |
| `native_boundary_seam` | the old X=43/X=299 boundary is a much larger pixel discontinuity than neighboring columns; this remains an image heuristic |
| `object_spawn_inside_wide_view` / `object_despawn_inside_wide_view` | a placed object crossed its active lifetime in a margin or near an old 4:3 culling boundary |
| `active_margin_object_without_obj_pixels` | an active placed object with a nonzero graphic had no nearby pixels in the isolated OBJ result |

Terrain identity comparisons use the runtime's world-keyed tile entries, not
RGB color, so palette animation and lighting do not create false tile-ID
matches. Screen warm-up, forced blank/fades, cyclic/repeated layers, and
unclassified terrain ownership are excluded. Per-scanline HDMA, destructible
or dynamically rewritten BG objects, intentional spawn triggers, and artistic
intent can still require a reference trace or a subsystem-specific rule.

The trace's `shadow` array carries the margin lookup counters of the two
terrain stores and, third, of the ship-deck rigging store; `rigging` reports
whether the cartridge's rigging streamer was recognized (`configured`),
whether the ROM decode reproduced the native window (`ready`), the native
cells compared, matching, and matching only through the row upload's
high-byte shift (`native`), and the margin cells decoded (`margin`). A recognized rigging layer that is not ready is clamped for that
frame, so `configured=1, ready=0` on a gameplay frame is a defect to chase.

`geysers` reports the lava geyser steam decode of NMI sub-mode 18 (Red-Hot
Ride): whether the stage runs the effect (`configured`), whether the decode
reproduced every geyser block the cartridge had fully drawn (`ready`), the
animation frame the ring shows against the frame-counter prediction
(`frame`, `predicted`; they have agreed on every traced frame), the native
cells compared and matching (`native`), the margin and inset cells forced
(`margin`), and the listed geysers overlapping the presentation
(`listed`). A geyser stage that is configured but not ready shows no BG3
margin for that frame, so that combination on a gameplay frame is also a
defect to chase.

`terrain_source.stride` is `[row_bytes, match_percent]` for row-major
level maps (vertical, square, narrow, and ship-hold layouts): the bytes
per metatile row the decode uses this frame and the percentage of the
fully staged native window it reproduces. The layout's default stride is
replaced from the candidate set when it falls below 90 percent (Bramble
$002D calibrates to 160 at 97 percent); `[0, n]` means no candidate passed
and the frame's margins are black.

`terrain_source.phase` is the terrain layer's rendered scroll phase used
for the world-store keys, the prefill's source rows, and the band
classification, as `[h, v, from_band]`; `from_band` is 1 when an HDMA band
at the camera phase supplied it because the frame-start register was off
the camera (Slime Climb leaves BG1VOFS at 80 while every rendered line
shows the camera row). A stage whose `phase` disagrees with every band's
scroll for the terrain layer would classify all of them as repeats.

`terrain_source.wall` reports, for the frame, the margin cells the
structural wall rule continued from a full edge metatile (`structural`),
the cells mirrored across a player-held wall whose edge metatile is
partial (`mirrored`), and how many of the continued cells took their
metatile from the level map's own adjacency rather than a copy of the
edge column (`chained`, see ARCHITECTURE.md); all are zero away from such
a wall. `chained` well below `structural` at a wall means the map never
continues that wall's metatiles and the edge column is being copied.

`ppu.window` reports the frame-end window registers: `w1` and `w2`
edges, the per-layer window select word (`sel`), the window logic
(`log`), the main and sub window masks (`tmw`, `tsw`), and `cgwsel` and
`cgadsub`. A stage whose acid or water covers the native width but not
the margins is either windowed here or, as in Toxic Tower, has its
terrain layer on the repeat policy for the frame (`DKC2_BAND_DUMP`).

`DKC2_BAND_DUMP=1` prints every scanline band each frame: its scanlines,
then for BG1 and BG2 the policy letter (`W` world, `P` plane, `R` repeat,
`-` not wide), the BGnSC register, and the band's scrolls. Diff two
frames' dumps to see whether a strip that changes texture is a band
changing policy or the HDMA table changing shape.

Three switches replay what the desktop app does around normal play.
`DKC2_SAVESTATE_RELOAD_FRAMES=a,b,...` reloads the input savestate at
those host frames instead of running the console, then draws, as a
rewind restore does. `DKC2_REWIND_REPLAY=<interval>` keeps an in-memory
snapshot every `interval` frames of the forward run and, after the last
frame, restores them newest first and draws each without running,
writing the frames with the frame-sequence prefix and an `r` suffix; a
rewound frame that differs from the forward frame of the same host frame
shows what the presentation keeps across a restore. `DKC2_DRAW_EVERY=n`
draws only every nth frame, as fast-forward does; the presentation bias
glides per drawn frame, so a shifted picture is expected there, a
changed margin texture is not.

`DKC2_TERRAIN_FILL_MAP=1` prints the prefill's metatile fill map to
stderr each frame (`.` empty, `+` partial, `#` full, `?` undecoded) for
the prefill window, eight columns past it on each side and two rows above
and below the visible rows, with `|` before the cartridge window's first
column and after its last. `=2` adds each cell's metatile id, followed by
`>` when the map never places a fully populated metatile east of it, `<`
for west, `*` for neither. Read it before reasoning about a wall from
screenshots: it shows at once whether a margin column is void, which
cells are the wall, and which edge metatiles the map continues.

`ppu.planes` counts, per wide layer, the HDMA bands presented as static
planes this frame (the layer's own 64-column map continuing into the
margins as its hardware wrap, see ARCHITECTURE.md). Red-Hot Ride shows
about a dozen per layer; a lava stage reporting `[0,0]` after the camera
has moved means the plane map was written or failed the wrap-authoring
test and its bands fell back to the repeat policy.

New traces include `terrain_source.margin_prefill` as
`[expected,present,matching_static]`. When `expected == present`, every margin
cell has an authoritative same-frame source; a mismatch with the static map
means a newer game tilemap write won and is not compared with a later
animation phase. A fixed-screen seam must persist across adjacent samples and
lack a complete proven wide-source policy before it becomes actionable.

The generated `report.json` is the machine interface. `index.html` ranks the
actionable events, links event/reference BMPs, and lists safe observations in
a separate table. A clean actionable report means that no encoded defect
detector fired; it is not a proof that every position, priority, animation, or
object behavior is correct.

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

Ordinary wasp-hive sub-mode `$03` is now an experimental square-layout route.
The cartridge normally dispatches those rooms through the same `$60`-byte-row
square scroller; Parrot Chute Panic level `$0013` keeps its separate narrow-row
classification. Record Hornet Hole and Rambi Rumble independently and inspect
composite, BG1, BG2, BG3, and OBJ before treating either as supported. King
Zing Sting requires a separate boss-arena audit rather than inheriting visual
acceptance from the scrolling hive stages.

Rattle Battle level `$0005`, sub-mode `$0006`, provides a source-signature
regression that does not require committing a private route. Mode 1 with BG1
`$71`, BG2 `$5C`, BG3 `$79`, main/sub enables `$17/$10`, and terrain target
`$7000` must classify BG1 as the horizontal terrain owner, BG3 as
`physical_64_column`, and bounded BG2 as the repeat candidate. Diagnostic
findings therefore require BG3 pixels in both margins when its native plane is
visible. This protects the policy decision; a clean fresh-entry moving route
is still required for stage-level acceptance.

Topsail Trouble has a separate exact-state signature: level `$000B`, sub-mode
`$0008`, terrain target `$7800`, `BGMODE=$09` (Mode 1 plus priority), BG1
`$79`, BG2 `$70`, BG3 `$6C`, and main/sub `$17/$13`. It must classify BG1 as
`row_major_vertical` terrain, BG2 and BG3 as `rendered_scanline_repeat`, and
never promote BG3 as a physical 64-column plane. For the preserved 308x224
state, isolate BG3 and require non-blank rain pixels in X=0-25 and X=282-307;
compare X=26-281 against the pre-fix plane with absolute error zero. The
preserved snapshot and captures live under the ignored private directory
`.cache/private-states/2026-08-30-topsail-trouble/`.

## Preserved-state corpus

`scripts/check_widescreen_state_corpus.py` turns every preserved Quick Save
into a regression at once. It replays each state at 4:3 and at the selected
wide aspects, in composite and per-layer isolation, and checks that the
presented native viewport equals the 4:3 render (bias-aware; the seven
endpoint pixels that a proven-period backdrop may rebuild are reported
separately), that a visibly enabled layer is not blank in a visible margin,
that the old 4:3 boundary is not a persistent discontinuity, that no lookup
fell through to raw rolling VRAM, and that the replay completed:

```sh
python3 scripts/check_widescreen_state_corpus.py \
  --executable build/macos/dkc2_snesrecomp_headless \
  --rom /private/path/dkc2.smc \
  --state .cache/repros --state .cache/private-states \
  --output-dir .cache/widescreen-state-corpus \
  --frames 6 --aspect 16:9 --aspect 16:10
```

A `blank_margin` warning is a heuristic, not a defect: DKC2 authors empty
spans in BG1 (the level `$000F` roller-coaster gaps, the Topsail rigging
gaps). Confirm by decoding the WRAM map for the margin's world columns, or by
checking whether another aspect shows the same world columns empty inside its
native view, before treating it as a streaming or prefill failure.

Every headless run honors `DKC2_WIDESCREEN_EDGE=reflect|bars|shift|glide`, so a
wall state can be captured under each edge policy for comparison; the trace's
`bias`, `left`, and `right` fields show which one was active.

`--reference-dir` compares against an earlier run's frames so a changed rule
is judged on every previously accepted state. `--boot-frames 5250` also
captures the neutral attract demos; because widened object activation can
change enemy behavior over a long route, the boot capture uses only the
reference comparison. Configure `-DDKC2_STATE_CORPUS=<dir>` to register the
private `supplied_rom_widescreen_state_corpus` CTest. Contact sheets and
`report.json` stay under the private output directory.

## Repeatable order for each new defect

1. Record the exact route and first visibly bad frame.
2. Capture the frame before and after the transition.
3. Identify the layer with isolation, beginning with BG1 terrain.
4. For objects, follow game sprite → OAM → OBJ pixels in that order.
5. Trace the relevant VRAM address only after the failing stage is known.
6. Express the fix as a property of the live PPU geometry or game data, not
   as a level or sub-mode identity.
7. Recapture the same frames, then run the preserved-state corpus with the
   previous build as the reference so every accepted state is re-judged.
8. Preserve the confirmed address/field/layer flow in the implementation
   journal and add a synthetic test before moving to the next object class.

A rule that only holds for one room is a symptom that the property behind it
has not been identified yet.

## Attract-demo regression frames

Neutral boot provides three reproducible routes without private input files:

| Demo | Active range | Representative frames | Expected policy |
| --- | ---: | --- | --- |
| Mainbrace Mayhem | 3,276-4,071 | 3,350 / 3,650 / 4,000 | vertical BG1 terrain; repeated BG2 and BG3 cloud/lighting |
| Rickety Race | 4,132-4,427 | 4,265 | horizontal BG1 terrain; repeated BG2 parallax |
| Parrot Chute Panic | 4,505-5,248 | 4,550 / 4,880 / 5,200 | narrow row-major BG2 terrain; repeated BG1/BG3 hive backdrops |

Capture the same frames with no recording or SRAM argument. The first two
screens must classify as their established vertical/horizontal layouts. Level
`$0013`, sub-mode `$03` must classify as `narrow_vertical_row_major` with BG2
as terrain owner. The midpoint reports have zero findings; frame 4,000 can
report one non-rendering margin sprite whose current graphic is zero, so use
the isolated OBJ and visible composite before treating that narrow object
finding as a presentation failure.
