# Interactive desktop test builds

## Scope

`dkc2_snesrecomp_desktop` is the accepted Windows presentation host for
the native `snesrecomp` path. It uses the same generated code, interpreter
fallback, PPU, SPC700, S-DSP, frame adapter, and verified-ROM loader as the
headless regression runner. The repository contains no ROM or generated game
code.

`dkc2_snesrecomp_sdl` is the parallel Windows/Linux/macOS gameplay host. It
uses SDL2 for video, audio, input, controllers, and timing while retaining the
same shared launcher and game/runtime behavior. Its lifecycle is automated on
Windows; native Linux/macOS acceptance is tracked in `CROSS_PLATFORM.md`.

The host currently provides:

- a resizable Windows window that presents the complete 256x224 BGRX frame at
  a conventional 4:3 display aspect;
- real-time pacing at 60.098811862 video frames per second;
- signed 16-bit stereo output at the SNES DSP rate of 32,040 Hz, queued in
  fixed 2,048-frame Windows wave-output blocks;
- keyboard input while the game window has focus; and
- two independently routed SNES controller ports using focused keyboard input
  or up to two hot-pluggable XInput gamepads;
- fixed 3x fast-forward and approximately 15 seconds of fixed 3x rewind; and
- load-on-start/clean-exit persistence for DKC2's 2 KiB battery SRAM;
- a once-per-second FPS readout in the game-window title; and
- opt-in main-thread phase telemetry for diagnosing slowdown.

## Build

From PowerShell at the repository root:

```powershell
git submodule update --init --recursive
.\scripts\generate_snesrecomp.ps1 -Rom "C:\private\dkc2.smc"
cmake -S . -B build-snesrecomp `
    -DDKC2_BUILD_SNESRECOMP=ON `
    -DDKC2_ROM="C:\private\dkc2.smc"
cmake --build build-snesrecomp --config Release `
    --target dkc2_snesrecomp_desktop
```

Build the portable host on any platform with target
`dkc2_snesrecomp_sdl`. It produces `DKC2RecompSDL.exe` on Windows and
`DKC2Recomp` on Linux/macOS. The portable private generator is:

```sh
python3 scripts/generate_snesrecomp.py --rom /private/path/dkc2.sfc
```

Release builds use `-O3` with GCC/Clang. MSVC uses `/O2`, its highest
supported speed preset. A private icon can be embedded without entering the
source tree by adding
`-DDKC2_DESKTOP_ICON="C:\private\dkc2.ico"` to the configure command.

This option enables both the diagnostic headless target and the desktop
target. The deprecated `DKC2_BUILD_SNESRECOMP_HEADLESS` name remains an alias
for existing build directories. Generated C stays under ignored
`generated/snesrecomp/`, and all build products stay under the ignored build
directory.

## Run

For the simplest launch, double-click:

```text
build-snesrecomp\Release\DKC2Recomp.exe
```

The application opens without a command-prompt window and asks you to select
your private DKC2 `.smc` or `.sfc` file. Cancelling the picker exits normally.
The verified ROM is read from its existing location and is never copied into
the repository.

The source-only launcher remains useful for repeatable command-line runs:

```powershell
.\scripts\run_snesrecomp_desktop.ps1 -Rom "C:\private\dkc2.smc"
```

Or invoke the executable directly:

```powershell
.\build-snesrecomp\Release\DKC2Recomp.exe `
    "C:\private\dkc2.smc"
```

The loader accepts only a payload whose headerless SHA-256 is
`35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`.
It removes an optional 512-byte copier header in memory; it never changes the
private file.

## Default controls

| SNES control | Keyboard | XInput controller |
| --- | --- | --- |
| D-pad | Arrow keys | D-pad or left stick |
| B | `Z` | A |
| A | `X` | B |
| Y | `A` | X |
| X | `S` | Y |
| Start | Enter | Menu/Start |
| Select | Shift | View/Back |
| L | `Q` | Left shoulder |
| R | `W` | Right shoulder |
| Rewind (3x) | `1` | Left trigger |
| Fast-forward (3x) | `2` | Right trigger |
| Save state (selected slot) | `F5` | — |
| Load state (selected slot) | `F9` | — |
| Toggle performance log | `F` | — |
| Overlay | Escape | Guide, or Start+Back |
| Quit | Overlay button | — |

