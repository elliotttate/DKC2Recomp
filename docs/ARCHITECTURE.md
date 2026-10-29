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

SNESRecomp's active function names live in the source-owned `recomp/bank*.cfg`
files, not in the ignored WLA overlay. The CFG set already carries the broad
revision-0 names imported during structural analysis. The optional
`scripts/promote_snesrecomp_symbols.py` pass expands a residual generic name
only when a private overlay provides one unambiguous contextual alias that
ends in the same full `CODE_BBXXXX` identity. Matching that embedded identity,
rather than the CFG range start, is required because conditional assembly can
move the revision-0 routine while a research label still contains a different
revision's address. Data aliases, bank mismatches, name collisions, and
ambiguous contexts fail closed. `recomp/funcs.h` and private generated C are
then regenerated from the updated CFG; this changes diagnostic readability,
not guest execution or hardware state.

The durable semantic layer is split deliberately:

1. `recomp/bank*.cfg` remains authoritative for structural function
   boundaries and dispatch contracts.
2. `recomp/symbols.toml` records curated function meaning by the exact
   supported-ROM `BB:OOOO` boundary. Revision-dependent historical names are
   aliases, never identities.
3. `recomp/layouts.toml` records confirmed WRAM objects, array dimensions,
   structures, and field offsets.
4. `scripts/build_dkc2_symbol_database.py` validates the layers, applies
   exact-address names to CFG, and generates diagnostic constants, a readable
   reference, and an ignored complete JSON inventory.

The generator rejects missing boundaries, name/address collisions, invalid
WRAM ranges, unknown field widths, duplicate constants, and overlapping
fields. `--check` makes stale tracked projections a test failure. Discovery
therefore accumulates in reviewed metadata instead of ignored generated C.

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
The current configuration has 3,325 roots across 13 banks and emits 3,475
exact CPU-mode variants AOT. Two deliberate original-game fault variants
remain LLE. This does not remove the interpreter: it remains the authoritative
safety tier and handles those explicit dormant edges into non-code bytes if
the original game's buggy calls are ever reached.

Whole-program analysis is available through matching Python and Rust
implementations. Python remains the semantic oracle and automatic fallback.
The Rust path supports HiROM, DKC2's indirect dispatch/return forms, recursive
exit-set solving, declared boundaries, data-region execution, and the same
analysis limits. The current native regeneration converges on 3,325 roots,
3,475 exact AOT variants, two deliberate LLE variants, and 13 emitted banks.
The earlier Python/Rust timing comparison remains in the implementation
journal; it is not projected onto this changed graph without rerunning both
backends.

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

### Runtime-selected interior entry and non-local return boundary

Structural whole-program coverage includes every entry demanded by the graph,
but a game can place an interior address into mutable state and later call it
through a runtime dispatcher. Swanky's gameplay dispatcher at `$B4:9EDC` reads
such a state pointer from `$079C`. The owner-recorded Version 11 run reached
`$B4:A3E0`, `$B4:A475`, and `$B4:A4CB` through that path even though those
interior states were not separate roots in the prior generated dispatch table.

The DKC2 configuration now divides the game-show routine at `$B4:A3E0`,
`$B4:A475`, `$B4:A4CB`, `$B4:A5D9`, and `$B4:A665`, and divides the prize
helper at `$B4:A7CA`. These are explicit AOT entry roots rather than inferred
fall-through-only labels. The interpreter remains the safety tier for a future
runtime-selected address that has no exact compiled variant. Regeneration
verified exact M0X0 dispatch rows for all six entries in `dispatch_v2.c`.
Diagnostics canonicalize the CPU-visible `$B4:*` ROM mirror to `$34:*`; the
two spellings identify the same cartridge bytes, so validators accept either.

A paired runtime call is also a real guest stack operation. The bridge pushes
the two-byte JSR return frame before entering either AOT code or the
interpreter. A compiled target receives `host_return_valid=2`, and both paths
return their `RecompReturn` to the generated caller. State `$B4:A4CB` relies on
an intentional non-local return: with M=0, `PLA` removes the two-byte JSR
frame, then `RTL` removes the surrounding three-byte JSL frame. The
interpreter bridge therefore returns `NORMAL` only when a clean call ends with
S equal to the balanced post-call value. A different S is resolved through
`cpu_resolve_post_return_skip`, clamped to at least `SKIP_1`, and propagated.
Step-cap bailout still restores the balanced post-call S and returns `NORMAL`.
Treating every clean runtime call as `NORMAL` would resume a compiled caller
that the guest already returned past.

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
double-buffered window. A visible Win32 OpenGL context dynamically requests
`wglSwapIntervalEXT(1)` and records whether the driver accepted it. Hidden
automation deliberately requests no interval so a driver-controlled swap wait
cannot stall a noninteractive process. If OpenGL initialization fails—or the user selects the
compatibility backend—the GDI path composes the same frame and black borders
into an off-screen DIB, then exposes the completed client image with one
`BitBlt`. Both paths avoid the former visible clear-then-draw intermediate
surface. It uses waveOut for
fixed 2,048-frame signed-16 stereo blocks, asynchronous keyboard polling, and
per-frame polling of SDL GameController devices, including USB/Bluetooth,
with two player assignments selected by saved SDL GUID. Launcher source choices route
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
deterministic swap boundary. Its visible context requests SDL swap interval
one on Windows and reports the accepted state through the same diagnostic
backend field; hidden automation disables the interval. Visible macOS instead
uses interval zero and paces on the display itself. The window's display link
(`runner/macos_display_link.m`, macOS 14) delivers the display's refresh
ticks on its own thread, asking the panel for 60 Hz so a ProMotion display
ticks at 60 rather than 120; `runner/desktop_pacer.c` measures the tick
interval and locks when one, two, three, or four ticks per frame keep the
frame rate within 2% of the cartridge's 60.098811862 Hz, so a 60-Hz display
shows every frame once and a 120-Hz display twice. Once locked, each frame
waits for the tick that is `ticks_per_frame` after the one the previous
frame followed and presents right after it, which lands every frame on the
same phase of the refresh about 13 ms ahead of it. A frame that misses its
tick presents on the next one without catching up, ticks that stop for 50 ms
release the lock, and a display whose rate cannot be locked (50, 75, 90, or
144 Hz) or a system without display links falls back to the earlier host
clock: one absolute Mach deadline at 60.098811862 Hz, a short final spin,
presentation after the wait, and re-anchoring after a miss of more than 2 ms.
`DKC2_DISPLAY_LOCK=0` keeps that clock deliberately.

Locking the frame cadence to the display makes the game run at the
display's rate, 0.16% slow on a 60-Hz panel, so the audio is kept in step
by dynamic rate control (`runner/desktop_audio_rate.c`) rather than by the
frame clock: every frame's samples pass through a linear resampler whose
ratio, within half a percent of unity, follows an exponential average of the
queue fill against a target of half a device pull plus two frames. The same
control runs under the host clock, where it also absorbs the audio device's
clock drifting from the host's. The queue is primed unpaced to the target
after start, a state load, or a speed change, and only a queue that has
drained below one frame after a host stall runs unpaced again. Swap policy
affects only host presentation; emulation state remains owned by the same
single-threaded host loop. `DKC2_PACING_LOG=<file>` records one line per
presented frame, with the display tick it followed and the time in each
loop stage, and `scripts/analyze_pacing_log.py` estimates from it how many
refreshes each frame was shown for.

The in-game overlay is a second, gameplay-lifetime recomp-ui/ImGui context;
the pre-boot launcher still owns and destroys its separate window/context.
The host-neutral C model owns open/closed state, the Assist Tools gate, a
wrapped 0–4 slot selector, one-shot Resume/Quit/Save/Load actions, and the
validated lifecycle of one active keyboard/controller binding capture.
Platform glue supplies SDL events or a small Win32 input translation, and the
presenter submits ImGui draw data after the game quad but before the same
buffer swap. Overlay geometry comes from `ImGuiIO::DisplaySize`, not the
presenter's physical drawable dimensions, so a Retina/high-DPI window centers
the menu in logical coordinates. The initial placement is centered once and
the title bar remains movable for the rest of the gameplay context. While
open, the hosts schedule no SNES frame, zero game input,
clear/pause queued audio, and continue presenting at the host rate. File-state
actions reuse SNESrecomp's `RtlSaveSlotPath`,
`RtlSaveSnapshot`/`RtlLoadSnapshot`, and `save_name_prefix`; the bounded
selector maps to `saves/dkc2s0.sav` through `saves/dkc2s4.sav`, and only slot
zero probes the old `saves/dkc20.sav` compatibility name. The GDI fallback
composites ImGui into its offscreen 32-bit DIB with the bundled SDLRenderer2
backend and an SDL software renderer before the single final BitBlt. It shares
the Win32 input backend, overlay model and settings persistence with OpenGL.

`RecompLauncherCSettings` is the one persisted settings value shared by the
pre-boot launcher, overlay, Win32 host, and SDL host. The overlay edits every
DKC2 setting shown before boot and exposes independent Player 1/2 input
source, deadzone, and complete gameplay/Assist binding controls. Volume,
texture filtering, screen model,
controller routing, and Assist policy are safe to apply live. Window scale,
renderer, audio enable, and skip-launcher remain restart-bound
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

The two hosts coexist deliberately. Windows remains the accepted public release
and regression baseline. The SDL target now produces an Apple-silicon
`DKC2Recomp.app` with AppKit menus, an icon, bundled SDL2, and mutable state
under `~/Library/Application Support/Flat2VR/DKC2Recomp`. The local bundle is
ad-hoc signed and tested, not Developer-ID signed or notarized. Linux remains a
source target pending native acceptance. The source does not infer success for
hardware or operating systems that were not available to test.

The Mac menu command queue is distinct from configurable Assist bindings.
Fixed Quick Save/Load menu commands are admitted directly to the Slot 1 state
path even when Assist Tools are off; rewind, fast-forward, overlay state
controls, and user-remapped shortcuts retain the opt-in gate. This distinction
does not enter controller registers or serialized SNES state.

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

The DKC2 report now includes the shared runtime's rolling indirect-dispatch
ring: the most recent 1,024 events retain source, target, CPU M/X mode, AOT
hit/miss, mirror resolution, and guest frame. This is host observation only.
`scripts/validate_swanky_run.py` combines that report with tier-2 coverage and
optional performance telemetry. A valid focused run must contain a native
M0X0 `$B4:9EDC -> $B4:A4CB` call and no interpreter cap, Swanky tier-down,
known corrupt edge, or SNES MMIO code address. Canonical `$34:*` report
addresses and CPU-visible `$B4:*` mirrors compare as the same ROM location.

The external private diagnostic packager carries the verified ROM, saves,
launcher settings, control bindings, paired recordings, normal/trace hosts,
and focused validator into a numbered folder outside Git. Its recorder treats
recording-specific files as append-only and deletes only the two rolling host
outputs before launch. A successful session must replace those rolling files
with fresh performance and last-run evidence before they are copied under the
recording's unique basename.

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
16:10 allocates 26 additional source columns per side for a 308x224 PPU
surface; and 16:9 allocates 43 per side for 342x224. With the SNES 7:6 pixel
aspect ratio, those surfaces present at approximately their named display
aspects without scaling the authentic center.

The game adapter chooses a layer policy every frame. In audited Mode 1
gameplay, enabled 64-column BG1/BG2 layers use DKC2's full WRAM camera X and
the live 10-bit PPU vertical source phase. SNESrecomp's shadow tilemap captures
the authentic center and the game's subsequent VRAM uploads in that same
coordinate domain; margin lookup therefore does not confuse a recycled
64-column VRAM page with a different part of the level.

The shadow's vertical key space contains 1,024 8-pixel rows. This is larger
than one 10-bit PPU scroll period by design: the adapter first unwraps the
rendered PPU phase into the level-relative vertical epoch, and a tall room can
therefore address rows 512-1023 even though the hardware tilemap itself is
recycled. Topsail Trouble's lower camera boundary produces exact source rows
512-540. A 512-row store rejected those prefills and made both host-created
BG1 margins transparent; increasing only the host shadow capacity preserves
the resolved coordinate domain without changing cartridge WRAM, VRAM, camera,
streaming, or collision state.

BG1 does not depend on history for unseen leading terrain. The adapter reads
the current decompressed 32x32-metatile map and 8x8 definition table from the
WRAM bank selected by `$9A`, reproduces the cartridge's vertical column-buffer
rotation, and prefills exact shadow entries. Shadow keys derive from the
10-bit PPU phase actually rendered. Horizontal decompressed-map rows instead
select the PPU's low-eight-bit phase nearest camera Y minus `$0100`, matching
the cartridge column builder's staged source page. This distinction matters
when camera Y crosses a 256-pixel boundary: the physical rolling tilemap page
can change while the semantic level-map row remains on the preceding source
page. WRAM camera Y can also contain the following frame's value at the NMI
boundary, so rendered fine phase remains authoritative within the selected
page. A shadow key at
camera/object X maps to source X minus `$0100`, matching the
source/destination relationship in
`$B5:ACA8-$B5:ACB7` and `$B5:ADF0-$B5:AE01`. `$0AFC` supplies the horizontal
camera bound; the one staged 32-pixel guard metatile is retained, while later
columns are filled with a character proven transparent from live VRAM. The
vertical decoder's terminal-edge rule below masks that guard only outside the
authentic viewport; other accepted layouts retain it. Unknown
cells use that verified-transparent entry rather than falling through to stale
VRAM. Pirate Panic's 32-column BG2 parallax map is intentionally cyclic, so its
already-rendered native scanline repeats into the margins. Bounded 32-column
BG3 remains centered or uses only an explicitly proven rendered-scanline
repeat. An enabled physical 64-column BG3 may join the final render mask only
after the exact BG1/BG2 terrain owner has passed the same readiness gate; a
wide `BG3SC` register by itself never opts a title, menu, or staging screen in.

### Ship-deck rigging decode

The Gangplank Galleon deck levels draw their foreground rigging on that
physical 64-column BG3, and the cartridge streams it with no lead at all:
`$B5:AA88` uploads the one 8-pixel column entering the native view the frame
it arrives, and `$B5:AC25` rewrites a row's 64 ring words from a buffer of
which only the 33 native columns were rebuilt. Every ring column outside the
native window therefore holds either the column from 512 pixels away or a
previous row's leftovers, and a margin read from the ring showed a second
rope strand cutting the real one off at a false apex. The rigging map is
static ROM data (bank `$F5`, map at `$26A7`, 40 column-major columns of
sixteen entries wrapping at 1280 by 512 pixels, 32-byte metatile definitions
at `$2087` whose bits 14-15 mirror the definition and toggle the tile's own
flip bits), so the host decodes it (`Dkc2VideoDecodeRiggingTile`) into a
third world-keyed shadow layer (`kDkc2RiggingLayer`), keyed by the rendered
PPU phase like the terrain owner (horizontally through the 10-bit scroll,
vertically through the layer's 8-bit scroll rebuilt in 256-pixel epochs
from the camera) and served through the same 2bpp renderer hook the 4bpp
layers already had. The decode is trusted only after it
reproduces all 32 fully uploaded native columns over the 28 fully visible
rows for the current frame (through the row upload's own high-byte shift,
`Dkc2VideoRiggingCellMatches`, described in HARDWARE_NOTES.md), and the
streamer is recognized by its own latches (`$C6` for the last column origin, `$17CE` for the last row
origin, each within one cell of the rendered phase); a recognized rigging
layer whose decode fails shows no margin at all rather than the ring. The trace reports
this as `rigging` with the native verification counts.

