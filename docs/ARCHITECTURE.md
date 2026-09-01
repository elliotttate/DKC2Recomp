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
deterministic swap boundary. Its visible context requests SDL swap interval
one on Windows and reports the accepted state through the same diagnostic
backend field; hidden automation disables the interval. Visible macOS instead
uses interval zero by default because a 60/120 Hz blocking swap would be a
second timing gate behind DKC2's 60.098811862 Hz clock. The Mac loop waits one
absolute Mach deadline, performs a short final spin, presents after that wait,
and re-anchors deadlines missed by more than 2 ms. The macOS compositor still
receives one complete frame atomically. Swap policy affects only host
presentation; emulation/audio state remains owned by the same exact-rate host
loop.

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
the margins. That is exactly what a wider PPU would draw from a map that
wraps at 256 pixels: HDMA phase, hardware windows, and color-math
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
rolling layer shows no margin content rather than raw recycled VRAM.

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

`scripts/create_private_diagnostic_version.ps1` is a deployment wrapper around
that boundary, not a new runtime architecture. It assembles a private,
external, append-only kit and preserves the SRAM that existed at recording
start beside each route. That paired SRAM is supplied to deterministic replay,
preventing later personal progress from changing a diagnostic run. ROMs,
saves, recordings, memory dumps, and captures remain outside Git.
