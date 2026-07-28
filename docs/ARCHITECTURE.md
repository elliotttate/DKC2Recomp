# Architecture

## Distribution boundary

The public source tree contains original tools, runtime code, tests, and
documentation. It never contains the game ROM, extracted game content, or
generated copyrighted data. Private products live beneath ignored `build`,
`private`, or `generated` directories.

## Target execution model

The intended first playable implementation combines translation with a tested
fallback:

1. Verify the user's ROM locally.
2. Decode reachable W65C816 code and build control-flow graphs.
3. Execute uncertain paths in the portable interpreter.
4. Emit native C for proven direct control flow and known calls.
5. Link both to a portable SNES hardware layer.
6. Replace translated routines with readable C subsystem by subsystem.

The interpreter is now functional and is the behavioral bootstrap for the
translator. It is not the final performance strategy.

## Static analysis layer

`dkc2_analyze` tracks processor mode and M/X operand widths, follows direct
branches and jumps, and can record or traverse direct calls. It recognizes
DKC2's `PEA`/`RTS` startup trampoline using a small abstract return-word stack.
Indirect jumps and unknown stack effects end a path rather than inviting a
guess.

An external WLA symbol map may be overlaid and a Graphviz DOT graph exported.
Neither is needed at runtime or included in releases.

## CPU execution layer

`dkc2_cpu_step` executes one complete logical W65C816 instruction against
generic 24-bit read/write callbacks. The register file includes A, X, Y, S, D,
PC, DBR, PBR, P, E, wait/stop state, and an instruction counter. Reset, NMI,
and IRQ entry points use the same callback boundary.

Every opcode and addressing mode is implemented, including decimal arithmetic,
native/emulation transitions, interrupt frames, long pointers, stack
exceptions, and complete block moves. The core passed 5,080,000 external
instruction-state comparisons. It is not cycle accurate: one step does not
expose dummy accesses or internal block-move iterations.

## Address-space layer

`dkc2_hirom_snes_to_rom` recognizes ROM windows only:

- `$40-$7D:0000-FFFF`
- `$C0-$FF:0000-FFFF`
- `$00-$3F:8000-FFFF`
- `$80-$BF:8000-FFFF`

`dkc2_bus` separately routes full WRAM, its low mirrors, DKC2's 2 KiB SRAM
mirrors, I/O callbacks, ROM, and unmapped/open-bus accesses. This keeps mapper
logic from silently treating hardware registers as ROM.

## Bring-up hardware layer

`dkc2_snes_io` currently provides:

- CPU and DMA register storage;
- PPU register storage;
- VRAM address/remap/increment and low/high data ports;
- CGRAM data ports and the OAM word-address/write-latch behavior used by the
  renderer;
- shared background scroll-offset latching and PPU-mode telemetry;
- the `$2180-$2183` 17-bit WRAM address and auto-incrementing data port;
- the shared `$211B-$2120` Mode-7 write latch and signed multiply result;
- delayed CPU multiplication/division and `$4214-$4217` results;
- all eight general-DMA B-bus offset patterns;
- fixed, incrementing, and decrementing A-bus sources;
- the four CPU/APU communication ports;
- an opt-in master-cycle event timeline with NTSC H/V counters;
- NMI/TIMEUP status, interrupt latches, and `WAI` wake-up support;
- direct and indirect HDMA for all eight transfer patterns;
- two serial controllers and timed automatic polling; and
- explicit barriers for unsupported I/O and B-to-A DMA.

This is enough to execute DKC2's reset initialization and exact 65,536-byte
fixed-source VRAM clear. The default probe can still stop at the first SPC700
IPL handshake for regression compatibility; `--with-apu` continues with the
executing APU. Richer PPU reads, access restrictions, several display modes,
and exact CPU/bus cycles remain future components.

## Headless PPU rendering layer