### Level-name cards

A level-name card runs the cartridge's NMI sub-mode 11 inside the
gameplay mode: a static picture on bounded maps with no camera and no
terrain stream, which the layout path cannot extend. The host presents a
card like every bounded screen, centered between black margins, and
never through the terrain path (a card whose picture sits on a
64-column map used to reach the unproven-terrain black fill instead,
with the same look). The 64-column cards hold a wider painting on the
right, but at scroll zero their left margin could only be the map's
wrap, and the 32-column cards have nothing beyond their 256 columns at
all; a mirrored presentation was tried and the owner preferred black.

### World store depth

The world-keyed store holds 4,096 tile columns by 2,048 tile rows
(32,768 by 16,384 pixels). It held 1,024 rows until Parrot Chute Panic:
that shaft is 13,040 pixels tall and its unwrapped keys pass row 1,700
near the bottom, where the smaller store silently rejected every capture
and prefill (`InBounds`), every margin lookup missed, and the terrain
margins showed the blank tile with the backdrop layers behind it. The
trace exposed it as `terrain_source.prefill` with zero present cells and
a `shadow` row of nothing but misses and blanks.

### Object planes

Haunted Hall draws Kackle as a 32-column by 13-row block into the left
page of a 64-column BG2 map (`$6800`) and moves him with the layer's
scroll; the right page is blank. The band is not at the terrain phase and
the map is rewritten as he animates, so it was a repeat band: the native
line repeated into the margins cut his off-screen part at the 4:3 edge and
copied his visible part into the far margin. A 64-column map with exactly
one populated page (`Dkc2VideoTilemapIsObjectPlane`) is now a plane band
without the static gate: read raw, the map's wrap shows his off-screen
part beside the view and nothing in the far margin, which is what a wider
console would draw.

### Object windows under the presentation bias

The cartridge keeps three camera-relative windows for objects: the sprite
renderer's cull (`$B5:9F40`, [-$30, $130)), the placement activation
radius (`$BB:BB07`, per-class pairs such as [-$20, $120)), and the live
sprite list's release window (`$B5:9C52`, [-$30, $130) horizontally and
[-$10, $120) vertically, built as x - camera - $80 + $B0 < $160). The
generated code adaptations (`scripts/apply_dkc2_widescreen_overrides.py`)
route each window's left slack and span through `Dkc2VideoExpandCullLeft`
and `Dkc2VideoExpandCullSpan`. Those helpers add one margin per side once
terrain is proven, and now also account for the presentation bias the
host rendered last (`Dkc2VideoSetPresentationBias`): the presented window
is the camera shifted by the bias, so the left slack is `extra - bias`
and the right slack `extra + bias`. Before this, the release window was
unadapted and the radius ignored the bias, so a barrel cannon standing in
the right margin next to a level's left wall (bias +26 at 16:10) was
released 314 pixels past the camera while the presented window reached
334, and it vanished on a small step left.

### Level-map row stride calibration

The decoded level map is column-major for horizontal stages and
row-major for the rest, with a bytes-per-metatile-row stride the scroll
handler bakes in: 64 for the vertical shafts, 192 for the square
scroller's audited stage (`$B5:B555` multiplies the row by six), 32 for
Parrot Chute Panic, 160 for the ship holds. A stage can run a different
column builder than its sub-mode suggests: Bramble Blast (`$002D`,
sub-mode `$10`) stores 80 metatiles per row like a ship hold, and
decoded with 192-byte rows every margin cell came from another row of
the map, which showed as blocks of unrelated tiles beside the view. The
prefill now verifies the stride in use against the fully staged native
window (32 columns by 28 rows of the ring, compared on the character
index) every frame; below 90 percent it tries every candidate stride
(32 to 256 bytes in 32-byte steps) and adopts the best one above the
gate for the rest of the stage (`Dkc2CalibrateRowStride`,
`Dkc2VideoDecodeLevelTileRowMajor`). With no candidate above the gate the
terrain stays unproven and the margins are black. The trace reports the
stride and its match as `terrain_source.stride`.

### The terrain phase

Every world key, the prefill's source rows, and the band classification
take the terrain layer's rendered scroll phase from the frame-start
BGnHOFS/BGnVOFS pair, which is the camera phase, trailing it by at most
a few pixels, in every stage audited before Slime Climb. Slime Climb
leaves BG1VOFS at $50 at frame start and lets its HDMA write the camera
row (with about two hundred bands for its water) on every rendered line,
so nothing displays the frame-start value: keyed from it, the store held
the wrong rows, every band was classified as a repeat, and the margins
showed pillars and rafts repeated from the native line. The host now
selects the phase per frame (`Dkc2VideoSelectTerrainPhase`): the
frame-start pair stands when it lies within the terrain lead of the WRAM
camera phase; otherwise the band pair within that lead covering the most
rendered lines replaces it. The trace reports it as
`terrain_source.phase`.

When neither the frame-start register nor any band is at the camera
phase, the phase follows the scroll that covers at least half the
frame's lines, provided it lies within `kDkc2VideoTerrainPhaseFollow`
(32 pixels) of the camera. Toxic Tower on Rattly is the case: the
bounces move the camera five pixels a frame and BG1's scroll follows
through the HDMA table from line 1, so the register is a frame behind
and beyond the four-pixel lead, and every band is five pixels beyond the
register. Keyed on the register, every band read as off-phase and the
whole layer fell to the repeat policy for that frame, repeating the
ring's edge columns into the margins as a one-frame flash. A parallax
layer's scroll sits hundreds of pixels from the camera and never
qualifies.

### Static plane bands

A wide BG1/BG2 band that is not at the terrain phase used to repeat its
rendered native scanline into the margins at the period its interior
proves, or at 256 pixels when it proves none. That is right for a bounded
map and for the ship hold's 96-pixel cabin wall (a 64-column map whose
pattern does not divide 512 pixels, so the cartridge re-bases its scroll
to keep the wrap seam off screen), but it cut Red-Hot Ride's foreground
rocks at the 4:3 edges: the rocks and the far lava spikes share one static
64x64 map (`$6400`) that HDMA swaps between BG1 (the rocks, at twice the
camera speed, below the lava line) and BG2 (the spikes, at half speed,
above it), and a 512-pixel plane repeated at 256 shows the wrong half of
itself beside the view. The HDMA scan now records each band's BGnSC, and a
band whose map is 64 columns wide, is not the terrain stream's destination
(`$17B6`), has had no VRAM write since the camera last traveled 24 pixels
(the engine stamps every VRAM page with the frame of its last write;
a level starts with its pages counted as traveled, and the audited
non-terrain maps are uploaded once at load), and whose content is authored
to continue across its own wrap (`Dkc2VideoTilemapWrapsAuthored`: no
populated row with a shortest period that fails to divide 64 columns, no
blank strip of four or more columns at either map edge) is a plane band
(`kDkc2BandPolicyPlane`). The engine presents it through a raw band
(`PpuSetWidescreenLayerRawBand`): the layer's own map continues into the
margins as the hardware wrap, the world-keyed shadow is bypassed like a
repeat band, and no padding merge replaces the rendered margins. The trace
reports the plane band count per layer as `planes`.

