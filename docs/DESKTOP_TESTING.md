# Interactive Windows test build

## Scope

`dkc2_snesrecomp_desktop.exe` is the first interactive presentation host for
the native `snesrecomp` path. It uses the same generated code, interpreter
fallback, PPU, SPC700, S-DSP, frame adapter, and verified-ROM loader as the
headless regression runner. The repository contains no ROM or generated game
code.

The host currently provides:

- a resizable Windows window that presents the complete 256x224 BGRX frame at
  a conventional 4:3 display aspect;
- real-time pacing at 60.098811862 video frames per second;
- signed 16-bit stereo output at the SNES DSP rate of 32,040 Hz, queued in
  fixed 2,048-frame Windows wave-output blocks;
- keyboard input while the game window has focus; and
- one hot-pluggable XInput controller, selected as the first connected pad;
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
build-snesrecomp\Release\dkc2_snesrecomp_desktop.exe
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
.\build-snesrecomp\Release\dkc2_snesrecomp_desktop.exe `
    "C:\private\dkc2.smc"
```

The loader accepts only a payload whose headerless SHA-256 is
`35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`.
It removes an optional 512-byte copier header in memory; it never changes the
private file.

## Controls

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
| Save state (slot 0) | `F5` | — |
| Load state (slot 0) | `F9` | — |
| Toggle performance log | `F` | — |
| Quit | Escape | Close the window |

Input is intentionally ignored when the game window is not focused. An XInput
pad can be attached or removed while the program is running. Native DirectInput
and PlayStation-controller APIs are not implemented yet; pads translated to
XInput by their driver or a launcher are expected to work.

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
presentations, for example `DKC2Recomp v0.0.1 (FPS: 60)`. Press `F`, or set
`DKC2_DESKTOP_PERF=1` before launch, to create `performance.log` beside the
executable. Each one-second sample reports average input, emulation, rewind,
PPU, audio, GDI presentation, pacing, and untracked time, plus the fraction of
wall time for which the main thread was active. Intentional frame-pacing wait
is excluded from that active percentage.

Gameplay currently presents through GDI on the same thread as emulation. GDI
does not expose GPU timestamps, so the log writes `gpu_ms=n/a backend=GDI`.
Use the phase totals to distinguish expensive game emulation from host
presentation or audio work; do not interpret unavailable GPU time as zero.
Telemetry is disabled by default and adds phase timers only while enabled.

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

F5/F9 file snapshots use `saves/dkc20.sav` beside the executable. Loading a
slot resets queued audio, deadline anchors, and rewind history before redrawing
the restored PPU boundary. A user-provided slot immediately before a Pirate
Panic death was replayed headlessly and used to verify clean level restart.

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

The automated desktop smoke test runs 180 hidden frames, executes one real 3x
fast-forward iteration, captures bounded history, and performs a real
full-state restore after frame 120. It proves that
the verified ROM, window, game loop, renderer, audio-output initialization,
and rewind load path can start and shut down cleanly. Synthetic regressions
cover gamepad/trigger mapping, history wrap/pop order, atomic GDI presentation,
FPS sampling, and telemetry accounting. The test explicitly disables SRAM
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
