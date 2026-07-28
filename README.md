# DKC2Recomp

> This recompilation is a byproduct of developing
> [snesrecomp](https://github.com/mstan/snesrecomp): the games are the proving
> ground, while the reusable framework is the larger goal. This is an early
> preview, not an official port. Expect rough edges and please report any
> reproducible gameplay, video, or audio regressions.

Static recompilation of *Donkey Kong Country 2: Diddy's Kong Quest* for SNES
into native desktop applications, using the `snesrecomp` framework. Windows
is the currently accepted release platform; an SDL2 gameplay host now provides
the source foundation for Linux and macOS acceptance.

The 65816 game program is translated to native C where analysis can prove an
exact entry state. The current profile compiles every demanded exact CPU-mode
variant; the shared 65816 interpreter remains available as a correctness and
exceptional-path fallback. SNES hardware outside the main CPU—the PPU,
SPC700/S-DSP, DMA/HDMA, controllers, and cartridge mapping—is modeled by the
shared runtime.

## Quick start

1. Download `DKC2Recomp-windows-x64-v0.0.1.zip` from
   [Releases](../../releases) and extract the complete archive.
2. Run `DKC2Recomp.exe`.
3. In the Dear ImGui launcher, select your own legally obtained North American
   v1.0 ROM and choose **Play**.

The selected external path is remembered in `rom.cfg` beside the executable.
The ROM is never copied into the release. Saves are written to
`saves/save.srm`, with the previous clean save retained as `save.srm.bak`.

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
open the Dear ImGui overlay. The SDL host also accepts the controller Guide
button; Start+Back is the portable fallback. The overlay provides Resume,
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

Rewind, fast-forward, and five save-state slots are intentionally gated behind
**Enable Assist Tools / Cheats**. This setting defaults off, persists as
`AssistTools` in `launcher.cfg`, and adds `(Assist Tools: On)` to the game
window title. The Assist Tools page selects Slots 1–5 and has explicit Save
and Load buttons; every configured Assist shortcut remains inert while the
gate is disabled.

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

**Widescreen 16:9** is an experimental opt-in setting in both the pre-boot
Settings page and the in-game Settings tab. It changes the internal PPU surface
from 256x224 to 342x224, preserving the original center while adding 43 source
pixels on each side. Pirate Panic's collision-bearing foreground margins use
DKC2's live decompressed WRAM level map to reconstruct exact 8x8 tiles, with
world-keyed history retaining game-authored updates. The adapter accounts for
the game's 256-pixel map/camera origin difference, its rotated column buffer,
and its single valid guard metatile at room ends. Its cyclic sky/ocean backdrop
repeats the already-rendered native layer. Unsafe BG3 staging and bounded
title/menu/room tilemaps remain centered instead of showing stale data. The
original 4:3 mode is the default.

The common DKC2 object activation/despawn and sprite-render boundaries have
been widened, and Pirate Panic has deterministic composite, per-layer, and OAM
margin evidence. Those widened object bounds activate only after the terrain
source for the frame has been verified. This is not yet a whole-game
widescreen certification:
vertical stages, bosses, bonuses, maps, Mode-7 screens, and special effects
still require explicit route testing, and this repair still needs the user's
normal-speed motion check. Use `DKC2_WIDESCREEN=1` for a one-process developer
override without changing `launcher.cfg`.

The **GDI compatibility** renderer remains selectable and is also used
automatically if OpenGL initialization fails. Screen-color selection is
renderer-independent, so CRT produces the same transformed source pixels on
both presentation paths. **Nearest/Bilinear** controls only how that completed
frame is scaled. Settings persist in `launcher.cfg`; Raw remains the default
unless the user opts in. For repeatable diagnostics, `DKC2_SCREEN=raw`, `crt`,
`composite`, or `trinitron` overrides the saved screen model for one process.

When Assist Tools are enabled, save states are stored beside the executable as
`saves/dkc2s0.sav` through `saves/dkc2s4.sav`; the menu presents these as
Slots 1–5. The first slot still loads the former `saves/dkc20.sav` name as a
compatibility fallback, but all new writes use the unambiguous names. States
are separate from the cartridge SRAM files used for normal in-game saves.

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

The current sound analysis profile emits all 3,468 demanded exact CPU-mode
variants as static C (100%). This is a compile-time structural count, not a
percentage of dynamically executed CPU instructions and not a claim that the
shared interpreter can be removed.

The interpreter remains the safe runtime default for an unavailable exact
entry state. Two dormant bugs in the original game also deliberately preserve
their real JSR stack frames and hand control to the interpreter if reached:
both calls enter bytes documented as data/garbage and crash on original
hardware. They are explicit exceptional edges from compiled callers, not
LLE-only code variants and not guessed native implementations.

The generated C remains ignored because it is derived from the user's ROM.
Only source-owned configuration and structural metadata are committed.

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