Two refinements from Toxic Tower. A cell counts as painted only when its
entry is non-zero and names a character with non-zero pixels
(`Dkc2VideoCharacterIsTransparent` at the layer's character base): the
tower's top-of-screen wall map fills the cells beyond its slanted edge
with `$8000`, a flip flag over character 0, which a non-zero test took
for painting, so the map passed as wrapping authored and the margins
showed the backdrop through the wall in patches that moved with the
per-line scroll. And a band is presented as a plane only when none of
the rows it shows, from its own vertical scroll and scanlines, is a
broken row (`Dkc2VideoTilemapBrokenRows`): a row painted in at least 48
of its 64 cells whose painting stops four or more cells short of either
wrap edge, a strip narrower than its map rather than scattered
decoration. Such a band repeats the ring instead. The verdicts are
cached per map with the character base they used.

### Lava geyser steam decode

The lava stages that run the cartridge's NMI sub-mode 18 (Red-Hot Ride)
draw their steam columns on a bounded 32x32 BG3 that scrolls with the
camera. The cartridge keeps the stage's geyser positions in ROM (a list at
`$B3:D65B`, bit 0 marking a tall column, `$8000` ending the list, WRAM
`$0959` holding the stage's first entry), registers the geysers near the
view in four WRAM slots (`$095B..$0961`: map word offset, bit 15 tall,
bit 14 being cleared), and draws each registered geyser as a three-column
block of 2bpp map entries (`$80:CB71`, one column DMA per block column with
`VMAIN=$81`) from the column-major animation tables the long-pointer table
at `$80:D3AD` selects: four short frames (three by ten, map rows 14-23),
four tall (three by eighteen, map rows 6-23), the frame being (frame
counter `$2A` >> 2) & 3, the leftmost map column ((X - 8) >> 3) & 31. A
256-pixel map wraps a geyser standing beside the view onto the opposite
edge, so the repeat policy's hardware wrap put a steam column over solid
rock in the margin, and the cartridge clears a slot as soon as its geyser
leaves the native view, so the ring held nothing for a geyser standing in
the margin. The host now decodes the geyser list and the animation tables
(`Dkc2VideoGeyserEntry`) into the third world-keyed shadow layer
(`kDkc2GeyserLayer`, shared with the rigging: world X from the rendered PPU
phase, world Y the PPU scroll itself since the map is periodic in Y),
forces every margin cell and a 24-pixel inset of each native edge (three
block columns, where the cartridge's own wrap sliver lands) each frame,
blank where no geyser stands, and trusts the decode only after it
reproduces every geyser block the cartridge has fully drawn this frame (a
registered but not yet drawn block, blank until the next four-frame
animation tick, is skipped). A geyser stage whose decode fails keeps BG3
out of both the widen mask and the repeat policy for that frame, so it
shows no BG3 margin rather than the wrap. The trace reports this as
`geysers`.

What a host margin shows where the level authors nothing is a selectable
edge policy (`Dkc2VideoEdgePolicy`, environment `DKC2_WIDESCREEN_EDGE`,
launcher key `WidescreenEdge`). Every known layout authors terrain from
world X=`$0100` through `maximumScrollX+256`, so the question arises within
one margin of a hard level wall and in rooms narrower than two margins.

- `reflect`: the presented view stays locked to the cartridge
  camera, both margins remain visible, and the terrain decoder mirrors the
  nearest authored columns across the boundary with the horizontal flip bit
  toggled (`Dkc2VideoResolveEdgeTile`). Within one margin of a wall a
  physical 64-column BG3 repeats its rendered line instead of reading ring
  columns the level never authored.
- `bars`: the presented view stays locked to the camera and each visible
  margin is clamped to the authored extent through the shared PPU's per-side
  margin, so the unauthored strip is black.
- `shift`: the presented view is moved inward by a bias while the room can
  absorb the margin, and centered with clamped margins when it cannot. The
  renderer shifts BG scroll and OBJ placement together. This keeps every
  margin inside the authored extent, but the view stands still for the
  first margin's worth of camera motion away from a wall and then starts
  scrolling at the camera's catch-up speed, and every sprite, HUD included,
  slides with the bias. Measured on the hard-left lava state at 16:9: the
  camera moves from 256 to 298 over frames 15-33 while the presented view
  stays at 256, then scrolls from frame 34.
- `glide` (default): the same pins as `shift`, so the margin never leaves the
  authored extent, but the inward bias is released one pixel per eight
  pixels of camera travel from each wall (`kDkc2VideoEdgeGlideSpan`)
  instead of all within the first margin. The background scrolls at seven
  eighths of the camera speed for the first eight margins away from a wall
  (344 pixels at 16:9, 208 at 16:10) and the sprites drift over it at one
  eighth of their speed, then everything is centered and locked. At the
  wall itself `glide` and `shift` are pixel-identical.

The policy is chosen in the pause menu's Settings page ("Level edge"),
remembered in `launcher.cfg`, or overridden for one run with
`DKC2_WIDESCREEN_EDGE`.

A bias changes what the PPU's own 256 columns contain. With bias b the
cartridge's authentic VRAM window sits at screen columns [-b, 256-b): the
first b columns of the 4:3 image move into the left margin and the last b
native columns are beyond anything the cartridge wrote for them (a rolling
ring's stale or prefetched page). The host therefore tells the world-keyed
shadow which end of the native window is not authentic
(`WsShadowSetNativeViewportInset`), so the renderer's native fast path stops
at the authentic window and those columns come from history and decoded
terrain like any margin; the same bounds drive the per-pixel split inside a
tile chunk that straddles the boundary. Repeat bands, which bypass the
shadow, do the equivalent in the padded merge: only the authentic window is
copied from the isolated render, a 32-column map's remaining native columns
are kept because they are its exact hardware wrap, a 64-column ring's stale
tail is continued from the authentic window, and the period detector and the
stale-endpoint repair work on the intersection of the authentic window and
the screen interior, so at bias 0 nothing changes.

Objects follow the same rule: the renderer shifts every object by the bias,
and the game's own culls (already widened by the margin) place objects for
the presented right margin up to the bias beyond the authentic margin. The
shared PPU's nine-bit object X decode counts an X as positive up to the
current right margin plus a positive bias (`PpuDecodeOamX`); before that,
a Zinger beside the rope net at the start of Topsail Trouble lost its right
half at exactly the authentic margin.

The prefill also distinguishes the cartridge window from the presented one.
Every presented cell outside the cartridge window takes the decoded map
over live history, the columns a bias slides into view included (a game
write from the last frame still wins, as in every margin); the columns a
bias moves into the margin are still inside that window and keep their
captured ring content, so a bottom guard row the cartridge has not staged
yet renders the same stale line the console shows instead of a decoded one.
The slid-in columns once kept whatever history they had, and that history
could be a misattributed capture: in a vertical stage the cartridge rewrites
the ring's other page with the same stale 32 entries on every row upload,
and the store attributes other-page writes by the last horizontal travel
direction, which a one-pixel camera jitter flips. The crow's-nest art then
sat on the mast at the right wall until the stage was left.

### Reconstruct upscaler (experiment)

This shader is implemented by the SDL presenter, including `DKC2RecompSDL.exe`
on Windows. Neither native Win32 GDI nor Win32 OpenGL implements it. The shared
pause Settings page now gates Reconstruct by host platform and displays the
effective nearest/bilinear sampler on Win32, without rewriting a saved shader
preference. Unsupported tuning controls are hidden and the page names the
supporting executable. The pre-boot launcher and native dropdown already
expose only the capabilities implemented by their host.

The macOS presenter is a fixed-function OpenGL 2.1 blit with nearest or
bilinear sampling. On a 16-inch MacBook Pro the 342x224 frame is shown at
about ten times its size, a fractional scale at which nearest gives uneven
pixel widths and bilinear blurs. Reconstruct (`desktop_present_sdl.c`) is
a single GLSL 1.20 fragment pass over the same texture, resolved through
SDL's GL entry points, that treats every output pixel analytically instead
of resampling a fixed 2x or 3x grid:

- Sharp boundaries: inside a texel the color is flat; within one output
  pixel of a texel edge it blends with the neighbor (the sharp-bilinear
  idea), so straight edges are crisp and moire-free at any scale.
- Dither decoding: a 2x2 checkerboard between two colors, or a one-texel
  vertical or horizontal line dither, is what the artists used for a
  mid-tone a CRT would blur; the texel takes that average before anything
  else looks at it. Only exact 3x3 patterns qualify, so genuine one-pixel
  lines are untouched.
- Diagonal edges: the xBR level-1 corner test on the 21-texel footprint
  decides whether a texel corner belongs to a diagonal edge; that corner
  takes the nearer neighbor's color along an antialiased 45-degree line,
  and level 2 adds 2:1 and 1:2 lines where the test says the edge
  continues. The pre-rendered DKC art keeps its shading and loses the
  staircase.

- Softening: the transition band of every reconstructed edge and pixel
  boundary widens from one screen pixel to up to three, and where a
  texel's neighbors are close in color (a shading band of the pre-rendered
  art rather than an outline) the flat interior blends toward a bilinear
  gradient, so terraced faces and dithered skies read as continuous
  shading while outlines keep their contrast.

Modes 0..4 enable the stages cumulatively (mode 4 adds 3:1 slopes) and the
strength, softness, and smooth-shading sliders scale the edge blend, the
band width, and the gradient blend, so each can be judged on its own; the
default is level-2 slopes with softness 50 and shading 60, which the
owner's play test preferred over the hard first cut. The shader runs after
the screen-model lookup table, so CRT, Composite, and Trinitron still
apply. When the shader cannot be built the presenter reports why and falls
back to the sampler implied by the texture filter. `DKC2_DESKTOP_SCREENSHOT`
reads the drawable back from a hidden run, and `DKC2_DESKTOP_TEST_LOADSTATE`
starts that run from a preserved state, which is how the experiment is
captured for comparison without a visible window. The state path must be
absolute, since the Mac app changes into its user directory at start.
`DKC2_PACING_LOG` and `DKC2_DISPLAY_LOCK` are the visible host's pacing
switches, described with the presenter above.

### CRT television display

The presenter's second display mode draws the finished frame as a tube
would show it. `runner/desktop_crt.c` is the model with no OpenGL in it:
the persisted settings (a preset and six percentage sliders), the presets,
and `Dkc2CrtDerive`, which turns them and the viewport into one frame's
shader parameters. `desktop_present_sdl.c` runs five GLSL 1.20 programs
over half-float `EXT_framebuffer_object` targets sized to the viewport, in
linear light:

- Lines: each source row decoded from sRGB and resampled to the viewport
  width with a Gaussian in source pixels (the video bandwidth; sharpness
  sets its width from 0.60 to 0.25 pixels), one target row per line.
- Beam: every output pixel sums the Gaussian beams of the nearby lines,
  each as wide as that line's brightness per channel, from `sigma_dark`
  (the scanlines slider, 0.50 to 0.18 line pitches) to 0.50 for white.
  The kernels are normalised, so the mean over a line pitch equals the
  source brightness for any width and no light is lost; a white field
  ripples by about five percent (its lines vanish) while a dim one shows
  its lines clearly, which is the behaviour of a real beam rather than of
  a fixed line pattern, and the analytic evaluation at every pixel has no
  grid to beat against the panel's fractional 8.7 to 10 pixels per line.
- Glow and halation: a 4x4 box reduction of the beam image blurred both
  ways, and a further reduction of that blurred again; the compose pass
  adds them and reduces the direct light by the same fractions.
- Compose: the beam image through cylindrical curvature (up to three
  percent, horizontal more than vertical) with rounded corners and a faint
  vignette, the phosphor mask in window pixels (an aperture grille of
  three or six pixels per triad, or a staggered slot mask) with the inverse
  of its mean transmission as gain and a soft knee above 0.9 in place of
  clipping, then sRGB encoding and a triangular dither of half a code
  value so the beam's gradients do not band.

Below three to five output pixels per line the beam fades to the flat
image, and below five to seven per column the mask fades out; both come
from the viewport, so a 1x or 2x window is simply a soft flat image. The
mask is the honest SDR compromise: at the default strength of 0.3 a white
field loses a few percent to the knee, and stronger masks dim, since the
panel has no headroom to pay for them. `Dkc2SdlPresenterSetDisplay`
selects the mode; when the programs or targets cannot be built it stays
flat and reports why in `crt_error`. The window and the hidden capture
share one `RenderFrame`, so `DKC2_DESKTOP_SCREENSHOT` records the whole
chain, and `DKC2_DESKTOP_TEST_WINDOW=WxH` sizes a hidden window in points
so a capture can be taken at the panel's full drawable.
`scripts/crt_capture_compare.py` judges a flat and a CRT capture of the
same state: the mean linear luminance within three percent, the strongest
short period in the row profile equal to the line pitch, and no periodic
residual between the smoothed profiles beyond the scene-following trend.
The pause menu's upscaler combo is disabled while the tube is on, since
the lines pass is the scaler; the phosphor-color model still runs first.

### Dispatch tables with null slots

The recompiler resolves a `JSR (abs,X)` into a static switch over the
table it reads from the ROM, stopping at the first null slot because a
real handler never sits at `$0000`. DKC2's sprite sub-state dispatcher at
`$B3:CB3D` breaks that assumption: its 16-slot table holds the six
handlers, two null slots, the six again for objects whose script has set
sub-state bit 3 (touch damage), and two more nulls. Auto-read to six,
every object with bit 3 set took the out-of-bounds arm and its behaviour
script stopped stepping, which is why Screech's Sprint's Kloak hung idle
instead of throwing. The site is declared in `recomp/bankb3.cfg` with
fourteen entries, and the decoder treats a null slot inside a declared
table as a null entry (codegen emits its unpop-and-fall-through arm), as
the explicit target form always did. No other DKC2 dispatch site has a
null slot followed by handlers.

### The map-derived west hold

The glide slides the frame inward at the level's walls and releases the
slide with travel away from them, but it only knows the walls the WRAM
gives it: the map's first page (`$0100`) on the west and `$0AFC` on the
east. A level can hold the player at the authored world's edge with no
camera bound there and nothing in the map beyond: Screech's Sprint
starts on a plank platform at world 608, the camera cannot go west of it,
and the map is empty for ten columns west. The glide saw a camera well
inside the level, kept both margins, and the platform ended at the
native edge with the bramble backdrop beside it. The prefill now reports
a west hold when the columns the unbiased margin would reach beside the
cartridge window are empty for the whole visible height
(`Dkc2VideoHoldWest`) and the player is pinned there: within forty pixels
of the frame's west edge (the camera leads a walking player by about
sixty, so twenty is a player the camera failed to centre) with the camera
unmoved since the last frame, or the level has just started, since a
level opens with its camera at a bound and a fresh Screech's Sprint
spawns Diddy sixty pixels in where the pin would wait for him to walk
into the edge (the first eight prefill frames after a level change; a
state restore is not a level start). Emptiness alone is not a hold:
Toxic Tower's interior is empty on BG1 beside a freely scrolling camera.
Once entered the hold persists while the void stays beside the window,
whatever the camera does. The window's first column at entry becomes the
west bound
(`Dkc2VideoPresentationMarginsBounded`), the frame slides so nothing west
of it shows, and the slide releases with travel as at any wall. The
presented bias moves at most one pixel per frame toward the glide's
target (`Dkc2VideoMarginsForBias` derives the margins from the bias the
host chose), so a hold that appears or vanishes as the camera scrolls
vertically never snaps the picture; a level change or a state restore
sets the bias directly. The east side keeps `$0AFC`.

### Structural wall continuation

A level map can hold wholly transparent 32x32 metatiles beside a shaft or
wall that the console can never show: the player, not a camera bound, stops
there, so no WRAM value tells the host where the reachable region ends. A
margin reaching such cells showed the backdrop through a hole the console
never has (crystal mine at camera 448, where the shaft's west wall sits at
world 448 and the map is empty west of it). The prefill now applies the
rule DKC1Recomp proved on its Croctopus walls
(`Dkc2VideoFindStructuralWallSource`): when a margin metatile column is
transparent from the visible top down through the row (a column empty
for the whole height, as in the crystal shaft, or one empty above a floor
that continues past the wall, as in the mine section at camera 256 whose
neighbouring room showed its backdrop through the void above that
floor) or the row lies in an empty run at least four metatiles tall that
a wall seals from the view on every row of the run (the unauthored gap
between two mine shafts, thirteen rows of void under a ceiling of
authored rock that the camera scrolls into view first; a porthole or
doorway is one or two rows, and a flooded hold's water beyond the view
opens into the view above the crate the rule would otherwise continue
into it), the first non-empty
metatile toward the native edge on a row is fully populated and backed by
another fully populated metatile toward the native center, and an adjacent
metatile row repeats that empty/full/full relationship (the proving row
must show the wall two thick as well), the wall is continued
from that source metatile; any partial metatile in between is an authored
opening and fails closed. The two extra requirements are what separate a
void beside a wall from an authored feature: a ship-hold porthole or a
doorway is an empty metatile with wall above and below it in the same
column, and a mast, post, or crate standing in open sky is one metatile
thick. The first cut without them filled Rattle Battle's portholes with
planks and stacked crates into Topsail's sky; a later cut that let the
proving row be thin put mast wood into that sky beside a one-cell mast on
the single row a sign hangs next to it. Metatiles are classified by
decoding their sixteen tiles and testing each character in live VRAM
(`Dkc2VideoCharacterIsTransparent`). The rule acts only on margin cells
outside the cartridge window, under every edge policy, and never on native
pixels. Where such a column has been corroborated as a wall on any visible
row, the rows the rule fails closed on (a cave pocket's boundary rows,
whose edge metatiles are partial) mirror the authored terrain across the
wall line instead (`Dkc2VideoMirrorSourceTileAcrossEdge`, the reflect
policy's geometry applied at a held wall): the pocket becomes a symmetric
hollow in the wall rather than an opening onto the backdrop wider than
the console ever shows, and rows whose edge metatile is empty mirror the
open cave, so the pocket's interior stays open.

What a continued cell shows is decided by the level map's own adjacency,
not by copying the wall's edge column. Copying repeats whatever stands in
that column once per margin column: a mine's panel of red lamps beside the
Kongs at a shaft's screen edge appeared three times in a row. The map
knows what belongs beside each of its metatiles, because the same lamp
panel sits at the edge of ten other shafts with the level's rock fill to
its east. A continued cell therefore takes the fully populated metatile
the map most often places beside the previous one on the outward side
(`Dkc2VideoMetatileNeighbours`, decoded by id with
`Dkc2VideoDecodeMetatileEntry`), column by column away from the wall,
which reproduces the level's own fill sequences (the 128-pixel periodic
rock of the mines and crystal shafts). A wall row the map never continues
(the lamp panel's unique upper half) starts its chain from the nearest
wall row above or below that the map does continue, so the panel is not
repeated; a wall with no such row at all copies its edge column as
before. Successors and per-id fills are cached per level and recounted
every 256 frames, since a level can decompress a new map section into the
same bank addresses. The trace reports the structural, mirrored, and
chained counts as `terrain_source.wall`.

The cartridge camera, collision, exits, streaming, and WRAM stay stock under
every policy; a fine-scroll guard tile outside the extent is verified
transparent. The former west-reflection and vertical-only east-mask tile
policies are subsumed: `reflect` mirrors at both boundaries by the same
rule, and the streamer's guard metatile beyond the extent is never shown.

Exact non-transparent terrain cells seed only missing shadow history, since
some stage details are legitimately written dynamically by the game. In
contrast, a decoded character index proven transparent (or an out-of-map cell)
is actively written as transparent for that world position. The terrain shadow
gives a live game write from the current or immediately preceding frame priority
over that clear. This removes stale recycled VRAM in void margin cells without
replacing active ship/foreground details with the static level map.

That BG1 ownership is not global. The `bg-01` evidence at frames 4,500 and
4,800 has `$17B6=$7800`, matching BG2's tilemap base while BG1 is `$7000`.
The adapter therefore matches the live stream destination, masked to its
`$400`-word tilemap base, against the enabled BG1/BG2 `BGxSC` bases. It keys
the matching terrain shadow to full camera X and the rendered PPU source Y,
decodes the decompressed map into that layer, and applies the periodic
parallax fold to BG2 only when BG2 is not the terrain owner. An unmatched
destination fails closed instead of guessing a layer.

Map geometry is classified separately from the live level `game_sub_mode` at
`$0529`. The reference-validated DKC2 main-loop table distinguishes
horizontal column-major terrain, vertical row-major terrain, and the square
scroller used by Bramble sub-mode `$10`. Exact prefill uses the corresponding
address formula; Bramble's square map has 48 metatiles per `$60`-byte row.
Ship-hold sub-mode `$02` is a separate rolling layout: its NMI handler still
uploads level rows and columns, but its decompressed source is row-major with
80 metatiles (`$A0` bytes) per row. Lockjaw's Locker's preserved exact state
matched 957/957 sampled visible BG1 cells with that formula. It therefore uses
the normal world-keyed shadow/prefill path rather than exposing the recycled
64-column VRAM ring as a static map.
Wasp-hive sub-mode `$03` normally calls `square_level_scroll_handler` at
`$B5:B54A`, so ordinary hive rooms share the 48-metatile/`$60`-byte source-row
decoder. Parrot Chute Panic is a separately proven exception: level `$0013`
takes the alternate `$B5:B317` path. Its 512-pixel map has 16 metatiles per
`$20`-byte row, and its live terrain target selects BG2 `$7800`. The scene
classifier therefore combines sub-mode and level identity rather than forcing
all hive rooms through Parrot's formula. Ordinary hive widening is explicitly
experimental until Hornet Hole, Rambi Rumble, and King Zing have route and
per-layer acceptance. Other square or special main loops still return
`unknown` and force a centered 256-column guest frame; a temporary 64-column
`BGxSC` value alone does not opt a screen into widescreen.

That Y choice is also required for history correctness. DKC2's terrain
tilemap is staged one 256-pixel page above camera Y. The `bg-02` route proved
that prefill/lookup already used that source-row domain while native viewport
capture and subsequent VRAM writes were still recorded under raw camera rows.
During vertical movement those misplaced historical cells could later win
over exact decoded cells. The selected terrain layer now unwraps the rendered
PPU tile origin near camera Y once, restores the fine three-bit phase, and uses
that result for capture, write history, lookup, and prefill. Masking before the
unwrap is required at the exact 512-pixel tie: otherwise the fine value and
tile-aligned prefill can select opposite 1,024-pixel epochs. Because selection
still comes from live `$17B6`, the correction
applies to standard rolling terrain on either BG1 or BG2 without a level ID.

Every other presentation decision is a property of the live PPU geometry
rather than a level identity; the adapter keeps no list of scenes.

A background is *bounded* when its tilemap is 32 columns wide, or when its
64-column allocation is not physically its own: the extension page of a
64-column map that is another enabled background's base page (Mudhole Marsh
BG3 `$6D` extends from `$6C00` into BG1's `$7000` map) holds that other
layer's rows, so `Dkc2VideoTilemapPagesCollide` classifies it as bounded.
Every enabled bounded background repeats its rendered native scanline into
the margins, where "enabled" is the union of the frame-start TM/TS and every
HDMA band's (the ship hold's BG3 water surface is switched on only inside
its band, with TM zero at frame start; read from the frame-start registers
alone it was neither wide nor repeated and stopped at the 4:3 edges). That
is exactly what a wider PPU would draw from a map that wraps at 256 pixels: HDMA phase, hardware windows, and color-math
participation are already in the rendered line, and the isolated-layer merge
cannot expose unwritten VRAM. This one rule covers the ship-hold water,
Topsail rain, Mainbrace and Krow's Nest cloud/lighting planes, Mudhole's
forest silhouettes, Parrot Chute Panic's hive backdrops, the lava-stage BG3
effect plane, and any bounded layer in a room that has never been audited.
An enabled 64-column BG3 whose pages are its own renders its authentic
adjacent columns after the terrain gate, which covers the Pirate Panic and
Rattle Battle rigging.

A 32-column map wraps at 256 pixels on hardware, so its repeated line keeps
exactly that period. A bounded backdrop kept in a 64-column allocation has
no hardware wrap to fall back on, so for the 64-column BG1/BG2 layers the
shared PPU continues each repeated line at the period that line's own
rendered interior (X=7-248) proves at least twice, up to 120 pixels, and
keeps the 256-pixel repeat when no period is provable
(`PpuSetWidescreenLayerRepeatAutoPeriod`). A ship-hold cabin wall is periodic
in rendered pixels at 96 pixels even though its tilemap uses a distinct
character index per column, so tile-level checks cannot see it; the pixel
rule reproduces the former hand-tuned 96-pixel continuation on every
periodic row and leaves the non-periodic picture rows at 256. The same
layers rebuild their seven endpoint pixels from that period, because only a
64-column ring can show stale fine-scroll columns. The rule is not applied
to 32-column maps, whose 256-pixel wrap is what the console itself shows
once the layer scrolls; the lava stage's BG3 plane has an authored seam at
its wrap, and the margins reproduce it rather than invent a continuation.

Rolling 64-column BG1/BG2 layers are classified per scanline band. Before
drawing, the adapter walks the HDMA tables the cartridge has already built
for the frame (`runner/dkc2_hdma.c` mirrors the runner's own table walk,
including the BG offset write latch, TM, and TS) and records the exact BG
scroll and screen-enable values every rendered line will use; consecutive
lines with identical values form a band. For each wide layer and band, the
layer is at the *terrain phase* when its band scroll is within the measured
lead tolerance (six pixels horizontally, four vertically) of the scroll the
live terrain owner rendered at the frame anchor. A terrain-phase band is
served from the world-keyed store: the owner reads its own store, and the
other physical layer reads the owner's store through a read-only alias view
(`WsShadowSetEntryAlias`) that shares the owner's keys, so the renderer's
per-line scroll delta selects the exact world cell without any backup or
restore of shadow cells. Any other band repeats its rendered line. The
lava-stage HDMA compositions, in which BG1 displays the streamed map in an
upper band while BG2 displays it below and each layer shows a lava plane in
the other band, follow from this rule with no swap direction, composition
signature, sticky state, or per-scanline detector. When no terrain owner or
exact prefill is available the wide layers are clamped, so an unproven
rolling layer shows no margin content rather than raw recycled VRAM, and
the host paints both margins black after the frame rather than letting
the PPU's backdrop color show there (Barrel Bayou's level intro, a static
picture on BG1's own map with no terrain stream, sets that color to pure
blue; the console never shows it).