`dkc2_ppu_renderer` is an optional observer of `dkc2_snes_io`. At visible
HBlank it snapshots the just-completed scanline before that line's HDMA
updates. At the end of the visible region it publishes a complete 512x224 RGB
frame and a deterministic SHA-256 fingerprint.

The renderer implements tiled backgrounds for modes 0, 1, 3, and 5; Mode-7
BG1 and EXTBG affine sampling; 2/4/8-bpp planar tiles; map, tile, and Mode-7
screen flips; layer and tile priority; low- and high-resolution output; all
object-size pairs; object priority rotation and scanline range/time limits;
and main/subscreen color math. Unsupported state is recorded in a feature mask
instead of silently claiming a fully supported frame. The current unsupported
set includes modes 2/4/6, windows, direct color, mosaic, pseudo-hires, and
interlace.

Rendering is opt-in so the existing CPU, APU, and timing checkpoints retain
their cost and behavior. `--frame-output=<private.ppm>` is also opt-in and
writes only to the caller's path; no ROM-derived image belongs in source
control. See `docs/PPU_RENDERING.md` for supported state and validation rules.

## Timing and event layer

`dkc2_snes_io_advance_master_cycles` is the common clock input for beam
progression, NMI/IRQ latches, HDMA, autojoy, delayed CPU math, and the APU. The
current boot adapter counts all host-visible A-bus byte accesses and assigns
eight master cycles to each. That adapter is intentionally replaceable: when
the CPU core later reports exact cycles, the hardware event API does not need
to change.

The compatibility modes are layered. The default stops at the original APU
barrier, `--with-apu` retains the port-access scheduler and `$4211` checkpoint,
`--with-timing` selects the event path, and `--with-render` adds scanline
capture and framebuffer publication to it. See
`docs/TIMING_AND_INTERRUPTS.md` for the complete contract and limitations.

## APU execution layer

`dkc2_apu` wraps the MIT-licensed LakeSnes SPC700/S-DSP subset. The wrapper
owns reset/execution, CPU-side port access, ARAM inspection, and cycle counts;
LakeSnes types do not escape the wrapper API. S-SMP registers, IPL ROM, timers,
DSP registers, BRR decoding, and sample generation are present.

The compatibility scheduler advances one complete SPC opcode per 65816 APUIO
access in `--with-apu` mode. The timed continuation instead derives SPC cycles
from the master timeline at a nominal 21:1 ratio and carries whole-instruction
overshoot as debt. CPU-to-master timing is still provisional, so audio timing
and race behavior cannot yet be called accurate.

## Current boot sequence

With zeroed host WRAM/SRAM and the NTSC status bit selected, `dkc2_boot`:

1. enters the ROM reset vector;
2. completes DKC2's RAM, SRAM, region, and startup checks;
3. initializes PPU, CPU, and DMA registers;
4. executes channel-0 DMA and verifies all 64 KiB of VRAM are zero;
5. clears both WRAM banks through two logical MVN instructions; and
6. reaches the SPC700 IPL-ready comparison at `$B5:821A`.

The measured checkpoint is 74,262 interpreted instructions, one 65,536-byte
DMA, and an explicit APU barrier. This is a deterministic bring-up regression,
not a console timing trace.

With `--with-apu`, the same probe executes both CPUs through the IPL transfer,
later DMA, and decompression work. It reaches an unsupported `$4211` TIMEUP
read after 1,359,156 65816 instructions. The accompanying deterministic ARAM
hash is `49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f`.
This value is a local regression checkpoint until compared with an accurate
emulator dump.

With `--with-timing`, the runner passes `$4211`, reaches `WAI`, delivers
repeated VBlank NMI, and performs general DMA, HDMA, controller polling,
Mode-7 multiplication, CPU math, and WRAM-port traffic. The current regression
runs 20,000,000 instructions without an unsupported-hardware barrier, with
5,619 general-DMA transfers and 4,005 HDMA line transfers. It prints SHA-256
fingerprints for WRAM, SRAM, VRAM, CGRAM, OAM, and ARAM. Its reported
frame/beam position remains tied to the provisional
eight-master-cycles-per-access adapter and is not a hardware timing oracle.

