# DKC2Recomp
> This recompilation is a byproduct of developing
> [snesrecomp](https://github.com/mstan/snesrecomp): the games are the proving
> ground, while the reusable framework is the larger goal. This is a community
> recompilation, not an official port. Please report any reproducible gameplay,
> video, or audio regressions.

Static recompilation of *Donkey Kong Country 2: Diddy's Kong Quest* for SNES
into native desktop applications, using the `snesrecomp` framework. Windows
and Apple-silicon macOS builds are available. The native Mac application
includes an AppKit menu, Dock icon, platform user-data directory, and
Mac-specific exact-rate frame pacing. It is ad-hoc signed; notarization remains
open.

The 65816 game program is translated to native C where analysis can prove an
exact entry state. The current profile emits 3,475 exact AOT variants and keeps
two deliberate original-game fault variants on the shared 65816 interpreter.
That interpreter remains available as a correctness and exceptional-path
fallback. SNES hardware outside the main CPUâ€”the PPU,
SPC700/S-DSP, DMA/HDMA, controllers, and cartridge mappingâ€”is modeled by the
shared runtime.

## Optional Donkey and Kiddy characters

Open the in-game pause menu with **Escape**, then choose **Characters**.
Each original character slot can use Donkey Kong or Kiddy Kong; **Donkey +
Kiddy** selects both, and **Original pair** restores Diddy and Dixie. The
game remembers your choices between runs.
Animal riders use mount-specific seated/hanging poses and attachment points.
Hold **Down + Y** on the ground for Donkey's hand slap or Kiddy's body slam.
Barrel carrying and throwing use each character's original style and timing.
Neither replacement inherits Dixie's helicopter flight. In single-player or
classic TEAM, **Select** plays a paired tag handoff when swapping Kongs.
Donkey/Kiddy team pickup and throws
use synchronized poses and shoulder/hand placement (private pack v5).
Kiddy uses a seated recovery after a missed throw; regenerate earlier packs
if he repeatedly snaps between hurt poses while waiting for the leader.
Other movement/collision still uses
DKC2's engine; this is not a complete DKC1/DKC3 mechanics transplant.
Replacement animations freeze during Start or Escape pause. Re-import older
packs for these changes.

Create the private character pack from your own Project Kongs checkout using
[the import instructions](docs/PROJECT_KONGS.md), then select **Load character
pack...**. Game art is never included in this source tree or the app bundle.

## Quick start

### Windows release

1. Download `DKC2Recomp-v0.0.8-Windows-x64.zip` from
   [Releases](../../releases) and extract the complete archive.
2. Run `DKC2Recomp.exe`.
3. In the Dear ImGui launcher, select your own legally obtained North American
   v1.0 ROM and choose **Play**.

`DKC2RecompSDL.exe` is the alternate SDL host. Keep the bundled DLLs and
`assets` directory beside both executables. The archive also includes the
character importer and MSU-1 setup guides; supply those packs separately.

The selected external path is remembered in `rom.cfg` beside the executable.
The ROM is never copied into the release. Saves are written to
`saves/save.srm`, with the previous clean save retained as `save.srm.bak`.

### Windows menus and controls

Both Windows hosts include a dark **Game / View / Input** dropdown bar.
Use Game for pause/resume and Quick Save/Load, View for aspect, scaling,
level edges, screen filters and fullscreen, and Input to choose each player's
Keyboard/Gamepad source or TEAM co-op policy. Changes apply while playing
and persist in `launcher.cfg`. **Alt+Enter** toggles fullscreen; **Escape**
leaves fullscreen when the overlay is closed, otherwise pauses/resumes.
The bar returns on leaving fullscreen.

Default keyboard: arrows move; **Z** jumps (SNES B), **A** runs/rolls (Y),
**X** is A, **S** is X, **Q/W** are L/R, **Enter** is Start and **right Shift**
is Select. Player 1 defaults to keyboard, Player 2 to the first connected
gamepad; select Gamepad for both players to assign two connected pads in order.
**Start+Back** opens/closes the pause overlay without sending the chord to the game.
Bindings and deadzones are editable in the launcher or in-game overlay.
GDI supports both the native menus and the full ImGui pause/settings overlay;
Reconstruct is available in SDL and native Windows OpenGL. GDI exposes its
supported Nearest/Bilinear samplers. The menus reflect compiled shader capability.

### CRT television, characters and MSU-1 music

Open **Escape > Settings > Display > CRT television**. Choose Living room,
Studio monitor, Soft or Custom; adjust Scanlines, Sharpness, Phosphor mask,
Mask strength, Glow, Halation and Curvature. Phosphor colors are separate.
Native Windows OpenGL and SDL use identical CRT passes. Tube mode supplies
its own scaling; Flat panel retains Reconstruct. GDI uses flat display.

The **Characters** tab offers Original, Donkey Kong and Kiddy Kong for each
slot, **Donkey + Kiddy**, and **Original pair**. Load an external character pack
as described in [Project Kongs](docs/PROJECT_KONGS.md). Choices and pack paths
persist in `kongs.cfg`. Original slot collision sizes and some movement rules
remain; this is not a full DKC1/DKC3 move-set transplant.

Under **Settings > Audio**, choose an extracted **MSU-1 music folder**, enable
**Replacement music (MSU-1)** and adjust **Music volume**. Standard
`dkc2_msu1-N.pcm` tracks replace music while original sound effects remain.
Missing tracks fall back to SNES music. The external folder preference is saved
in `msu1.cfg`. No patched ROM or `.msu` marker is needed. See
[MSU-1 audio](docs/MSU1_AUDIO.md) for state/rewind behavior and validation.

### Unlocking every level in a save

`scripts/dkc2_unlock_levels.py` marks every real level of a save file as
cleared, which opens every path on the world maps and every world whose
boss it clears, and can open the Lost World and grant coins:

```bash
python3 scripts/dkc2_unlock_levels.py --save "$HOME/Library/Application Support/Flat2VR/DKC2Recomp/saves/save.srm" --rom /private/path/dkc2.sfc --file all --lost-world --kremkoins 75 --banana-coins 99 --snapshot "$HOME/Library/Application Support/Flat2VR/DKC2Recomp/saves/dkc2s0.sav"
```

It backs each file up beside itself first (`.before-unlock`, numbered
when one exists). Quit the app before running it, since the app writes
its own copy of the SRAM back on exit, and start from the file select
afterwards. `--lost-world` marks every Klubba kiosk as paid, so Klubba
lets the Kongs through without a toll, and records the five Lost World
levels as beaten, which opens Krocodile Kore. `--kremkoins` sets the
Kremkoin count the kiosks charge (15 each; the game holds 75). Banana
Coins are not stored in the save file, the game zeroes them whenever it
loads a file, so `--banana-coins` sets them only in the quick save named
by `--snapshot`. The percentage is recounted by the game at its next
save. `--no-levels` leaves the cleared flags alone, and `--repair`
recomputes the header of a file the game shows as empty because its sums
disagree while its data is intact.

### Native macOS release

1. Download `DKC2Recomp-v0.0.6-macOS-arm64.zip` from
   [Releases](../../releases) and extract it.
2. Open `DKC2Recomp.app` and select your own legally obtained North American
   v1.0 ROM. The ROM remains outside the application bundle.

The v0.0.6 Mac archive is an ad-hoc-signed Apple-silicon build and is not
notarized. It adds the CRT television display, the Retina pause-menu fix, and
the optional Donkey/Kiddy character slots. The v0.0.8 release carries this
Mac archive unchanged alongside the new Windows build; the Mac binary does
not contain the new simultaneous co-op or MSU-1 support. If Gatekeeper quarantines the
downloaded archive, open the app from Finder with **Control-click > Open** and
confirm once.

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
| Toggle performance log | F | â€” |
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
Player 2. The Windows host now uses SDL GameController for USB and Bluetooth
controllers. Select the actual controller for each player in the launcher's
controller list; device choices, sources, deadzones and bindings persist in
`launcher.cfg`. Both controllers can drive the two SNES ports independently.
Controllers connected after game launch are detected automatically; an
explicitly selected missing controller stays neutral until reconnected.
The bundled mapping also recognizes the 8BitDo SN30 Pro in Bluetooth DInput
mode, which the older SDL database omitted. Keyboard-mapping software is
unnecessary for the detected Xbox One S and SN30 Pro devices. In the pause
menu, each player's Controls tab shows its device and captures that player's
controller bindings. Physical button/rumble testing remains owner acceptance.

### Two-player co-op

Selecting "2 PLAYER TEAM" in the game's own menu used to alternate control:
one Kong was player-controlled while the other followed by AI, and control
passed only when a Kong was hit or swapped. The port now makes TEAM mode
simultaneous by default: controller 1 always drives the first Kong slot and
controller 2 always drives the second, so both players play at the same time,
each with their own Kong. The second Kong stands by (and auto-catches-up when
separated) until player 2 first touches their controller, then stays fully
playable, including after landing. Both players use normal bright colors and
can attack enemies; Player 2's roll, stomp and bounce are covered by a private
first-level replay, along with contact damage and a Player 1 attack regression.
Roll/stomp recovery restores walking and jumping. Held barrels follow and
launch from their owner, including throws across different platform heights.
An empty-handed partner's run/pick and jump inputs do not release the owner's
item. Ground and airborne item ownership also survive save/load; the private
`supplied_rom_coop_item_input` replay covers both player slots.
Either player can finish the level by landing on the goal. The Kong that
hits it runs the native ending sequence, with fixed controller assignments
preserved. With `DKC2_ROM` and an external `DKC2_COOP_GOAL_STATE` configured,
`supplied_rom_coop_goal` replays the reported goal regression for both players.
Hold **Select** (keyboard **Right Shift** by default) for the configured hold (**two seconds** by default) to
bring your Kong beside your teammate in simultaneous TEAM mode. Release it
before using it again. If both players hold it, the camera follower joins the
leader once. Select is reserved for this catch-up action in that
mode; classic TEAM and single-player retain the normal tag action. Rescue
requires two living players and a grounded or swimming destination. It can pull
the requesting Kong off a rope, including an offscreen rope or rope junction,
and works into, out of and within water. Swimmers turn to face their movement.
An on-foot partner can also recover beside a grounded Rambi rider, including
a live Kong stuck waiting below the level. The rider and Rambi stay in place.
Riders themselves, other mounts, barrels and scripted actions remain excluded; pause/loading resets
the hold. Existing saves with a stale lost flag on a confirmed live Kong recover
automatically. To replay the reported rope rescue, run
`python scripts/check_coop_rescue.py --runner <headless-runner> --rom <external-ROM> --state <external-save>`.
Use `--water-state <external-save>` for the reported swimming regression.
Use `--mount-state <external-save>` for the reported Rambi/waiting regression.
Either player can walk up to their partner and press **SNES A** (keyboard
**X** by default) to carry them. Pickup requires both Kongs on foot and within
24 game pixels horizontally and 16 vertically. Press A again to put them down,
or Y to throw them. The carrier keeps their own controls; both players regain
independent movement after release. Diddy/Dixie and Donkey/Kiddy checks cover
near/far pickup, carrying, drop/throw and save/load.
Each player can grab and climb ropes independently, with climbing animations
in both directions. Single/double-rope junctions complete normally, including
when loading a save already stuck at the end of a transition. The Topsail Trouble
regression covers Player 2 attaching, both players climbing in opposite
directions, save/load and jumping to the neighboring rope. To repeat it with
the private reported snapshot, configure `DKC2_COOP_ROPE_STATE` with its
external path and run the `supplied_rom_coop_ropes` CTest check. Adding
`DKC2_COOP_ROPE_JUNCTION_STATE` enables the second reported checkpoint's
double-rope animations, side exits and transition save/load checks.
It also checks crossing the entire net and reversing direction to reach both
outer ropes without repeating the turning animation in place.
A hurt Kong completes its departure and waits for a DK barrel; pressing its
controls or loading a save cannot revive it. The survivor continues without
the original TEAM turn-taking prompt. A DK barrel restores independent control.
If either player falls into a pit, a live teammate keeps playing with their
own controller. Level loss occurs when the last surviving player falls.
Private checks cover both Kong roles after a leader swap and save/load across
loss, revival and a second loss.
Either player can mount Rambi; the rider becomes the camera leader and keeps
their own controller. The other Kong retains on-foot movement. Private checks
cover riding, jumping, dismount/remount, passing Rambi from P1 to P2 and loading
while mounted. Death handoff keeps the survivor in place and resumes enemies.
Both players can collect banana trails while the other rides Rambi; overlapping
hitboxes and revisiting collected bananas after save/load award each only once.
Both Kongs can move into the visible side areas in 16:10 and 16:9, with the
level walls retained. In simultaneous TEAM, the camera targets the midpoint
of both playable Kongs, with native scrolling limits and framing retained.
Transport-cannon entry and firing carry both living players together when
either player enters, including the current follower. Manual rotating barrels
also take both players inside; either player can press jump to fire both.
After the native launch lock ends, either player can steer left/right in
the air independently. Both use native barrel-shot physics for equal upward
force and return to normal walking animation after landing. Steering and normal
movement retain separate controller
ownership. Bonus-room
entrance restores both live players on the arrival platform without holding
Select. A fixed tail-hook attaches the Kong that touched it; both players
can share the hook and jump off independently, including the last occupant's
first jump after the other player leaves. Kleever's dropping hooks also support
both players in either grab order, with separate single-press departures.
Throwing a carried Kong into the
reported elevated Gangplank barrel completes entry for both players and
restores control on the upper platform.
The on-foot player can also use hangers while the teammate rides Rattly.
Rattly's superjump can leave the partner below the camera without losing them;
hold that partner's Select for the configured duration to teleport beside the rider,
including while the snake is airborne. Actual pit deaths still require rescue
from a DK barrel. A Rattly rider falling into a pit releases the mount and
restores its palette ownership, leaving the survivor on foot.
Both living players hang from Squawks together. If the controlling Kong is
hit, flight passes to the surviving player's controller, including after save/load.
Scripted boss intros keep the partner in native wait/follow handling until
normal play resumes; the Kleever intro no longer lets the partner sink into lava.
Full-game death/respawn, other animal variants and special throwables remain unverified.
The "2P Team mode" choice in the overlay's Settings
page (or
`CoopMode` in `launcher.cfg`, or `DKC2_COOP=simultaneous|classic`) restores
the classic alternating behavior; 1 PLAYER and "2 PLAYER CONTEST" are never
affected.

## In-game overlay and Assist Tools

Press **Escape** during gameplay to pause on a completed frame boundary and
open the Dear ImGui overlay. In a fullscreen SDL/Mac game window, the first
Escape returns to windowed mode without also opening the overlay; Escape then
retains its normal overlay behavior. The SDL host also accepts the controller
Guide button; Start+Back is the portable fallback. The overlay provides Resume,
Settings, Controls, Assist Tools / Cheats, Credits, and Quit.
Gameplay input and audio are paused while it is open. Its first placement is
centered from ImGui's logical display size, including on Retina/high-DPI
windows, and the title bar can then be dragged to reposition it.

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

The overlay is available in the Windows OpenGL and GDI presenters and the
SDL/OpenGL host. GDI draws ImGui through an SDL software renderer into the
completed game buffer before presentation, so Escape and Game > Settings /
Controls work with the compatibility renderer too.

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
and the one-frame WRAM/PPU latch difference that can occur while the camera
crosses an 8-pixel row. Terrain shadow Y unwraps the PPU's tile-aligned
origin before restoring the fine phase, and the world-keyed store retains
1,024 tile rows so tall vertical rooms such as Topsail Trouble keep their
second epoch.

Every other margin decision is a property of the live PPU geometry, not a
level list. Each enabled bounded background (a 32-column tilemap, or a
64-column allocation whose extension page is another enabled layer's map),
whether enabled for the whole frame or only inside an HDMA band, repeats
its rendered native scanline, which is exactly what a wider PPU
would draw from a map that wraps at 256 pixels. A bounded backdrop kept in a
64-column allocation continues each line at the period its own rendered
pixels prove, so a 96-pixel cabin wall does not restart every 256 pixels,
and rebuilds its stale endpoint pixels from that period; 32-column maps keep
their exact hardware wrap. A 64-column BG3 with pages of its own
renders its authentic adjacent columns; the ship-deck rigging, which the
cartridge streams into that ring with no lead, is instead decoded from its
ROM map into a third world-keyed store after the decode has reproduced every
fully uploaded native column of the current frame. The lava geyser steam of
Red-Hot Ride, a bounded 32-column BG3 the cartridge draws only for geysers
inside its own view, is decoded from the stage's ROM geyser list and
animation tables into the same store, so a geyser beside the view keeps its
column and the map's 256-pixel wrap no longer puts steam over solid rock.
A static 64-column plane that the cartridge never streams (the lava
stages' foreground rocks and far spikes) continues into the margins as its
own hardware wrap once its content proves it is authored to wrap.
Rolling BG1/BG2 terrain layers are classified per HDMA scanline band, read
from the cartridge's own HDMA tables before drawing: a band at the terrain
phase is served from the one world-keyed terrain store (the second physical
layer reads it through an alias view), and any other band repeats its
rendered line, so the lava stages' BG1/BG2 exchanges need no special case.
At a hard level wall the 4:3 edge stays pinned at the wall, so nothing past
the level is ever shown, and the inward view is released gradually over eight
margins of camera travel (edge policy `glide`, the default). Three
alternatives are selectable from the pause menu's Settings page ("Level
edge"), the `WidescreenEdge` key in `launcher.cfg`, or the
`DKC2_WIDESCREEN_EDGE` environment variable: `reflect` keeps the view locked
to the game's camera and mirrors the nearest authored columns into the
unauthored strip; `bars` keeps the view locked and leaves that strip black;
`shift` is the earlier inward clamp, whose view stands still for the first
43 pixels of camera motion away from a wall while the HUD slides with it. The presented native viewport is exact under
every policy.
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
center is pixel-identical to the pre-fix render. Its later lower-camera Quick
Save is also accepted: exact BG1 terrain now fills both margins through tile
row 540, and the native center is pixel-identical to the 4:3 oracle. A
fresh-entry moving route is still required before treating the complete stage
as closed.
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

Both desktop hosts offer experimental **Reconstruct** scaling. In the regular
`DKC2Recomp.exe`, select OpenGL and restart if currently using GDI; all five
reconstruction levels and the edge strength, softness, and smooth shading
sliders then apply live. `DKC2RecompSDL.exe` supports the same shader. The
shader source is shared, and all four screen-color presets compose with it.
The shader keeps pixel edges sharp at any
fractional scale, decodes the checkerboard and line dithers SNES artists
used for mid-tones, and rebuilds diagonal edges of the pre-rendered art
with an xBR-style corner test evaluated per output pixel, then softens
the result: wider transition bands and gradient shading where neighboring
colors are close. Its mode combo adds the stages one at a time and sliders
scale the edge blend, the softness, and the shading;
`DKC2_UPSCALER=nearest|bilinear|reconstruct` overrides the saved choice in SDL.

The Settings page's **Display** combo switches the Mac app from the flat
panel presentation to an optional **CRT television** simulation
(`docs/CRT_DISPLAY_PLAN.md` is its design). It is not a scanline overlay:
every source line becomes an electron-beam profile whose width grows with
its brightness, so bright lines widen and merge while dark lines stay thin,
and the beam is normalised so the picture keeps its brightness. A fine
aperture-grille phosphor mask (three panel pixels per triad, below the eye's
resolving limit at arm's length), a soft glow and halation from blurred
copies, a gently curved tube face with rounded corners, and a dither finish
the look. "Living room" is the default preset; "Studio monitor" is sharper
and flat, "Soft" is wider and glowier, and the sliders (scanlines,
sharpness, mask, glow, halation, curvature) make a custom tube. The
upscaler is bypassed while the tube is on, and the tube fades to the flat
image in small windows where its lines cannot be drawn.
`DKC2_DISPLAY=flat|crt`, `DKC2_CRT_PRESET=living-room|studio|soft`, and
the `DKC2_CRT_*` sliders override the saved choice. The frame after the
phosphor-color model is the tube's input, so those models still apply; the
native frame, its hashes, and save states are untouched.

Visible OpenGL gameplay windows on Windows request a one-buffer swap interval
to reduce tearing. The accepted status is written with the presentation
backend in `diagnostics/last_run_report.json`; `on` means the graphics driver
accepted the request, while `request-failed` or `unsupported` means it did
not. Hidden automation disables the request so driver pacing cannot block
unattended tests, and GDI synchronization remains managed by the Windows
compositor. The Mac app instead paces each frame on the display's own refresh
tick and keeps the audio in step with dynamic rate control, so every frame is
shown for exactly one refresh on a 60-Hz or ProMotion display;
`DKC2_DISPLAY_LOCK=0` returns to the host clock and `DKC2_PACING_LOG=<file>`
records the cadence for `scripts/analyze_pacing_log.py`. Neither changes the
emulated 60.098811862 Hz clock.

Save states are named `saves/dkc2s0.sav` through `saves/dkc2s4.sav`; the
overlay presents these as Slots 1â€“5 and the native Mac Game menu uses Slot 1.
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

- `recomp/` â€” source-owned CFG and structural metadata.
- `runner/` â€” DKC2 host adapters, input, presentation, rewind, and ROM checks.
- `snesrecomp/` â€” pinned shared recompiler and SNES runtime. The submodule
  currently uses the `Nicktendonick/snesrecomp` integration fork so its
  DKC2-specific commits are fetchable; `mstan/snesrecomp` remains the
  authoritative upstream.
- `recomp-ui/` â€” pinned shared Dear ImGui launcher. The submodule currently
  uses the `Nicktendonick/recomp-ui` integration fork so its configurable
  DKC2 binding ABI is fetchable; `mstan/recomp-ui` remains authoritative.
- `docs/RECONCILIATION.md` â€” provenance and disposition of the pre-upstream
  working tree.
- `docs/CROSS_PLATFORM.md` â€” SDL host builds and native acceptance gates.
- `docs/BUILD_HYGIENE.md` â€” canonical build, output, and test-version policy.
- `docs/WIDESCREEN_DIAGNOSTICS.md` â€” deterministic layer/object evidence
  bundles and the terrain-first widescreen debugging workflow.
- `scripts/` â€” regeneration, testing, packaging, and launch helpers.
- `generated/`, `private/`, and build directories â€” ignored local artifacts.

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


Display and controller parity with DKC3 is recorded in
[DKC3_FEATURE_PARITY.md](docs/DKC3_FEATURE_PARITY.md). Both hosts now expose
21:9 (446x224) alongside 4:3, 16:10, and 16:9. Stomp rumble and separate P1/P2
test pulses are in Escape > Settings, with controller routing matching input
assignment (keyboard P1 plus gamepad P2 uses that gamepad for P2 feedback).
Physical rumble sensation and full-game ultrawide coverage require further play.


Shared manual barrel aiming (2026-10-21): when both players are inside, either
controller can rotate and fire the barrel. Opposite directions cancel per axis;
matching directions retain the native rotation speed. After firing, air controls
remain separate. The reported level-$28 barrel passes both entry orders and
save/reload checks. Optional private CMake test: DKC2_ROM plus
DKC2_COOP_BARREL_ROTATION_STATE (external snapshot).


### Compiled co-op gameplay tuning

Edit [config/coop_tuning.cfg](config/coop_tuning.cfg), then rebuild with VS Code's
CMake Build command (or `cmake --build build-game-vscode --config Release`).
These values are compiled into the executable; restarting alone does not apply
changes. The compiler rejects values outside the documented ranges.

| Setting | Default | Meaning |
| --- | --- | --- |
| DKC2_COOP_SELECT_HOLD_MS | 2000 | Select hold in milliseconds before teleport; rounded up to console frames |
| DKC2_COOP_STOMP_HORIZONTAL_PX | 32 | Maximum horizontal separation between simultaneous stompers |
| DKC2_COOP_STOMP_VERTICAL_PX | 32 | Maximum vertical separation between simultaneous stompers |

When a real enemy stomp is accepted, a nearby living teammate who is also
airborne and descending above that enemy gets the same bounce impulse. This
distance window forgives slightly staggered landings. Each player retains
their own position and horizontal movement. Grounded, rising, distant,
lost, mounted and scripted players keep normal behavior. Shared bouncing does
not grant invincibility or teleport either player. The reported level-$28
checkpoint passes both first-stomper orders, separate steering, mid-bounce
reload and both players reaching the next platform. Private checks assume the
default tuning; configure DKC2_ROM and DKC2_COOP_SHARED_STOMP_STATE externally.


### Independent enemy pushes and actor poles

Enemy knockback now keeps a controlled partner independent instead of running
the original AI follower's automatic regroup/pickup. Intentional team pickup
still works from either controller. The reported pole supports both players
in either grab order, separate up/down movement, native height limits and
single-press jump-off without immediately regrabbing. Mid-push and mid-jump
save/load retain those behaviors. This actor-pole policy currently covers
regular type $2D8 states $05/$06; other pole families and physical controller acceptance remain
unverified. Optional private CMake snapshots: DKC2_COOP_ENEMY_PUSH_STATE and
DKC2_COOP_POLE_STATE, together with external DKC2_ROM.

Pole climbing also advances each player's native up/down animation and returns
to the native idle pose when stopped, including the inactive Kong.

The two-player midpoint camera also includes actor-pole climbers, with either
one or both players on a pole. Native level camera limits still apply.

Enemy bounce state $16 contributes to the two-player midpoint camera too,
whether a stomp launches one player or both.

A living player hanging on an actor pole now survives their teammate's pit
death, keeps their own controls, and can continue after saving/loading.

A DK-barrel return finishes at the living teammate's current position with
entrance momentum cleared, preventing an idle player from sliding into a pit.

Shared barrel launches also copy native gravity and terminal fall speed into
each player's physics work. Old grounded saves restore the player's own normal
fall-speed limit rather than retaining the follower's floating descent.

Mounted bonus returns preserve the native animal/barrel arrival until the
rider lands; the dormant partner then regains independent control on safe
ground beside the rider. Verified with the reported Rambi bonus timeout.

Animal-transformation sections keep the inactive Kong hidden. Each accepted
surviving hit switches the animal to the other controller while native health,
DK-barrel healing and final loss remain intact. Ordinary mounts still support
the independently controlled on-foot partner.

A teammate entering water no longer drops the other player's held beetle or
other grabbable. The carrier's own water-entry drop remains native.

Thorn contact in simultaneous co-op keeps the cartridge's hurt animation,
death arc and survivor handoff for the player who touched the hazard. Instant
pit removal still preserves an eligible teammate. Copied-save acceptance:
`scripts/check_coop_thorns.py --runner <headless> --rom <external-rom> --state <external-save>`.

Shared enemy stomps also accept descending barrel-flight players, keeping the
compiled proximity thresholds and excluding rising or grounded teammates.
Private copied-save check: `scripts/check_coop_barrel_stomp.py` with external
`--runner`, `--rom` and `--state` paths.

Kudgel's ground shake checks both co-op players independently. Grounded
players receive the native stun and recover normally; jumping players avoid
it. Private copied-save acceptance: `scripts/check_coop_ground_shake.py`
with external `--runner`, `--rom` and `--state` paths.

Honey-floor sticking belongs to the player who touches it. Either Kong can
jump free independently, including when both stand on honey; the dry teammate
retains normal movement. Private acceptance: `scripts/check_coop_honey.py`
with external `--runner`, `--rom` and `--state` paths.

Sticky-wall contact also belongs to the touching player. Both Kongs can
attach to the same wall and jump away independently. Private acceptance:
`scripts/check_coop_honey_wall.py` with external `--runner`, `--rom` and
`--state` paths.

The co-op midpoint camera includes honey-wall attachment, wall jumps and
sliding, plus sticky-floor movement/carry states. Live players continue to
contribute their positions while using honey terrain.

### Developer shortcuts
`config/developer.cfg` is compiled into the game. `DKC2_DEVELOPER_ENABLED=1`
enables developer tools; set it to `0` and rebuild to disable them. With the
default `DKC2_DEVELOPER_BEAT_LEVEL_KEY=8`, press **F8** during gameplay
to complete the current level, boss fight or bonus room. Normal levels and
bosses return to the world map; bonuses credit their reward and return to
the parent level, preserving the animal form. Use `0` to disable this shortcut,
or `1..12` to choose another function key, then rebuild. Menus, maps, death sequences
and an already-running exit reject the command. Rewards are credited once.
Special final-boss ending cinematics have not been verified. Windows and SDL hosts use
the same compiled policy; holding the key does not repeatedly trigger it.

Team throws into hangers now release the thrower's movement once the passenger
is attached. The thrower can walk or fall normally while the other player
continues hanging; saved throw waits recover on load.

Select rescue also works while attached to sticky walls or stuck on honey.
Teleporting to a teammate on a sticky wall shares that wall attachment;
teleporting from the wall to the floor restores normal movement.

A living teammate on a sticky wall or honey floor now prevents a pit fall
from ending the level. Either fallen player is marked lost consistently;
revival uses a DK barrel rather than Select teleport.

A Squitter rider's pit death now releases the spider mount before control
passes to the survivor, preserving character palettes and normal movement.
The fallen rider needs a DK barrel to revive.

With both players alive, the co-op camera centers its view between them,
including animal riders and hanging players. Native level boundaries and
scroll smoothing still apply. Scripted/death sequences keep native framing.

Pole jump-off suppression now expires once the player leaves contact, so a
later grab or a pole reusing the same sprite slot remains available. A revived
active player also clears a stale lost flag after a new life; dead waiting
teammates still require a DK barrel.

Coaster enemy stomps bounce the rider upward, including a nearby descending
teammate within the configured shared-stomp threshold. Leaving an old cart
preserves forward momentum and each player's own movement context. A missed
track gap loses the fallen rider; a safe teammate continues to the next cart.

A lone coaster survivor can board the next cart through the native descending
foot contact. Accepted contact finishes seating immediately; the lost teammate
stays lost and native airborne steering remains available.

Paired coaster jumps allow a descending partner within 32 horizontal pixels
of the empty seat and from 32 pixels above to 24 below to join a teammate who
has landed. Own-seat jumps stay independent; distant fallers stay lost.

Shared stomp bounce also applies after coaster riders detach from a cart.
Nearby descending riders retain native coaster bounce and can land on the
next cart; compiled stomp distance settings still control eligibility.

An accepted cannon entry releases the departed coaster so an early jump
cannot be pulled back into its pit. Native cannon shooting and destination
cart landing remain available; genuinely missed jumps still lose a life.

Coaster riders remain attached during the scripted track exit, allowing the
normal transition to the goal area and completion back to the map.

Ceiling-thorn damage follows the player who touches it, including an inactive
co-op player. Native hurt/death and survivor handoff remain intact.

Shared mode-02 karts keep both Kongs seated and jump as one vehicle. The first
player to board controls Jump; the passenger cannot jump out or trigger it.
Ordinary mode-01 coasters retain separate rider jumps.

Hook contacts accept horizontal team throws as well as upward throws in
simultaneous co-op. Hooks with camera-framing parameters support sharing
while retaining their native framing. Each occupant jumps off independently.

Hook grabs also restore interaction eligibility after a team throw, including
older hanging saves. A player hanging on another hook cannot block new grabs.

Cat O' Nine Tails grabs only the player that touches it in simultaneous
co-op. The other player stays independent while the captured player spins
and launches; the camera continues to include both live players.

### Commit validation evidence

The [2026-10-29 evidence manifest](docs/evidence/coop-validation-2026-10-29.md)
records the external checkpoint inventory, reproducible replay commands and
original results: 56/58 CTest checks and 44/45 targeted private checks passed.
The packaging and blank-SRAM item-pickup setup failures were fixed in the
follow-ups below. Save files remain private and outside Git.

Packaging follow-up: both module-resolution failures are fixed; the complete
public suite now passes **58/58**. See [follow-up evidence](docs/evidence/packaging-validation-2026-10-29.md).
The earlier evidence retains its original results.

Item-input follow-up: the blank-SRAM setup is repaired and both players pass
ownership checks. See [the new evidence](docs/evidence/item-input-validation-2026-10-29.md).
This reruns the previously failing case; the other 44 private successes remain
recorded in the original validation. No gameplay code changed.