Under the `shift` policy the presentation bias makes the presented 4:3
region straddle the PPU's own margin boundary near a level endpoint: a bias
of +43 places the first 43 columns of the authentic viewport left of screen
X=0. A repeated layer therefore renders those columns from real VRAM,
exactly as the unbiased frame would, and applies its repeat or period
continuation only beyond them (`PpuWidescreenRepeatAuthenticExtra`). The
presented 4:3 region never depends on a repeat approximation under any
policy.

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

Bananas bypass both common boundaries. DKC2 walks a compact banana-list and
writes its compound tiles directly to OAM. The source-owned regeneration
adapter therefore also widens the four constants in the dedicated banana
index/render/clip routines, gated by the same terrain-readiness contract. The
fourth constant is a renderer-local negative-X allowance: native DKC2 accepts
15 pixels beyond the left edge, while ready widescreen terrain accepts those
15 pixels plus the 43-pixel host margin. This is distinct from list activation
and from the formation span, and remains byte-for-byte native in 4:3. The
original OAM packer obtains its high-X bit from coordinate bit 15 because the
native viewport only needed negative off-left positions. For the widened
right margin, `Dkc2VideoPromoteOamXHigh` mirrors coordinate bit 8 into bit 15
immediately before that original packer; native mode and unready terrain are
unchanged. The transformation is deliberately restricted to the two banana
OAM writes rather than changing general PPU coordinate semantics.

The pre-boot launcher and in-game pause overlay edit the same persisted
`widescreen` setting. Switching at runtime clears both host frame buffers,
changes the PPU pitch, and recomputes the presenter viewport. It does not enter
SNES save states, SRAM, input recordings, or deterministic 4:3 hashes.

Widescreen diagnosis is a separate developer boundary. The trace-only
`scripts/capture_widescreen_diagnostics.py` repeats a deterministic frame with
host layer masks and exports private evidence under an ignored directory. It
does not add instrumentation to guest execution. Its DKC2 decoder reads only
documented camera, sprite-table, and render-table WRAM fields; it keeps a game
sprite record distinct from the compound OAM tiles that record may emit.
Automatic findings follow the data path from active game sprite to
render-consumed OAM to isolated OBJ pixels. Background findings measure
non-backdrop pixels in each margin independently. This classification narrows
investigation but cannot certify that a non-empty tile, position, priority, or
animation is correct. Reports also expose a logical top-left 43x64 region; the
private Pirate Panic route regression checks that BG1 region at frame 6,750
without committing ROM-derived images or recordings.

Route-scale widescreen diagnosis is a second, temporal layer implemented by
`scripts/audit_widescreen_route.py`. The headless host emits opt-in JSON lines
under `DKC2_WIDESCREEN_TRACE`; these contain host-observed PPU state
(including the presentation bias, the visible per-side margins, and the
number of HDMA scanline bands), documented DKC2 WRAM fields, and read-only
projections of the world-keyed terrain store. The shared shadow runtime accounts the final source of every
margin miss as periodic fold, verified blank, or raw rolling-VRAM fallback.
The last category is the direct stale-VRAM hazard. Its diagnostic lookup reads
a world tile without changing renderer counters or behavior.

`scripts/check_widescreen_state_corpus.py` is the third layer: it replays
every preserved Quick Save at 4:3 and at each wide aspect, in composite and
per-layer isolation, and checks that the presented native viewport equals the
4:3 render (bias-aware, with the seven endpoint pixels reported separately),
that visible margins of a visibly enabled layer are not blank, that the old
4:3 boundary is not a persistent discontinuity, that no margin lookup fell
through to raw rolling VRAM, and that every replay completed. An optional
reference directory turns it into a before/after comparison, so a general
rule is validated against every previously accepted case at once. The
optional `DKC2_STATE_CORPUS` CMake path registers it as a private CTest.

The offline analyzer reruns deterministic composite/BG/OBJ presentations,
compares exact terrain-entry identity as a cell crosses between a margin and
the authentic viewport, scores discontinuities at the former 4:3 edges, and
tracks placed-object activation/despawn around those boundaries. It does not
modify guest memory, VRAM, input, timing, or save state. Raw PPMs and derived
BMP/JSON/HTML evidence remain ignored/private. Image scores and object
lifetime rules are candidate generators; only raw fallback and observed tile
identity are exact machine facts, and neither alone establishes artistic
intent.

The terrain trace includes aggregate `[expected, present, matching]` counts
for the complete decoded viewport and a separate margin-only triple. The
margin presence count is a same-frame provenance proof: each cell came from
the decoded map or a newer cartridge tilemap write. This supersedes comparing
an animated cell with a different frame. Old-boundary seam candidates require
persistence across adjacent samples. They are not suppressed merely because
the affected screen has complete source provenance: source ownership does not
prove that the margin used the same presentation phase as the native center.
Verified-transparent
fallbacks remain visible as safe observations but do not inflate the
actionable finding count.

Terrain X and Y presentation are keyed from PPU-latched scroll values unwrapped
near the WRAM camera. The WRAM camera may lead the PPU during an NMI boundary;
using it directly for the margins while the center consumes latched hScroll
creates a transient split at X=43/X=299. Prefill limits, margin classification,
shadow capture, and margin lookup therefore share the rendered X coordinate.

Vertical address resolution has two distinct domains. The shadow key first
masks the PPU phase to its 8-pixel tile origin, unwraps that origin once near
camera Y, restores the fine phase, and advances every row continuously. Direct
fine-value unwrapping at the exact half-period tie, or independent unwrapping
per row, can select opposite 1,024-pixel epochs. The
decompressed map address uses the PPU's low-eight-bit phase nearest
`cameraY-$0100`, matching the cartridge column builders for every currently
proven rolling layout. Keeping those domains separate prevents both
cross-row discontinuities and wrong source-page reconstruction.

Input recording is a host-only component shared by the playable Win32 and SDL
front ends through `runner/input_recording.{c,h}`. It samples the final
controller word once for every emulated frame and emits a fixed six-hex-digit
line. Opening occurs before the frame loop, LF output is byte-stable across
platforms, and all open/write/flush/close failures are surfaced to the user.
It does not modify guest memory or participate in save states. The stream
currently contains controller samples only: it does not encode rewind,
save-state creation, or save-state loading. Rewind can restore older guest
state while host recording continues forward, so any route that uses rewind
or loads a state is not exactly reproducible from the input and starting SRAM
alone. Fast forward remains deterministic because each emulated frame still
receives and records one sample.

## Simultaneous co-op boundary

The frame-based catch-up assist in `dkc2_coop.c` is called once after each
forward console frame by `dkc2_game.c`. Its lightweight `dkc2_coop_assist.h`
interface keeps it separate from generated operand wrappers. Raw per-port
Select input charges for 181 NTSC frames (three seconds rounded up); the
action-input wrappers suppress Select in simultaneous TEAM so charging cannot
start the cartridge's tag action. Other modes preserve their original words.
It teleports only the requesting living on-foot/swimming actor, next to a grounded or swimming
teammate, clears fractional position and momentum, preserves action/animation
and moves that actor's held item by the same displacement. The camera leader
and fixed controller bindings do not change. Simultaneous requests bring the
follower to the camera leader once. Timers are host-transient and reset on
release, pause, scene/level changes, mode/session changes and snapshot loading.
They are not added to the guest save format. A requester in rope states
`$35-$38` can also leave the rope: its state/action and animation context reset
to that character's idle descriptor from the ROM table used by `$B9:D0C6`.
This discards the old rope callback; normal guest dispatch then owns movement,
gravity and terrain. A rope actor cannot be the destination. Scripted hurt,
barrel, mounted requesters and team-carry transitions remain excluded.
A grounded Rambi rider may be the destination: the partner moves while the
actual rider, animal and selector metadata remain unchanged. This validates
the active Kong's state `$09`, live actor, ground flags, `$006E == $019C` and
the aligned live animal slot in `$006C`. Other mount contexts remain excluded.
A live, present waiting requester in `$13` with no lost bit may also recover;
its state/action/animation reset to normal, removing the old waiting callback.
Water state `$3D` qualifies as both requester and destination, without requiring
ground contact on the swimming target. Crossing water resets movement and
animation to idle or swimming as appropriate. Water entry initializes only the
requester's work-block gravity/terminal speed from its own `$FF` property record
(`actor+$54`, properties `$76/$78`, work `$08/$0A`), preserving the other Kong.
Using the normal global water-entry path can trigger a follower transition;
merely assigning `$3D` leaves dry-land physics behind. Same-water rescue retains
the swimmer's existing animation and physics. The native swim speed/facing
callback at `$B9:DB6E` now runs for independently controlled swimmers too.
State dispatch reconciles a stale saved lost bit only for a standing/swimming/rope Kong
with a valid actor type, live collision and the guest both-present flag.
Waiting actors marked lost and dead actors retain their loss and require native DK-barrel revival.

For ordinary unmounted TEAM goals, the native goal idle gate admits either
live, non-lost descending airborne Kong. The existing two-Kong collision path
tests both native hitboxes, filtering the original landing eligibility per
collider. Grounded/ascending actors cannot steal or falsely trigger a landing.
Only after the native collision succeeds does the accepted collider become
the camera/ending leader. The active/inactive work selectors and their cached
24-byte collision records swap together; positions and fixed controller slots
do not. Native goal rewards, animations, completion and scene transitions run
unchanged. The policy stores no transient goal owner across frames or loads.