With `--with-render`, the same real-ROM execution also proves that the
renderer can consume changing PPU state for thousands of frames without
creating a new execution barrier. The 1,700,000-instruction private regression
pins a Mode-7 frame, while the 2,000,000-instruction regression preserves the
later modes-1/5 hash. After low-resolution normalization, the Mode-7 image is
an exact RGB match to an official Snes9x 1.63 screenshot. VRAM, CGRAM, and OAM
also match an adjacent private Snes9x state byte for byte. Beam-aligned display
registers and provisional timing still need event-aligned comparison, and the
executable is not a playable desktop build.

## Verification strategy

Each milestone should combine synthetic unit tests, CPU state conformance, and
private real-ROM integration. Future differential checkpoints will compare:

- CPU registers and mode flags;
- WRAM and SRAM;
- VRAM, CGRAM, and OAM hashes;
- DMA/HDMA channel state;
- APU communication and ARAM/DSP state;
- rendered frame hashes; and
- deterministic input playback.

The user's ROM running in an accurate emulator remains the behavioral oracle;
it is never distributed with the project.

## Native snesrecomp execution path

The production-direction experiment is built around the pinned `snesrecomp/`
submodule. Its fetch URL uses the DKC2 integration fork while
`mstan/snesrecomp` remains authoritative upstream; exact revisions and license
status are recorded under `third_party/snesrecomp/`. Private ROM-derived C is
generated into ignored storage, while the repository owns only configuration,
the DKC2 adapter, and verification tools.
Unavailable runtime entry states continue through the shared 65816 interpreter.
The current configuration emits all 3,468 demanded exact CPU-mode variants AOT
(100% structural coverage), with zero `LLE_ONLY` code nodes. This does not
remove the interpreter: it remains the authoritative safety tier and handles
two explicit dormant fault edges into non-code bytes if the original game's
buggy calls are ever reached.

Whole-program analysis is available through matching Python and Rust
implementations. Python remains the semantic oracle and automatic fallback.
The Rust path supports HiROM, DKC2's indirect dispatch/return forms, recursive
exit-set solving, declared boundaries, data-region execution, and the same
analysis limits. Both backends independently converge on 3,318 roots, 3,468
exact AOT variants, and zero LLE-only variants. A full Python regeneration took
447.84 seconds; native analysis took 13.710 seconds and its complete generation
took 39.51 seconds on the validation machine.

The final normal control-flow gap is DKC2's WRAM-clear restart sequence. The
call at `$80:85E8` enters `clear_full_wram` at `$80:8E7F`; that routine removes
its own JSR return, saves a continuation, resets the hardware stack while
clearing WRAM, and ends with `JMP ($0032)` at `$80:8EB8`. Configuration records
the call as terminal and the indirect jump as a `ptrtail_popcall` dispatch to
the proven continuation at `$80:85EB`, so the routine and its continuation are
both compiled without pretending it executes an RTS.

The other two gaps are bugs, not dynamic game dispatch. Calls at `$B3:BC20`
and `$BA:9C36` enter `$B3:F289` and `$BA:F305` respectively, where the source
layout documents data/garbage rather than callable code. `noreturn_jsr` ends
lexical analysis after each real JSR while preserving the pushed guest frame;
the emitted exceptional edge enters authoritative LLE at the exact target.
Consequently the compiled callers are valid without fabricating return modes
or compiling data as code.

DKC2's NMI dispatcher is non-returning: it jumps through direct-page `$20`,
resets the stack, and reaches a new `WAI`. The game adapter therefore treats
that wait as the next continuation instead of requiring `RTI`. A shared
interpreter correction also makes `BRA` and `BRL` consume their operands before
adding the signed displacement, avoiding compiler-dependent PC bases.