Input is intentionally ignored when the game window is not focused. The ImGui
launcher exposes Player 1 and Player 2 source selectors. Keyboard is Player 1
and Gamepad is Player 2 by default; Gamepad players receive connected XInput
devices in player order. Selecting two Gamepad sources assigns the first two
connected devices to SNES ports 1 and 2. Source, deadzone, keyboard bindings,
and standard-controller bindings persist in `launcher.cfg`. Each player's
Configure page exposes the complete SNES layout. Its compact Assist Shortcuts
row exposes Rewind and Fast-forward; the top-level Assist page additionally
exposes Save State and Load State. Select a chip, press a key/button/axis, then
press Play to commit it. Pads can be attached or removed while the program is
running. Native DirectInput and PlayStation-controller APIs are not implemented
yet; pads translated to XInput by their driver or a launcher are expected to
work.

## In-game overlay and Assist Tools

Escape opens the Dear ImGui overlay in the Windows OpenGL and SDL/OpenGL
hosts. Emulation stops at the completed host-frame boundary, controller input
is suppressed, and queued audio is cleared/paused until Resume or Escape
closes the menu. The pages are Main, Settings, Assist Tools / Cheats,
Controls, and Credits.

Assist Tools default off. Enabling them permits the existing 3x rewind,
3x fast-forward, and five file-state paths; disabling the gate makes their
keyboard and controller shortcuts inert. Previous/Next wraps through Slots
1–5, and Save/Load acts on the selected slot. Files are `dkc2s0.sav` through
`dkc2s4.sav`; the legacy `dkc20.sav` fallback is limited to the first slot.
The setting is saved as `AssistTools` in `launcher.cfg`, and an enabled run
adds `(Assist Tools: On)` to its title. This is host policy only and is not
serialized into the SNES snapshot.

Settings mirrors the pre-boot DKC2 choices. Volume, screen model, texture
filtering, both player source/deadzone values, and Assist Tools apply live.
Window scale, fullscreen, presenter choice, audio enable, and skip-launcher
are persisted but require a restart. The sample-rate value is mirrored and
persisted, but DKC2 currently outputs only the SNES-native 32,040 Hz stream;
alternate rates need a future tested host resampler. Controls has nested
Player 1, Player 2, Assist, and Fixed Shortcuts tabs. Each player page edits
the input source, deadzone, and all 12 SNES keyboard/controller bindings.
Assist edits Rewind, Fast-forward, Save State, and Load State. These are the
same settings used by the pre-boot launcher, so changes apply to the live host
and persist to `launcher.cfg` on clean exit. Restore All Settings resets the
complete shared value, including gameplay/Assist bindings and the Assist
Tools gate.

To test an in-game remap, open Controls, select a keyboard or controller chip,
and provide the replacement input. Escape cancels capture instead of closing
the overlay. Controller capture requires one neutral/released poll before it
accepts a button or signed axis; this prevents the UI activation button from
becoming the new binding. Resume and confirm the new mapping drives gameplay,
then close and relaunch to confirm persistence. Repeat once for each player
and once for an Assist action with the Assist gate both off and on. The Fixed
Shortcuts page should continue to show Escape / Guide / Start+Back for the
overlay and F for the performance log; those recovery shortcuts are
intentionally not remappable.

The atomic GDI compatibility presenter does not have an ImGui renderer.
It remains available for machines where OpenGL cannot start, but has no
overlay; Escape retains its former Quit behavior there. Assist shortcuts can
still be enabled from the pre-boot launcher and operate on the first slot.

The pre-boot launcher has separate Assist Tools and Credits pages. Confirm the
Assist checkbox and all four Assist bindings survive a launcher restart and
that Back returns to the Dashboard. Confirm each player binding chip captures
keyboard and controller input, and that Reset restores DKC2 defaults. Confirm
the host-supplied credits wrap and remain readable at every supported launcher
scale.

## Restore launcher defaults

