# Visible Widescreen Debugger

`DKC2VisibleDebugger.exe` is a private developer build for diagnosing the
experimental 16:9 renderer while the game is running. It uses the same game,
input, audio, save, and OpenGL paths as `DKC2Recomp.exe`, but reserves a panel
on the right for live evidence. The normal player build is unchanged.

The executable is kept in the external **Private Version 14** folder. ROMs,
saves, recordings, captures, and compiled binaries remain outside Git.

## Start it

Open `DKC2VisibleDebugger.exe` in the Version 14 folder. If ROM selection is
requested, choose the headerless North American v1.0 ROM. Enable **Widescreen
16:9** in Settings before pressing Play.

## Controls

| Key | Action |
| --- | --- |
| F1 | Toggle margin pixel-source colors |
| F2 | Composite picture |
| F3 | BG1 only |
| F4 | BG2 only |
| F5 | BG3 only |
| F6 | Sprites/OBJ only |
| F7 | Pause or resume emulation |
| F8 | Advance exactly one game frame while paused |
| F9 | Export the current framebuffer and live state |
| F11 | Save the selected save-state slot |
| F12 | Load the selected save-state slot |

Save states from Version 13 and early Version 14 use the older v6 host layout.
The refreshed debugger rejects them without changing the running game. Boot
normally and press F11 to create a new v7 slot before using F12.
| Escape | Open the normal DKC2 overlay |

F9 writes a new timestamped directory under `captures` in the directory from
which the debugger was started. It currently exports `frame.ppm` and
`report.json`. This is a single-frame evidence snapshot, not yet DKC1's
rolling input/snapshot repro bundle.

## Pixel-source colors

F1 colors only the added widescreen margins; the native 4:3 picture remains
untouched.

| Color | Meaning |
| --- | --- |
| Green | Tile captured from DKC2's live VRAM stream |
| Cyan | Tile reconstructed from DKC2's decompressed level map |
| Magenta | Repeated/folded live backdrop tile |
| Gray | Verified transparent fallback |
| Red | Unproven wrapped VRAM fallback; investigate this first |
| Yellow | Deliberately repeated native edge/backdrop |

The live panel also reports the host frame, CPU resume address, raw scene and
mode words, camera position, PPU mode, visible/widened layers, and cumulative
west/east shadow hit/miss/fallback counts. Raw WRAM addresses are shown until
their semantic names are confirmed in the project symbol database.

## Recommended investigation loop

1. Reproduce the defect in composite mode.
2. Pause on or immediately after it with F7; use F8 to bracket the first bad
   frame.
3. Isolate BG1, BG2, BG3, and OBJ with F3-F6.
4. On a bad BG margin, enable F1. Red identifies unsafe wrapped VRAM; gray
   identifies missing verified content; green/cyan with wrong art indicates
   an ownership, coordinate, priority, or source-map problem.
5. Press F9 on the first bad frame and again on a nearby correct frame.
6. Keep both timestamped capture directories with the matching input
   recording when reporting the defect.

Pixel provenance is presently available for the world-shadow BG1/BG2 paths.
BG3 isolation is useful, but BG3's DKC2-specific repeat/widen policies do not
yet provide the same per-pixel source classification. Sprite isolation shows
rendered OBJ output; it does not yet annotate individual OAM owners.

## Design boundary

The debugger is host-only. It does not modify guest WRAM, cartridge logic, or
serialized save-state data. Provenance recording is off by default and is
cleared every rendered frame. The implementation independently adapts the
evidence concepts observed in DKC1Recomp's MIT-licensed Visible Widescreen
Debugger; no DKC1 game code, assets, or ROM-derived data were copied.