The native headless host currently supplies neutral input and a 256x224 BGRX
surface. Each host frame has a `1364 * 262` master-clock budget; long LLE work
yields at that deadline and resumes at the saved 24-bit PC. Audio consumption
uses a fractional `32040 / 60.098811862` accumulator, requesting 533 or 534
native-rate stereo frames without long-term drift. The host reports aggregate
blank-video and silent-audio runs, clipping, maximum same-channel sample jump,
state/audio fingerprints, and can export private PPM/PCM evidence.

For deterministic gameplay routes, the desktop host can record the complete
packed 24-bit two-controller input word once per emulated frame and the
headless host can replay the same stream. The source tree owns only the parser,
telemetry, and assertions; recordings stay in ignored private storage. Route
acceptance rejects interpreter-cap and unresolved-dispatch diagnostics so a
completed frame count cannot conceal skipped game-code side effects.

Current generated targets define
`SNESRECOMP_EXTERNAL_RAM_ROUTINE_GUARDS`. This makes the generated
`dispatch_v2.c` table the sole strong owner on MSVC while preserving
SNESrecomp's fallback table for standalone and older generated clients.

Static MVN/MVP follows the same scheduling contract. A block move always
transfers at least one byte, updates DB and A/X/Y after every byte, wraps X/Y in
8-bit index mode, charges seven CPU cycles per repeated byte at the mapped bus
speed, and may yield only between bytes when an owning LLE scheduler reaches
its frame deadline. Resumption re-enters the architectural opcode with the
updated count and indices; the host never observes a partially applied byte.

The frame adapter runs the shared PPU's VBlank handler after the visible-line
pass; this reloads the internal OAM data-port address before the following
NMI's complete 544-byte OAM DMA. A 12,000-frame gate proves two ordered attract
cycles and a one-cycle PCM comparison passes against Snes9x's silence envelope,
level, peak, and discontinuity metrics. Native semantic transitions still
differ slightly from the reference: after preserving the full 65816 program
bank and therefore FastROM timing, the first-cycle completion is six frames
early instead of 54 frames late.

The Windows desktop target is a thin project-owned presentation layer over the
same core. Its default OpenGL backend uploads one completed BGRX8888 frame to a
texture, draws it into the shared centered 4:3 viewport, and swaps a
double-buffered window. If OpenGL initialization fails—or the user selects the
compatibility backend—the GDI path composes the same frame and black borders
into an off-screen DIB, then exposes the completed client image with one
`BitBlt`. Both paths avoid the former visible clear-then-draw intermediate
surface. It uses waveOut for
fixed 2,048-frame signed-16 stereo blocks, asynchronous keyboard polling, and
per-frame polling of up to two XInput devices. Launcher source choices route
keyboard or connected gamepads to two 12-bit controller words packed into the
shared runtime's existing `RtlRunFrame` input. Audio samples still come from
the exact fractional accumulator; the fixed device blocks are only a queueing
boundary. The first
three blocks are prebuffered to absorb normal scheduler jitter, after which a
high-resolution performance counter paces frames at 60.098811862 Hz. Runtime,
rendering, and audio stay on one thread, avoiding unsynchronized access to the
shared SNES state. The target uses the Windows GUI subsystem: an explicit ROM
argument supports scripts and tests, while a no-argument launch opens the
standard file picker and never creates a console window. Both routes enter the
same exact-ROM verification and runtime function. Exact cycle alignment and
perceptual sign-off remain open.

The parallel `dkc2_snesrecomp_sdl` target is the portable gameplay host. It
uses SDL2 for the native window and OpenGL context, texture presentation, keyboard,
two hot-pluggable GameController devices, monotonic timing, and queued audio.
It consumes the same generated C, runtime, frame adapter, verified-ROM loader,
screen-color adapter, input router, FPS counter, rewind ring, and shared
recomp-ui launcher as the Win32 host. The 4:3 viewport calculation and launcher
settings/ROM-cache persistence are project-owned host-neutral modules rather
than duplicated platform behavior. The portable presenter requests an OpenGL
2.1 compatibility context so the game and recomp-ui overlay share one
deterministic swap boundary. The core continues to publish one complete
256x224 BGRX frame.