On the Settings page, **Restore Defaults** opens a confirmation dialog and
then replaces the entire editable DKC2 launcher configuration with the values
from `Dkc2LauncherSettingsDefault`. This includes display, audio, player
sources/deadzones/bindings, Assist bindings, and `SkipLauncher`. It does not
change the selected ROM or
touch SRAM/save-state files. Press Play to commit the restored settings to
`launcher.cfg`; Cancel leaves every current value unchanged.

## Video and CRT screen-color model

The launcher's Display card offers **OpenGL** (default) and **GDI
compatibility**, **Nearest/Bilinear** scaling, and the screen models **Raw**
(default), **CRT**, **Composite**, and **Trinitron**. CRT is opt-in: select CRT,
press Play, and the choice will be written to `launcher.cfg`. Raw bypasses the
screen-color conversion byte for byte. The filtered choices use the
PSXRecomp-derived 32,768-entry present-time color LUT and do not change SNES
memory, save states, deterministic hashes, or private raw frame captures.

This initial CRT feature models color response—phosphor gamut, gamma,
luminance, and black floor. It intentionally does not yet simulate scanlines,
screen curvature, a bezel, or phosphor persistence. Nearest/Bilinear is a
separate scaling choice. If OpenGL cannot start, the window is recreated and
the atomic GDI compatibility presenter receives the same transformed pixels.

For repeatable tests without changing `launcher.cfg`, set `DKC2_SCREEN` to
`raw`, `crt`, `composite`, or `trinitron`. `DKC2_DESKTOP_REQUIRE_GPU=1` turns an
OpenGL initialization failure into a test failure;
`DKC2_DESKTOP_FORCE_GDI=1` explicitly exercises the fallback. These variables
are diagnostic controls, not emulated SNES settings.

Fast-forward executes three console frames per presented host frame. Rewind
stores one complete in-memory state every three console frames and restores
one state per presented host frame, so both controls move at approximately
3x. Rewind history is bounded to 300 snapshots (about 15 seconds) and is lost
when the application closes. Audio is muted and the queued Windows audio is
discarded while either time control is active; normal audio resumes from the
restored/current SNES state when the control is released. If both controls are
held, rewind wins. The fixed multiplier is a named host constant so a later UI
can replace it with a slider without changing the state format.

## Performance diagnostics

The game-window title updates once per second with the number of completed
presentations, for example
`DKC2 Recomp Alpha Pre-Release (FPS: 60)`. Press `F`, or set
`DKC2_DESKTOP_PERF=1` before launch, to create `performance.log` beside the
executable. Each one-second sample reports average input, emulation, rewind,
PPU, audio, presentation, pacing, and untracked time, plus the fraction of
wall time for which the main thread was active. Intentional frame-pacing wait
is excluded from that active percentage.

The log records `backend=OpenGL` or `backend=GDI`, the selected screen model,
and CPU-side presentation time. GPU timestamp queries are not implemented, so
it writes `gpu_ms=n/a` for both backends. Use the phase totals to distinguish
expensive game emulation/filtering from host presentation or audio work; do
not interpret unavailable GPU time as zero. Telemetry is disabled by default
and adds phase timers only while enabled.

## Crash reports and diagnostic bundles

Every playable launch refreshes `diagnostics/last_run_report.json` beside the
executable. It records the build, host, operating system, loaded modules,
recent host breadcrumbs, last frame/resume PC, presentation backend, selected
screen model, audio availability, outcome, and fatal reason when present.
This is host observability only; it does not modify or serialize the emulated
machine.

A controlled runtime failure creates a timestamped
`diagnostics/diagnostic_bundle_YYYYMMDD_HHMMSS_PID` folder immediately. On
Windows, an unhandled exception also writes `crash_minidump.dmp`. Linux and
macOS fatal-signal handlers write a minimal async-safe marker; the next launch
turns that marker into a bundle because complex JSON and module enumeration
are not safe inside a signal handler.

To request a bundle after an otherwise clean manual run:

```powershell
$env:DKC2_DIAGNOSTIC_BUNDLE = "1"
.\DKC2Recomp.exe
```