Two-player input is a game-behavior adaptation like widescreen: host-side
routing is unchanged (the two packed 12-bit controller words still enter the
two SNES ports), and the game's own "2 PLAYER TEAM" menu selection remains the
co-op switch. The stock TEAM mode alternates control — the dispatch at
`$80:8A00` fills one active input word from one controller each frame, and the
Kong action gate at `$B8:B9C7` admits only the active Kong (`$0593`). The
simultaneous policy instead lets both Kong slots read their own controller
every frame: slot `$0DE2` (controller 1's Kong) reads the P1 held/pressed
words `$0502/$0506`, and slot `$0E40` (controller 2's Kong) reads
`$0504/$0508`, matching the cartridge's own slot-to-controller binding from
`set_active_kong` (`$80:883B`).

`scripts/apply_dkc2_coop_overrides.py` rewrites the generated gate the same
way the widescreen adapter rewrites generated C: the gate compare's `$0593`
read is wrapped with `Dkc2CoopGateActiveValue` (substituting
`current_sprite` for the active-slot compare, so both Kongs are admitted; the
fade fall-through is untouched) and the input path's `$050E/$0510` reads are
wrapped with `Dkc2CoopSelectHeldWord`/`Dkc2CoopSelectPressedWord`
(substituting per-controller words by slot). On the real USA v1.0 emission
the gate lives inside `process_player_action_M0X0` at trace label `L_B8F3`,
one byte and a function boundary away from both reference disassemblies —
which is why the anchors are instruction-shape contexts (the current_sprite
load feeding the `$0593` compare and its equality branch; the `$0983/$0981`
stores after the word loads) with exactly-one-match counts and idempotent
re-application, so generator drift fails regeneration instead of silently
mis-patching. `runner/dkc2_coop.{c,h}` owns the policy: it defaults to
simultaneous, never activates outside TEAM mode, and returns cartridge values
byte-for-byte under the classic policy or in 1 PLAYER / 2 PLAYER CONTEST.

Private-ROM replay exposed another requirement: the follower never reaches
the input gate from its passive script state `$13`, and landing can return it
to `$22`, which copies the leader's position history from `$7FA532/$7FA572`.
The adapter therefore also wraps the `$2E,x` read in `kong_state_handler`.
`Dkc2CoopSelectStateWord` changes an initially waiting `$13` to idle on that player's input and
changes `$22` to idle even with neutral input. This runs before the follower
handler can replay the leader's history and does not depend on the leader
reaching its own input gate. Lost Kongs cannot use this initial join again.
The alternating TEAM wait `$6F` resumes its existing post-handoff jump `$06`,
clearing the turn-wait bit and its blink while preserving invincibility.
Other action states retain their handlers. The
state helper uses bank-$00 WRAM because the dispatcher sets DB to `$B8`.
Every wrapper requires exactly one anchored call; nested or misplaced
existing wrappers fail closed. The new-TEAM route in `check_coop_route.py`
checks each player alone, post-landing control and both together against the
classic policy, using observation-only `DKC2_COOP_TRACE` records. The
policy is persisted as `CoopMode` in `launcher.cfg`, switchable live from the
overlay Settings page, and process-overridable with
`DKC2_COOP=simultaneous|classic`.

Movement alone was insufficient: the cartridge gives the follower a zero
interaction mask (`sprite+$30`) and back render order (`+$02 = $D8`), and
`get_active_kong_clipping` skips it. For simultaneous TEAM, ordinary idle
dispatch promotes only the inactive Kong's zero or `$06` follower mask to `$1E`
and back order to `$E4`, leaving other action masks and states intact. `$06`
appears after leader swaps and previously allowed enemies to pass through.
The clipping
entry redirects to the cartridge's existing inactive-Kong clipping function
(`$BC:FB2C`), which maintains a separate hitbox and tests both Kongs. The redirect
reuses the existing guest call frame; it does not introduce another JSL frame.

The stomp reaction (`player_interaction_1B`) otherwise always selects the
active Kong. When `set_player_interaction` accepts a new highest-priority
reaction, the source wrapper records the enemy (`$0A84`) and collision Kong
(`$6A`) in host policy state. The stomp consumes that record once and chooses
the stock active/inactive metadata selector accordingly. A later collision
cannot steal its target, a higher-priority queued reaction replaces the record,
and the source must still match at consumption. This state is transient between
queue and reaction; controller words and save formats are unchanged.

The palette selector at `$BB:8B66` normally adds `$1E` bytes (15 colors) to
choose the dim follower palette. A narrow immediate-value wrapper returns zero
for simultaneous TEAM only. Status-effect and contest palettes keep their
existing branches. Color changes take effect when the game refreshes its palette.
`check_coop_combat.py` exercises P2 roll, stomp/bounce, contact damage and P1
roll attacks from a new TEAM game, and checks both CGRAM palettes against the
external supported ROM. It also requires walking and jumping after roll/stomp
recovery. The recovery compare in `CODE_B9D705` treats independently controlled
roll-stop `$04`, stomp-bounce `$16` and throw-finish `$3F` states as active recovery,
so the original animation/landing path clears the action state at its normal time.

Accepted pickup records the object slot/id and its Kong. Attachment, four
animation commands and all five active-owner reads in `CODE_B8D4AE` (the release
terrain sweep) use that owner. The terrain sweep must begin at the thrower:
using a Kong on a lower platform can otherwise relocate a regular barrel at
release. The shared `$0D7A` word is hidden from the non-owner's idle recovery,
jump/fall selection, slope animation selection, animal/item action test,
honey-floor animation selection and animation callbacks that select jump,
landing, carry states and movement speed. Those actor-local decisions use the
existing
`Dkc2CoopHeldObjectValue` policy; object attachment and global pickup exclusion
still observe the real shared slot. Otherwise an empty-handed jumper can enter
a carrying state and release the other player's item through its own controls.
Guest carry states `$0C` through `$0F` and recovery `$3F` reconstruct ownership
after snapshot restore,
and recycled sprite ids invalidate the transient record. Joining sets the
cartridge's other-Kong-present bit so breaking a DK barrel does not rescue an
already controlled player.

A two-bit host loss mask observes hurt/death states `$24/$25/$05/$59` and
blocks `$13` re-entry until the game accepts DK-barrel rescue `$3E`. Session
reset clears the mask. `Dkc2HostSnapshot` stores a marker and the mask in two
previously zeroed tail-padding bytes; original offsets and total size remain
unchanged. Loading resets transient interaction/pickup records. For legacy
snapshots without the marker, an absent waiting Kong that has left the initial
follower render order is treated as lost; snapshots captured during hurt are
observed again by dispatch. The original pre-promotion legacy waiting state is
ambiguous, so its initial-join behavior is retained for compatibility.
Private lifecycle acceptance saves after damage, reloads with held P2 input,
rescues via P1's DK-barrel throw, moves P2, loses P2 again, then reloads again.

These first-level routes do not imply full-game combat or throwable coverage.

Hurt, death, barrel and control-swap animations retain their cartridge paths,
with the bounded ownership, recovery and turn-wait adaptations above. Their
full-game interaction with simultaneous inputs remains unverified. In simultaneous
TEAM, the camera targets the midpoint of both playable Kongs using their full
16.16 positions. Native room clamps, smoothing and framing remain in charge.
Scripted/lost/waiting actors fall back to the native target; joining from the
wait state can still invoke the cartridge's catch-up behavior.
The dispatch's `$0B02` bit-$10 remap
(level-end forced-walk input neutralization) applies to the single active
word and is bypassed in simultaneous mode, so both pads stay live during the
scripted outro where stock neutralizes one; the forced walk itself is driven
by `$091B`, not the gate. The policy reads guest WRAM through the
runtime's CpuState accessors on the generated-code path; its direct guest
writes are the selected sprite's state, interaction mask and render order at
dispatch. The policy setting never enters controller registers,
save states, SRAM, or input recordings, and the deterministic attract gates
are unaffected because attract demos inject the active words directly and
the game starts in 1 PLAYER mode.

`scripts/create_private_diagnostic_version.ps1` is a deployment wrapper around
that boundary, not a new runtime architecture. It assembles a private,
external, append-only kit and preserves the SRAM that existed at recording
start beside each route. That paired SRAM is supplied to deterministic replay,
preventing later personal progress from changing a diagnostic run. ROMs,
saves, recordings, memory dumps, and captures remain outside Git.


## Optional playable Kong presentation

`runner/dkc2_kongs.c` loads a bounded, versioned private sprite pack. The
project-owned importer reads Project Kongs graphics and animation declarations
as data, decodes the compound 4-bpp frames and projects visual animation loops;
it never executes assembly commands or imports the ROM hack's gameplay code.
Each original Diddy/Dixie slot has an independently persisted replacement.
Original abilities, hitboxes, game state, animal transformations and audio stay
under the original game program's ownership.

DKC2 actor types `$00E4`/`$00E8`, graphics at actor offset `$18`, properties at
`$12`, and animation IDs at `$36` identify the two playable sprites. Dixie's
animation IDs have a `$00A3` offset. Actor WRAM can advance ahead of committed
OAM; the adapter therefore verifies every submitted piece of the displayed compound layout
against the ROM graphics directory at `$BC:8000` and recovers its displayed
origin. Palette and reserved tile allocation identify candidate OAM pieces;
geometry, size and tile order must all match before any piece is replaced.
Partly clipped sprites may submit only a subset of a known compound layout.
An unrecognized layout remains the original sprite.
While mounted, `$006C` identifies the separate Kong rider (types `$0190`
through `$01A0`); its semantic animation identifies the original slot. The
original player record then renders the animal and is left unchanged.
Pack version 2 contains five attachment points per replacement character,
separate mounted idle/movement sequences, and compound pose records keyed by
the leader's semantic animal animation and graphic. The adapter subtracts the
original attachment (`$0D72/$0D74`) from the imported attachment, preserving
the existing bobbing. Explicit compound offsets instead replace the current
total (`$0D76/$0D78`). Adjustments are mirrored with the displayed OAM facing.
Rider movement follows the source callback's leader `$26` predicate for
Squitter/Rambi; Enguarde stays seated and the other mounts have dedicated poses.
Kiddy's Squitter rider uses his seated sequence instead of the source's crouch
mount callback loop; his Rattly pose uses seated art with a frame-origin adaptation.

`cmake/ProjectKongsPpu.cmake` inserts guarded OAM visibility and raster callbacks
into a build-local copy of the pinned shared PPU. Exact unique anchors fail at
configure time if the upstream integration drifts. The submodule stays clean.
The replacement occupies the original object's OAM priority position, before
background/window composition and main/subscreen color math. Its frame bounds
control visibility so taller replacement sprites are not clipped to the
original sprite's first tile. A temporary per-scanline palette is restored
before HDMA; WRAM, VRAM, OAM and the final CGRAM are unchanged. Unmounted visual
sequence phase follows the serialized SNES frame counter. Mounted sequences
have host-only elapsed clocks that restart on mount/movement changes or frame
counter discontinuities. Repeated presentation of one guest frame does not
advance them. Compound poses follow the current animal graphic and hold their
last matching pose while the animal holds that frame. Restore/rewind can restart
the cosmetic cycle and adds no guest state.

The pause menu owns character selection and pack loading. `kongs.cfg` contains
the selected pair and external pack path in the platform user-data directory.
The default is the original pair, and missing/malformed packs cannot replace
sprites. Invalid replacement loads preserve the previous valid pack. Tests
cover the binary boundary, sprite identification, visibility, palette restore,
and disabled behavior; private state/input comparisons cover real gameplay.

## Optional Kong gameplay adapters (September 5 follow-up)

Pack v3 extends the bounded private sprite format with hand-attachment records
and attack sequences. v1/v2 remain readable. A pause-aware simulation clock
feeds per-actor animation clocks; repeated presentation never advances them.
`Dkc2KongsGameplay` runs only at nine verified US v1.0 instruction boundaries.
It retimes native throw callbacks, updates carried-object hand coordinates and
adds Donkey's hand slap / Kiddy's body slam using legal native actor states,
original terrain physics, enemy clipping, defeat flags and the audio queue.
The renderer still does not mutate WRAM, OAM or VRAM.

Seven generated callbacks can be called directly from compiled code. The
checked, idempotent `apply_dkc2_kongs_overrides.py` adapter routes these short
RTS routines through the existing paired interpreter ABI only when the active
leader is a selected replacement. The shared runtime stays pristine. CMake
applies the adapter on configuration and rejects missing/changed entry anchors.
Other code remains on the existing compiled path. Original-pair behavior is
unchanged; carry and ground-attack WRAM/audio differences are intentional.

Host attack state is canceled on restore instead of changing the save format.
Kiddy's solo slam is a local DKC2 adaptation, not DKC3 floor-breaking physics.
Donkey's bonus-banana spawning from the reference hack is not implemented.

Pack v4 adds incoming/outgoing tag sequences. A shared presentation clock
follows the incoming actor's semantic 73 and also animates its partner, whose
animation ID can remain idle while native paired-frame commands control it.
The native 44/26-frame release points and outgoing hop stay authoritative.
The fifth callback is `glide_action`: the replacement path preserves its
shared run-speed update, then returns before Dixie's flight logic. Restoring
an existing glide resets the actor's native gravity and terminal speed before
resuming its falling animation. This remains a partial mechanics adaptation;
ordinary movement constants and collision geometry still belong to DKC2.

Pack v5 adds top idle/walk/air/windup sequences 178-181 and replaces DK's
borrowed top idle with seated DK art. A shared team phase follows carrier
semantics 29-38 while $D7A names the partner. After matching both native OAM
owners, the renderer places the top relative to the displayed carrier origin;
it does not write guest state. Simulation hooks B9:DCEA and B9:D8BE separately
prepare/release the top at DK/Kiddy phases 15/18 and set its actual launch
origin. The existing B9:DFD5 callback retains each full recovery sequence.
The bounded callback seeker handles native paired $8A frames as well as $8B
carry frames; preparation is consumed once so it cannot prevent release.
Reset, damage, dropping, mounting and changed selections abandon host action
state. Mixed original/replacement teams retain native partner placement.

Kiddy's semantic 40 is a dedicated grounded sit-up ending in a held seated
frame. It is not the generic six-tick hurt loop used by semantic 41. The
importer isolates four sit-up frames from the supplied sequence, excluding
its surrounding death/cry actions. This is a pack-data correction within v5;
the native post-throw waiting state and rejoin logic are unchanged. Diagnostic
Kong traces now include the recovered displayed origin for jitter checks.

## Simultaneous animal ownership and death handoff

The cartridge has one mounted-animal context (`$6C/$6E`). Mounting now uses
both existing Kong hitboxes when the inactive Kong is eligible to land. The
candidate's state flags come from the owner's ROM; a stationary or rising
partner cannot mask the leader's valid landing. The mount gate and its state
lookup use the colliding Kong, scoped to unmounted animal updates so barrel
callers of the shared state-flags routine retain their behavior.

Accepted reaction `$17` records its source and collider before later collisions
can overwrite `$6A`. At mount dispatch, a follower rider becomes the leader by
swapping the guest Kong/work selectors and updating the character/controller
selector words. Positions, action states and collision masks stay intact. The
stock mount/dismount handlers then own the animal lifecycle. Controller routing
still uses fixed Kong slots, and the guest selectors persist through existing
save/load and rewind. The transient accepted-mount record resets on load.

Named Kong movement, reaction and animation reads of `$6E` return zero for the
on-foot partner. The generated tail paths are covered as well as their named
entry routines; crate spawning retains its global occupancy check. The stock
follower-to-animal attachment suppresses only its follower-present bit. These
are fail-closed, idempotent source-owned adaptations; generated game code and
private integration snapshots remain outside Git. One animal has one rider.
Pirate Panic Rambi is accepted; other animal types and transformations are not.

The dedicated banana collector accepts only two rectangles, preferring the
leader and animal over the follower. When all three hitboxes are present in
simultaneous TEAM, the group-entry operand temporarily supplies the follower's
rectangle in the second scratch slot (`$0D3C/$0D40/$0D44/$0D48`). After that
pass, the continuation restores those four words and repeats the cartridge's
existing second-slot call for the animal, then processes the leader normally.
The rectangles remain separate; empty space between players is not collectible.
Original group bits, rewards, sound and collection animation remain guest-owned,
so overlapping passes cannot award a banana twice. One transient host record
holds the scratch backup only within the group and resets on session/load/mode
change. Frame-boundary saves need no new fields. Classic, solo/contest and
cases with two or fewer hitboxes retain their original collection path.

Normal death handoff skips state `$27`'s follower movement and enters the
existing completion block with the survivor's own X/Y. Stock selector swapping,
invincibility and the recovery jump remain. When simultaneous dispatch resumes
TEAM wait `$6F`, it now clears turn freeze `$0A36 == 7` and its timer `$0A38`, in
addition to the existing turn-pause/blink cleanup. Other freeze reasons remain
untouched. Blank-SRAM acceptance requires the survivor to stay in place and an
actual enemy to move after handoff, then checks survivor control and saved loss.

The normal Kong horizontal clamp at `$B8:D13D` previously restricted each
player to camera-relative X `[16,240]` in every aspect. Two operand wrappers
now extend its left inset and span for simultaneous TEAM Kong slots when the
terrain renderer confirms a wide scene. They use the current aspect's extra
columns and presentation bias: left slack is `extra - bias`, right slack is
`extra + bias`. Slack is capped by the camera's distance to the level's outer
scroll bounds, so reflected or shifted artwork does not grant access beyond
the level. The native inset, terrain collision and exits remain; native aspect,
classic TEAM, solo/contest, non-Kong sprites and unproven wide scenes keep their
original operands. Aspect changes apply through the existing live video state.

## Windows dropdown and input boundary

`desktop_menu` owns validated command ranges and checkmark state;
`desktop_menu_settings` projects the shared settings value and launcher-owned
edge/co-op/upscaler settings. `windows_menu` attaches a dark owner-drawn HMENU
to either the Win32 HWND or the SDL window's HWND and queues WM_COMMAND IDs for
host-loop dispatch. It does not mutate guest state from a window callback.
Quick Save/Load commands use `Dkc2ApplyAssistGate` to bypass only the shortcut
opt-in, matching the established Mac policy. Menu visibility follows fullscreen;
unsupported overlay/reconstruction commands are disabled by host capability.
The same overlay model provides the full visible pause/settings UI on OpenGL
and GDI. GDI's software-renderer texture resources are recreated after resize.

Win32 maps persisted SDL scancodes directly to virtual keys without depending
on SDL's initialized keycode table. Keydown edges are retained through one host
poll, alongside asynchronous held-key state. SDL similarly retains unconsumed
keydown edges until its next poll. Focus loss and native menu transitions clear
pending edges, and keys held across a native menu transition must release before
game input resumes. Start+Back remains consumed until both buttons release.
These are host input policies; SNES controller words and guest scheduling are
unchanged. The three supported aspect choices remain 4:3, 16:10 and 16:9.

The MIT-licensed DKC3 menu adaptation and exact source revision are recorded in
`third_party/dkc3_menu/`, including the original license.


## DKC3 display and feedback parity (2026-09-13)

`desktop_reconstruct_shader.h` contains the shared SDL/WGL shader source. The
native OpenGL presenter loads shader entry points through WGL, compiles once
per context, binds uniforms for the current viewport and restores the fixed
pipeline before drawing ImGui. Color LUTs run before scaling on both hosts.
Menus receive actual compiled-shader capability; GDI retains fixed sampling.

The maximum host framebuffer is 446x224 for 21:9; 256/308/342-wide behavior
remains separately selected. Desktop and headless buffers allocate the maximum.
Rumble consumes host-only events from the existing accepted-stomp reaction,
with per-player routing shared with the selected input-device assignment.
Both hosts use SDL timed rumble.
Focus loss, disabling feedback, disconnect and shutdown stop outstanding pulses.


## CRT, optional characters and MSU-1 (2026-09-13)

`desktop_crt` owns the portable settings/model. `desktop_present_sdl` exports
a GL pipeline with a procedure loader. SDL owns its window/context; native
WGL owns its own. Both use identical reconstruction and five CRT passes,
including half-float linear-light targets, beams, blur and mask/curvature.
`desktop_launcher` persists CRT settings and parses shared environment overrides.

Project Kongs uses an external validated data pack. Build-local PPU callbacks
substitute compound OAM rendering. Verified instruction hooks adapt moves and
attachments; generated wrappers bridge the compiled callback paths. Each
original slot owns its move state so TEAM dispatch cannot cancel its partner's
attack. Save layout remains unchanged.

`dkc2_msu1` owns mapped PCM and resampling. `dkc2_music` owns host settings,
SPC command observation and tracks. `dkc2_spc_music` restores the verified
music-only scheduler policy after load/rewind or live selection. All three
hosts mix PCM after stock SPC rendering. No shared submodule source is changed.

Optional character instruction callbacks resolve the co-op owner and held
object using the existing value wrappers. Temporary callback context is
restored synchronously before any guest execution. The interpreted attachment
routine receives the same owner register as its generated C counterpart.

## Rope contact ownership (2026-09-13)

Terrain contact queues the grabbing Kong in the guest interaction source
`$0A84`. Rope reactions `$11/$12` previously always selected the active Kong's
work pair, so a follower's valid contact attached the leader instead. The
source-owned adapter redirects their active-work call to the existing
inactive-work routine when `Dkc2CoopRopeUsesFollower` identifies the source as
the other fixed Kong slot. Guest terrain results and native rope state,
animation, movement and jump code are retained. Camera leadership and fixed
controller assignments are unchanged. The source is already serialized by
guest snapshots, so this needs no new host state. Classic TEAM, solo, contest
and non-Kong sources keep the original call. Generation checks require one
call in each named reaction and reject misplaced or missing adaptations.

The single-rope animation callbacks `CODE_B9DAB7`, `CODE_B9DAE0`,
`CODE_B9DB19` and `CODE_B9DB45` also skip the AI follower, as do double-rope
callbacks `CODE_B9DD61`, `CODE_B9DD7C`, `CODE_B9DD8E`, `CODE_B9DD9C` and
`CODE_B9DE17`. Horizontal-rope callbacks `CODE_B9DDB7`, `CODE_B9DDC9` and
`CODE_B9DDE8`, and the shared facing callback `CODE_B9E013`, use the same
policy. Their `$0597` comparison operands now exclude independently
controlled Kongs in rope states `$35` through `$38` from that early return.
The original callbacks complete junctions, select Up/Down sequences and
advance frames using the Kong's own movement; other states and policies keep
their original operands. Rich headless traces expose animation and graphic
IDs, and the rope replay requires multiple distinct frames in each direction.
Allowing the native facing change is necessary to cross consecutive net
columns: otherwise the follower repeats its turn animation indefinitely.

An older simultaneous snapshot can already have skipped the junction's final
callback and parked at a terminal animation wait in state `$36`. Before state
dispatch, the co-op policy validates the transition animation, expired timer,
speed callback and native script's `$81` completion followed by `$83/$D12B`.
Only this exact finished transition rewinds three script bytes to replay its
own completion callback. Unfinished transitions retain their remaining frames;
coordinates and state are left to the native callback. No host-only recovery
flag or snapshot format change is required.

## Nearby TEAM partner carrying (2026-09-13)

The source-owned co-op adapter wraps the team-up action's partner selector
and state eligibility operand. Simultaneous players target the opposite fixed
slot; only nearby, present, on-foot Kongs qualify. The accepted carrier takes
the guest active/work selector pair, preserving actor positions and controller
ports. Native pickup/drop/throw reactions and animations remain authoritative.
Held-Kong ownership resolves from those guest selectors after save/load too.
The state policy preserves the carrier's pickup wait and restores independent
control and collision flags when a thrown partner has landed. Classic, solo
and contest retain the original eligibility and state paths.


### Independent midpoint camera and transport cannon

The generated camera handler CODE_B5E560 (entry $B5:E552) reads a shared
midpoint through four narrow X/Y integer/fraction operand helpers. Both
16.16 positions are added in 64 bits before division; overlapping +5/+9
reads retain their native low integer byte and high fraction byte. Helpers
require simultaneous TEAM gameplay, two present non-lost eligible actors,
and a Kong source selector. Normal movement, swimming, ropes, riding and
cannon entry/flight can contribute; waiting, injury/death and scripted ending
states retain the native target. No actor positions or leader selectors are
changed by camera reads. Eligible co-op gameplay also centers the desired
viewport around this midpoint before native room bounds, smoothing and
streaming; single-player lead/dead-zone offsets no longer displace the pair.

Transport entry CODE_B3F069 ($B3:EFD9) originally places the inactive Kong
into follower recruitment state $2F before starting active-Kong state $30.
Simultaneous play now retains that native paired recruitment and flight when
both Kongs are present and neither is lost. Source $0A84 must identify an
aligned transport actor of type $013C, $0140, $0174 or $0178, with no animal mounted.
The older independent shortcut remains only when a partner is absent/lost;
its accepted follower callback can promote the actual entrant, swapping guest
work selectors and cached collision records together. Entry flight
CODE_B8A592 targets the cannon in $0A84 rather than the partner's position.
During paired flight the animal-follower flag operand retains bit $4000 and
state $22 remains native while the leader is in cannon states $10/$11/$12/$30.
This allows the existing native snap/launch paths to move both actors. Native
bonus introduction and arrival then restore independent controls. There is
one shared cannon context and no pending host owner or snapshot extension.
The reported $013C cannon passes either entrant and old-loop reload; $0174/
$0178 and full-game barrel variants still need private input acceptance.


## Windows Bluetooth controllers (2026-10-18)

The Win32 presenter now uses project-owned desktop_gamepads.c for SDL
GameController input instead of XInput-only polling. Its main-thread pump
retains up to eight mapped controller handles by SDL instance, closes detached
handles and opens new instances without resetting surviving assignments.
Gameplay still routes exactly two ports and releases all input when unfocused.
Only SDL joystick/controller events are drained; Win32 window messages remain
with the native message loop. SDL initializes after launcher teardown and
releases only its owned game-controller subsystem on game shutdown.

Launcher player_gamepad_guid selections now persist as Player1Device and
Player2Device in launcher.cfg. The native host honors these exact GUIDs before
automatic assignment, reserves a neutral input slot for a disconnected explicit
choice, and preserves the other player's connected device. Automatic choices
fill unclaimed devices. Same-model devices with indistinguishable SDL GUIDs
remain an acceptance limitation. Button/axis conversion preserves existing
standard binding codes, axis polarity, trigger range, and independent player
port packing. Player binding capture uses the assigned device; Assist and menu
navigation retain the first available controller. Rumble targets the same
selected device and uses SDL's bounded pulses and explicit focus/shutdown stops.

The pinned SDL2 database omits the observed Bluetooth DInput device
2DC8:6101. One compatible zlib-licensed SDL_GameControllerDB Windows SN30 Pro
mapping is registered before launcher SDL initialization and again before
gameplay initialization. Its source, exact revision, original data and license
are recorded under third_party/sdl_gamecontrollerdb. Existing SDL mappings
supply the Xbox One S path. Unmapped unrelated models are not given guessed
button layouts.


## Bonus-room arrival and shared fixed hooks (2026-10-19)

The live follower can fall below a bonus room's entrance barrel when the
native intro launches only its active Kong. At state dispatch, a bounded
arrival policy checks bonus/sublevel mode $0515 == 1, both-present flag,
no lost players, live actors, source barrel type $0140 exactly at the native
$0533/$0535 spawn point, a grounded idle leader near that point and a partner
in an entrance/wait state within 48 pixels of the spawn. Once the leader lands,
the existing rescue initializer places the partner beside it on the platform
and restores its own animation/movement without requiring any controller
input. It also repairs an older waiting snapshot with that same guest evidence.
Ordinary waiting, lost/dead actors, remote players and other room contexts
retain their existing behavior. No pending host flag or save-format addition.

For accepted follower contact with fixed hook type $014C, CODE_B398D7 skips
its follower recruitment block ($B3:98E0 to $B3:990B) and promotes the touching
follower with its guest selectors/collision caches. Native movement into the
prop targets the recorded hook instead of the other Kong; native attachment,
pose and jumping remain authoritative. The occupied fixed-hook dispatcher
reuses the native collision handler while either live Kong is hanging at the
hook's exact anchor. A scoped collision operand excludes the already-hanging
Kong so it cannot mask the second collider. The leader's busy-state flag is
relaxed only while hanging from that fixed prop, allowing the other Kong's
accepted native grab. Occupancy is derived from per-Kong state/anchor position,
so either can release without invalidating the other's hold, including after
snapshot load. A release still clears $0D82 and runs the native hook cooldown;
shared collision scanning pauses during that cooldown to prevent immediate
reattachment of the jumping Kong. Hook state $3C contributes to the midpoint camera. Hooks whose parameter has high-byte script/camera flags retain their original
occupied-state handling; other variants remain unverified.

Each accepted fixed-hook jump ($B8:C621) re-arms that hook's state 1 before
the native action clears $0D82. Matching uses the departing live Kong's exact
anchor and fixed-hook parameters. Thus the last occupant also gets the native
cooldown after the first departure's timer has expired; guest state preserves
this across snapshots, without a host timer.

CODE_B3F069's independent cannon shortcut applies only to source types $013C,
$0174 and $0178. Its shared reaction also handles a thrown Kong entering a
barrel; a Kong source retains the native reunion setup. After launcher type
$0140 parameter $0040 finishes, a live waiting follower close to that launcher
can complete arrival when the leader is grounded above it. The existing
movement/animation initializer restores control at the leader's proven ground
coordinate, avoiding the narrow platform edge. Lost actors and unrelated
sources retain native behavior.

## Independent pit loss (2026-10-19)

The native accepted-pit blocks $B8:B481 and $B8:B499 consult a scoped co-op
survivor policy before queuing global level loss. A live independent teammate
with a playable movement, rope or fixed-hook state must remain outside pit
terrain and the native lower-screen cutoff. Promote that actor by swapping
selectors, work pointers and cached collision records, preserving position,
action and controller ownership. Mark the fallen fixed slot lost, clear its
collision flags and the both-present flag, then run the original inactive-Kong
parking block $B8:B4CA. The existing saved lost mask prevents input/reload
revival; native DK-barrel rescue clears it. No eligible teammate, classic mode
and bonus failure retain native handling. No host timer or save-format change.

## Rattly partner hangers and camera separation (2026-10-19)

A verified Rambi/Rattly rider requires matching live Kong/animal slots, native
mount selectors and supported riding/jump/charge states. The on-foot actor's
pit test substitutes the native maximum camera Y ($0AFE) for current camera Y
only while that rider exists. This changes the lower-screen cutoff, leaving
the actual camera and accepted terrain-pit path intact. An actual pit can mark
the on-foot player lost while the verified rider survives. Confirmed live
mounted states reconcile stale saved loss just as established on-foot, rope
and swimming states do; unrelated absent actors are never revived.

Hook collision's global animal gate is scoped to the on-foot collider while
its teammate rides. Exclude the rider from hook collision and bypass its busy
flag for that contact. The accepted follower callback selects native direct
attachment ($B3:9914), with inactive work selection in CODE_B3992E; preserve
the rider's active selector and animal context. The native hook owns pose,
anchor, parameter behavior and release. This avoids both recruiting the rider
and repeatedly restarting the follower's flight into the hook.

Select rescue accepts a verified Rattly rider as destination in riding,
jumping and superjump states, including airborne frames. Only the requesting
on-foot partner moves. Rambi retains its grounded-destination requirement.
Waiting-state rescue still restarts the requesting actor's native animation;
no save format or persistent host state was added.

Select alone also leaves a live waiting actor in its waiting state while the
host rescue charges. It does not release that actor into native pit checks
before teleporting. Other movement input retains existing waiting-state control.


## Shared Squawks and mounted pit cleanup (2026-10-20)

An aligned mounted Squawks actor ($006C, type $0198 at $006E) uses the native
shared follower path when both Kongs are present and neither is marked lost.
Dkc2CoopAnimalFollowerFlags retains $4000 and moves an eligible on-foot partner
into native follower state $22; independent input cannot cancel that state
while the controlling Kong uses Squawks state $10. The native bird update
positions and animates the hanging partner. Other mounts retain independent
on-foot followers. No extra sprite or host attachment owner is introduced.

Native damage already swaps leadership, palette ownership and controller
selection. The simultaneous turn-prompt bypass previously converted state
$6F into ordinary jump $06 even though Squawks remained mounted. It now
resumes $10 only with a verified mounted Squawks actor and active survivor;
ordinary on-foot handoff still resumes $06. Lost partners remain excluded
from sharing and cannot steer the surviving rider. The copied checkpoint
passes shared movement/reload and real damage followed by survivor flapping.
Both ownership roles have synthetic policy coverage; physical play and other
bird variants remain unverified.

At accepted Rattly/Rambi rider pit loss, release the shared mount before
promoting the on-foot survivor. Restore sprite palette-index owners using the
native dismount exchange (attribute bits $0E00), put the animal into unmounted
idle/order $D0 and clear mount selectors and flag $0200. Native animal terrain
and deletion continue normally. A narrowly guarded lost-inactive/on-foot-active
snapshot repair performs the same cleanup for older orphaned-mount saves.
Private Rattly replay verifies actual animal despawn, unchanged survivor colors,
on-foot controls and reload; Rambi pit cleanup has synthetic coverage only.


## Manual shared barrel input (2026-10-21)

Manual rotating barrels use source type $0140. Include that type in the
shared transport predicate so native recruitment and partner flight run as
for transport cannons. The reported barrel is $0F5A at (472,6336); either
contact brings both Kongs to that coordinate, in states $31/$11.

Dkc2CoopSelectPressedWord keeps the active occupant's normal port input and
ORs only the other controller's newly pressed B bit ($8000), only during
state $11 with a valid shared barrel source, no mounted animal and no lost
players. The native jump action owns shot direction, timing and paired flight.
Partner steering, held buttons, item actions and on-foot input remain separate.
Classic and contest modes retain native controls. No adapter or save-format
change is needed. Source type $0140 also respects the lost-partner shortcut.

Input acceptance covers both entrants crossed with both firing controllers,
reload inside the barrel, paired flight to the upper platform, and both
players' separate jumps after landing/reload. Native rotation is preserved;
replays wait for its direction rather than forcing velocity or positions.


## Independent shot physics after shared barrel firing (2026-10-21)

When a manual barrel's occupant starts state $12, initialize a native $12
shot for the passenger instead of continuing follower $22 or switching to
ordinary falling $01. At the passenger dispatcher, inherit the occupant's
16.16 coordinates (+$04/$06/$08/$0A), action/contact/collision fields
(+$1C/$1E/$30), X/Y/target velocities (+$20/$24/$26) and shot parameters
(+$42 through +$4E). Each Kong then runs its own native shot update, including
the native launch/action locks, gravity and fixed-port steering. Preserve
identity, palette and character-specific animation context. These guest fields
persist naturally across save/load; no host ownership or snapshot extension.

The prior falling-state release changed gravity and retained a barrel animation
cursor. It is superseded by independent native shot updates. Recovery's
existing follower-compare operand helper now treats $12 like roll/bounce
recovery, permitting its native idle/animation transition after contact.
Without that, the follower can land but remain in shot state/animation.

Require live shared manual source $0140, no mounted animal/lost players/held
object, passenger $22 and occupant $12; keep native scripted exits when
occupant +$48 is nonzero. Transport sources $013C/$0174/$0178, Squawks and
classic play retain existing handling. The two update orders can differ by
one native frame initially, but private upward replays reach equal peak
height for either leader and either firing controller. Native team-up and
put-down obtain the reversed-leader checkpoint without memory edits.

Private checks cover upward-right reach, equal apex, native lock timing,
opposing air input, native-shot reload, normal landing, and animated leftward
walking afterward. DKC2_COOP_BARREL_AIR_STATE extends barrel-sharing acceptance;
DKC2_COOP_BARREL_MOMENTUM_STATE registers supplied_rom_coop_barrel_momentum.
Other barrel variants and physical-controller acceptance remain unverified.


## Scripted intro follower ownership (2026-10-21)

Native Kong $74 holds animation and $75 walks using velocity without ordinary
terrain/gravity correction. Converting the partner's follower $22 into normal
movement while the leader is in these states leaves normal grounded vertical
velocity (+$24=$0300) on that actor. A later scripted $75 then applies it each
frame, sinking the partner even though its animation is still scripted.

Retain native inactive-Kong $13/$22 while a verified live leader is in $74 or
$75. This also prevents held input waking the initial waiting partner before
the native DK-barrel arrival. Native follow owns position/velocity, then the
script owns both actors' hold/walk. When the leader returns to gameplay,
normal independent state dispatch resumes automatically. Do not reposition
actors, zero arbitrary velocities, change intro timers or grant invincibility.
Existing lost-Kong guards still apply. Classic modes retain native behavior.

Both ownership roles and both scripted states have synthetic coverage. The
selected Kleever map checkpoint passes entry, its complete intro, held inputs,
mid-intro reload, native floor positions and independent post-intro jumps.
Configure external DKC2_COOP_BOSS_INTRO_STATE and DKC2_ROM for
supplied_rom_coop_boss_intro. Other boss/cutscene variants and the full Kleever
fight remain outside this acceptance check.


## Shared Kleever dropping hooks (2026-10-21)

Kleever's hooks are generic actor type $0024, animation $02AB and parameter
$E000 in level $21, rather than fixed-hook type $014C. Recognize that complete
identity with aligned slot validation; unrelated generic actors, levels and
special hook parameters retain native handling. Reuse independent accepted
grab ownership, native hook-targeted entry flight, hanging collider exclusion
and the busy-leader exception. Keep each native pose at hook Y+23.

These actors run dropping_hooks_sprite_code, not hook_main. Their occupied
handler CODE_B6E21D (actual $B6:E2BC) calls the native camera/framing routine
before continuation $B6:E2CD. A guarded generated adaptation there performs
an additional native CODE_B39982 collision scan for a second player, then
continues the original boss animation/lifetime logic. Use a paired JSL frame,
restore PB and propagate non-local returns; retain authoritative interpreter
fallback for other M/X modes. Fixed hooks keep their existing dispatcher path.

Occupancy derives from live Kong state $3C at the exact anchor. A native
release clears $0D82; re-arm the boss hook's occupied transition for either
leaver so its native 30-frame cooldown runs, including the final occupant's
single-press release. Do not copy players or disable boss damage in game code.

Both clean grab orders pass from the supplied snapshot. Extended reload and
release-order checks may enable native invincibility counters only on temporary
copied shared checkpoints, after unmodified grab proof. The optional private
CMake DKC2_COOP_KLEEVER_HOOK_STATE test uses that isolated mode. Native camera
flags are retained. Other dynamic hooks and full boss-fight acceptance remain
unverified.


## Shared manual barrel aiming (2026-10-21)

Dkc2CoopSelectHeldWord combines direction bits $0F00 only for the active fixed
Kong slot in state $11, a verified shared transport source of type $140, and
the other fixed Kong in state $31. Existing co-op, both-present, lost-player and
animal guards apply. Opposing horizontal/vertical bits cancel independently;
other buttons retain their fixed port owner. This feeds the cartridge's normal
input/work update and rotation routine without writing barrel angle or speed.
The merge ends on launch, leaving state-$12 air steering independent.


## Shared enemy stomp assistance and tuning (2026-10-21)

config/coop_tuning.cfg is a source-controlled C-preprocessor configuration
included by dkc2_coop_assist.h. Compiler dependency tracking rebuilds consumers
when it changes. Milliseconds map to ceil(ms*60/1000)+1 console ticks: 3000 ms
retains the previous 181-tick hold; the user's current 2000 ms config uses 121.
Public rescue tests use that compiled duration.
Compile-time range guards bound duration to 1..60000 ms, horizontal distance
to 0..256 pixels and vertical distance to 0..128 pixels.

The accepted $1B reaction retains source/Kong ownership as before. Its input
selector records an eligible partner before the owner's native impulse changes
direction: both live Kongs must be in ordinary airborne states $01/$04/$06/$16
and descending, within the configured X/Y separation, with both present and
neither lost or mounted. The partner must also be near the source enemy's X
and above its contact plane. No unaccepted collision creates a shared bounce.

At the native on-foot completion $B8:87F4, before selecting the inactive work,
Dkc2CoopShareStomp consumes the candidate once. Revalidate the partner and
owner's completed upward state-$16 impulse, validate the two work pointers,
then copy only velocity +$24 and work +$08 gravity / +$0A terminal speed. Give
the partner state $16 while retaining its own airborne animation, position,
horizontal speed, identity and palette. OR its fixed-port feedback bit into
the stomp event mask. Native enemy damage, score and sound execute once.
No recursive native reaction or extra enemy collision is introduced. Pending
assistance resets on load/session/mode changes; mid-bounce saves retain normal
guest state. Strict adapter and real-emitter tests pin the completion anchor.


## Independent enemy push and actor-pole ownership (2026-10-21)

At accepted interaction-source recording, retain Kong/source for push reactions
$1D/$1E, clearing the cache on replacement, mode change and snapshot/session
reset. Keep the guest reaction priority unchanged. For a live controlled
inactive Kong with both present and no lost player, the native $1D handler
tail-enters CODE_B88936 before creating its own host frame. That native
knockback routine selects the captured Kong's active/inactive work once and
consumes the ownership cache. It initializes native state $15 and normal
recoil rather than AI state $40 followed by pickup $05. An actor-local held
operand keeps another player's item separate. Explicit pickup reactions
$04/$05 retain their native implementation and eligibility.

Actor poles use global $0BA0 attached/$0BA2 candidate/$0A86 contact. The original
candidate check considers only the leader, another Kong's terrain update clears
the attachment, and CODE_B8B4F7 clamps only the leader. For live type-$2D8 poles,
in regular pole states $05/$06, derive the current Kong's candidate from native runtime clipping-table data,
its native velocity-dependent horizontal window, and vertical contact interval.
Feed that value through the cartridge's ordinary eligibility/reaction path;
do not assign hanging merely because a player is nearby.

Player interaction $0A uses the accepted source Kong for work selection and
actor-local contact operands. In state $45, derive the matching aligned pole
and retain it in Kong+$4C. CODE_B8B4F7 selects the current climbing Kong instead
of the leader, preserving native clamp math. Kong+$4C is otherwise unused by
the verified native pole state and ordinary jump initializer; in this action
context it also retains the last pole during state-$06 jump contact. The
candidate/current comparison then suppresses native re-entry until the jumper
leaves that pole's contact, when the latch clears. Prefer the existing valid
anchor over overlapping aligned poles. This guest word survives snapshot/rewind
without extending the save format. Other actions retain their native use of
their context words. New independent discovery excludes uninitialized/hidden
and other pole families; their existing leader contact and non-co-op modes
remain native. Do not reuse a leader-only unsupported contact for the partner.

Actor-pole state $45 shares the independent animation callback policy with
terrain ropes and swimming. The callback bypasses the native inactive-Kong
skip for fixed Kong slots in simultaneous mode; native scripts still select
climb direction, frame timing and the idle transition.

Actor-pole state $45 also participates in CoopCameraPosition eligibility.
Both live fixed Kong slots contribute native fixed-point positions whether
one or both climb; existing lost/dead/scripted/mode guards remain in force.
Only the camera input target changes; native scrolling and level bounds apply.

Enemy bounce state $16 is eligible for CoopCameraPosition independently of
rescue eligibility. Both live fixed Kong positions contribute during single
or shared stomps; native fixed-point math, level bounds and lost/mode guards
remain unchanged. This does not broaden rescue or scripted-state policies.

Pit-survivor eligibility includes live actor-pole state $45, alongside the
existing rope/hook states. At an accepted native pit-death block, park only
the fallen Kong and promote the survivor without changing its state, pole
anchor, position or animation. Lost/dead/bonus/offscreen guards still apply.

DK-barrel state $3E keeps its native entrance animation and revival sequence.
On the final dispatch tick (+$42 <= 0 before the native decrement), refresh
its native finish coordinates +$48/+$4A from the live teammate and copy the
position fractions. Clear entrance X/Y velocity and horizontal acceleration
before the native completion transitions to $22. This requires a live,
non-lost fixed teammate and simultaneous mode. It uses only existing guest
state, so mid-entrance snapshots need no new host latch or save format.

When controlled follower $22 becomes native shot $12, copy work+$08 gravity
and work+$0A terminal speed from the launch owner in addition to sprite shot
context. Validate the distinct $16B2/$16D8 work pair before writing. These
values then evolve under each Kong's native shot update. For old grounded
on-foot state-$00 saves, restore only terminal speed from the Kong's own
$FF property record +$02, with a live actor, ground contact, no mount and
valid work/property pointers. Preserve gravity here: native jump initiation
can set jumping gravity while the actor still reports standing.

Mounted arrivals retain the native cannon reaction setup when the global
animal selector identifies an aligned live animal actor of type $190..$1A0.
Do not promote the on-foot partner over that single native animal owner.
A dormant inactive Kong (zero interaction mask, $22/$2A, both present and
neither lost) remains in native follow while the rider is airborne or in
barrel states $11/$12/$30. The active rider must be in $09/$0B with real
ground contact before release. Then place the partner at the rider's native
fixed-point coordinate, clear flight momentum, and let ordinary independent
state-$00 promotion restore its collision/rendering. Live controlled partners
and shared Squawks flight retain their existing separate policies. All
arrival evidence is guest-owned and survives snapshot/rewind.

Animal-only control is distinct from riding a separate animal actor: the
guest has animal type $190..$1A0 (stride four), a null $6C animal sprite, and
mounted flag $0200. Keep its inactive fixed Kong dormant, mask that port's
input, retain the native single-actor input gate, and disable timed rescue.
Route active held/pressed input using guest $08A2/$08A4 controller ownership;
ordinary co-op continues fixed-slot routing. Mounted barrels retain native
setup in this context.

Accepted reaction $23 records the animal owner and interaction source. At the
native surviving animal-damage continuation $B8:8DCB, consume that candidate
once and toggle only controller ownership. Native damage, recoil, health and
invincibility remain untouched. Rejected/replaced reactions and load/session/
mode resets discard the candidate. The selected controller lives in existing
guest state, so reload/rewind preserve the current turn without a save-format
change. Feedback follows the selected animal controller too. No health-count
threshold controls switching, allowing repeated surviving hits.

The shared item operand in drop_sprite_if_holding and
drop_follower_kong_if_teamed_up now uses Dkc2CoopHeldObjectValue. Water
entry and other Kong-local drop callers see only their own held object;
another player's item appears absent to that drop decision. Preserve the
actual global held slot and native drop/release implementation. The carrier
and non-Kong global cleanup keep the original operand. Ownership reconstruction
from carry states also works immediately after snapshot load.

### Onscreen terrain damage versus pit removal
The native accepted-death helper serves both instant pits and harmful terrain.
Before survivor parking, onscreen actors with native +$10 bit $0100 and no
+$1E instant-pit bit $0080 retain native interaction $23. Promote the touching
actor through the existing leader/work/controller/collision swap before that
reaction so inactive-player terrain contact cannot damage the other Kong.
Native states $24/$25 retain their animation and existing survivor handoff;
no animation constants or new host death timer are introduced. Offscreen and
instant-pit recovery retain the existing path.

### Barrel descent shared-stomp eligibility
CoopStompAirborne includes native state $12, which persists after the barrel
launch during descent. Both the accepted stomp owner and nearby partner can
be in $12 or ordinary jump states. The existing nonnegative vertical-speed,
no-ground-contact, live-actor, mode, mount, lost-player and distance guards
still apply. The existing accepted native bounce copies Y impulse plus work
gravity/terminal speed and changes the partner to $16; no barrel trajectory,
input mapping or threshold changes are required.

### Independent ground-shake stun
Kudgel's landing candidate read in bank_B6_819C falls back to an eligible
inactive Kong when the leader is airborne or otherwise ineligible. Reaction
$0B selects that Kong's native work context, and repeats its native body once
for a grounded partner after the leader enters $63. Guest state $63 makes
the repeat bounded without new host latches or snapshot fields. Each native
initialization retains its character animation, physics setup and item policy.
The source adapter wraps CODE_B9DFE5's follower decision for $63, and the
existing recovery helper admits $63 at the final idle transition. This lets
the inactive Kong leave stun through native completion. The shared $091F
clock is decremented in kong_state_63 at $B8:AFAA only for the active Kong
when both are stunned; a lone stunned follower still decrements it normally.
No guessed animation or timer values replace the cartridge's setup.

### Honey-floor reaction ownership
Native terrain reaction $07 uses the same queued-Kong $0A84 evidence as rope
contact. Extend the existing work-context redirect to $07 so it initializes
the touching Kong, preserving both leader and controller ownership. Its shared
held-item read also uses Dkc2CoopHeldObjectValue, preventing another player's
item from selecting honey carrying state $7A. Native sticky state $43,
animation, input mask and jump physics remain intact. No new runtime policy,
host latch or snapshot format is needed.

### Sticky-wall contact ownership
Terrain reaction $08 joins the existing queued-Kong work-context redirect.
Its native work selection now follows $0A84, preserving active/inactive work,
fixed controller ownership and each character's native attachment animation.
The original wall state $41 and wall-jump state $42 remain cartridge-owned;
no guessed wall coordinates, forced attachment, new host state or global
wall-control override is introduced. Native actor-local held-item dropping
continues through the existing drop helper policy.

### Honey movement camera eligibility
CoopCameraPosition accepts native live honey states $41..$44 and $7A, alongside
its existing movement states. This covers attached walls, wall jumps, sticky
floors, wall slides and honey-floor carrying without changing native physics.
The original dead/lost/mode/scripted-actor guards and fixed-point midpoint
arithmetic remain unchanged. Camera target calculation uses both actors on
both axes, regardless of which Kong supplies the native camera selector.

### Compiled developer commands
config/developer.cfg controls DKC2_DEVELOPER_ENABLED and the function-key
number DKC2_DEVELOPER_BEAT_LEVEL_KEY (0 disables it; 1..12 selects F1..F12).
runner/dkc2_developer.c owns a portable beat_level command. Windows and SDL
queue a non-repeating key-down only outside the overlay, then execute it
between emulated frames. Disabled builds expose no developer shortcut and
reject commands without guest writes. Native CPU registers/stack/continuation
are never changed by the command.
Gameplay room kinds 0/1/2/3, fixed valid Kong ownership and a live
leader can complete. The gameplay continuation $0020=$8608 also gates the
command, since world maps can retain a boss room kind and camera context.
Menus, maps and active/death exits are rejected. Bonus exit selector $059D=1
is valid gameplay, rather than a death sequence. $08A8 selects the map-level completion word at $7E:59F2;
the mask comes from the supported ROM's $BB:817F table, matching the native
$BB:8158/$8169 routines. Native $B8:A5CA/$A5DA and $80:8C03 establish the
leader/follower exit states, fade and exit flag; the command applies those
same guest fields and leaves map transition/progression to the cartridge.
Bonus/boss reward records use room $D3 and bitmap $7E:59B2; new rewards
increment $08CC up to 75 and set $08C4 bit 2. A bonus leaves the parent
level completion bit unchanged and uses the native parent/animal return.
A boss selects the normal destination exit (kind 0), bypassing kind-3
sublevel restoration. The skip credits completion without playing the
boss defeat script; final ending cinematics remain unverified.
Headless DKC2_DEVELOPER_COMMAND/FRAME provide deterministic private checks.
Hidden Windows acceptance supports DKC2_SAVESTATE_INPUT and test F-key message
injection, allowing exact checkpoint tests; normal desktop launches use UI
save slots. Controller input recordings do not encode developer commands;
snapshots do preserve resulting guest progress.

### Team throws ending at hangers
The state-dispatch policy releases native throw-wait states $20/$2C only when
both independent Kongs are live, the hands are empty, and the partner is in
$3C at a recognized hook's exact native anchor (hook X, hook Y + 23). The
thrower resumes state 0 with its interaction bits restored; position, velocity,
work record, controller identity and passenger state remain intact. This also
recovers a saved wait, without host-only pending state. Flying passengers,
other props, mismatched anchors, animals, lost players and classic mode retain
the native wait.

### Sticky-wall Select rescue
CoopRescueMove recognizes live honey source states $41..$44/$7A locally;
the general CoopRescueMovable predicate retains its existing callers' policy.
A live attached $41 destination is valid without grounded flags; grounded
honey-floor states $43/$7A are also valid. Airborne wall-jump destinations
remain excluded. Wall destinations use the exact established anchor instead
of an offset that could enter solid terrain. The rescued actor receives native
wall state $41, its character's animation $2B, zero render/velocity offsets,
and only the destination's facing bit and wall side flags. Target state,
position and controller ownership remain intact. Leaving honey resets stale
animation callbacks and restores that character's normal gravity/terminal
speed from its property record; water destinations retain swim initialization.

### Honey survivors after pit loss
Dkc2CoopPitSurvivor accepts live honey states $41..$44/$7A alongside ordinary
movement and existing hanger/pole states. Keep the same lost-mask, collision,
hazard and screen-boundary guards. The helper promotes the living survivor,
parks the fallen player with collisions disabled and records its lost bit;
that bit survives snapshots and blocks Select revival. This preserves the
existing DK-barrel revival policy for both players. Survivor pose, animation
and coordinates are unchanged, including a sticky-wall attachment.

### Squitter mounted pit loss
CoopMountedRider and CoopReleaseLostMount include Squitter type $190 alongside
Rattly/Rambi. Live riding poses include $09/$0A/$0B; airborne $0A is not an
on-foot transition. Once the cartridge accepts a fixed Kong's pit loss, active
rider cleanup validates the mount record itself, without requiring a riding
pose that may already have changed. Release swaps the original palette-index
bits back, leaves the animal in its native unmounted lifecycle and clears
$6C/$6E and mounted flag $08C2 bit $0200 before promoting the survivor. The
existing old-save repair also recognizes stale Squitter context after a lost
rider. Lost-state persistence and DK-barrel revival policy remain unchanged.

### Centered co-op viewport
The earlier midpoint operand policy selected the right target but still let
native single-player offsets displace it within the viewport. A new guarded
Dkc2CoopCameraCenter call at camera_handler $B5:E549 follows desired-coordinate
calculation and precedes native bounds/region/scroll processing. It reuses the
existing live-pair eligibility checks and saved camera source $0A98. Both 16.16
positions are averaged in 64 bits, converted to native 24.8 desired coordinates
at $0ADE/$0AE2, and offset by half the native viewport (128,112). Negative
coordinates saturate at zero; native processing retains regional/world clamps,
smoothing and streaming. CPU registers, actor positions and camera profiles
are untouched. Ineligible/dead/scripted pairs leave native desired values
unchanged, so no cached co-op profile leaks after a handoff. The source adapter
requires one exact continuation and fails closed on missing/misplaced hooks.

### Pole departure and respawn reconciliation
The native no-contact branch does not call PoleCurrentValue. PolePendingValue
therefore expires a recognized pole reference in +$4C when a jump leaves
contact, or when the player is no longer in a pole/jump action. Inside-contact
jump suppression still prevents immediate reattachment. This prevents stale
references from rejecting a later pole occupying the same actor slot.
The state dispatcher also reconciles a live established gameplay actor's
lost bit when it is the active Kong, even without the both-present flag. An
inactive actor still requires both-present evidence; waiting/death poses and
missing/live-mask checks retain the loss. New lives can thus restore one
player without leaving it logically dead or reviving its lost teammate.

### Independent coaster jumps, stomps and departures
Simultaneous TEAM scopes skull-cart adaptations to native type $0158. Seated
$5B/$5C Kongs start their own $67 jump using their own controller and ROM
physics; $68 retains the native coaster stomp bounce. Cart seat placement
preserves airborne height and uses ROM seat/slope offsets when landing.
Only a proven different cart owner expires an old seat reference, so delayed
initial barrel-to-cart recruitment remains intact. Native cart track movement
and sparks are preserved.
Enemy collision branches classify the actual airborne Kong independently of
$0D5E's shared cart-body attack. Accepted stomps use native reaction $1B;
shared stomp eligibility includes descending $67/$68 and retains each
partner's coaster bounce state. Seated and classic reactions remain native.
The $B8:B038 air-follow routine writes the current Kong's work record, not
only active work $0595. Departure releases only that Kong's seat, carries
forward cart velocity, and clears its stale horizontal correction at work
+$0E. Jump and seated landing also reset that correction.
Falling-cart placement and native reaction $29 inspect each player's own
position/attachment before ending the level. A seated faller is lost; a safe
airborne Kong or rider of another cart retains its trajectory and control.
Old cart references are detached before they can pull a survivor down from
a new cart. Lost, non-colliding coaster poses normalize to inactive $13;
DK-barrel revival remains responsible for restoring them. Final loss retains
native team-life and Game Over handling.

Native cart follower scanning at $BE:D88B also translates the inactive
Kong by cart-history deltas before testing contact. Independent players skip
that translation and retain the native contact test at $D8AA. Reaction $15
rejects lost riders and automatic reunion during an existing jump/bounce; a
different cart still accepts a descending contact. This avoids large inactive
position changes and premature jump cancellation. Recruitment from the initial
barrel remains native, including its delayed ownership pointer update.

### Accepted coaster landing with a lost teammate
Reaction $15 already represents a native accepted foot collision. A detached
$67/$68 survivor can have its origin more than 24 pixels above the cart at
that point. The extra origin-height rejection is removed; own-cart/rising
reunion guards remain. A descending new-cart reaction aligns Y, clears its
fraction and ends vertical velocity before native boarding initializes the
seat and animation. This prevents next-frame contact validation from ejecting
a newly accepted rider. Detached live coaster flight retains its own active
collision box. Native airborne horizontal steering is preserved.

### Bounded partner cart landing
The Kong state dispatcher can complete a $67/$68 descending flight beside a
teammate already seated in a valid $0158 cart. It requires a genuinely empty
front/rear slot, a live non-lost partner owning that cart, and a different
source cart. Destination uses the same native character/slope seat offsets as
cart placement. Acceptance bounds are +/-32 X and -32..+24 Y relative to the
empty seat. Rising actors, non-jumpers, lost actors and own-cart jump arcs are
ineligible. Accepted landing clears only the old owned seat, sets destination
ownership/pose/animation, ends vertical motion and clears own air correction.
Filling a missing front seat advances native waiting $0101 to moving $0201;
track motion remains native. No host-only landing state needs serialization.

### Detached coaster shared stomp
A live coaster flight ($67/$68) remains eligible after +$42 cart ownership
clears at departure. Accepted-owner bounce validation uses flight identity
instead of requiring a live cart pointer. The partner retains coaster $68
bounce for attached or detached flight; generic airborne actors still use
$16. Collision attack classification likewise uses the actual live coaster
collider, independent of stale/shared $0D5E ownership. Existing distance,
descending, lost, mount and both-present guards remain unchanged.

### Coaster release on cannon capture
Native cannon occupant states $11/$31 cannot be recruited by a $0158 cart.
The existing collision-flags operand filters them before native cart contact
can write seats or queue interaction $15; the accepted interaction guard also
rejects an already captured occupant. At Kong dispatch, capture releases that
Kong from stale cart seats and its old ownership pointer. Global $0D5E is
cleared when no real live rider still owns that cart, preserving another
independent rider when present. This prevents a departing cart from reclaiming
a cannon occupant and prevents a reused actor slot from adding remote cart
geometry to cannon-shot collision bounds. $12 shot flight retains native
cart recruitment so destination landing remains intact.

### Scripted coaster exit seating
The seat-placement adapter also carries a live non-lost $70 exit rider whose
+$42 still owns the current cart. Native $70 waits for the cart to carry the
Kong past the viewport edge before setting its end fade. Treating it as an
ordinary excluded action stranded the Kong while the cart moved away.
Placement retains $70 and native slope/seat offsets; it does not initialize
a jump or change the native ending. Missing, lost, mismatched-owner and
classic actors retain their previous behavior. The broader gameplay rider
classification remains unchanged so scripted exit is scoped to placement.

### Self-sourced terrain hurt ownership
Ceiling/side movement can queue native interaction $23 directly with the
current Kong as source; it bypasses the floor/pit handler that already
promotes terrain victims. Capture the live, non-lost source when it is a
fixed Kong slot and matches current_sprite at accepted-reaction recording.
The existing accepted-hurt ownership hook (historically CartHitOwner) promotes
that recorded victim before native work selection, just as it does for cart
collisions. Validate the accepted source again, consume ownership once, and
retain source invalidation/session reset. Do not infer this owner from $6A,
which may already name an unrelated collider. Native hurt/death and handoff
continue unchanged; classic behavior and other sources retain their policy.

### Shared kart jump ownership (2026-10-27)
Type $0158 mode $02 uses the native whole-vehicle jump, unlike mode $01
coasters. The bounded $BE:D822 active-owner read selects the existing front
rider or first live collider when empty; accepted $15 recruitment promotes
the front owner for the original seat-state assignment. Native cart +$42
therefore persists first-boarder ownership without host metadata. The
$BE:DB91 work read chooses that rider's work record and records only their
own Jump edge. Native buffered jump, impulse, gravity and track landing
remain unchanged. Both riders bypass independent $67 launching in mode $02.

### Thrown contact and framed hook sharing (2026-10-27)
A live non-lost state-$1F collider receives the hook interaction bit only
inside the hook collision filter. Native hitboxes and overlap directions
still decide contact. The throwing leader's state-$20 wait cannot veto
its own partner's accepted hook collision. The inactive contact callback
selects native $B3:9914 direct attachment, promoting the actual collider
unless the leader is mounted. Native follower $98BD instead starts an AI
team-reunion flight and can stall independent thrown actors.
Hook +$4E high bytes encode camera framing, not unshareable moving hooks.
Recognize occupied hooks regardless of those bits. High-byte hooks retain
their native camera pass; a bounded scan at $B3:986B then uses the same
long-call collision wrapper as boss hooks. Low-byte-only hooks retain
the earlier shared state dispatch. Release re-arms native cooldown for
framed hooks too, with per-collider exclusion for actors already hanging.

### Successive hook contacts (2026-10-27)
Direct attachment can interrupt state-$1F before grounded state-$21 restores
interaction flags. A live non-lost state-$3C actor now restores +$30 bits
$0018 in its state dispatch. This repairs saved hangs with mask $0016 and
preserves all other flags, so later jump contacts regain native bit $0008.
The native busy-leader gate ignores a live non-lost leader's hanging state
for any valid hook, rather than only when that leader occupies the current
hook. Other busy actions keep native rejection. Native overlap, jump arcs,
hook framing and cooldowns remain responsible for contact and release.

### Cat O' Nine Tails contact ownership (2026-10-27)
Record the fixed Kong slot at accepted native interaction $13, validate the
source type $0204, and promote that collider before native captured work
selection. Consume/reset this transient record with existing accepted
reaction metadata. Skip native inactive-Kong state-$2F recruitment and
spin-position duplication in simultaneous co-op. Spin selects its live
state-$48 Kong rather than assuming the global active slot still owns it;
that native state persists through saves and unrelated teammate promotion.
The camera's $0AE8 selector ordinarily points to the spinning enemy. For
this enemy only, select its captured Kong and admit valid captured state
$48 to the existing midpoint policy. Otherwise camera panning clamps the
faraway teammate against the screen edge despite removing paired capture.
Keep native cat animation, contact, release actions and classic behavior.

### Packaging validation follow-up (2026-10-29)
Packaging scripts resolve bundled hash/ZIP modules from the running host PSHOME to tolerate cross-edition PSModulePath inheritance through CTest.

### Item replay setup follow-up (2026-10-29)
Blank-SRAM item acceptance uses actor-specific input-only approaches and
retains Run after chest pickup. Assertions validate native chest identity
and both owners across partner inputs and snapshot loading. Runtime policy
is unchanged.