The in-game overlay is a second, gameplay-lifetime recomp-ui/ImGui context;
the pre-boot launcher still owns and destroys its separate window/context.
The host-neutral C model owns open/closed state, the Assist Tools gate, a
wrapped 0–4 slot selector, one-shot Resume/Quit/Save/Load actions, and the
validated lifecycle of one active keyboard/controller binding capture.
Platform glue supplies SDL events or a small Win32 input translation, and the
presenter submits ImGui draw data after the game quad but before the same
buffer swap. While open, the hosts schedule no SNES frame, zero game input,
clear/pause queued audio, and continue presenting at the host rate. File-state
actions reuse SNESrecomp's `RtlSaveSlotPath`,
`RtlSaveSnapshot`/`RtlLoadSnapshot`, and `save_name_prefix`; the bounded
selector maps to `saves/dkc2s0.sav` through `saves/dkc2s4.sav`, and only slot
zero probes the old `saves/dkc20.sav` compatibility name. The GDI fallback has
no ImGui renderer, but its keyboard Assist shortcuts follow the pre-boot
opt-in state.

`RecompLauncherCSettings` is the one persisted settings value shared by the
pre-boot launcher, overlay, Win32 host, and SDL host. The overlay edits every
DKC2 setting shown before boot and exposes independent Player 1/2 input
source, deadzone, and complete gameplay/Assist binding controls. Volume,
texture filtering, screen model,
controller routing, and Assist policy are safe to apply live. Window scale,
fullscreen, renderer, audio enable, and skip-launcher remain restart-bound
because they affect native resources or startup flow. The shared audio
frequency field is mirrored and persisted for launcher compatibility, but the
current DKC2 hosts deliberately consume the S-DSP's native 32,040 Hz stream
without a host resampler. Both hosts copy the final overlay value back to
`launcher.cfg` on exit.

The generic recomp-ui ABI has additive optional `has_assist_tools`,
`assist_tools_note`, and `credits_text` game fields plus the persisted
`assist_tools` setting. Games that do not set them retain the former
Dashboard/Settings/Controller surface; DKC2 receives two additional top-level
pre-boot pages without game-specific code in the shared renderer.
The project pins these additive changes from
`Nicktendonick/recomp-ui@0b1ac7f` while the corresponding upstream review is
pending; `mstan/recomp-ui` remains the authoritative source.

The launcher receives a host-owned, complete default-settings snapshot through
the additive recomp-ui game ABI. Recomp-ui copies it into the view model during
initialization and exposes a confirmed Restore Defaults action only when that
snapshot exists. Confirmation replaces the settings working copy atomically;
ROM selection and save files are separate state and therefore remain intact.
Both playable hosts use the same `Dkc2LauncherSettingsDefault` function for
startup and reset, preventing the UI defaults from drifting from first-run
behavior.

The same settings value now owns input bindings. Additive
`player_key_bind`, `player_pad_bind`, `assist_key_bind`, and
`assist_pad_bind` arrays are enabled only when a host advertises
`settings_bindings`. Keyboard entries are SDL scancodes; controller entries
encode SDL's standard controller button or signed axis vocabulary. The
launcher capture state writes those arrays directly, so it never presents a
binding file that DKC2 ignores. The SDL host evaluates scancodes natively; the
Win32 host translates the same scancodes to focused-window virtual-key state.
Both hosts evaluate the same standard-controller encoding and route the
resulting 12 logical SNES buttons into the existing packed input word. The
overlay writes those same arrays, with SDL event capture for keyboard input
and a host-neutral first-controller snapshot for buttons and signed axes.
Controller capture must observe a neutral state before accepting input, so
the navigation button used to enter capture cannot self-bind. The model
cancels capture when the overlay closes. Assist bindings are global, but
policy still masks every Assist action when the gate is off. Escape,
Guide/Start+Back, and the F performance-log key remain fixed recovery and
diagnostic shortcuts.