The bundle allowlist is `report.json`, `README.txt`, optional `launcher.cfg`,
optional `performance.log`, and optional `crash_minidump.dmp`. It excludes
`rom.cfg`, ROM content and paths, generated code, SRAM, save states,
screenshots, and captured audio. Reports do contain loaded-module paths and
basic machine/OS details; review them before sending. Developers can run the
three deterministic private drills with:

```powershell
ctest --test-dir build-snesrecomp -C Release --output-on-failure `
  -R "supplied_rom_diagnostic_"
```

The drills cover clean requested bundles, a controlled fatal exit, and a real
Windows exception with a non-empty minidump. The harness rejects any file not
on the bundle allowlist. `SNESRECOMP_CRASH_TEST=die|seh` exists only for these
contained tests and intentionally terminates the process.

## Battery saves

The desktop host reads DKC2's 2 KiB cartridge SRAM after startup and writes it
after a clean exit. Paths are anchored beside the executable, independent of
the folder from which it was launched or the folder containing the ROM:

```text
build-snesrecomp\Release\saves\save.srm
build-snesrecomp\Release\saves\save.srm.bak
```

Before writing a new `save.srm`, the prior file is renamed to `save.srm.bak`.
These files are private user data and are ignored by Git. Automated CTest runs
set `DKC2_DESKTOP_DISABLE_SRAM=1`, so they cannot read or overwrite a player's
save. SRAM is separate from rewind: the former is the game's normal battery
memory, while rewind snapshots the full running console state only in memory.

F5/F9 file snapshots write `saves/dkc2s0.sav` beside the executable. Loading
tries that name first and then accepts the former `saves/dkc20.sav` name, so an
existing slot is not stranded. A successful load resets queued audio, deadline
anchors, and rewind history before redrawing the restored PPU boundary. A
user-provided slot immediately before a Pirate Panic death was replayed
headlessly and used to verify clean level restart.

## What to test

For the first manual pass, leave input neutral and watch one complete title and
three-demo cycle. Then press Start at the title, select a new game, and confirm
that directional and face-button mappings respond. Record:

1. whether any frame freezes, flashes incorrectly, loses a layer, or shows
   corrupt sprites;
2. whether music changes pitch, crackles, drops out unexpectedly, or develops
   repeated gaps;
3. whether keyboard and controller inputs respond once per press and remain
   stable when held; and
4. the approximate title/demo or level location of every problem.

The automated desktop smoke test runs 180 hidden frames, opens and renders the
overlay for 30 host presentations while confirming emulation is paused,
closes it and resumes, executes one real 3x fast-forward iteration, captures
bounded history, and performs a real full-state restore after frame 120. It proves that
the verified ROM, window, game loop, renderer, audio-output initialization,
and rewind load path can start and shut down cleanly. Synthetic regressions
cover gamepad/trigger mapping, two-player source routing and port packing,
history wrap/pop order, five-slot wrap/clamp behavior, assist-action gating
and one-shot delivery, shared
4:3 viewport math, Raw byte-exact bypass, CRT
LUT application, atomic GDI presentation, FPS sampling, and telemetry
accounting. Dedicated hidden private-ROM tests require OpenGL+CRT and force
GDI+CRT for 60 frames each. Every automated desktop test disables SRAM
persistence.
The 12,000-frame headless test remains the authoritative deterministic gate for
two complete attract cycles. The 0.0.1 gate additionally includes completed
human watch/listen/controller passes for DKC2 and the four regression titles;
this is exercised-path sign-off, not a claim of 102% completion.

The intermittent one-frame black flashes in the first recorded test were host
presentation artifacts, not SNES forced blank: the old paint path visibly
cleared the client black and then stretched the game image in a second GDI
operation. The host now composes black borders and the scaled frame into an
off-screen bitmap and publishes it with one `BitBlt`. A corrected active-demo
recording contained zero isolated black frames; manual retesting on the user's
capture setup remains the final perceptual check.

The former 54-frame first-cycle timing lag has been corrected at its source:
the 65816 interpreter was discarding program-bank bit 7 and treating FastROM
execution as its SlowROM mirror. Current first-cycle completion is six frames
early relative to Snes9x, with every level-loading window within one frame.
