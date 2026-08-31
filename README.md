# DKC2Recomp
Note: This Recompilation is not complete and not ready to be released. Pre-Release.

> This recompilation is a byproduct of developing
> [snesrecomp](https://github.com/mstan/snesrecomp): the games are the proving
> ground, while the reusable framework is the larger goal. This is an early
> preview, not an official port. Expect rough edges and please report any
> reproducible gameplay, video, or audio regressions.

Static recompilation of *Donkey Kong Country 2: Diddy's Kong Quest* for SNES
into native desktop applications, using the `snesrecomp` framework. Windows
and Apple-silicon macOS builds are available, with the project still explicitly
in alpha. The native Mac application includes an AppKit menu, Dock icon,
platform user-data directory, and Mac-specific exact-rate frame pacing. It is
ad-hoc signed for testing; notarization remains open.

The 65816 game program is translated to native C where analysis can prove an
exact entry state. The current profile emits 3,475 exact AOT variants and keeps
two deliberate original-game fault variants on the shared 65816 interpreter.
That interpreter remains available as a correctness and exceptional-path
fallback. SNES hardware outside the main CPU—the PPU,
SPC700/S-DSP, DMA/HDMA, controllers, and cartridge mapping—is modeled by the
shared runtime.

## Quick start

### Windows release

1. Download `DKC2Recomp-windows-x64-v0.0.1.zip` from the
   [upstream releases](https://github.com/mstan/DKC2Recomp/releases) and
   extract the complete archive.
2. Run `DKC2Recomp.exe`.
3. In the Dear ImGui launcher, select your own legally obtained North American
   v1.0 ROM and choose **Play**.

The selected external path is remembered in `rom.cfg` beside the executable.
The ROM is never copied into the release. Saves are written to
`saves/save.srm`, with the previous clean save retained as `save.srm.bak`.

### Native macOS release

1. Download `DKC2Recomp-macos-arm64-v0.0.3.zip` from
   [Releases](../../releases) and extract it.
2. Open `DKC2Recomp.app` and select your own legally obtained North American
   v1.0 ROM. The ROM remains outside the application bundle.

The v0.0.3 Mac archive is an ad-hoc-signed Apple-silicon alpha build, not a
notarized distribution. If Gatekeeper quarantines the downloaded archive,
open the app from Finder with **Control-click > Open** and confirm once.

### Native macOS source build

Install Xcode Command Line Tools, CMake, Ninja, Python 3, Rust/Cargo, and SDL2,
then run:

```sh
./build_macos.sh "/private/path/to/DKC2-USA-v1.0.sfc"
open build/macos/DKC2Recomp.app
```

The resulting application is `build/macos/DKC2Recomp.app`. It bundles its SDL2
dynamic library, carries the project icon, is ad-hoc signed, and stores ROM
selection, launcher settings, SRAM, save states, and diagnostics under
`~/Library/Application Support/Flat2VR/DKC2Recomp`. The private ROM remains at
its original path and is never copied into the application bundle.

## Supported ROM

The launcher and runtime verify the ROM after removing an optional 512-byte
copier header.

| Property | Expected value |
| --- | --- |
| Size | 4,194,304 bytes (headerless body) |
| CRC32 | `006364DB` |
| SHA-256 | `35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633` |
| Internal name | `DIDDY'S KONG QUEST` |
| Region/version | North America v1.0 |

You must supply your own lawfully obtained dump. ROMs, extracted in-game
graphics, music, level data, save files, and generated ROM-derived C are not
distributed in this repository or its release archives. The launcher includes
North American retail cover art solely to identify the supported game and
region; its source and copyright notice are documented in
`recomp/launcher/README.md`.

## Semantic symbols for development

Reverse-engineering knowledge is kept outside the generated C.
`recomp/symbols.toml` assigns reviewed names, aliases, confidence, provenance,
tags, and notes to stable USA v1.0 CFG addresses. `recomp/layouts.toml` records
confirmed WRAM objects and fields. `docs/SYMBOL_DATABASE.md` is the generated
readable index.

After editing either TOML file, validate and regenerate with Python 3.11+ (or
Python 3.9/3.10 with `tomli`):

```powershell
python scripts\build_dkc2_symbol_database.py --apply-cfg
python scripts\build_dkc2_symbol_database.py --check
python scripts\lookup_dkc2_symbol.py banana
```

The build tool imports all 3,314 structural CFG entries into an ignored JSON
index at `.cache/dkc2-symbols.json`, updates only exact-address CFG names, and
generates the constants consumed by widescreen diagnostics. Generated game C
remains ignored and must still be rebuilt from the user's verified ROM.

## Widescreen route auditing

The experimental 16:9 path includes an automatic deterministic route auditor,
not only single-frame screenshots. `scripts/audit_widescreen_route.py` replays
composite/BG/OBJ layers and correlates them with camera state, exact
world-keyed terrain entries, margin-source provenance, and placed-object
lifetimes. Its HTML/JSON report flags raw rolling-VRAM fallback, missing
terrain replaced by transparent tiles, terrain identity changes, old-edge
seams, object spawn/despawn near the former 4:3 boundary, and active margin
objects with no OBJ pixels.

The trace also proves, for each sampled frame, how many expanded-margin cells
were requested, present, and equal to the static level source. A newer
cartridge tilemap write is treated as authoritative rather than compared with
a later animation phase. Verified transparent fallback is retained in a
separate safe-observation section; it is not counted as an actionable defect.
Object lifetime conclusions require `--step 1`.

Start with a coarse `--step 4` route, then rerun a short suspicious interval at
`--step 1`. All generated evidence belongs under ignored `.cache/` or an
external private test directory. See
[`docs/WIDESCREEN_DIAGNOSTICS.md`](docs/WIDESCREEN_DIAGNOSTICS.md) for commands,
confidence meanings, storage costs, and limitations.

## Default controls

| Action | Keyboard | Controller |
| --- | --- | --- |
| D-Pad | Arrow keys | D-Pad |
| A / B | X / Z | B / A |
| X / Y | S / A | Y / X |
| L / R | Q / W | Left / right shoulder |
| Start / Select | Enter / Right Shift | Start / Back |
| Rewind | Hold 1 | Left trigger |
| Fast-forward | Hold 2 | Right trigger |
| Save state (selected slot) | F5 | Unbound |
| Load state (selected slot) | F9 | Unbound |
| Toggle performance log | F | — |
| Open/close overlay | Escape | Guide or Start+Back |

Each player's **Configure** page in the pre-boot launcher and the in-game
**Controls** tab edit the real keyboard and standard-controller mapping
consumed by both playable hosts.
Rewind and Fast-forward also appear there as global Assist shortcuts. The
top-level **Assist Tools** page edits Rewind, Fast-forward, Save State, and
Load State keyboard/controller bindings. Press a binding chip and then the
desired key, controller button, or controller axis. All mappings persist in
`launcher.cfg`; per-player and Assist reset buttons restore the defaults above.

The launcher exposes independent Player 1 and Player 2 source selectors. By
default the keyboard controls Player 1 and the first connected gamepad controls
Player 2. The accepted Windows host uses XInput; the portable host uses SDL
GameController. Players set to Gamepad receive connected devices in player
order, so two gamepads drive the two SNES controller ports independently.
Source, deadzone, and binding choices persist in `launcher.cfg`.

## In-game overlay and Assist Tools

Press **Escape** during gameplay to pause on a completed frame boundary and
open the Dear ImGui overlay. In a fullscreen SDL/Mac game window, the first
Escape returns to windowed mode without also opening the overlay; Escape then
retains its normal overlay behavior. The SDL host also accepts the controller
Guide button; Start+Back is the portable fallback. The overlay provides Resume,
Settings, Controls, Assist Tools / Cheats, Credits, and Quit.
Gameplay input and audio are paused while it is open.

The Settings page exposes the launcher's display, audio, filtering, screen
model, widescreen, skip-launcher, and Restore Defaults choices. Volume,
widescreen, texture filtering, screen model, Player 1/2 source/deadzone, and
the Assist gate apply immediately; window scale, fullscreen mode, renderer,
audio enable, and skip-launcher take effect on the next launch. The shared
sample-rate choice is mirrored and
persisted, but this host currently outputs the SNES-native 32,040 Hz only.
The Controls page has Player 1, Player 2, Assist, and Fixed Shortcuts tabs.
Player tabs expose source, deadzone, and all 12 SNES keyboard/controller
bindings. The Assist tab edits Rewind, Fast-forward, Save State, and Load
State bindings. Select a binding and press the replacement key, controller
button, or axis; controller capture waits for a neutral release first to avoid
recording the button that opened the editor. Changes apply immediately and
are written to `launcher.cfg` on clean exit. Per-player and Assist reset
buttons restore only their respective control defaults. Escape cancels an
active capture; the menu and performance shortcuts are listed read-only so
they cannot be made unreachable.

Rewind, fast-forward, the overlay's five save-state slots, and every configured
Assist shortcut are intentionally gated behind **Enable Assist Tools /
Cheats**. This setting defaults off, persists as `AssistTools` in
`launcher.cfg`, and adds `(Assist Tools: On)` to the game window title. On the
native Mac app, the Game menu's fixed **Quick Save State** and **Quick Load
State** commands always operate Slot 1, even when Assist Tools are disabled.

The overlay is available in the Windows OpenGL presenter and the SDL/OpenGL
host. The atomic GDI compatibility fallback remains a game-only emergency
path: Escape quits there, but Assist shortcuts follow the setting chosen in
the pre-boot launcher.

The pre-boot launcher now also has top-level **Assist Tools** and **Credits**
sections beside Settings. Credits text is supplied by this project rather than
hardcoded in recomp-ui.

The Settings page has a fixed **Restore Defaults** button. After confirmation,
it restores the complete DKC2 launcher configuration: window/display choices,
audio, controller sources/deadzones/bindings, Assist bindings, and the
skip-launcher preference.
The selected ROM, cartridge SRAM, save states, and rewind history are not
deleted or replaced. The restored choices are persisted when Play is pressed.

## Video settings

The launcher defaults to the OpenGL presenter with nearest-neighbor sampling
and the **Raw** screen model. Raw is a byte-exact presentation bypass. The
opt-in **CRT**, **Composite**, and **Trinitron** choices apply the present-time
screen-color lookup table used by PSXRecomp: CRT models a consumer SMPTE-C-like
phosphor gamut, display gamma, luminance, and black floor; the other two retain
the corresponding upstream variants. This model changes color response only;
it does not add scanlines, curvature, a bezel, or persistence blur.

The experimental aspect selector is available in the pre-boot Settings page,
the in-game Settings tab, and the native Mac **View > Aspect Ratio** menu.
Authentic 4:3 remains 256x224, Mac-oriented 16:10 uses 308x224 (26 added source
pixels per side), and 16:9 uses 342x224 (43 per side). Both wide choices
preserve the original center. Pirate Panic's collision-bearing foreground margins use
DKC2's live decompressed WRAM level map to reconstruct exact 8x8 tiles, with
world-keyed history retaining game-authored updates. The adapter accounts for
the game's 256-pixel map/camera origin difference, its rotated column buffer,
its single valid guard metatile at room ends, and the one-frame WRAM/PPU latch
difference that can occur while the camera crosses an 8-pixel row. Terrain
shadow Y unwraps the PPU's tile-aligned origin before restoring the fine phase,
so source prefill and lookup cannot choose opposite 1,024-pixel epochs at an
exact half-period tie. Its cyclic
sky/ocean backdrop repeats the already-rendered native layer. After the exact
terrain source is ready, any enabled Mode-1 BG3 with a physical 64-column
tilemap renders its authentic adjacent columns; this covers the standard
ship-deck rigging in both Pirate Panic and Rattle Battle. Unsafe bounded BG3
staging and title/menu/room tilemaps remain centered instead of showing stale
data. Topsail Trouble is a narrow source-backed exception: its physical BG1
terrain remains wide while the bounded `$6C00` rain BG3 repeats only after
normal PPU rendering. At a hard-left level boundary, the known-layout terrain decoder
reflects only the first few authored tiles into the host-only gutter west of
world X=`$0100`; this common presentation rule covers horizontal, vertical,
square, narrow-vertical, and ship-hold terrain without a room allowlist.
Ship-hold rooms decode their distinct 80-metatile/`$A0`-byte source rows while
their bounded BG3 water scanline repeats into the host margins after normal
PPU rendering. Their exact BG2 `$7000` cabin-wall signature uses the same
post-render repetition with its verified 12-tile/96-pixel screen-space
period. It renders at authentic width so the room's hardware window no longer
leaves a blue seam at the old viewport edge. That deliberate correction is
limited to native X=0-6 and X=249-255; the rest of the 256-column center is
unchanged.
Unknown layouts remain transparent rather than guessing.
The original 4:3 mode is the default.

The widescreen adapter reads DKC2's live gameplay sub-mode before choosing a
terrain-map policy. Proven horizontal stages decode the game's column-major
map, and proven vertical stages decode its row-major map. Bramble Scramble's
sub-mode `$10` uses a distinct 48-metatile/`$60`-byte square layout confirmed
against 954/957 visible BG1 cells. Ordinary wasp-hive sub-mode `$03` calls the
same cartridge square scroller and now exposes that terrain path
experimentally; Parrot Chute Panic retains its separate narrow-row layout.
Ship-hold sub-mode `$02` also uses the rolling row/column DMA path, but its
source map is 80 metatiles wide; Lockjaw's Locker's exact state matched all
957 sampled visible BG1 cells under that decoder. Treating the room's
64-column VRAM ring as a complete static map was the cause of the missing and
unrelated edge strips during movement.
Hornet Hole, Rambi Rumble, and King Zing still need route and per-layer visual
acceptance. Other square rooms and special handlers remain centered until they
have reference-backed reconstruction and route coverage; a 64-column PPU
tilemap alone is not sufficient evidence that a screen is safe to widen.

All three neutral-input attract demos now retain true 16:9 gameplay. Mainbrace
Mayhem uses its existing vertical BG1 terrain reconstruction and repeats the
authentic BG3 cloud/lighting scanline, removing the former 4:3 brightness
seams. Rickety Race uses the established horizontal policy. Parrot Chute Panic
uses the disassembly-confirmed alternate wasp-hive handler: its BG2 terrain is
decoded as a 512-pixel-wide row-major map with 16 metatiles (`$20` bytes) per
row, while its cyclic BG1/BG3 hive artwork repeats after normal PPU rendering.
Representative early/middle/late captures are full width, and a 12,000-frame
run completes two ordered attract cycles with zero sequence errors. Final
normal-speed owner validation remains required.

The common DKC2 object activation/despawn and sprite-render boundaries have
been widened, and Pirate Panic has deterministic composite, per-layer, and OAM
margin evidence. Those widened object bounds activate only after the terrain
source for the frame has been verified. This is not yet a whole-game
widescreen certification:
vertical stages, bosses, bonuses, maps, Mode-7 screens, and special effects
still require explicit route testing. The complete recorded Pirate Panic route
and two late BG1 margin regressions pass deterministically. The newer regression
clears source-map cells proven transparent on every frame while preserving
current dynamic game writes, removing stale deck fragments without flattening
ship details. Horizontal source-page calibration improves the sampled frames
at 12,000 and 12,300, which the owner accepted, but frames 12,900, 13,800, and
15,900 remain open visual defects. Physical 64-column BG3 ship rigging can now
render in the margins after the same exact terrain-readiness gate; synthetic
coverage includes Pirate Panic's and Rattle Battle's shared ship-deck PPU
signature, while normal-speed route acceptance remains separate.
Topsail Trouble's supplied exact state is separately accepted at 308x224: its
isolated BG3 rain now reaches both 26-pixel margins, and the original 256-pixel
center is pixel-identical to the pre-fix render. A fresh-entry moving route is
still required before treating the complete stage as closed.
The private Rambi route additionally retains an 8-pixel horizontal guard and
the tile-aligned vertical epoch correction. Exact frames 6,509, 6,511, and
6,512 no longer produce the previous 1,120-sample blank-margin bursts; final
normal-speed owner validation is still required.
Use
`DKC2_WIDESCREEN=1` for a
one-process developer override without changing `launcher.cfg`.

The private 3,134-frame `bramble-01` route now exercises Bramble Scramble's
entrance, horizontal movement, vertical climb, and late-stage area. Its BG1
terrain reconstructs into both margins, bounded BG2 uses the existing
rendered-scanline repeat, and BG3 remains clamped because the audited composite
does not expose a gap. The route records sprite output in both margins and
finishes with zero sequence/runtime errors, but it ends before the level goal;
entrance-to-goal acceptance and normal-speed owner testing remain open.

Collectible bananas are a separate cartridge subsystem, not ordinary entries
in the common game-sprite table. Their list traversal and clip span receive the
same fail-closed widening, while a banana-only coordinate adapter supplies
OAM's ninth X bit for positions `$0100-$012A`. Without that adapter, a banana
at widescreen X=291 was submitted as X=35 and appeared on the wrong side of
the screen. The private `bg-02` frame-2,582 replay now places both banana tiles
at X=291 in the right margin. A separate native negative-X tile cutoff has
also been extended through the left margin: the full marsh replay now records
banana tiles at X=-43..-1 rather than only X=-14..-1. Both adaptations remain
off in 4:3; other dedicated object/effect renderers still need route-by-route
audit.

The private `bg-01` route confirms why policies must remain screen-specific:
a later forest screen streams its collision terrain to BG2 `$7800`, not BG1
`$7000`. Widescreen reconstruction now matches live `$17B6` to the enabled
BG tilemap base and prefills the selected layer. Deterministic frames 4,500 and
4,800 now contain the missing BG2 terrain without the prior colored BG1 margin
cells. The automated classifier still flags a sparse secondary BG1 margin, so
owner motion testing and screen-specific foreground auditing remain required.
Mudhole Marsh additionally opts its cyclic BG3 `$6C00` forest backdrop into
scanline repetition. This fills the formerly flat-colored margins without
reading unseen BG3 tilemap columns. Other BG3 uses remain conservatively
clamped until audited.

The subsequent `bg-02` vertical-motion recording exposed a separate
engine-level row-association error. DKC2 stages the rolling terrain tilemap one
256-pixel page above camera Y. The active BG1/BG2 terrain shadow now keys live
viewport captures, VRAM uploads, and exact prefill in the same rendered PPU
source-row domain. This follows the live terrain destination and is therefore
not hardcoded to Mudhole Marsh, but screens that do not use the standard
rolling terrain streamer remain intentionally excluded.

The **GDI compatibility** renderer remains selectable and is also used
automatically if OpenGL initialization fails. Screen-color selection is
renderer-independent, so CRT produces the same transformed source pixels on
both presentation paths. **Nearest/Bilinear** controls only how that completed
frame is scaled. Settings persist in `launcher.cfg`; Raw remains the default
unless the user opts in. For repeatable diagnostics, `DKC2_SCREEN=raw`, `crt`,
`composite`, or `trinitron` overrides the saved screen model for one process.

Visible OpenGL gameplay windows request a one-buffer swap interval to reduce
tearing. The accepted status is written with the presentation backend in
`diagnostics/last_run_report.json`; `on` means the graphics driver accepted
the request, while `request-failed` or `unsupported` means it did not. Hidden
automation disables the request so driver pacing cannot block unattended
tests, and GDI synchronization remains managed by the Windows compositor.
This presentation request does not change the emulated 60.098811862 Hz clock.

Save states are named `saves/dkc2s0.sav` through `saves/dkc2s4.sav`; the
overlay presents these as Slots 1–5 and the native Mac Game menu uses Slot 1.
On macOS this relative directory lives under
`~/Library/Application Support/Flat2VR/DKC2Recomp`; portable builds keep it
beside the executable. The first slot still loads the former
`saves/dkc20.sav` name as a compatibility fallback, but all new writes use the
unambiguous names. States are separate from the cartridge SRAM files used for
normal in-game saves.

The launcher and game window use the development title
`DKC2 Recomp Alpha Pre-Release`; the game window appends the measured
presentation rate once per second.
Press `F` to write per-phase main-thread timings to `performance.log` beside
the executable; press it again to stop. The log identifies the active OpenGL
or GDI backend and selected screen model. It measures CPU time spent submitting
presentation work, but neither path currently collects GPU timestamp queries,
so GPU time remains explicitly unavailable instead of being reported as zero.

## Crash reports and support bundles

Both playable hosts maintain `diagnostics/last_run_report.json` beside the
executable. A runtime failure or native Windows exception also creates a
timestamped `diagnostics/diagnostic_bundle_*` folder; Windows exception bundles
contain a minidump. Set `DKC2_DIAGNOSTIC_BUNDLE=1` before one launch to request
the same support folder after a clean exit.

Bundles use a strict allowlist: the JSON report, instructions, an optional
`launcher.cfg`, an optional `performance.log`, and an optional Windows
minidump. They never copy `rom.cfg`, ROM bytes or paths, generated game code,
SRAM, save states, screenshots, or audio captures. Loaded program-module paths
and basic operating-system/hardware information are included, so inspect the
folder before sharing it. See
[`docs/DESKTOP_TESTING.md`](docs/DESKTOP_TESTING.md) for the crash drills and
platform behavior. Mod-aware save isolation remains deferred until a real mod
manifest and loader exist; current save locations are otherwise unchanged.

## First-level route testing

Pirate Panic is the next gameplay correctness target. The desktop host can
record per-frame input with `SNESRECOMP_INPUT_REC`, and the headless host can
replay it with `SNESRECOMP_INPUT_PLAY`. The new private route gate checks that
the replay enters Pirate Panic, stays active, changes the completion flags,
triggers a level-exit transition, and keeps audio unclipped.

Recordings should live in ignored private storage such as `recordings/` or
`private/`. See
[`docs/FIRST_LEVEL_ROUTE_TESTING.md`](docs/FIRST_LEVEL_ROUTE_TESTING.md) for
the exact recording and replay commands. The first captured route reaches the
goal, but the native replay currently exposes an unresolved dispatch at
`$BA:B33F`; Roadmap #2 remains open until that path replays without an
interpreter-cap or unresolved-dispatch diagnostic.

## Static recompilation coverage

The current analysis profile has 3,325 roots across 13 banks. It emits 3,475
exact CPU-mode variants as static C and retains two deliberate original-game
fault variants on LLE. This is compile-time structural closure, not a
percentage of dynamically executed CPU instructions and not a claim that the
shared interpreter can be removed.

The interpreter remains the safe runtime default for an unavailable exact
entry state. Two dormant bugs in the original game also deliberately preserve
their real JSR stack frames and hand control to the interpreter if reached:
both calls enter bytes documented as data/garbage and crash on original
hardware. They are explicit exceptional LLE edges from compiled callers, not
normal game logic and not guessed native implementations.

The generated C remains ignored because it is derived from the user's ROM.
Only source-owned configuration and structural metadata are committed.

### Runtime-selected entry coverage

The structural-closure figures above cover every entry state demanded by the
static graph; they do not prove that every address selected later from mutable
game data was already named as a root. An owner-recorded Version 11 session
exposed that distinction in Swanky's Bonus Bonanza. The process exited cleanly,
but runtime state `$B4:A4CB` reached the interpreter's 2,000,000-instruction
safety cap and appeared interactively as 1-3 FPS.

The source configuration now declares Swanky's states `$B4:A3E0`, `$B4:A475`,
`$B4:A4CB`, `$B4:A5D9`, and `$B4:A665`, plus helper `$B4:A7CA`, as explicit
AOT roots. The shared call bridge also preserves the handler's intentional
non-local return: in 16-bit accumulator mode, `PLA` consumes the paired JSR
frame and the following `RTL` consumes an outer JSL frame. That return must
unwind the compiled caller rather than resume code the game deliberately
skipped. The shared-bridge regression passes 62/62 checks. DKC2 regeneration
and both optimized Release and trace builds now succeed, and the generated
dispatch table contains all six exact entries.

Rolling diagnostic reports now preserve the last 1,024 runtime indirect
dispatch events. `scripts/validate_swanky_run.py` requires a native M0X0
dispatch from the Swanky dispatcher to `$B4:A4CB`, rejects interpreter caps,
missing Swanky AOT entries, the original corrupt sequence, and execution in
SNES MMIO addresses. Its synthetic regression passes and it correctly fails
the original Version 11 artifact. The complete available suite is 52/53; the
only failure is the unchanged supplied-ROM frame-3,309 sprite-reference
mismatch. The owner's fresh normal-speed game-show check remains pending.

Input files record the resolved controller word for each forward emulated
frame. They do not encode host rewind or save-state save/load actions. Fast
forward remains recordable because every forward emulated frame is sampled,
but a route that rewinds or loads a state cannot be reproduced from its
`.input` and starting SRAM alone. Capture focused regression routes without
those actions until the recording format gains an explicit host-action stream.

## Building from source

Prerequisites on Windows:

- CMake and Ninja;
- a C/C++ toolchain (MSVC or MinGW-w64; an installed SDL2 package is optional);
- Python 3.9+; and
- Rust/Cargo for the native whole-program analyzer.

```powershell
git clone --recurse-submodules https://github.com/mstan/DKC2Recomp
cd DKC2Recomp

.\scripts\generate_snesrecomp.ps1 -Rom "C:\private\dkc2.sfc"

$env:Path = "C:\msys64\mingw64\bin;$env:Path"
cmake -S . -B build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DDKC2_BUILD_SNESRECOMP=ON `
  -DSDL2_DIR=C:\msys64\mingw64\lib\cmake\SDL2
cmake --build build-release --target dkc2_snesrecomp_desktop
```

The build can fetch the pinned SDL 2.30.9 source when an installed SDL2 package
is unavailable, so `SDL2_DIR` is optional. Windows also builds the portable
host as `DKC2RecompSDL.exe` with target `dkc2_snesrecomp_sdl`.

Linux and macOS use the portable generator and SDL gameplay target:

```sh
python3 scripts/generate_snesrecomp.py --rom /private/path/dkc2.sfc
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DDKC2_BUILD_SNESRECOMP=ON
cmake --build build-release --target dkc2_snesrecomp_sdl
./build-release/DKC2Recomp /private/path/dkc2.sfc
```

The checked-in `recomp/bank*.cfg` files already contain the active,
revision-validated disassembly names used in generated C and trace logs. A
private WLA symbol overlay can conservatively expand remaining `CODE_...`
names without copying the overlay into the repository:

```powershell
python .\scripts\promote_snesrecomp_symbols.py `
  --cfg-dir .\recomp `
  --symbols .\private\dkc2-yoshifanatic-v1.sym

# Review the dry run, then apply the same validated set.
python .\scripts\promote_snesrecomp_symbols.py `
  --cfg-dir .\recomp `
  --symbols .\private\dkc2-yoshifanatic-v1.sym `
  --apply
```

The tool only accepts an unambiguous `context_CODE_BBXXXX` alias that retains
the original generic identity. It rejects bank mismatches, collisions, and
ambiguous aliases; raw same-address matching is intentionally unsupported
because revision-dependent layout changes can shift dense dispatch tables.
Regenerate after applying so `recomp/funcs.h` and private generated C receive
the new names.

The SDL host is runtime-tested on Windows. Linux and macOS are not called
supported releases until their native acceptance matrices pass. See
[`docs/CROSS_PLATFORM.md`](docs/CROSS_PLATFORM.md) for prerequisites, exact
commands, implemented features, and the remaining platform gates.

Project-owned desktop and headless code is compiled for speed in Release
builds (`-O3` with GCC/Clang and `/O2`, MSVC's maximum speed preset, with
MSVC). To embed a private Windows `.ico` without adding it to Git, configure
with `-DDKC2_DESKTOP_ICON="C:\private\dkc2.ico"`.

Create the next source-clean, user-testable Windows snapshot with:

```powershell
.\scripts\create_windows_version.ps1
```

The first run creates `versions\Version 01`, the next creates
`versions\Version 02`, and so on. Existing numbered folders are never deleted
or overwritten. Each folder contains both playable Windows hosts, the required
launcher assets, documentation, and a `VERSION.txt` provenance/hash manifest.
Normal packaging refuses uncommitted source; `-AllowDirty` is available only
for an explicitly marked development snapshot.
The packager allowlists the documented launcher cover and refuses ROM, save,
generated, diagnostic, configuration, screenshot, and audio artifacts. The
compiler continues to reuse its normal build tree; only testable handoffs are
duplicated, avoiding multi-gigabyte source/build copies.

For private play testing, create a second copy outside the repository after
the normal version has been packaged:

```powershell
.\scripts\create_personal_test_version.ps1 `
  -PublicVersionDirectory "versions\Version 05" `
  -RomPath "C:\private\dkc2.smc" `
  -SavesDirectory "build-snesrecomp\Release\saves" `
  -LauncherConfigPath "build-snesrecomp\Release\launcher.cfg"
```

The helper verifies the exact supported ROM hash and, by default, creates
`..\DKC2 Personal Test Builds\Version NN` with a relative `rom.cfg`, the ROM,
the selected saves, and optional launcher settings. It refuses destinations
inside this repository and refuses to overwrite an existing private version.
These personal folders must never be committed, uploaded, or attached to a
release.

For the current widescreen investigation, a private diagnostic version can be
created directly without making a redundant public package:

```powershell
.\scripts\create_private_diagnostic_version.ps1 `
  -RomPath "C:\private\dkc2.smc" `
  -Sequence 10
```

It assembles the normal and trace executables, verified ROM, saves, existing
private recordings, launcher settings, control bindings, and capture helpers
outside Git. Its bundled `Record-Pirate-Panic.ps1` preserves starting SRAM,
refuses reused evidence names, and requires fresh performance, tier-2, and
last-run reports; `Diagnose-Frame.ps1` then creates an isolated same-frame
layer/object report. The focused Swanky validator is packaged under `tools/`.
See
[`docs/BUILD_HYGIENE.md`](docs/BUILD_HYGIENE.md) and the package's
`TESTING_README.md`.

Use `build-snesrecomp/` as the single routine Windows compiler workspace and
launch manual-test builds only from `versions/Version NN/`. The older
`build*` folders are explained and classified in
[`docs/BUILD_HYGIENE.md`](docs/BUILD_HYGIENE.md); they are not additional source
versions.

## Repository layout

- `recomp/` — source-owned CFG and structural metadata.
- `runner/` — DKC2 host adapters, input, presentation, rewind, and ROM checks.
- `snesrecomp/` — pinned shared recompiler and SNES runtime. The submodule
  currently uses the `Nicktendonick/snesrecomp` integration fork so its
  DKC2-specific commits are fetchable; `mstan/snesrecomp` remains the
  authoritative upstream.
- `recomp-ui/` — pinned shared Dear ImGui launcher. The submodule currently
  uses the `Nicktendonick/recomp-ui` integration fork so its configurable
  DKC2 binding ABI is fetchable; `mstan/recomp-ui` remains authoritative.
- `docs/RECONCILIATION.md` — provenance and disposition of the pre-upstream
  working tree.
- `docs/CROSS_PLATFORM.md` — SDL host builds and native acceptance gates.
- `docs/BUILD_HYGIENE.md` — canonical build, output, and test-version policy.
- `docs/WIDESCREEN_DIAGNOSTICS.md` — deterministic layer/object evidence
  bundles and the terrain-first widescreen debugging workflow.
- `scripts/` — regeneration, testing, packaging, and launch helpers.
- `generated/`, `private/`, and build directories — ignored local artifacts.

## Acknowledgements

- [H4v0c21's DKC2 disassembly](https://github.com/H4v0c21/DKC2-disassembly)
  provides the independently verified symbols and structural boundaries used
  during analysis. No disassembly or ROM-derived assets are redistributed.
- [snesrecomp](https://github.com/mstan/snesrecomp) provides the static
  recompiler, interpreter fallback, and shared SNES runtime.
- [recomp-ui](https://github.com/mstan/recomp-ui) provides the shared Dear
  ImGui launcher.
- The optional screen-color LUT is adapted from
  [PSXRecomp](https://github.com/mstan/psxrecomp) at the pinned revision and
  implemented by the shared SNESRecomp color-LUT module. It retains its
  upstream license and JRickey/gba-recomp attribution under
  `third_party/psxrecomp_color_lut/` and in the submodule's matching
  `third_party/` notice directory.
- The SNES hardware implementation derives from LakeSnes, with additional
  algorithms credited to Snes9x in the relevant source files.

## License

Project-owned source is available under the [MIT License](LICENSE). Vendored
dependencies and submodules retain their own licenses. In particular, the
PSXRecomp-derived screen-color component is PolyForm Noncommercial 1.0.0 with
an MIT/Apache-2.0 color-science lineage; the complete notices are in
`third_party/psxrecomp_color_lut/` and it is not relicensed by the root MIT
license. Nintendo and Rare
own their respective game content and trademarks; no license in this
repository grants rights to that content.