The two hosts coexist deliberately. Windows remains the accepted release and
regression baseline while the SDL target is exercised there. Linux and macOS
are source targets until each passes native build, visible video/audio,
controller, persistence, and packaging acceptance. The source does not infer
success for hardware or operating systems that were not available to test.

Screen-color modelling is a separate present-time stage before any backend.
Raw returns the core-owned pixel pointer without conversion. CRT, Composite,
and Trinitron first quantize the rendered BGRX channels to the SNES five-bit
channel domain, then consult the 32,768-entry color LUT in SNESRecomp's shared
`runner/src/snes/color_lut.{c,h}` module. That module was aligned with the exact
PSXRecomp revision documented under `third_party/psxrecomp_color_lut/`; it
models phosphor primaries, display gamma, luminance, and black floor, not
scanlines or curvature. The transformed pixels live in a host-only
scratch frame. They never write PPU state, emulated memory, save states, raw
frame export, or deterministic reference hashes. Nearest/bilinear sampling is
applied later by OpenGL and is independent of the screen model. The GDI path
uses its established scaling behavior while still receiving the same
color-model output.

The desktop host's time controls use shared-runtime in-memory snapshots. A
generic memory-backed `SaveLoadInfo` adapter serializes the same SNES state as
the existing file-state path. DKC2 appends its external CPU continuation,
frame deadline, APU pacing counters, MEMSEL, HDMA enable, and frame counter;
load hooks repair host pointers and clock anchors. The host captures before
the PPU draw pass because drawing advances HDMA/VBlank state, then performs
one draw immediately after restore to recreate the original post-draw
boundary. A bounded LIFO ring retains 300 snapshots at three-frame intervals.

Battery SRAM is a separate persistence boundary. After exact-ROM loading the
process anchors relative paths to the executable directory, creates `saves`,
and reads the runtime's 2 KiB cartridge RAM. A clean exit rotates
`save.srm` to `save.srm.bak` and writes the live cartridge RAM. Integration
tests disable this path so deterministic automation cannot mutate user data.

Host observability remains outside the emulated machine. A small presentation
counter updates the Windows title once per wall-clock second. Optional
telemetry measures input, emulation, snapshot work, PPU drawing, audio,
presentation, and pacing with `QueryPerformanceCounter`, then writes aggregate
samples to `performance.log`. It does not alter SNES clocks, controller bits,
memory, snapshots, or generated code. Telemetry records which of OpenGL or GDI
is active and measures CPU-side presentation duration. GPU timestamp queries
are not implemented, so GPU time remains explicitly unavailable on both paths.

Windows icon packaging is also host-only. `DKC2_DESKTOP_ICON` configures an
optional `.ico` resource into the executable and window class while keeping
the image external to the source repository. Release speed flags apply only
at compilation (`-O3` for GCC/Clang and `/O2` for MSVC); they do not change the
runtime scheduler's target rate.

## Host diagnostics boundary

`runner/diagnostics.c` composes DKC2-specific run state with SNESRecomp's
shared host-report layer. Both playable hosts initialize it only after paths
are anchored beside the executable, update a frame/resume-PC heartbeat, record
the selected presenter/filter/audio state, and close it before host teardown.
Controlled runtime failures write immediately; `Die()` reaches the same path
through an `atexit` handler. Windows registers an unhandled-exception filter
and delegates minidump creation to the framework. POSIX signal handlers do
only async-signal-safe marker I/O, and the next launch completes the bundle.

The rolling report and timestamped bundle are host artifacts, never emulated
state. Bundle construction copies from a fixed allowlist rather than scanning
the working directory. It cannot include the ROM cache, ROM, generated C,
SRAM, file states, frame captures, or audio captures. Module paths and machine
information are intentionally present for debugging and are disclosed in the
bundle README.

Save paths have not yet been redesigned for mods. Five Assist slots write
`saves/dkc2s0.sav` through `saves/dkc2s4.sav`; the first retains load-only
compatibility with `dkc20.sav`. Official SNESrecomp now has an opt-in
versioned package/plugin runtime, but DKC2 has not adopted or validated it.
Mod-aware isolation must be designed against that stable identity rather than
inventing folder names before the mod integration lands.

## Public and personal package boundary

Repository-local `versions/Version NN` folders are source-derived public-safe
handoffs and intentionally omit ROMs, saves, configuration, diagnostics, and
generated game code. `scripts/create_personal_test_version.ps1` may transform
one completed handoff into a ready-to-run personal copy only in an external
directory. It verifies the supported ROM hash, uses a relative `rom.cfg`, and
optionally transfers the user's saves and launcher settings. This is a
deployment convenience only; it does not alter emulation, save formats, or
the repository's content boundary.

## Experimental widescreen boundary

Widescreen is a host-owned, opt-in presentation and game-boundary adaptation.
`runner/dkc2_video.{c,h}` owns the geometry: authentic mode remains 256x224;
16:9 mode allocates 43 additional source columns per side for a 342x224 PPU
surface. With the SNES 7:6 pixel aspect ratio, that surface presents at
approximately 16:9 without scaling the authentic center.

The game adapter chooses a layer policy every frame. In audited Mode 1
gameplay, enabled 64-column BG1/BG2 layers are keyed by DKC2's full WRAM camera
and the live 10-bit PPU scroll phase. SNESrecomp's shadow tilemap captures the
authentic center and the game's subsequent VRAM uploads by world coordinate;
margin lookup therefore does not confuse a recycled 64-column VRAM page with
a different part of the level.

BG1 does not depend on history for unseen leading terrain. The adapter reads
the current decompressed 32x32-metatile map and 8x8 definition table from the
WRAM bank selected by `$9A`, reproduces the cartridge's vertical column-buffer
rotation, and prefills exact shadow entries. A shadow key at camera/object X
maps to source X minus `$0100`, matching the source/destination relationship in
`$B5:ACA8-$B5:ACB7` and `$B5:ADF0-$B5:AE01`. `$0AFC` supplies the horizontal
camera bound; the one staged 32-pixel guard metatile is retained, while later
columns are filled with a character proven transparent from live VRAM. Unknown
cells use that verified-transparent entry rather than falling through to stale
VRAM. Pirate Panic's 32-column BG2 parallax map is intentionally cyclic, so its
already-rendered native scanline repeats into the margins. BG3 remains centered
because DKC2 also uses it for HUD and staging data whose off-screen contents
are not generally valid.

Other modes and screens composed only from bounded 32-column tilemaps are
rendered as the authentic 256 columns centered in the same 342-column buffer.
The adapter clears those side columns before drawing so a wide gameplay frame
cannot survive as stale host pixels on a following menu or room. Bounded-screen
reconstruction remains screen-specific future work; repeating a title or room
is not accepted as widescreen.

DKC2's common object behavior is adapted at two independently identified game
boundaries. The placement-radius loader expands its left allowance by the
per-side margin and its total horizontal span by twice that amount. Both paths
in the shared world-sprite renderer use the same transformation. With
widescreen disabled the helpers return the cartridge constants exactly. In
widescreen mode they also fail closed to those native constants until exact
terrain prefill succeeds for the current scene, preventing objects from being
activated over unavailable terrain.
Generated game C remains private and disposable: the source-owned
`scripts/apply_dkc2_widescreen_overrides.py` locates the named generated
functions, verifies every expected anchor, applies the calls idempotently, and
fails regeneration if SNESrecomp output changes unexpectedly.

The pre-boot launcher and in-game pause overlay edit the same persisted
`widescreen` setting. Switching at runtime clears both host frame buffers,
changes the PPU pitch, and recomputes the presenter viewport. It does not enter
SNES save states, SRAM, input recordings, or deterministic 4:3 hashes.
