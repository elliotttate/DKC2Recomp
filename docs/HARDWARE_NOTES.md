# Hardware notes

## DKC2 cartridge map

The supported cartridge is a 4 MiB FastROM HiROM image with 2 KiB battery-backed
SRAM. Its internal header reports map mode `$31` and SRAM size code `$01`.

The baseline map follows the standard SNES system areas and the known
SHVC-1J1M board map used by North American DKC2 cartridges:

| SNES address | Runtime destination |
| --- | --- |
| `$7E-$7F:0000-FFFF` | 128 KiB WRAM |
| `$00-$3F,$80-$BF:0000-1FFF` | first 8 KiB WRAM mirror |
| `$00-$3F,$80-$BF:2000-5FFF` | I/O callback or open bus |
| `$20-$3F,$A0-$BF:6000-7FFF` | 2 KiB SRAM mirrors |
| `$00-$3F,$80-$BF:8000-FFFF` | HiROM upper-half mirrors |
| `$40-$7D,$C0-$FF:0000-FFFF` | full-bank HiROM windows |

References:

- [SNESdev memory map](https://snes.nesdev.org/wiki/Memory_map)
- [SHVC-1J1M-20 board map](https://snescentral.com/pcbboards.php?chip=SHVC-1J1M-20)
- [fullsnes hardware specification](https://problemkaputt.de/fullsnes.htm)

## Diagnostic WRAM identities

`recomp/layouts.toml` is the reviewed source for WRAM names used by host
diagnostics. It identifies live camera/configuration words, background
streaming state, the 25-slot sprite table, its render-order table, and 13
confirmed fields in the `0x5E`-byte sprite record. These labels describe the
supported USA v1.0 game layout; they do not replace the shared SNES memory
model.

`scripts/build_dkc2_symbol_database.py` checks every object against the 128 KiB
WRAM range and every typed field against its structure bounds. The generated
`scripts/dkc2_symbols_generated.py` is imported by the TCP capture tool, so
camera, object, placement, state, and despawn reports cannot silently diverge
from the documented offsets. New fields remain `guessed` or `contextual` until
repeatable code/runtime evidence justifies `confirmed`.

## Open bus

An address with no responding device does not simply read as zero on a SNES.
The baseline runtime retains the most recent A-bus data byte and returns it for
unmapped or unhandled I/O reads. DKC2 is known to depend on open-bus behavior,
so this is required functionality, not an optional accuracy tweak.

The final model must be more detailed. PPU1, PPU2, and CPU-side registers can
have distinct latches or mixed driven/open bits, and DMA affects which values
remain on the bus. Those rules will live behind the I/O callbacks and will be
validated with reference traces.

## Current general-DMA model

`dkc2_snes_io` implements synchronous A-bus-to-B-bus general DMA for channels
0 through 7. It supports all eight B-bus transfer patterns, fixed/incrementing/
decrementing A-bus addresses, a transfer size of zero meaning 65,536 bytes, and
register write-back after completion.

The model currently rejects B-bus-to-A-bus transfers explicitly. It also does
not consume CPU cycles, interleave refresh, arbitrate HDMA, or expose bus pins.
Those are timing-layer responsibilities.

The first real-ROM checkpoint configures channel 0 as mode 1 with a fixed ROM
source and writes alternating bytes to `$2118/$2119`. The runtime transfers
65,536 zero bytes and confirms the complete 64 KiB VRAM array is clear.

## Current PPU memory ports

The bring-up model stores PPU register writes and implements:

- `$2115-$2119` VRAM increment, remap, address, and data behavior;
- the shared background horizontal/vertical scroll write latch used by
  `$210D-$2114`, while retaining the Mode-7 H/V offsets;
- the shared low/high write latch for Mode-7 matrix/center registers
  `$211B-$2120`;
- signed `M7A * high_byte(M7B)` output through `$2134-$2136`;
- `$2121-$2122` CGRAM address and paired data writes; and
- `$2102-$2104` OAM word addressing, paired low-table writes, high-table
  mapping, and write-address progression.

PPU read buffers, VRAM/OAM/CGRAM access restrictions, and dot-level behavior
remain to be implemented and checked against traces.

## Current headless PPU renderer

The opt-in renderer turns the stored PPU state into a 512x224 RGB frame. It
implements tiled modes 0, 1, 3, and 5; Mode-7 BG1 and EXTBG with signed affine
coordinates, screen flips, repeat modes, and interleaved VRAM; 2/4/8-bpp
planar tiles; 8x8 and 16x16 background tiles; map/tile flips and priorities;
sprites in every OBSEL size pair; priority rotation; the 32-object/34-sliver
scanline limits; and main/subscreen fixed-color addition/subtraction.
Low-resolution pixels are doubled; Mode 5 retains separate 512-wide pixels.

A visible scanline is captured when the scheduler reaches HBlank, before that
line's HDMA changes register state. A complete frame is published after line
224. Each frame records which modes it used and any unsupported features it
encountered. Global counters separately retain limitations from earlier
frames.

Modes 2, 4, and 6, windows, direct color, mosaic, pseudo-hires, and interlace
are explicitly marked unsupported. This keeps a recognizable image from being
mistaken for a complete PPU implementation. See
`PPU_RENDERING.md` for the detailed priority, hashing, export, and verification
contract.

## WRAM data port and CPU arithmetic

`$2181-$2183` select a 17-bit WRAM address. Reads or writes at `$2180` transfer
one byte and increment that address modulo 128 KiB. The unused `$2184-$21FF`
B-bus range follows the current main open-bus model. This matters because
DKC2 uses a 16-bit store that writes the high byte immediately after `$2183`.

Writing `$4203` captures two unsigned 8-bit operands; `$4216/$4217` receive
the product after 48 master cycles. Writing `$4206` captures a 16-bit dividend
and 8-bit divisor; `$4214/$4215` receive the quotient and `$4216/$4217` the
remainder after 96 master cycles. Division by zero produces quotient `$FFFF`
and returns the dividend as the remainder. The delays run on the shared master
timeline, so their real-console alignment is limited by the provisional CPU
clock adapter.

## Current APU model

The default boot probe retains the version-0.3 barrier at the SPC700 IPL-ready
comparison at `$B5:821A`. This makes the older 74,262-instruction checkpoint
directly reproducible.

With `--with-apu`, `$2140-$2143` connect to four separate one-way values in
each direction. The imported SPC700 executes the real IPL, publishes `$AA/$BB`,
accepts `$CC`, echoes transfer counters, writes ARAM, and starts the uploaded
program. A synthetic test independently transfers two bytes to `$0200/$0201`.

DKC2's 16-bit store of `$01CC` writes CPU port 0 and port 1 on consecutive
65816 bus accesses. Advancing the APU by 64 cycles between those writes made
the IPL observe the old zero in port 1 and jump instead of entering transfer
mode. The temporary scheduler therefore advances one complete SPC opcode per
APUIO access, the smallest unit exposed by the core. This is correct enough for
the deterministic bootstrap but is not cycle-accurate.

The optional probe leaves the audio loader, performs later DMA/decompression,
and stops at a `$4211` TIMEUP read. Its private 64 KiB ARAM SHA-256 is
`49dd67b90ddb9ba3b7c75c3fcd02bf1bcebaf3ecabfa4392cb84a4e68b17784f`.
The hash must still be compared with an accurate emulator at the same point.

## Current CPU timing boundary

The opt-in timing path now models `$4200`, `$4207-$420A`, and `$4210-$4212`,
including clear-on-read NMI/TIMEUP latches, H/V timer comparisons, interrupt
delivery, scanline progression, and `WAI` wake-up. It uses 262 scanlines of
1,364 master cycles, HBlank at 1,096, and non-overscan VBlank at line 225.

The hardware timeline consumes master cycles, but the current CPU adapter
estimates eight master cycles per visible A-bus byte access. Internal cycles,
dummy accesses, and address-dependent bus speeds are missing. See
`TIMING_AND_INTERRUPTS.md`; reported frame and beam positions are provisional.

## Current HDMA and controller model

HDMA initializes enabled channels at frame start and runs at visible HBlank.
All transfer patterns, direct/indirect tables, repeat/write-once line counts,
and register write-back are implemented. Transfers currently occur as atomic
HBlank events without consuming their bus duration.

Two controllers expose manual `$4016/$4017` serial reads. Autojoy begins at
VBlank when enabled, reports busy for 4,224 master cycles, and publishes
results at `$4218-$421B`; controllers 3 and 4 are zero. The private probe uses
neutral input by default. `--controller1=<mask>` and `--controller2=<mask>`
allow deterministic held-button probes; masks use the standard 16-bit autojoy
layout (`$1000` is Start).

The interactive Windows host converts focused keyboard and XInput state to two
12-bit controller masks once per frame. The shared ImGui launcher exposes both
players and persists each None/Keyboard/Gamepad source plus deadzone. Connected
gamepads are assigned in XInput user order to players that selected Gamepad,
then packed into the shared runtime's existing controller-1/controller-2 input
word. D-pad and left-stick directions share the directional bits. The game's
own "2 PLAYER TEAM" menu selection is the co-op switch; a source-owned
generated-code adapter (`scripts/apply_dkc2_coop_overrides.py`,
`runner/dkc2_coop.{c,h}`) then lets each Kong slot read its own controller
every frame instead of the cartridge's alternating control, keyed by the
cartridge's slot-to-controller binding at `$80:883B`. The classic alternating
policy remains selectable (overlay "2P Team mode", `CoopMode` in
`launcher.cfg`, or `DKC2_COOP=simultaneous|classic`) and is byte-for-byte
stock outside TEAM mode. The adapter chooses which controller words the
Kong action gate admits and redirects passive/follow-history states `$13/$22`
at Kong dispatch to retain independent control. On ordinary follower idle it
also enables the normal `$1E` interaction mask and `$E4` render order, preserving
special action masks. Normal Kong clipping now uses the existing inactive-Kong
hitbox path for the second actor. A queued stomp remembers the colliding Kong
so its bounce does not go to the active leader. The normal follower palette's
`$1E` byte offset is suppressed in simultaneous TEAM mode; special palettes
retain their own paths. The two controller masks on the bus are unchanged.
The follower's normal mask can also be `$06` after a leader swap; idle
promotion restores `$1E` for that exact mask while keeping hurt invincibility.
Recovery, attachment animation and the throwable release terrain sweep consult
the acting/owning Kong. The policy records lost Kongs until an accepted DK-barrel
rescue; a small host snapshot field preserves that record across save/load and
rewind. This uses existing zeroed tail padding without moving guest state or
changing snapshot size. Legacy slots are still accepted. TEAM's turn-handoff
wait resumes the survivor's already initialized jump without changing SNES inputs.
Synthetic tests cover every face, menu, shoulder, D-pad, analog direction, trigger
threshold, two-player route, and port packing. Left trigger is a host-only
rewind action and right trigger is host-only fast-forward; neither is exposed
to an SNES controller register. Both actions, plus configurable and overlay
file-state actions, are suppressed unless the user explicitly enables Assist
Tools in the host launcher or overlay. The native Mac Game menu is a separate
fixed host command surface: its Quick Save/Load commands always address Slot 1
and do not require Assist Tools. The overlay's five-slot selector and snapshot
files are host state and never appear on the SNES bus. Opening the overlay also replaces
the packed controller word with
zero, pauses host audio, and stops scheduling console frames; none of those
menu inputs enter an SNES controller register. The SDL/OpenGL overlay uses
ImGui's logical display size rather than the backing drawable pixel size, so
Retina scaling affects neither its center nor its drag coordinates. Rumble,
DirectInput, and native PlayStation
APIs are not exposed by the desktop host yet.

The accepted desktop executable is a Windows GUI host. A no-argument launch selects an
external `.smc` or `.sfc` through the standard file dialog; an explicit ROM
argument is retained for automation. Both paths use the same SHA-256-enforcing
loader, so file selection does not weaken the private-ROM boundary.

The SDL2 portability host maps the same two packed SNES controller words from
SDL keyboard/GameController state. Its queued 32,040 Hz signed-16 stereo and
OpenGL 4:3 texture are host transports around the same S-DSP samples and
PPU frame. The SDL target has completed this lifecycle on Windows. Linux and
macOS behavior is also exercised on Apple silicon through the native `.app`,
including visible video/audio, menus, aspect/fullscreen changes, exact-rate
pacing, and private-ROM smoke tests. The Mac bundle remains an ad-hoc-signed
local build rather than a notarized release. Linux behavior remains unverified;
no SNES hardware result is synthesized to cover that missing host evidence.

DKC2's HiROM header declares 2 KiB of battery SRAM. The desktop runner loads
and writes that exact runtime allocation at `saves/save.srm` beside the
executable and retains the prior clean file as `save.srm.bak`. CPU access still
uses the tested HiROM SRAM mirrors; persistence only copies the cartridge RAM
allocation to and from disk at process boundaries.

FPS display, host phase telemetry, the OpenGL/GDI presenters, optional
screen-color LUT, and executable icon are not SNES hardware. They observe,
transform a host-only copy of, or package the Windows output and do not write
emulated RAM, PPU registers, controller state, or master-clock counters. Raw
presentation bypasses the LUT exactly; CRT/Composite/Trinitron quantize the
already-rendered BGRX frame to five-bit channels and apply the documented
PSXRecomp-derived display-color model only for presentation. Raw frame hashes
and exports therefore stay authoritative and unchanged. Telemetry measures the
main-thread submission cost and active backend but does not yet claim a GPU
hardware duration.

Visible Windows OpenGL hosts request a one-buffer swap interval and publish the
accepted VSync state in the diagnostic presentation-backend string. This is a
host/display synchronization request, not SNES timing. Hidden automation uses
interval zero; GDI remains compositor-managed. Visible macOS deliberately uses
interval zero as well, and paces on the window's display link: the display's
own refresh ticks, requested at 60 Hz so a ProMotion panel does not tick at
120, decide when each complete frame is submitted, and dynamic audio rate
control absorbs the 0.16% between a 60-Hz display and the cartridge's
60.098811862 Hz. Measured on the 16-inch MacBook Pro's ProMotion display
during scrolling gameplay, the earlier free-running Mach clock dropped or
doubled a frame about twice a second as its phase wandered across the
refresh, and its swaps occasionally blocked for over 30 ms when the 60.0988-Hz
stream ran ahead of the 60-Hz compositor; the display lock showed every one
of 1,259 frames for exactly one refresh, presented 13.5 ms ahead of it with
a 1.4 ms spread. `DKC2_DISPLAY_LOCK=0` keeps the Mach clock, and
`DKC2_KEEP_OPENGL_VSYNC=1` retains the blocking swap as a diagnostic
comparison; neither is the default.

## Current long-run boundary

The former `$2135` boundary is implemented and covered by signed-product and
shared-latch tests. Subsequent real-ROM probes exposed and then crossed CPU
math reads at `$4216` and WRAM-port traffic at `$2181/$2184`. The neutral-input
integration test now runs to its 20,000,000-instruction limit with no explicit
hardware barrier. That is strong deterministic progress, but it does not prove
that the emulated state matches a real console or accurate emulator; exact
reference comparison is still required. The optional renderer now runs for the
same full probe and publishes deterministic frames. One Mode-7 Rareware-logo
frame is an exact RGB match to a Snes9x 1.63 capture, and its VRAM, CGRAM, and
OAM match an adjacent save state. Incomplete mode coverage, non-beam-aligned
registers, and provisional timing still prevent a console-accuracy claim.

## Native shared-runtime checkpoint

The pinned shared runtime now completes 12,000 neutral-input host frames. The
former first hang occurred while DKC2 sorted an object list in `$B5:F0E5`.
Opcode `$82` at `$B5:F348` is `BRL -$72`; the architectural target is
`$B5:F2D9`, using the PC after the two-byte operand. An expression that both
read and advanced `cpu->pc` allowed MSVC to add the displacement to the old PC
and land at `$B5:F2D7` (`STZ $44`), restarting the list forever. Reading the
operand into a temporary before changing the PC fixes both `BRA` and `BRL`.

At 600 frames the instrumented native run reports 589 active video frames,
11 blank frames (maximum six consecutive), 553 audio-active frames, and
588,046 nonzero stereo samples. These are liveness checks, not audio-fidelity
claims.

The former frame-3,600 sprite corruption was an OAM-port timing error. DKC2's
WRAM `$0200-$041F` source already matched Snes9x byte for byte and channel 0
correctly transferred 544 bytes to `$2104`, but the DKC2 frame adapter never
called `ppu_handleVblank`. The PPU therefore retained the internal address
left by the previous transfer and rotated the next sprite table through OAM.
The adapter now runs the shared VBlank handler after visible-line rendering.

At the aligned paced native frame 3,575 / Snes9x frame 3,578 checkpoint, VRAM, CGRAM,
and OAM hashes match exactly. Snes9x's libretro surface expands green through
RGB565 while the native surface expands SNES RGB555 directly; reducing the
reference green channel to five bits makes all 57,344 pixels byte-identical.
The native scheduler now limits each callback to `1364 * 262` master clocks and
uses the real suspended PC in its interrupt frame. A 12,000-frame run completes
two ordered attract cycles without a runtime failure, clipped audio, or a large
sample discontinuity. One full cycle's 32,040 Hz PCM has seven long silence
regions on both native and Snes9x, a 0.54% RMS difference, and comparable peak
and maximum-delta values.

The former 54-frame lag was an architectural program-bank bug, not a global
frame-rate error. `JSL`, `JML`, `RTL`, and `JMP [abs]` cleared PBR bit 7 in the
shared interpreter, so FastROM `$80/$B5/$BB` execution was charged through its
byte-equivalent SlowROM `$00/$35/$3B` mirror. Preserving all eight PBR bits
makes the three level-loading windows align at 152/152, 134/135, and 152/153
native/reference frames. First-cycle completion is now six frames early rather
than 54 late. Perceptual output-device validation remains open.

The Pirate Panic death/restart black screen was a static block-move timing bug,
not a PPU or game-state workaround. MVN/MVP count is A+1, so A=`$FFFF` means
65,536 bytes rather than zero. Every byte updates A, X, Y, and DB; X/Y wrap to
eight bits when the index flag is set; and each repeated byte adds seven CPU
cycles at the source opcode's mapped bus speed. A scheduled static transfer may
yield only after completing a byte, then resumes at the same opcode with the
updated architectural state. This matches the interpreter oracle and prevents
one host call from running through multiple frame deadlines.

## DKC2 stack-reset and fault-path contracts

The Rareware-logo restart path uses a legal but nonstandard 65816 calling
sequence. `$80:85E8` executes JSR to `clear_full_wram` at `$80:8E7F`. The
callee pops and saves its return address before clearing WRAM, resets S, and
uses `JMP ($0032)` at `$80:8EB8` to resume at `$80:85EB`. Static generation
therefore treats the JSR as terminal to its lexical block and models the final
indirect jump as a stack-reset tail dispatch. Modeling it as an ordinary
return would invent a destroyed stack frame; interpreting the entire routine
is no longer necessary.

Two other JSRs are explicitly different. `$B3:BC20` targets `$B3:F289`, and
`$BA:9C36` targets `$BA:F305`; disassembly layout and ROM-byte inspection show
that both destinations are data/garbage reached only by dormant original-game
bugs. They are not hardware behavior and are not assigned guessed exit M/X
states. Compiled callers preserve the JSR frame and enter the authoritative
interpreter at the exact destination if either fault path is invoked, retaining
the original crash semantics while excluding those bytes from the code graph.

## Host crash reporting is not SNES hardware

The desktop rolling report, breadcrumbs, diagnostic bundles, and Windows
minidumps observe the native host only. Their frame number and resume PC are
copies used to locate a failure; no report writer reads ROM data, serializes
emulated memory, changes timing, or enters the save-state format. POSIX signal
capture is similarly limited to a marker that is completed on the next launch.

## Deterministic route input is not SNES hardware

Desktop input recording and headless replay are host-side verification tools.
They capture the already-resolved 24-bit controller word at the frame boundary
and feed it back through the existing controller input contract. They do not
change serial-controller timing, WRAM, PPU state, audio generation, or the
master-clock budget. Private route recordings are external evidence and are
never source-controlled. The current format does not record host rewind or
save-state save/load actions. Fast forward still records every forward
emulated frame, but rewinding can restore an earlier guest state without
rewinding the host-side recording sequence. A route using rewind or state load
therefore cannot be an exact input-plus-SRAM replay fixture.

## Swanky game-show soft hang is not SNES timing

An owner-recorded Version 11 session appeared to fall to 1-3 FPS in Swanky's
Bonus Bonanza, but the native process did not crash. It exited cleanly after
34,960 host frames. Tier-2 evidence records one call to runtime-selected state
`$B4:A4CB` exhausting the host's 2,000,000-instruction interpreter safety cap,
followed by invalid dispatches and execution in PPU data-port addresses. The
long CPU safety loop explains the observed slowdown; it is not evidence of a
GPU presenter bottleneck or an authentic SNES wait state.

The underlying handler uses legal 65816 stack behavior. With M=0, `PLA` pops
two bytes and therefore consumes the JSR return frame installed by the runtime
dispatcher. The following `RTL` pops the surrounding three-byte JSL return
frame. This is an intentional guest non-local return. The native bridge must
translate the final hardware S into a host caller-skip result; forcing a normal
return would execute a compiled caller whose guest frame no longer exists.

The CPU executes these handlers through the `$B4:*` ROM mirror. Diagnostic
tools canonicalize that mirror by clearing bank bit 7 and therefore report the
same bytes as `$34:*`; for example, `$B4:A4CB` and `$34:A4CB` are one
cartridge location, not two functions.

The regenerated dispatch table now contains exact native entries for all six
Swanky states/helper addresses. Optimized Release and trace builds both
succeed. The focused validator requires a native M0X0 dispatcher hit at
`$B4:A4CB` and rejects any Swanky interpreter use, step-cap bailout, original
corrupt edge, or execution in `$00:2100-$00:21FF`. Its synthetic test passes
and the original Version 11 artifact fails as expected. Final gameplay
acceptance still requires a fresh owner run because the earlier controller
recording did not encode host rewind or save-state operations. The full CTest
result is 52/53; its sole failure is the unchanged supplied-ROM frame-3,309
sprite-reference mismatch.

External private Version 12 passed a 120-frame packaged record/replay/capture
smoke test with a clean exit, 84 retained native-dispatch events, and zero
layer-capture findings. This verifies the evidence plumbing and does not
substitute for reaching the Swanky state during the owner's focused run.

## Symbol promotion is not SNES hardware

Function labels in `recomp/bank*.cfg`, generated C, and trace output are host
analysis metadata. Expanding `CODE_BBXXXX` to a contextual name does not alter
the compiled instruction sequence, ROM address, guest registers, memory,
timing, PPU, APU, or controller state. The promotion tool deliberately retains
the original `CODE_BBXXXX` identity in each expanded name so an engineer can
still correlate it with the reference label and revision-specific CFG range.
Any behavioral or generated-instruction difference after a name-only pass is
therefore a regression, not an expected hardware effect.

## Post-rebase reference boundary

Upstream SNESrecomp now couples APU events to guest frame time. DKC2 builds and
completes two neutral-input attract cycles with active video/audio and no
clipping, but the former native-frame-3309 sprite checkpoint no longer matches
its pinned frame, WRAM, VRAM, or OAM hashes. The palette hash still matches and
the live OAM table matches its WRAM source at that frame. The trusted hashes
remain unchanged until a new event-aligned Snes9x comparison proves whether
the checkpoint moved or execution diverged.

## Personal test packaging is not SNES hardware

Copying the verified ROM, saves, and launcher configuration beside a numbered
executable is a host deployment convenience. The personal bundle uses the same
ROM validator, executable, relative save paths, and emulated hardware as its
matching public-safe package; no SNES timing or game state is transformed.

## Configurable host bindings are not SNES hardware

The pre-boot and in-game pause-menu keyboard/controller editors change only
host-side `launcher.cfg`. Both desktop hosts resolve those bindings into the same
12-bit controller words that were already delivered at the frame boundary.
Rewind, fast-forward, save-state, and load-state bindings remain host actions
and are suppressed by the Assist Tools gate. The native Mac menu's fixed Slot
1 Quick Save/Load commands bypass only this binding gate. No binding value is
written to WRAM, controller registers, snapshots, SRAM, or deterministic recordings.
Opening the pause menu suppresses the resulting controller word; remap
capture therefore cannot become an in-game input on the same frame.

## Widescreen is not SNES hardware

The cartridge still programs a 256-column SNES viewport. The optional
342-column output is a host PPU capability that exposes tilemap and OBJ data
outside that authentic center. It does not change dot clocks, H/V counters,
DMA timing, VRAM size, OAM layout, or the 65816 camera coordinate system.

DKC2's central placement-radius function at `$BB:BB07` stores a left allowance
and a complete horizontal span. The host adaptation adds 43 pixels to the
left allowance and 86 pixels to the span. The two world-sprite visibility
paths beginning at `$B5:9FC9` receive the corresponding transformation of
their native `$30` and `$160` constants. Disabled mode returns all four native
values exactly. This expands activation/despawn and rendering boundaries; it
does not manufacture level objects.

The rolling level foregrounds used by Pirate Panic expose 64 tile columns, but
that fact alone does not make every column current. DKC2 recycles those two
32-column VRAM pages as the camera moves. The host expands the live 10-bit PPU
scroll phase around the full WRAM camera, records authentic viewport tiles and
later VRAM writes in SNESrecomp's world-keyed shadow tilemap, and refuses to
serve an unknown margin cell from raw VRAM. This prevents old page contents
from appearing as sharp moving chunks at the side edges.

The cartridge's horizontal column builder reads source X at camera X (or
camera X minus `$0100` while moving left), but uploads that buffer to the VRAM
column for camera X plus `$0100` (or camera X while moving left). Consequently
the decompressed level-map origin is 256 pixels behind the camera/object
coordinate system. A matching frame-5,499 WRAM/VRAM capture measured
1,754/2,048 BG1 entries (85.6%) at source tile `world tile - 32`; the next-best
tested alignment measured 746/2,048. Remaining cells include live dynamic or
partially staged writes.

That offset also explains the repeated hard-left entrance defect: a symmetric
host viewport asks for world tiles below 32, but the decompressed map begins at
source tile zero. Pirate Panic `$0003`, its ship-deck bonus `$006F`, and
Mainbrace Mayhem `$000C` all exposed the same gap despite using horizontal and
vertical map layouts. Once the live stream destination and known layout prove
that the level decoder owns the terrain, the host reflects source tiles 0, 1,
... into world tiles 31, 30, ... and toggles the SNES tile H-flip bit. The rule
is margin-only and presentation-only; it cannot change the authentic center,
camera, collision, actors, or save state. Unknown map handlers still receive a
verified transparent margin instead of synthesized art.

`$0AFC` is the maximum horizontal scroll after the level-camera initializer
subtracts the native `$0100`-pixel viewport at `$B5:E36C-$B5:E373`. The
streamer keeps one additional 32-pixel metatile staged beyond that limit.
Reading the following metatile crosses into unrelated decompressed WRAM and
caused the colorful strip at the far-right room boundary. The host retains the
guard column, then uses an all-zero 4bpp character discovered in live VRAM so
the lower parallax layer remains visible.

The owner-recorded late Pirate Panic route also demonstrated that a transparent
map cell must not merely be skipped during source prefill. A skipped cell left
any older capture in its world-keyed shadow slot, producing brown/black deck
fragments in an otherwise transparent upper-left margin. Source-map calibration
at frame 14,400 matched all 896 inspected native BG1 cells and identified the
affected source cells as transparent. The host therefore actively clears only
verified transparent or out-of-bounds source cells, while respecting a live
game-authored write from the current or prior frame. This is history cleanup,
not a change to SNES VRAM upload behavior or the authentic center viewport.

At `Pirate Panic - 02` frame 15,900, camera Y is `$0100` while rendered BG1
vertical scroll is `$00FF`. Treating the PPU's full physical page as the
horizontal decompressed-map page selected source rows 31-59 and agreed with
only 277/957 native BG1 cells. The column builder's semantic source is the
low-eight-bit rendered phase nearest camera Y minus `$0100`; selecting the
preceding page agrees with 924/957 cells. The remaining 33 cells are consistent
with live/dynamic tilemap writes. This calibration is useful but not a complete
visual proof: the owner accepted frames 12,000 and 12,300, while frames 12,900,
13,800, and 15,900 still expose transition, bounded-room, or dynamic-layer
cases that the static BG1 source reconstruction cannot solve alone.

WRAM camera Y and the PPU scroll register are not guaranteed to describe the
same temporal frame at the host's draw boundary. During the reproduced late
Pirate Panic transition, WRAM had crossed an 8-pixel row while the rendered
PPU phase was still one pixel behind. Combining the PPU map row with the WRAM
destination row made `(5 - 6) & 31` select terrain 31 rows away for a row that
should have been transparent. Widescreen BG1 reconstruction uses one rendered
PPU phase for its shadow key and fine row, then selects the separately staged
horizontal source page as described above. This is a host-side correction
only; it does not alter the SNES camera, scroll register, VRAM upload timing,
or authentic 256-column center.

The owner-recorded `bg-02` swamp route exposed the complementary history
problem. DKC2 stages the rolling terrain tilemap one `$0100`-pixel page above
camera Y. Exact prefill and margin lookup were already using those source
rows, but native viewport captures and VRAM-write history were recorded using
raw camera rows. A cell remembered 32 tile rows too low could later override
the correct decoded margin cell as the camera moved vertically. The active
terrain layer now uses one unwrapped rendered PPU source-Y origin for all four
operations: live capture, VRAM-write association, lookup, and exact prefill.
The layer is still selected dynamically from `$17B6`, so the correction covers
the standard BG1/BG2 rolling streamer rather than one named stage.

Pirate Panic's 32-column BG2 sky/ocean map is intentionally wrapping, so the
host repeats the rendered native BG2 scanline. Collision-bearing BG1 is never
repeated. Bounded 32-column BG3, menus, and rooms remain centered and cleared
until an explicit reconstruction or rendered-scanline policy exists. An
enabled 64-column BG3 is different: after the live BG1/BG2 terrain target has
matched and exact prefill has succeeded, its physical adjacent columns can be
rendered without synthesizing or repeating content.

Rattle Battle level `$0005`, horizontal game sub-mode `$0006`, uses the
standard ship-deck Mode-1 configuration: BG1 `$71` at `$7000`, bounded BG2
`$5C`, BG3 `$79` at `$7800`, and main/sub enables `$17/$10`. The former host
tested only BG1/BG2 physical width and granted BG3 a Pirate-Panic-specific
level-effects exception. Rattle Battle therefore widened its terrain while
clamping the authentic mast/rigging layer at the original edges. The final
render-width mask now considers enabled BG1-BG3 physical allocation, but only
after terrain readiness. For this signature the result is BG1+BG3 (`$05`),
while bounded BG2 receives its existing rendered-scanline repeat (`$02`). This
is presentation-only: WRAM, VRAM, camera, collision, and streaming remain
cartridge-owned.

Topsail Trouble is deliberately different from the physical Rattle Battle
case. The preserved live state identifies level `$000B`, vertical sub-mode
`$0008`, terrain destination `$7800`, PPU Mode 1 (`BGMODE=$09`), BG1/BG2/BG3
screen registers `$79/$70/$6C`, and main/sub enables `$17/$13`. The byte-exact
`ship_mast_topsail_trouble_tileset_config` selects
`ship_mast_rainy_vram_payload`; that payload writes the same bounded rain
tilemap into `$6C00`, `$6D00`, `$6E00`, and `$6F00` in four `$0200` transfers.
It does not prove additional authored rain columns. The host therefore keeps
only BG1 in the physical-wide mask and repeats the fully rendered BG3 rain
scanline into the two presentation margins. On the exact 308x224 state, the
BG3 margins changed from one-color blank strips to the same four-color rain
plane while an absolute-error comparison of native X=26-281 remained zero.

A later owner Quick Save reaches Topsail Trouble's lower fixed camera at
`(636,3848)`, with the same level `$000B`, sub-mode `$0008`, BG1/BG2/BG3
screen registers `$79/$70/$6C`, and terrain destination `$7800`. The decoded
vertical source requests shadow tile rows 512-540. The original 512-row host
store silently rejected all 1,189 exact cells on both outer prefill bands:
BG1 then recorded 1,792 west and 1,792 east shadow misses in the captured
frame. A 1,024-row presentation store accepts the already-resolved world keys.
After that capacity correction, all four prefill bands contain 1,189 cells,
the three margin-prefill counters are 290, and both BG1 margins record zero
misses or raw fallbacks. The native 256-pixel center matches an exact 4:3
capture with zero differing pixels, and the before/after WRAM and VRAM hashes
are byte-identical. This is a host-history capacity defect, not a cartridge
streaming or gameplay-bound change.

The next owner Quick Save is still Topsail Trouble but at the terminal-right
camera `(768,3301)`, where maximum X is also 768. Shadow coverage is complete,
yet the widened east margin exposes source tiles 96-99: the streamer's hidden
32-pixel guard metatile. Its disconnected black/white rigging is valid
underrun protection, not accepted vertical-layout side art. The vertical
decoder now replaces only complete tiles at or east of world X
`maximumScrollX + 256` with the live verified-transparent character when they
are in a host-created margin. The exact correction changes 1,312 composite
pixels, all at 16:10 output X=282-307; the native center has zero differences
from 4:3, and WRAM/VRAM remain byte-identical. Horizontal and other layout
guard policies are unchanged.

Krow's Nest's preserved Quick Save identifies level `$0009`, sub-mode
`$0008`, camera `(256,288)`, BG1/BG2/BG3 `$79/$70/$6C`, main screen `$04`,
subscreen `$13`, `CGWSEL=$02`, and `CGADSUB=$24`. Isolated output proves BG2
already supplies full-width colored clouds while BG3 supplies the bounded
grayscale lighting input used by subscreen addition. Outside the authentic
256 columns, the old host therefore added BG2 to a black main-screen backdrop
and produced darker side bands. The accepted presentation rule repeats the
already-rendered BG3 scanline only for this exact scene and color-math
signature, changing the repeat mask from `$02` to `$06`. Frame-zero WRAM and
VRAM remain byte-identical, and the complete 256-pixel center has zero RGB
differences from the exact 4:3 oracle.

The preserved lava-stage state (level `$0007`) exposes a different HDMA use:
the frame begins with BG scroll anchors H=`[829,414,103]`,
V=`[448,487,121]`, while its upper band changes them to
H=`[414,207,103]`, V=`[487,859,121]`. BG1 therefore becomes the terrain plane
only inside that scanline band. Applying the frame's BG2 ownership to every
line left blank or unrelated margin cells; blindly repeating every live layer
made the terrain discontinuous; and reading raw BG1 margin columns exposed an
invalid 24x16 staging block. The accepted host path detects the two-plane
phase exchange, repeats only BG2/BG3, prepares a BG1 world shadow for the live
band, and overwrites the native strip from the authentic 64x64 tilemap. The
256-pixel center is unchanged, cartridge memory is never written, and the
ordinary frame keys are restored on exit or decode failure.

Level `$0008` reuses the same BG1/BG2 exchange with a different screen
composition. Its frame starts at main/sub `$13/$04`, line 1 switches to
`$13/$00`, and line 123 restores only BG3 on the subscreen while the exchanged
scrolls remain active through line 181. Line 182 restores the frame scrolls.
The terrain proof must therefore key on BG1/BG2 plus OBJ on main and no
BG1/BG2 on sub; requiring BG3 to remain on main incorrectly rejects the whole
band. BG3's independently HDMA-driven screen assignment selects whether it is
included in rendered-line continuation, but does not change terrain ownership.

The `bg-01` route demonstrates the opposite terrain ownership on a later
forest screen. At frames 4,500 and 4,800, WRAM `$17B6` is `$7800`, equal to
BG2's live tilemap base; BG1 is `$7000`. BG2's east-shadow misses rise from
740,390 to 1,086,159 while its unseen right terrain remains blank. Sparse
colored BG1 margin cells are the same decompressed level source being
prefilled into the wrong layer, not a presenter artifact.

The corrected adapter masks `$17B6` and each enabled `BGxSC` base to `$FC00`,
then selects BG1 or BG2 only on an exact match. In deterministic replays,
shadow BG2's world key now equals the camera at both focused frames
(`390,384` and `1088,375`). Right-margin BG2 non-backdrop pixels increase from
347 to 4,842 at frame 4,500 and from 324 to 3,939 at frame 4,800, while the
wrong colored BG1 terrain cells disappear. This verifies the layer-association
repair at those frames, not every foreground effect on the route.

The same focused captures showed that BG3 remained clamped: each entire margin
contained only one sampled color even though BG3's native region carried the
forest silhouettes. Mudhole Marsh identifies this layer as enabled Mode-1
2bpp BG3 at `$6C00`. Repeating its rendered native scanline raises the sampled
BG3 margin palette to 11/9 colors at frame 4,500 and 9/9 at frame 4,800,
removing the flat purple bands without widening raw BG3 tilemap reads.

The level configuration at `$0515-$0539` is a sequence of mostly 16-bit
fields. In particular, `$0529` is the gameplay sub-mode that selects DKC2's
per-theme main loop. The validated reference shows two distinct rolling-map
organizations: horizontal loops address a column-major map with a 16-metatile
vertical period, while vertical loops address a row-major map with a
32-metatile row stride. Bramble Scramble's sub-mode `$0010` instead invokes a
square scroller whose row contribution advances `$60` bytes, or 48
metatiles. At private frame 1,600, independent reconstruction with that formula
matches 954/957 native BG1 tilemap cells (99.69%); the horizontal and vertical
formulas match only 62.8% and 57.8%. The host therefore enables the square
layout for `$0010`. The wasp-hive main loop at `$80:D517` normally calls that
same square handler at `$B5:B54A`; it diverts through `$B5:B317` only when the
level-variant nibble equals five. The host now exposes standard sub-mode `$03`
hive rooms through the square decoder as an experimental policy. Other square
and special loops remain unclassified and centered.

Ship-hold game sub-mode `$0002` is not the static ship-cabin handler. The
reference `ship_hold_game_sub_mode` calls the square scroll family and its NMI
path executes both `dma_level_columns` and `dma_level_rows`. Its source map is
row-major with an 80-metatile (`$A0`-byte) row: the offset is the aligned X
contribution divided by 16 plus aligned Y multiplied by five. Lockjaw's
Locker exact-state reconstruction matches 957/957 sampled visible BG1 cells.
The host uses that source formula for both unseen margins and keeps the
cartridge's rolling `$3800` VRAM tilemap as the authoritative native source.
The same room's BG3 `$6C00` water is a bounded cyclic layer, so only its fully
rendered native scanline is repeated into the margins. BG2 `$7000` and the
later `$7800` page contain the same bounded cabin wall behind that terrain.
Reading either adjacent
64-column allocation exposes the lower blue layer at both old viewport edges;
the visible follow-up also proved that a 256-pixel post-render repeat copies
those clipped endpoint columns back into the margins. The wall's interior
matches at a 12-tile/96-pixel screen-space period. The accepted path renders
BG2 in isolation at authentic width, deliberately replaces only native X=0-6
and X=249-255 from the matching interior period, and uses that period for both
margins. No adjacent BG2 VRAM is exposed. The `$7800` page was confirmed at
fixed camera `(1592,1469)`; without the shared repeat it produced a blank
left extension despite an unchanged native wall.

Parrot Chute Panic demonstrates why sub-mode alone is not a complete geometry
key. Its level `$0013` runs wasp-hive sub-mode `$03`, but the level-selected
alternate entry at `$B5:B317` invokes `$B5:B0FC/$B5:B20D`. Those routines add
the aligned Y coordinate directly to the X contribution, proving a `$20`-byte
row stride: 16 32x32 metatiles, or 512 pixels. At attract frame 4,880 the live
terrain destination `$7800` matches enabled BG2. Exact prefill with this
formula extends the honeycomb terrain, while rendered-scanline repetition of
bounded BG1 `$6C00` and BG3 `$6800` retains the cyclic lighting and hive wall
without reading unseen VRAM.

Mainbrace Mayhem attract frames use a different combination: vertical BG1
terrain is already reconstructable, but bounded BG3 `$6C00` supplies the
cloud/lighting mask. Leaving that layer centered caused color seams exactly at
the old 4:3 edges; repeating its fully rendered scanline removes them. Rickety
Race remains on the standard horizontal policy. Captures at frames 3,350,
3,650, 4,000, 4,265, 4,550, 4,880, and 5,200 cover early/middle/late motion;
all background checks pass. The complete 12,000-frame widescreen run retains
28 state events, six demo starts, six demo ends, two complete cycles, zero
sequence errors, and zero clipped audio samples.

The resulting 3,134-frame private replay records 7,991 render-consumed OAM
samples in the widened margins: 7,357 left and 634 right, across 1,100 frames
from X=-43 through X=298. Selected composite frames 1,600, 2,400, 2,800, and
3,000 show continuous BG1 terrain and the existing safe BG2 scanline repeat;
BG3 remains bounded. The recording ends while level `$002E` remains active,
so these measurements do not certify the goal transition.

A fresh `bg-02` frame-2,600 calibration identifies sub-mode `$000F`, BG2
terrain target `$7800`, horizontal/column-major layout, and the audited BG3
repeat. Its composite/BG2/BG3 bundle has zero automatic findings. The narrow
terrain pieces visible at some side/bottom intersections decode exactly from
the live WRAM level map and agree with the cartridge-populated VRAM cells.
They are therefore authored data outside the original viewport, not evidence
of stale VRAM. Whether to conceal such unintended art is a later presentation
policy decision and must not be conflated with correctness repair.

## Presentation policy from PPU geometry

The following measured facts replaced the level-specific widescreen
exceptions that had accumulated through 2026-09-01. Each was checked on the
preserved Quick Save corpus with `scripts/check_widescreen_state_corpus.py`.

- A 32-column tilemap wraps at 256 pixels, so repeating its rendered
  scanline at period 256 is what a wider PPU would draw. Every bounded BG3
  that previously carried a named exception (the `$6C00`, `$6800`, and
  `$7400` backdrops of Mudhole Marsh, Topsail Trouble, Mainbrace Mayhem,
  Krow's Nest, Parrot Chute Panic, the ship holds, and the lava stages) is a
  32-column map or a colliding 64-column allocation.
- Mudhole Marsh's BG3 register `$6D` claims 64 columns at `$6C00`, but its
  extension page `$7000` is BG1's tilemap base. A 64-column allocation whose
  odd page is another enabled background's base page is bounded content.
- Lockjaw's Locker's cabin wall (BG2 `$71`/`$79`) is periodic at 96 pixels
  in rendered pixels on 151-171 of 224 rows depending on the camera, but not
  in tilemap entries: each column uses a distinct character index over
  duplicated character data. The remaining rows hold a non-periodic picture.
  Per-line pixel period detection reproduces the former hand-tuned 96-pixel
  continuation on the periodic rows (170 of 224 rows identical at camera
  `(1592,1469)`) and leaves the picture rows at 256. The former whole-layer
  96-pixel edge repair was also rewriting those picture rows' native
  endpoints; the per-line rule leaves them authentic.
- The lava stages (`$0007`, `$0008`) build their BG1/BG2 exchange from HDMA
  tables whose line counts exceed 127 and are therefore split entries; the
  dry run reads the same tables the runner applies. Frame anchors hold the
  lower-band roles and line 1 switches to the upper-band roles. The
  lower-band BG1 lava plane is not pixel-periodic (202 of 224 rows prove no
  period), so a repeat approximation cannot stand in for it anywhere inside
  the presented 4:3 region. At the hard-left camera bound the +43 bias moved
  eight of those columns into the PPU margin path, which differed from the
  4:3 render by 46 pixels per frame until repeated layers began rendering
  the biased 4:3 columns from real VRAM.
- Ship-hold water HDMA produces 208 scanline bands per frame; the band
  table handles that count with no per-line detector.
- The lava stage's 32-column BG3 plane (`$74`) has an authored seam at its
  256-pixel wrap: the contrast between map columns 255 and 0 is 114.5
  against about 15 for neighboring columns, and no line of it proves a
  shorter period. At the hard-left wall its scroll is 0, so the old
  presentation bias placed that seam at wide column 255 and the locked view
  places it at both old 4:3 boundaries; the console shows the same seam as
  soon as the layer scrolls. The margins match the exact wrap pixel for
  pixel, so the seam warning is authored content, not a decode defect.
  Per-line period continuation is limited to 64-column allocations, the
  only case with no hardware wrap to reproduce.
- Leaving a hard-left wall while holding Right (hard-left lava state,
  16:9): the cartridge camera stays at 256 for 14 frames, then advances 2-3
  pixels per frame. Under the `shift` edge policy the presented view stayed
  at 256 until frame 33 while the camera reached 298, then scrolled at the
  camera's speed; the HUD slid 43 pixels during that window. Under `reflect`
  and `bars` the presented view tracks the camera from frame 15. Under
  `glide` it moves from frame 15 at seven eighths of the camera speed and
  is centered 344 pixels in; the wall frame itself is identical to `shift`.
- Under a presentation bias the rolling ring's page past the cartridge
  window is not authored. Level `$0008` sub-mode `$0012` at camera 414
  (`glide` bias 24) showed a strip of unrelated tiles at world columns
  670-693, exactly the last 24 columns of the PPU window, on both the shadow
  path and the repeat-band path; the crystal mine (level `$0024`, camera
  260, bias 43) showed the same at its last 43. With the native viewport
  inset and the authentic-window merge, `glide` and `reflect` agree on every
  world column of both states and the corpus centers stay exact. The old
  screen-edge endpoint repair under a bias also changed four interior pixels
  at screen column 0; repairing at the cartridge window's edge instead moves
  that into the exempt 7-pixel edge band.
- The crystal mine (level `$0024`) shaft at camera `(448,3425)`: holding
  Left does not move the camera because Squawks meets the shaft wall, while
  `$0AFC` still reads the level-wide maximum; there is no minimum-scroll
  word. The map west of world 448 is wholly transparent for the whole
  visible height, and the wall metatile column at 448-479 is fully
  populated on the top band and the three bottom bands and open cave on the
  three bands between. The margin therefore showed the BG2 crystal backdrop
  through a hole the console never shows; the structural wall continuation
  fills the solid bands and leaves the open ones open, matching the cave
  beside them.
- The Kloak in Screech's Sprint (level `$2F`, object type 740 at world
  1632x2528, placement entry 32, `dkc2s0-20260903-phantom-enemy.sav`)
  hung idle in the brambles above the plank platform at camera 1483 and
  never threw. That was a recompilation defect, not the game: the sprite
  sub-state dispatcher `JSR ($CB46,X)` at `$B3:CB3D` indexes a 16-slot
  table whose slots 0-5 are the sub-state handlers (script runner, timer,
  player-range wait, two holds, player-side test), 6-7 are null, 8-13
  repeat 0-5 for objects whose script has set sub-state bit 3 (touch
  damage), and 14-15 are null. The auto-read table stopped at the first
  null slot, so every object with bit 3 set took the out-of-bounds arm
  and its behaviour script never stepped again. The Kloak's script sets
  bit 3 two commands before loading its projectile descriptor, so it
  froze there; with the table declared in full it throws a TNT barrel
  at the Kongs, floats off, and is released off-screen. Behaviour scripts
  live in bank `$FF` (direct page `$90` holds the bank, record offset
  `$50` the cursor); the interpreter is at `$B3:CB66` with opcodes
  `$F6`-`$FF` at `$B3:CB93`.
- Two "blank margin" observations are authored emptiness, not defects. In
  the level `$000F` sub-mode `$0009` Quick Save at camera `(813,469)`
  (maximum X 24,320), the decoded BG1 map is empty for world tiles 131-139
  and 143-148 beyond the viewport and equally empty for tiles 116-123 inside
  it; the east margin decodes to the transparent character with every cell
  present. At terminal-right Topsail `(768,3301)`, the 16:10 left margin
  covers world X=716-742, which is also empty inside the 16:9 native view.

## Level streamer geometry

The generated column and row streamers were read to assess whether the
cartridge could be made to stream the wide window itself instead of the host
reconstructing margins.

- `dma_level_columns` (`$B5:ADD8`) runs when camera X aligned to 8 pixels
  (`$17BA & $FFF8`) differs from the last streamed column latch `$17CA`. It
  uploads one 8-pixel column of 32 entries (64 bytes from `$185A`) with
  `VMAIN=$81`, to the `$17B6` map at the column for camera X plus `$0100`
  when moving right, or camera X when moving left (`$17D6` sign). The column
  builder at `$B5:AC9C` uses the same latch and direction, reads the source
  column at camera X (or camera X minus `$0100`), and adds the `$0098` map
  base, the `$1E0`-masked vertical page contribution, and the `$17B4`
  metatile table.
- `dma_level_rows` (`$B5:B00B`) runs when camera Y aligned to 8 differs from
  `$17CE`, direction from `$17D2`, and uploads two 32-entry halves (`$17DA`
  and `$181A`) to the row and to the same row plus `$400`, so a streamed row
  already spans both 32-column pages of the 64-column ring.
- The ring therefore holds authentic adjacent columns only where a row
  stream has recently written them; a column stream writes one 8-pixel
  column per camera step at the leading edge and nothing behind the trailing
  edge.

Arctic Abyss (K. Rool's Keep's underwater stage, level `$6C`, gameplay
sub-mode `$19`, maximum scroll 2560x1656) stores its level map 80
metatiles per row like a ship hold: the 160-byte row stride reproduces
all 896 native cells of its start at zero offset, the column-major
horizontal layout matches 36. Its name card, like every level-name card,
runs NMI sub-mode 11 inside the gameplay mode (`$24` = `$8819`) with no
terrain stream and no camera; the card pictures are bounded maps (a
32x64 map for Arctic Abyss, 64x32 maps with a wider painting on the
right for Barrel Bayou and Slime Climb).

Castle Crush (K. Rool's Keep's rising-floor tower, level `$62`, gameplay
sub-mode `$14`, camera 256..512 horizontally) stores its level map 16
metatiles per row: the 32-byte row stride reproduces all 837 native cells
of its start at the one-page offset (map column = world column minus 8
metatiles, map row = world row minus 8), and no other stride or offset
matches more than a quarter of them. The 512-pixel map spans exactly
world 256..767, so the widescreen margins never leave it: at either end
of the camera range the presentation bias moves the whole margin to the
open side.

Screech's Sprint (level `$2F`, gameplay sub-mode `$10`, the square
scroller at its 192-byte stride, maximum scroll 3072x2568) starts the
Kongs on a plank platform whose first plank is at world 608, the camera's
position there. Holding Left moves neither Kongs nor camera, `$0AFC`
reads the level-wide maximum, and there is no minimum-scroll word; the
map's ten columns west of 608 are empty for the whole visible height.
The hold is authored by collision and the void is never shown by the
console.

The K. Rool Duel arena (level `$61`, gameplay sub-mode `$17`, camera
256..512 horizontally with no vertical scroll) is the smallest level map
in the game: 16 metatiles by 8 at world 256..767, 256 bytes, with the
metatile definitions starting at byte 256 of bank `$7F`. It streams into
BG1's 64-column ring like a narrow vertical stage and the 32-byte stride
reproduces all 837 native cells of the fight's start at the one-page
offset. BG2 is a flat colour and BG3 the gas texture; both are bounded.

Toxic Tower (level `$6E`, gameplay sub-mode `$1C`, camera 256..512) is
laid out the same way: 16 metatiles per row, 32-byte stride, all 837
native cells of its start at the one-page offset. Its main screen holds
only BG3 (the acid's surface texture) with BG1, BG2, and the sprites on
the subscreen under colour math, and the acid body is BG1 rows the game
rewrites as the level rises.

The lava stages keep their foreground rocks and far lava spikes on one
static 64x64 map at `$6400`, uploaded at load and never streamed. HDMA
channel 3 (mode 1, `$2107`/`$2108`) swaps the maps at the lava line: above
it BG1 shows the terrain ring (`$7800`, at camera speed) and BG2 the spikes
from `$6400` at half speed; below it BG1 shows the rocks from `$6400` at
twice the camera speed and BG2 the terrain. The map's rows 40-63 (the
spikes) repeat every 32 columns; rows 16-31 (the rocks) have no period and
span the full 64 columns, so the 512-pixel hardware wrap is the authored
continuation. The ship hold's cabin wall (`$7800`, 64x32, rows 0-11) is a
12-column pattern that does not divide the map, and its second backdrop
(`$7000`, 64x32) leaves columns 55-63 blank in every row; neither wraps by
design, which is what `Dkc2VideoTilemapWrapsAuthored` tests. Across the 26
preserved states, no non-terrain 64-column map received a VRAM write during
160 frames of movement.

The lava geyser steam of NMI sub-mode 18 (`lava_geyser_nmi_sub_mode`,
`$80:C01A`; Red-Hot Ride) is a bounded 32x32 BG3 (map `$7400`) at camera
speed (BG3 HOFS = camera X - 1 plus the heat-haze HDMA wobble of up to two
pixels per band from the table at `$BB:9CAF`; BG3 VOFS = `$17C2`, the
8-bit camera Y - `$101`, written once per frame and untouched by HDMA):

- `handle_lava_geyser_positioning` (`$80:DDC1`) walks the stage's geyser
  list at `$B3:D65B` from the offset in `$0959` (entries: world X with bit
  0 set for a tall column; `$8000` ends the list) and registers the first
  geyser with X >= camera X - 10 and X < camera X + 266 in a free slot:
  `$0963,Y` takes the list offset and `$095B,Y` the map word offset,
  `((X - 8) >> 3) & $1F` plus row 14 (`$01C0`) for a short column or row 6
  with bit 15 (`$80C0`) for a tall one. A registered geyser is marked with
  bit 14 once X - camera X + 12 leaves 0..279, cleared by the next block
  upload, and its slot zeroed.
- `update_lava_hot_air_effect` (`$80:CAB5`) rebuilds the haze HOFS table
  at `$7E:8048` (camera X plus 0, 1, 2, 2, 1, 0, -1, -1), writes window 1
  from `$84`, sets `VMAIN=$81`, and for each of the four slots calls
  `$80:CB71`: a marked slot uploads the blank block; an active one uploads
  its block on frames where `$2A & 3` is zero, from the long-pointer table
  at `$80:D3AD` indexed by `$2A & $0C` (plus `$10` for tall), 20 or 36
  bytes per column, three column DMAs at consecutive map columns wrapping
  within the row (`(offset + 1) & $1F`). The eight blocks are column-major:
  `$80:D0A1`, `$D0DD`, `$D119`, `$D155` (short, three columns of ten) and
  `$D1FD`, `$D269`, `$D2D5`, `$D341` (tall, three of eighteen); the blank
  block is the zero padding at `$D191`. The ring shows the frame
  `($2A >> 2) & 3` at draw time on every traced frame.
- Because the map is 256 pixels wide and always fully visible, a block
  written for a geyser partly outside the view wraps onto the opposite
  edge on the console as well (a few pixels to two tiles, from the 12-pixel
  registration lead on each side); the cartridge tolerates this at the
  screen border, the host serves those columns from its store.

The ship-deck rigging on BG3 has its own streamer with the same shape and
even less lead:

- `handle_ship_deck_rigging_scroll` (`$80:E4EB`) keeps the rigging's
  target, camera X times 5/4, in `$17BC` and moves the rigging scroll `$B8`
  toward it by the frame's target delta clamped to -8..7 pixels, so after a
  fast run (Rambi) `$B8` trails the target and never catches up; `$B6` is
  that scroll reduced modulo the 1280-pixel map width (`$B7 % 5` through
  the hardware divider).
- `$B5:A950` builds one 8-pixel column of 36 entries into `$195A` when
  `$B8 & $FFF8` differs from the last column latch `$C6`, for the column at
  `$B6 + $FF` when moving right or `$B6` when moving left, from the map at
  `$F5:26A7 + (x & $0FE0) + ((y & $01E0) >> 4)` (y = camera Y - `$0100`)
  and the 32-byte metatile definitions at `$F5:2087 + (entry << 5)`; the
  entry's bits 14-15 select the mirrored row or column of the definition
  and are EOR'd into the tile. `dma_ship_deck_rigging_columns` (`$B5:AA88`)
  uploads 32 of those entries from `$189A` with `VMAIN=$81` to the `$7800`
  map column `($B8 >> 3) & $3F` and latches `$C6`.
- `$B5:AAE6` builds one row of 36 entries the same way when camera Y & `$F8`
  differs from the shared row latch `$17CE` (eight bits), then copies 33 of
  them into the 64-word ring-row buffer at `$18DA` at the ring position of
  the view;
  `dma_ship_deck_rigging_rows` (`$B5:AC25`) uploads the whole 64-word
  buffer to both pages of the row. The 31 words the copy did not touch are
  whatever the buffer held from the previous row upload.
- The PPU scroll, the column upload, and the `$C6` latch are applied in the
  NMI after the frame logic that advanced `$B8`, so at draw time the ring
  and the latches agree with the PPU phase, one frame behind WRAM.
- The rigging's PPU vertical scroll is eight bits, `(camera Y - $101) & $FF`,
  against a map 512 pixels tall; the ring is 32 rows, so the PPU does not
  care, but a host key must rebuild the map epoch from the camera
  (`Dkc2VideoRiggingShadowY`). Unwrapping it like the terrain's 10-bit
  value chose the wrong epoch once the camera passed Y 512 (the deck after
  the barrels), which dropped the streamer recognition and showed the raw
  ring again.
- Neither row DMA writes `VMAIN` (`$2115`); the column DMAs set `$81` and
  restore `$80`, and the only routine that sets `$00` (increment on the
  low byte) is `upload_mode_7_tilemap` in bank `$80`, which restores `$80`
  before returning. Which write is in effect when the rigging row DMA runs
  was not traced, but the ring shows the low-byte mode's result: the word
  DMA lands every high byte one word late: each ring cell keeps its own low byte and takes the
  previous source word's high byte, the first word of each 32-word page
  keeps the high byte VRAM already held, and the last high byte spills into
  the next row's first word. Measured on the second Pirate Panic state
  after Rambi's descent: row 34's rope tiles lost their priority bit and
  blank tiles changed flip bits, on the console as on the host. The host's
  decode verification accepts exactly that pattern and nothing else
  (`Dkc2VideoRiggingCellMatches`); the margins show the map's own flags.

The ring therefore never holds a correct column beyond the 33 the native
view shows: ahead of travel it is 512 pixels stale, and after a vertical
step every column outside the view carries the previous row's leftovers.
The host decodes the same map for its margins (see ARCHITECTURE.md, ship-deck
rigging decode) and verifies the decode against the native window each frame.

A wider stream would need the column builder and DMA to lead by six more
columns per side, the level-entry fill to seed them, and a trailing-edge
refill after direction changes. Every one of those columns would be decoded
from the same `$0098`/`$17B4` map that the host already decodes into the
world-keyed store, so the presented pixels could not differ from the current
margins; the change would only move the work into guest VRAM, change save
states and VRAM hashes, and bend the engine's rule that simulation stays
untouched. It was assessed and not implemented.

## Dedicated banana OAM coordinates

DKC2's collectible bananas do not use the common placed-object sprite
renderer. A separate list walker selects groups, clips their pieces, and emits
OAM directly. The native path uses horizontal limits `$0107`, `$0100`, and
`$010F`; these now gain the 43-pixel per-side widening only when the current
terrain source has passed the widescreen readiness gate.

The tile emitter has another left-only cutoff, `$000F`, independent of those
list/formation limits. It is a magnitude test for negative screen X, not the
nearby `$0167` vertical span. Leaving it native explained why the `bg-02`
route showed banana tiles only at X=-14..-1 on the left while the right margin
was complete. Ready widescreen terrain now changes this cutoff to `$003A`
(15 native pixels plus the 43-pixel margin). The same replay reaches X=-43;
4:3 and unready screens retain `$000F`.

Widening those limits exposed a second SNES-specific assumption. The banana
writer stores the low coordinate byte, then derives OAM's ninth X bit from the
16-bit coordinate sign through its existing `XBA; ASL` sequence. This encodes
negative off-left coordinates correctly, but X=`$0123` has bit 8 set and bit
15 clear, so it wraps to `$23`. At private `bg-02` frame 2,582, banana world X
`$08D0` minus camera X `$07B0` projected into the right margin while its OAM
tiles appeared at X=35. Mirroring bit 8 into bit 15 immediately before the two
banana OAM packing sites preserves the low byte and makes the original high-X
sequence emit the required ninth bit. The corrected replay records the same
tile pair at X=291 and shows it in the right margin.

## Widescreen margin source provenance

A 256-pixel rolling SNES tilemap can contain valid native-view cells and old
or opposite-page cells immediately outside that view. A visually non-empty
margin therefore does not prove that its VRAM is current. The world-shadow
lookup now counts the final source selected for every off-native fetch:

- exact world/history hit;
- exact periodic fold from the current native row;
- verified transparent-tile fallback; or
- raw wrapped VRAM fallback.

Only the final case directly exposes an unproven rolling VRAM cell. A blank
fallback is safe from stale art but can still produce a visible hole or later
pop when the real terrain becomes available. The route auditor separately
records the exact world-keyed terrain entry in the margin and when that same
cell reaches native view, so a wrong prefill/history value is detectable even
when no raw VRAM fallback occurred.

The 2026-08-13 attract audit sampled frames 3,200-5,250 every 12 frames across
composite, BG1, BG2, BG3, and OBJ. It observed **zero raw VRAM margin
fallbacks**. It did retain two verified-blank intervals (Mainbrace BG1 during
early scene fill and two Parrot BG2 samples) plus exact terrain-identity and
old-boundary seam candidates. Thus the current attract defects are not proven
raw stale-VRAM reads; they are more narrowly missing or disagreeing world
content/policy candidates and must be debugged from their retained frame/layer
evidence.

## Attract vertical-page correction

The retained attract trace exposed a renderer-side source-address defect that
was independent of raw VRAM. Near the PPU's 1024-pixel half-period, each
viewport row was unwrapped separately. Mainbrace could therefore map row 0 to
world tile 275 and row 1 backwards to tile 148; Parrot Chute Panic showed the
same failure during rapid vertical motion. The corrected path unwraps the top
row once and increments subsequent rows.

The decompressed source map is a separate 256-pixel phase. DKC2's bank-B5
column builders begin at camera Y minus `$0100`; applying that source-page
selection to all proven layouts changes the representative Mainbrace source
row from 275 to 179 and Parrot from 135 to 39. Exact decoded entries now
replace retained history for tiles touching either widened margin, while the
shadow runtime still gives a newer game write priority.

The coarse private Version 13 replay covers frames 0-5,874 every 12 frames in
composite, BG1, BG2, BG3, and OBJ. It produced two safe verified-transparent
observations. Its original zero-actionable conclusion was too strong because
the analyzer suppressed seams on screens with proven source ownership. Owner
motion footage subsequently exposed a transient Mainbrace split that this
rule hid. The one-frame follow-up showed WRAM camera X leading BG1 hScroll by
up to three pixels; terrain margin capture and prefill now use the rendered
PPU X phase. Final normal-speed owner validation remains open.

Fine horizontal scroll can sample the tile immediately beyond the nominal
342-pixel output interval. Terrain prefill consequently keeps an additional
8-pixel source guard at both margins. Pirate Panic frame 6,404 demonstrated
the failure mode: the nominal decoded span was complete, but two east-margin
pixels missed the shadow and became verified transparent during Rambi's fast
downward charge. The guard removes that small fallback without permitting
reads beyond the already validated level-source boundary, but it did not
explain the larger owner-visible Rambi artifact.

The exact later interval exposed a separate vertical epoch tie. At frame 6,509
camera Y was `$0204` and the fine PPU Y value was `$0004`. Unwrapping the fine
value directly selected world row zero, while source prefill first masked to
the 8-pixel tile origin and selected row 128. Frames 6,509, 6,511, and 6,512
therefore substituted 1,120 verified-blank margin samples apiece despite the
correct source tiles being decoded. Shadow Y now unwraps the common tile
origin (`ppuY & $03F8`) and restores `ppuY & 7` afterward. Exact replay removes
all three large blank bursts.

## The save record, Klubba's toll, and the Lost World

`update_save_buffer` at `$BB:C5E0` packs a 680-byte file: data byte 5
is the Kremkoin count (WRAM `$08CC`), bytes 6 and 7 the DK coin and
life counts, byte `$AD` the world number (1-based; the Lost World
variants a kiosk leads to are 10 to 14), byte `$AF` the map node, and
bytes `$B4..$E3` a mirror of WRAM `$08D2..$0901`, which holds the shop
and kiosk state. Banana Coins (`$08CA`) are not in the record: the map
loader at `$B4:800E` zeroes the three counters and fills back only the
two that were stored, so a loaded file always begins with none.

Klubba's kiosk is NPC 8 (`$0689`). `get_player_coin_count_npc` at
`$B4:A1C6` returns Kremkoins for him and Banana Coins for the other
shopkeepers, and the purchase path at `$B4:9C66` subtracts the price of
15. Paying records nothing in the per-shop bytes; the kiosk screen at
`$B4:91F4` tests bit `1 << world` of `$08FA` (record byte `$DC`), which
the transition at `$B4:9F53` sets when the Kongs go through, and offers
free passage when it is set. `$08F9` (record byte `$DB`) counts newly
cleared levels numbered 196 to 200, incremented at `$B4:B26B`; the map
init at `$B4:80CD` compares it with five, runs a one-off event, and sets
`$08FC |= $0C`, which is the Krocodile Kore opening.


## Optional Kong sprite presentation

The character adapter changes host OBJ rasterization, not SNES DMA payloads.
The original OAM can lag current actor WRAM by one update; matching only the
current animation header causes intermittent replacement failures. Match the
committed OAM layout and derive its origin before rendering. Temporary CGRAM
replacement is restored before each HDMA step, preserving the guest palette.
Replacement objects use native priority/window/color-math composition. Their
host raster bypasses their original compound sprite's sliver count; this is
an optional presentation policy, not a claim of SNES sprite-limit accuracy.

Mounted presentation must distinguish the rider at `$006C` from the leader at
`$0593`, whose original player object renders the animal. `$0D72/$0D74` are
the native rider base offset; `$0D76/$0D78` include animation bobbing. External
animation commands `$85/$86` pair animal and rider graphics; `$86` additionally
sets offsets. The presentation adapter reads that data and the live animal
graphic without executing the reference callbacks or modifying these fields.
Matching OAM ownership alone does not prove that a replacement rider pose is
correct; mounted motion and attachment need image-backed validation.

## Replacement Kong pause, hands and ground attacks

SNES Start pause sets WRAM $08C2 bit $0040 while console frames continue.
Replacement animation clocks must exclude those frames; host-frame count alone
is insufficient. Escape pause already stops simulation.

The US v1.0 attachment adapter runs at B3:9FE7 before native carried-object
positioning. Throw callbacks B9:D8AC/D967/DFD5 preserve the native stack and
object lifecycle while using donor timing and hand offsets. B9:D9E0 adjusts
forward velocity before native terrain correction. Actor dispatch B8:9616 and
physics continuation B8:995F support ground attacks without inserting invalid
actor states. Some reference-project address comments belong to a different
layout and must not be used as ROM addresses without verification.

The reference adds a clipping entry after the base ROM's final attack box;
that entry is not readable from the unpatched ROM. The host expresses its
geometry explicitly and reads enemy boxes from the actual ROM. Impacts use
native defeat bit 8 and the existing bounded sound queue. Hardware comparison
of the new host moves remains outside this milestone; these are enhancements.

The verified US v1.0 glide action starts at B8:C921. Its initial JSR updates
shared run speed; B8:C924 then starts flight-specific checks. Replacements
redirect that boundary to the native RTS at B8:C92D. An existing glide is
semantic 11/state 6, with control variables $16B2/$16D8 and gravity/terminal
speed at +8/+10. Normal values are restored from the actor's ROM constants
pointer $8E-$90. Animal and original-character paths are excluded.

Native swap animations 73/236 call B9:E164/B9:E1E4 at 44/26 elapsed frames.
Paired commands can change the outgoing graphic without changing that actor's
animation ID, so independent idle clocks cannot represent a tag handoff.
Replacement handoff art uses one pause-aware clock for both participants.

Team throws use native animation 38/201. Their base ROM streams begin with
ten-byte paired $8A commands, followed by eight-byte $8B carry commands.
B9:DCEA selects the inactive Kong's thrown animation; B9:D8BE assigns native
forward/upward velocity, enables collision and clears $D7A. They are separate
from the barrel release at B9:D967. Their callback cursor points to the next
opcode, and preparation must not be sought again once consumed. Host team
presentation retimes only the replacement carrier, preserving native throw
collision/terrain and excluding animal riders. No hardware parity is claimed
for this optional enhancement.

The reported Kiddy jitter snapshot had the follower in native state $21,
animation $CB (Dixie-slot semantic 40), held pointer zero and constant world
position. Native graphics $05B8/$05BC/$05C0 form a waiting cycle. Mapping
that state to Kiddy hurt graphics $3F68/$3F7C instead produced a repeating
six-tick upright/horizontal snap. The corrected private projection holds its
seated frame; it does not change the machine's waiting or recovery behavior.


## Windows menu input isolation (2026-09-13)

Native menus route settings/actions on the host thread and preserve the two
packed 12-bit joypad words. Keyboard polling no longer requires SDL video
initialization; transient keydowns survive until the next frame. Focus and menu
transitions release host input without fabricating guest responses. Synthetic
coverage exercises all twelve default Win32 buttons across SDL init/teardown,
controller routing, deadzones and menu-chord release. Physical controller
hot-plug and two-controller gameplay still require device acceptance.


## Co-op animal and turn-freeze observation (2026-09-13)

Animal ownership and turn freeze are guest game policy, not new emulated
hardware responses. Co-op input still traverses the two real SNES controller
ports. Optional sprite tracing now reports mounted-animal slot/type (`$6C/$6E`)
and the game time-freeze reason (`$0A36`). The death regression observes a live
enemy moving again after the turn-wait freeze is cleared; changing player
coordinates alone is insufficient evidence that simulation resumed.

The external Pirate Panic snapshot exercises Rambi with both controller words,
including a player on foot, saved mounted ownership and passing the animal.
This does not establish physical gamepad, other-animal or full-game coverage.


The co-op screen clamp now observes the confirmed host viewport width/bias to
admit the visible side areas. Its two generated operands change only for
simultaneous TEAM Kong updates; CPU arithmetic and SNES input hardware remain
unchanged. Trace fields `camera_x`, `camera_max`, `wide_extra`, `wide_bias` and
`wide_ready` make movement-to-viewport acceptance reproducible. Synthetic checks
cover unavailable terrain, native/classic/solo/contest modes and both outer
level bounds; private routes cover both players and all three aspect ratios.


## TEAM banana hitbox capacity

The dedicated list prepares at most two rectangles from the leader union at
`$09B3`, the animal/rider special box at `$09EB`, then the follower union at
`$09CB`. With all three valid, the follower is skipped. The mounted Pirate Panic
snapshot reproduced 30 bananas before and after the on-foot jump through the
trail; the adapted group walker reaches 38 using the same controller inputs.
After a controller-driven rider exchange, Player 2 on foot reaches 35. A rider
route reaches 43 both alone and after revisiting the first player's collected
trail. Counts are decoded from the shared BCD word `$096D`.

The adaptation temporarily supplies the third rectangle to the existing
second-slot pass and restores the second rectangle before repeating that pass.
No hitbox union spans the two players. Cleared banana-group bits prevent double
collection, and the normal guest counter/life mechanism owns awards. This is a
gameplay adaptation, with no changes to PPU or controller hardware. Other
animal types and transformations remain outside this route's acceptance.


## Reconstruct host capability

The same GLSL 1.20 source now runs in both the SDL and native WGL presenters.
Native GDI continues to use its fixed samplers. Compiled shader availability,
not the saved renderer preference, gates the menus. This is a presentation
change; color LUTs and all guest hardware remain unchanged.

The 21:9 source width is 446 (95 extra columns per side), the nearest symmetric
width below the shared runtime's nine-bit OAM limit. Host buffers now reserve
that maximum while native and existing wide widths retain their identities.
Co-op movement derives its limits from the current extra columns and level walls.

Rumble observes the existing accepted player_interaction_1B callback, then emits
host-only P1/P2 event bits. Solo maps to P1; classic/contest use the original
active controller; simultaneous TEAM uses the colliding Kong's controller.
Consuming/resetting events never writes WRAM or changes guest/save contents.


## MSU-1 and CRT host policies (2026-09-13)

US v1.0 $B5:8291 uploads ROM $EE:0088 to APU $0560. Scheduler $07A9 has
its music branch at $07AB (ROM offset $2E02D3). SFX ownership is APU
$01E0..$01E7. The host validates surrounding scheduler bytes against the
verified image before repairing policy or releasing existing music voices.
SPC command entry $B5:81FB receives X; primary song is WRAM $001C. Variant
restoration reads stable APU $E5/$E6 and the $1312 sequence table. These are
DKC2-specific verified addresses. Old shared echo is cleared on a mute change.

CRT is a host GL simulation after color conversion. GDI retains saved CRT
preferences but disables unavailable shader controls. Tube mode performs its
own scaling; Flat panel retains Nearest/Bilinear/Reconstruct.

## TEAM rope contact (2026-09-13)

US v1.0 terrain routine `CODE_B8B714` is at `$B8:B627`. It queues rope
reactions through `set_player_interaction_global`, whose accepted reaction
stores current sprite `$64` in `$0A84`. Reaction `$12` at `$B8:865F` enters
single vertical rope state `$35`; `$11` at `$B8:86B4` handles the other rope
contact branch. Both normally call `work_on_active_kong`; simultaneous TEAM
selects the contact source's work pair. The reported Topsail Trouble scene
uses normal gameplay submode `$08`, and its actual pause bit is `$0040` in
`$08C2`. The private replay resumes with Start instead of modifying WRAM.
The rope callbacks at `$B9:DAB9`, `$B9:DAE2`, `$B9:DB1B` and `$B9:DB47`
contain follower early-outs. The single-rope semantic animations are `$2F`
hang, `$30` Up and `$31` Down; the Dixie slot adds `$A3`. The sprite's `$36`
word selects the animation, while `$18` is the displayed graphic. The replay
checks changing graphic IDs, so coordinate movement alone cannot pass.

State `$36` at `$B8:A664` runs animation without an action gate, waiting for
the single/double-rope transition to finish. Its speed callback `$B9:DD63`
and completion callbacks `$B9:DD7E` (enter `$37`, double rope) and `$B9:DD90`
(return to `$35`, single rope) also skip the follower. Double-rope idle uses
`$B9:DD9E`; climbing direction/idle completion uses `$B9:DE19`. Semantic
animations `$34/$35` are the transitions, `$36` hangs, `$37` climbs Up and
`$38` climbs Down. These are animation IDs, distinct from actor state IDs.
The second reported save had Dixie in state `$36`, animation `$D7`, timer
zero, callback `$DD63` and script cursor `$4504` pointing at `$83/$D12B`.
The preceding `$81/$DD7E` had been skipped. Replaying this validated completion
recovers to double-rope state `$37` at the same `(947,2480)` coordinates.
Single-rope turn animation `$32` (Dixie `$D5`) calls `$B9:E015` to toggle
facing bit `$4000` in sprite `$12`. Skipping that callback makes a controlled
follower turn repeatedly without passing the next lateral action gate.
The host enables this shared callback only in rope states. Horizontal-rope
state `$38` uses semantic animations `$39/$3A` and callbacks `$B9:DDB9`,
`$B9:DDCB` and `$B9:DDEA` for moving, idle and animation speed.

## TEAM carrying (2026-09-13)

The US v1.0 team-up action at $B8:C750 checks SNES A ($0080 in $0983)
and normally accepts only follower states $22/$2A. Independent state $00
therefore fails the original gate. The host substitutes only this operand
after checking separation <=24 X / <=16 Y pixels. $0593/$0595 and
$0597/$0599 identify carrier/passenger actor and work pairs; controller ports
remain fixed to $0DE2/$0E40. $0D7A records the carried actor. Reaction $04
uses carrier state $13 during pickup, then $17/$18 while carrying. Drop uses
reaction $06; a missed throw enters $1F flight then $21 waiting. Grounded
$21 resumes independent control in simultaneous TEAM without changing the
airborne trajectory or reintroducing the follower history teleport.

## TEAM Select catch-up (2026-10-18)

The Select catch-up assist samples the actual runtime controller ports (Select
bit `$0004` in each packed 12-bit port word) once per console frame, including
lag frames that skip guest polling. The action wrappers mask the corresponding
guest Select bit `$2000` only for simultaneous TEAM. 181 supported NTSC frames are at least three
seconds; the guest pause bit `$0040` in `$08C2` cancels the charge. The assist
requires both-present `$4000`, ordinary live Kong states and a grounded target
(`sprite+$1E & $0101`). It translates world position, clears position fractions
and velocity (`+$20/$24/$26`) and stale contact flags, leaving state/animation
and hardware controller registers intact. A barrel owned by the moved player
receives the same displacement. No ROM bytes or hardware success responses
are replaced. This is a host co-op assist, not original cartridge behavior.

Rope rescue also accepts the requesting live Kong in states `$35-$38`, while
the target must still be on foot and grounded. Detachment resets `$2E/$32` to
idle and initializes animation `$01` (Diddy) or `$A4` (Dixie). `$B9:D0C6` uses
four-byte descriptors at `$F9:0000 + animation*4`; actor `$3C/$40` receive the
script pointer/descriptor, `$38/$3E` clear and `$3A` becomes `$0100`. Old rope
callbacks must not survive the move. Normal dispatch subsequently resolves
terrain, gravity and graphics. No ROM animation/script data is stored in Git.
The reported save also contained lost-mask bit 1 for a standing, live Player 1.
State dispatch clears a stale bit only with live type/collision, both-present
and standing/rope evidence; absent or waiting Kongs remain lost.

Swimming state `$3D` is established by `$B8:B5F3` with semantic animation `$43`
(Dixie `$E6`). Rescue accepts a live swimmer as either endpoint; ground contact
is required only for dry destinations. Crossing water restarts the appropriate
movement/animation context. Entering water also reads the requester's own
`$FF:(actor+$54)+$76/$78` properties into its `$0595/$0599` work block at
`+$08/+$0A`, matching the gravity/terminal-speed fields initialized at `$B8:B5DA`.
Work pointers must be one of `$16B2/$16D8`. The original global entry can change
the follower, so the assist initializes only the moving actor. The swim
animation callback `CODE_B9DB6C`, entry `$B9:DB6E`, has a `$0597` early-out before
speed/facing updates. Its operand now admits independent swimmers, letting the
native code update facing bit `$4000` in actor `$12` from that actor's motion.

The goal main sprite is type `$0160`, idle state 1. Its routine at `$B3:A275`
originally gates only `$0593` on `+$1E & $0101 == 0` and nonnegative `+$24`,
then calls the active-only collider. Simultaneous unmounted TEAM instead
admits either eligible Kong and uses the existing inactive-then-active native
collision pass. The `check_for_collision` flags operand at `$BC:FD61` filters
each Kong's eligibility only for this goal context. Accepted collider `$006A`
becomes leader before the native success block at `$B3:A29B`; selector pairs
and collision records `$09AF..$09C6` / `$09C7..$09DE` swap together. The original
carry result decides success. No synthetic collision or completion is issued.

The reported mounted save keeps P2 in `$13` with collision `$1E`, both-present
`$4000` and no saved lost bit, far below P1 riding Rambi. A rescue request may
reset this live waiting actor's state/action/animation to normal. A validated
grounded Rambi rider in state `$09` may be the destination: `$006E == $019C`
and `$006C` must identify an aligned animal sprite with that type, while the
target matches `$0593`. Only the requester moves; `$006C/$006E`, rider and
animal records remain authoritative. Rider requests, airborne riders, other
mounts, dead/lost actors and invalid animal pointers do not qualify.

## TEAM item input isolation (2026-10-18)

`$0D7A` is a shared held-sprite pointer, not a per-controller inventory.
The original jump and fall selectors consult it before choosing carrying
states. In simultaneous TEAM, an empty-handed partner could therefore jump
into carry/throw states and release the owner's item. Actor-local jump,
fall, slope, animal/item, honey-animation and jump/landing animation callback
reads now filter the pointer
through the co-op owner policy, as idle recovery already did. Global item
attachment and pickup exclusion retain the actual pointer. Ground/air carry
states `$0C`-`$0F` reconstruct ownership after a guest snapshot restore.
No controller-register or hardware behavior changes.


## Simultaneous TEAM camera/cannon observations (2026-10-18)

The native camera reads Kong X/Y integer words at sprite +6/+A and overlapping
fraction words at +5/+9. Its target integer words are $0ABF/$0AC3 (not the
low words at $0ABE/$0AC2); camera scroll words are $17BA/$17C0. Substituting
only the integer reads is insufficient for fractional smoothing. A separated
water snapshot settles with Kongs at (758,1905)/(838,1665), target (798,1785).
Room clamps and native framing remain effective after midpoint substitution.

Transport cannon type $013C occupies slot $0FB8 in the reported save. The
native follower contact schedules reaction $0F and callback $B3:EFD9;
reaction is reset before the callback and later collision scans overwrite
$6A. Its original follower choreography caused repeated $2F/$31 recruitment
while the other Kong entered state $30. Skipping follower recruitment alone
leaves entry flying toward the other Kong; CODE_B8A592's target read must
instead use the cannon slot recorded in $0A84. The accepted follower callback
also must transfer active/work/cache ownership before native entry selects
its active Kong. Both players' entry/firing, partner movement/jump, occupied
snapshot reload, and an old-loop snapshot recovering are verified with real
input only. Full-game cannon variants and concurrent occupants are unverified.


## Bluetooth controller enumeration (2026-10-18)

Read-only Windows HID inspection found two present game controllers. The
pinned SDL2 runtime enumerated Xbox One S Controller (GUID
030044f05e040000e002000000007200) as mapped, and Bluetooth Wireless Controller
(03009761c82d00000161000000000000, VID 2DC8/PID 6101) as unmapped. The latter
was therefore excluded by the launcher, while native gameplay's separate
XInput-only path could miss Bluetooth devices entirely. Registering the
upstream SN30 Pro Windows mapping makes SDL recognize both physical devices;
the production backend's --list test mode confirms their names and GUIDs.

SDL virtual-controller tests exercise both ports, reversed saved choices,
buttons, full negative stick saturation, trigger scaling, disconnect isolation,
reconnection, and initialization after SDL_Quit. Desktop hidden smoke covers
the native window/SDL subsystem lifecycle. These are not physical button or
rumble acceptance; actual Bluetooth interaction, latency, identical-GUID pads,
other device models and controller-mode switches remain unverified.


## Bonus teleporter and fixed tail-hook evidence (2026-10-19)

Follow-up: sequential departures expose a second cooldown issue. The first
release's timer can expire while the other actor remains in $3C; the last
release then collides with state-0 hook again next frame ($30 then $3C).
Re-arm native hook state 1 at the accepted jump block $B8:C621, before its
$0D82 clear. Single-press replays cover both departure and grab orders.

Gangplank's elevated barrel is type $0140 at (3120,444), parameter $0040.
The thrown Kong's accepted entry uses the same CODE_B3F069 reaction as cannons,
but $0A84 names the thrown Kong. Skipping native recruitment leaves the carrier
in $30 and passenger in $2B indefinitely. Restricting the shortcut to actual
cannon source types restores native $2F/$30 entry. The passenger later waits
below the launcher while the carrier lands on the platform at y=401; complete
that bounded arrival at the grounded actor's coordinate and restart its own
movement animation. A 16-pixel left offset falls off this platform's edge.

Topsail Trouble's reported save starts in level $0C beside the bonus teleporter.
The native bonus intro switches level $00D3 to $79; the old $0529 trace value
still says $08 and cannot identify this room transition. Rich co-op traces now
include level_id ($00D3) and bonus_mode ($0515). The room spawns at (464,647)
with an entrance barrel type $0140 there. The leader launches onto (464,589),
while the independently controlled follower previously fell from (432,647)
into live wait $13 at (432,682). Its native flags remain $1E and both-present
stays set. Bounded arrival recovery waits for the grounded leader and restores
the partner on that platform. It does not infer a DK-barrel revival or clear
lost/death evidence.

The second reported save is Gangplank Galley, level $04, with fixed hook
$014C at (2064,464). Native attachment is Kong state $3C at (2064,487).
Accepted follower entry formerly recruited state $71 and attached the old
leader. The busy leader gate also rejected a second grab while the first
Kong hung. Co-op fixes preserve native collision/attachment but associate it
with the touching Kong and allow both fixed slots at that anchor. The native
shared $0D82 pointer alone cannot describe two occupants: each release clears
it. Co-op shared occupancy therefore uses live state $3C and exact anchor
coordinates, without adding a hardware register or host save field.

## Pit leader loss evidence (2026-10-19)

The reported Gangplank save has P2 active at (5118,593), P1 standing at
(5083,593), both live. P2 walking right reaches pit state $3B at frame 39
(y=743), sets $08C2=$6000 and eventually exits the level despite safe P1.
Follower P1 falling instead parks in $13, but previously retained live flags
and no saved lost evidence. Both paths need per-player loss ownership.

Accepted terrain-pit block $B8:B481 normally queues reaction $23; below-screen
block $B8:B499 selects inactive parking versus global reaction $29. With a
verified live teammate, redirect either accepted block to $B8:B4CA after
promoting the survivor and marking only the fallen slot lost. Retain the
native cutoff of camera Y+$120 and reject a candidate in pit terrain ($1E bit
$80), a loss/scripted state, or the saved lost mask. Private input-only checks
cover both native leaders, both fallers, survivor jump/control and unchanged
position, held input after loss, save/load, last-player death and both falling.
Animal-mounted pit handoff, bonus failures and full-game terrain variants have
not been verified.

## Rattly high jump and hanger evidence (2026-10-19)

Reported level $0B save: P1 active state $09 at (940,3622), Rattly slot $0E9E
type $0194, P2 idle at (840,3608). Hook $0F5A is type $014C at (880,3472),
parameter $1000. CODE_B39986 rejected all hook contact while $006E was nonzero;
the active rider's state flags also rejected follower entry. Preserve the
rider and route the accepted on-foot contact directly through native $9914
attachment with follower work. Expected hanging position is (880,3495), state
$3C. Native approach into state $30 alone loops because repeated follower
contact restarts its approach rather than attaching it.

Rattly superjump: hold P1 A for 100 frames then release. Camera rises from
3466 to 3237; P2 remains grounded at y=3608, exceeding camera+$120 despite
standing on real terrain. Native follower parking changed it to $13 with live
flags $1E. Using level maximum camera Y ($0AFE=$0F08 here) for the independent
partner's cutoff preserves its movement while native pit terrain remains
authoritative. The rider's stale saved lost bit also blocked all Select rescue
until a verified live mount reconciled it. Native Rattly bobbing/jumps require
rescue to permit an airborne destination; P2 moves, leaving rider and animal
selectors intact. Replays exercise the old parked snapshot as well.

Regression check: the older Rambi snapshot's live waiting partner is below
the native level bottom. Treating Select as movement woke it into pit loss
before the three-second charge could complete. Mask Select from waiting-state
control decisions as well as native action inputs, preserving the explicit
live-wait rescue path and real pit loss evidence.


## Shared transport/Squawks and Rattly pit observations (2026-10-20)

The copied shared-bird save (SHA-256
304a5a52acfdc64c85f1505e26a668f5cfeb9694c0bbef9d186702311a61d727)
has P2 active, Squawks type $0198 at $0EFC, state $10, and live P1 below it.
Native snap_follower_kong_to_animal ($B3:8CAB) reads the shared-present flag
and puts the partner at the animal position; state $22 supplies the hanging
animation. Restoring this path keeps both attached while steering. Repeated
B presses (one frame pressed, six released) ascend; holding B alone only
produces one flap. At replay frame 656 the native hit changes P2 to $59 and
P1 to the TEAM handoff prompt $6F with mount type $0198 still present. Resume
Squawks $10 instead of ordinary jump $06. P1 then steers and ascends using
only controller 1; controller 2 cannot steer or revive its lost Kong.

The reported transport $013C recruits partner states $2F/$31 or $32, then
follows the leader's $11/$12 flight as state $22. Preserve shared bit $4000
and the native follower state through flight. Either entrant reaches bonus
level $79; after the native introduction both arrive at Y=589 and can jump
independently. Older looping snapshots recover through the same arrival path.
Types $0174/$0178 share the source guard but lack a supplied input replay.

Rattly loss checkpoint SHA-256
104388bbbf579cdbdb8e0cc4c77c79cd8fe2c64172e08a2a331a4917b1c3f297:
P1 at (908,3219), P2 at (948,3239), animal $0194 at $0E9E. Walking P1 left
into the pit formerly left $006C/$006E assigned to the fallen rider while
P2 became leader. Native dismount ($B8:94BB/$B8:954E) exchanges palette-index
bits $0E00, restores animal idle/order and clears the mount context. Applying
that cleanup at accepted rider pit loss prevents inherited mounting and
palette corruption. The unmounted animal is removed by its native pit path;
P2 keeps the original colors and jumps on foot after reload. Older corrupted
snapshots recover too. No CGRAM colors, input-driven guest memory, or saves
were patched for acceptance. Rambi's corresponding cleanup is synthetic only.


## Manual barrel sharing (2026-10-21)

Copied checkpoint SHA-256
ab95401f5f3f5df057b633ad7546fd86a398d656edce988c35f3ad5ea5754379:
level $24, active P2 at (545,6449), P1 at (574,6449), manual barrel type
$0140 in $0F5A at (472,6336). The earlier transport guard omitted $0140,
so the native follower flag was masked and only the touching player entered.
Retaining that flag brings both into native states $31/$11 at the barrel.

The barrel's native action reads the active Kong's newly pressed B bit $8000
through the existing pressed-word helper. Merging only the other live port's
B edge in state $11 permits either player to shoot without sharing steering
or on-foot actions. Native shot states are follower $22 and leader $12.
Both players land on the upper-left platform at Y=6257 and jump separately.
All four entrant/shooter pairs pass, including inside and landing reloads.
Rotation timing differs between approaches; the P2 route waits 60 frames
before firing toward that platform. No private checkpoint bytes are in Git.


## Barrel passenger air-control timing (2026-10-21)

Copied checkpoint SHA-256
f6d98587741aa6a5194cdd3fd8f15bf508ce6ec14992ae5738a9824308b6ce3e:
level $24, both Kongs inside type $0140 at (962,5264), P2 active $11 and P1
passenger $31. Native shot becomes active $12 / passenger $22. At frame 20,
active +$42 is zero and +$44 is four; at frame 24 both are zero. The $12
handler ($B8:9CD2) waits for these timers before steering/action processing.
The old passenger continued native follow $22 until about frame 37, so its
own horizontal input was ignored while P2 could already steer.

At the unlocked passenger dispatch, horizontal input now resumes native fall
$01 with the occupant's current velocities (+$20/$24/$26), its own unchanged
position, no ground-contact flag and normal collision/action mask. The initial
25 trace frames still match neutral paired flight exactly. By frame 35, P1
left/right trajectories separate by over 15 pixels while P2's trajectory is
unchanged. P2 retains its own left/right response; opposing controllers
separate and detached flight persists across save/load. Native terrain,
launch lock, shot direction, palette ownership and eventual landing remain
under guest routines. No acceptance checkpoint memory was patched.


## Equal shared-barrel force and landing recovery (2026-10-21)

Current checkpoint copy SHA-256
4ba7dbe4545bc39bd240fbf635c1e5517e8ebe8835f9ab6562b0b32fbb2f7c2c:
both Kongs alive below manual barrel $0140 at (368,6012) in level $24.
The earlier +$20/$24/$26 copy followed by state $01 preserved a velocity
sample but changed the passenger's subsequent gravity. On an upward-right
shot the passenger was about 75 pixels lower by frame 45 and retained its
old animation $40. This was a limitation of the previous air-control fix.

Keep both Kongs in $12, copying the complete position/lock/shot context on the
passenger's first shot dispatch. Both reach the same upward peak; P2-led
traces have equal Y frame-for-frame, while P1-led ordering is at most one
native update apart initially and has the same apex. Both can steer right
far enough to reach the next barrel. The existing recovery follower compare
at $B9:D707 must also allow independent $12 to take the active idle transition;
otherwise a neutral passenger lands but remains in $12. No new generated
adapter is needed because this operand is already wrapped.

The copied save and a native team-up/put-down leadership swap pass all four
leader/shooter combinations, equal apex, upward steering and in-flight reload.
An up-left launch followed by right input lands both at Y=6129. After reload,
either player walks left with advancing graphics and normal walk animations;
barrel/spin poses $1C/$BC/$40/$E3 do not persist. Source checkpoint bytes and
palette ownership remain unchanged. Full-game shot variants remain unverified.


## Kleever intro partner sinking (2026-10-21)

Preserved selected-level map checkpoint SHA-256
f13ae0c9d9a0f9b8acbc41e8486ef426564820521efb17de11737dc17d64558b.
Entering level $21 reaches Kleever's Kiln. In the neutral input replay,
the leader is scripted $75 at frame 95; the partner waits $13, arrives through
native DK-barrel rescue $3E and becomes $22 at frame 206. Independent dispatch
previously changed that to ordinary movement at frame 207. The partner stood
with normal vertical velocity $0300, then scripted $75 at frames 277-317
applied that velocity without terrain correction. At frame 318 it was at
Y=777 while the leader remained on the floor at Y=657.

A classic-mode comparison keeps native $22 at frames 206-227; its subsequent
scripted walk retains zero vertical motion and both remain at Y=657. Retain
$13/$22 while the live leader is in $74/$75 to preserve this native path.
Kong $75 calls interpolate_x_velocity, apply_position_from_velocity and
process_anim_preserve_state; it does not run normal terrain/gravity handling.
The intro ends around frame 408. Independent controls then resume with live
players at (996,657) and (950,657). DK-barrel arrival legitimately arcs up to
Y=606 earlier; the acceptance requires staying above the lava floor rather
than artificially fixing every frame's Y.

Neutral, held-partner and held-both input replays keep identical intro paths.
A frame-300 checkpoint resumes safely through the intro; either controller
can independently jump afterward. Native combat damage still occurs after
the intro if players remain idle; no combat immunity was added. Full boss
fight and other cinematic variants remain unverified.


## Kleever shared dropping-hook observations (2026-10-21)

Preserved checkpoint SHA-256
1bf52eafd8e88fdca7bc5303fc3b3b47c8ff552004d4cc5b89c1027adf0d8a7e:
P1 (960,657), P2 (955,657), level $21. The first dropping hook is actor $0024
at $10D2, animation $02AB, parameter $E000. It settles at (1008,536) and hangs
a Kong at (1008,559). The earlier $014C-only policy missed this actor. P2's
first grab used native follower recruitment, putting P1 on the hook while P2
returned to follower/fall states. Extending ownership also requires targeting
the real generic hook in CODE_B8A592; otherwise its entry loops in $30.

The occupied boss handler is CODE_B6E21D, actual $B6:E2BC; it calls
CODE_B398A7 for camera setup and framing, then continues at $B6:E2CD. Its free
handler normally calls CODE_B39982 for collision. Reuse that native collision
call after occupied camera setup, with hanging actors excluded, to admit a
second grab while preserving the original animation/lifetime update. Shared
checkpoints retain camera flags $0AB8 & $6000 == $6000 and framing $0AF2=$D180.
This is separate from fixed hook_main's occupied dispatch. The boss release
loads +$42=$001E and advances its cooldown state; retain that native delay.

Both orders now reach shared $3C at the exact anchor using unmodified inputs
and save bytes. Long release-order tests use the user's authorized temporary
invincibility: native work selectors $0595/$0599 identify the two work blocks,
whose +$16 counters are set to $7FFF with native actor mask $9E on copied
shared states only. No positions, velocities, grab states or original saves
are altered by the cheat. Both can release with one press while the partner
stays attached, and the last release works after the first cooldown expires.
The original production game retains normal damage and lava hazards.


## Native barrel rotation input (2026-10-21)

The supplied level-$28 manual barrel is actor $0EFC, type $140, parameter $0400.
After both Kongs arrive, active P2 is state $11 and P1 state $31. The normal
per-player input update at $B8:B8F3 feeds held directions to native rotation;
fixed-port input previously excluded the waiting player's aim. Merging only
directions at that boundary makes either controller advance barrel+$48 by the
same native amount: 12 right-held frames yield $2400, left yields $DC00 from
zero. Opposed inputs yield zero, matching inputs yield $2400 rather than double
speed. Reloaded aimed snapshots fire identical paired trajectories from either
port. Original save and ROM remain external. Physical controller acceptance
and other manually controlled barrel variants remain unverified.


## Shared enemy bounce evidence (2026-10-21)

The supplied level-$28 save contains P1 at (1563,690), P2 at (1579,686), and
enemy type $0220 at (1667,672). Sixteen right-held frames followed by a joint
20-frame right-jump and neutral descent reach an accepted P2 stomp at frame 56:
P1 (1642,653), P2 (1650,659). Previously P1 stayed in jump $06 and subsequently
fell; now both enter $16 and rise using the accepted native vertical impulse.
An eight-frame P1 approach lead produces the reverse first-stomper order.
Continuing right after either joint contact lands both on the next platform
at Y=645. A slightly staggered descent is accepted within the default window;
single-player jump routes leave the grounded/distant teammate unboosted.

Native jumping gravity is player-work+$08 and terminal speed work+$0A. The
shared assist uses these native outputs after on-foot animation initialization
at $B8:87F4. It does not copy animation cursors, horizontal velocity, position,
collision flags or enemy fields. The original stomp retains its native enemy
semantics. Config thresholds approximate simultaneous contact spatially; no
new real-time clock, rollback window or extra hit is required. Physical paired
controller play and exhaustive enemy/level acceptance remain unverified.


## Enemy knockback and actor-pole evidence (2026-10-21)

The level-$28 push snapshot accepts $1D from enemy slot $0EFC with collider P1
$0DE2 while P2 is the active Kong. Without buttons, frame 44 enters AI state $40
and frame 59 enters passenger/carrier states $18/$17. Native active push $1E
uses CODE_B88936 (actual $B8:88EA) and state $15. Reusing that native routine
with the accepted collider's work fixes the involuntary pickup. The unchanged
reaction priority still decides competing same-frame contacts. Native recoil,
recovery, saved recoil and both intentional pickup ports pass.

The later level-$29 snapshot's pole is actor $2D8 at X=1704. Old P2 entry
alternates $45/$01 while sinking because the other Kong clears $0BA0; P1 entry
also needs its own reaction work. The native contact window is 4 pixels when
absolute horizontal velocity is at least $40, otherwise 12, with strict upper
and inclusive lower Y clipping boundaries. Read the runtime clipping table
through its native bank/pointer rather than copying game geometry into source.

CODE_B8B4F7 at $B8:B40A uses the active Kong for its clamp even when another
Kong is climbing. Substitute the current state-$45 Kong there. In the supplied
scene, the native upper limit settles at Y=584; independent controls no longer
warp the other player. Regrab suppression is a separate requirement: native
$0BA0 retains a jumped-from pole until contact ends. Keeping that association
per Kong in action-local +$4C prevents immediate re-entry, including a snapshot
at the first airborne frame while still aligned to the pole.

Both entry orders and simultaneous entry pass stable shared hanging, separate
up/down motion and clamps, either first release and the last single-press
release. Private different-pole and exhaustive disappearing/moving-pole variants
and physical controller play remain unverified.

Pole animation follow-up: state $45 previously remained subject to the native
inactive-Kong animation skip in the existing $B9 animation callbacks. Position
updates alone did not expose the frozen sprite. Allowing that state through
Dkc2CoopRopeAnimationFollowerValue restores native up/down frame progression
and eventual idle after deceleration, without changing the animation scripts.

Pole camera follow-up (2026-10-22): camera target operands in CODE_B5E560
already support a fixed-point midpoint, but the eligibility whitelist omitted
actor-pole state $45. In the supplied route, Kongs at Y=584/696 produced
a single-player target Y=696. State $45 now uses their midpoint Y=640.
No camera registers or native level bounds are overridden.

Enemy-bounce camera follow-up (2026-10-22): native stomp reaction $1B enters
Kong state $16. That state was absent from camera eligibility, causing the
original active-player coordinate to be selected for the entire bounce. The
reported copied scene reproduces a shared bounce at Y=626/612 with target
Y=612; the corrected target is midpoint 619. Add $16 only to camera eligibility.

Pole pit survival (2026-10-22): the native pole action is Kong state $45.
Dkc2CoopPitSurvivor previously accepted terrain ropes and tail hangers but
excluded that state. In the supplied scene, a pole jumper fell at frame 62
while the other Kong stayed alive at X=3024/Y=567; native death then reset
the level at frame 274. Including the live pole action in survivor eligibility
uses the existing lost-Kong parking path and preserves the survivor's anchor.

DK-barrel respawn placement (2026-10-22): state $3E decrements +$42 and,
on expiry, copies +$48/+$4A to the Kong position before selecting state $22.
Native follower tracking normally absorbs its remaining entrance X velocity.
Independent control bypasses that tracking: in the supplied route the returned
Kong drifted from X=3670 to X=3614 and fell. On expiry only, refresh the finish
point/fractions from the living teammate and clear +$20/+$24/+$26. Preserve
native animation, revival, invincibility and scripted entrance timing.

Barrel gravity follow-up (2026-10-22): in the copied scene the inactive Kong
was grounded but work+$0A still held follower terminal speed $0100 versus
its native property value $0800. The existing shared-shot context omitted
work+$08/+$0A. Copy both on conversion to $12. Unlocked $12 uses native
property +$4A gravity ($50 in this scene), distinct from normal gravity
at property +$00 ($70); never force normal gravity during shot flight.
Grounded old-state repair restores only terminal speed from property +$02.
Native jump initiation may already own work+$08 while state remains $00.

Mounted bonus return (2026-10-22): the supplied Rambi scene times out from
level $8B to $29. Native restoration correctly recreates Rambi, its rider in
$09, and a dormant zero-mask follower in $22. Releasing that follower before
landing sends it into another barrel; skipping the mounted follower-cannon
setup then strands the rider in $30 with automatic motion. Classic-mode
comparison follows native $11->$12->$09 and lands at X=5192/Y=645. Preserve
that native arrival sequence until the rider is grounded; release the dormant
partner on that proven coordinate. Native idle follower state $2A also needs
this release, or the partner can remain inert after landing.

Animal-only sections (2026-10-22): the reported entry transforms to Rattly
with $6E=$194 and $6C=0; ordinary mounted animals instead have an actor pointer
in $6C. The initial transformation room uses $0515=2 but the following section
uses zero, so that room-mode word cannot scope the rule. Use the native animal
type, null actor selector and $08C2 bit $0200. A dormant state-$13 Kong must
not join on input in this mode.

A real surviving hit reaches player_interaction_23 at $B8:8DCB, after native
health processing and before damage invincibility. Switch $08A2/$08A4 there
once per accepted source/owner candidate. This avoids duplicate switches if
a generated deadline resumes the block. The copied Rattly route consumes
$08C2 health bit $4000, and a later DK barrel restores it without requiring
Kong revival. Controller changes must not rewrite either health representation.

Water/carry ownership (2026-10-22): CODE_B8B5C3 calls the generic team/item
drop routines when a Kong enters water. Their unqualified $0D7A read can
release the other Kong's carried item and leave that carrier in a non-carry
action. Wrap the operand in drop_sprite_if_holding ($B8:8E44) and
drop_follower_kong_if_teamed_up with the existing actor-local held-object
view. It applies to any valid grabbable, without an item-type special case.
The current carrier's water entry still sees the object and drops normally.

### Thorn checkpoint evidence (2026-10-22)
Copied external save SHA-256:
`e2766c8174c29cc487b21fcf56b074554edc617e4012ae25030798e8a50f4097`.
In level $2D, either player walking left reaches harmful terrain. Previously
P1 frame 81 / P2 frame 71 immediately entered parked $13. The native terrain
reaction instead uses $24 then the moving $25 death arc, with the survivor
passing through $26/$27/$6F. The inactive touching actor must be promoted
before interaction $23; otherwise the active, safe Kong takes the damage.
Both actors, mid-hurt snapshot reload, survivor jumping and final-player loss
pass input-only replay. These checks use the supported external ROM; no game
data is tracked. Physical controller play and other thorn layouts are unverified.

### 2026-10-25 — Barrel-to-enemy bounce checkpoint
External copied save SHA-256:
`32e5ca9d99d78e466a550309d9d317a8ce9b81532c6bf7131c7b9cfb8b016828`.
Level $2D starts with P2 in barrel $11 and P1 in shared $31. After a delayed
shot and paired right steering, both descend in native $12 within two pixels
of each other. Before correction, contact with enemy $220 gave only one $16
bounce/feedback bit because shared eligibility excluded $12. Delays 8 and 62
exercise opposite native stomp owners; either firing port now produces both
$16 states and feedback mask 3, with equal lift ten frames after contact.
Input-only checks also verify separate steering and exact player/camera/event
sequences after pre-impact and mid-bounce reload. Other barrel/enemy layouts
and physical paired controllers remain unverified.

### 2026-10-25 — Kudgel ground-shake checkpoint
Copied external save SHA-256:
`26b59a774b3c396578355cd89e79f4faf10f4743efcb60ca51ea2dd1e02e7ded`.
The level-$63 checkpoint begins in the boss intro. Moving right, waiting for
Kudgel to commit, then moving left avoids direct body damage and isolates
his first landing shake at frame 387. Native grounded eligibility checks
+$1E bit 1 and rejects +$30 bit $80; accepted reaction $0B enters stun $63.
The original issuer considers only $0593. Both co-op actors need their own
native initialization and follower recovery. The stun state's own $091F
decrement at $B8:AFAA must run once per native clock phase, not once per
stunned Kong. Other boss/ground-shake variants and physical controllers are
not covered by this checkpoint.

### 2026-10-25 — Honey-floor ownership checkpoint
External copied save SHA-256:
`aad638ae8dcb5eda4f3a37a60478c90600f378edcb414080ac9ae20617fddc5e`.
Level $11 starts with P1 active at X464 and P2 at X432, both Y3185.
Walking P2 right into honey previously left P2 in ordinary $00 and changed
the untouched active P1 to sticky $43. Reaction $07's unconditional active
work context caused this cross-player terrain effect. After redirecting it
to the queued touching Kong, either player reaches X625 in $43 while the
teammate remains $00 and can walk. Both can share the honey patch and jump
free separately. The private test also checks repeated native hops and exact
mid-jump reload trajectories. Honey-wall/carrying routes and physical paired
controllers remain unverified; item selection is covered synthetically.

### 2026-10-25 — Sticky-wall checkpoint
Copied external save SHA-256:
`95eeff972533610bf14f10475e24498f383136da57c9a29ffb4a3bbf4898c2b2`.
Level $11 starts with P1 active at X1781 and P2 at X1772, both Y3249.
P1's right jump reaches the sticky wall at X1839/Y3188. P2's right jump
previously placed P1 in $41 at the untouched position while P2 continued
falling, proving reaction $08 still selected the active Kong. After redirect,
each touching actor enters its own native $41, retains the hang across reload
and uses its own left-plus-jump input to enter $42 away from this right wall.
Both can occupy the wall and leave independently. Other wall orientations,
wall-slide state $44, carrying routes and physical controllers are unverified.

### 2026-10-25 — Sticky-wall midpoint camera evidence
Copied external camera save SHA-256:
`ab817c217bfe89082bfb201d5fcf5e426eede685886e4c95b8030bcbc81c7a2f`.
Both Kongs are in $41 on opposite walls: P1 X2640/Y2789, P2 X2735/Y2906,
with P2 active. Before correction the raw camera target was P2's X2735/Y2906;
the integer midpoint is X2687/Y2847 (native subpixels preserve the fractions).
The state eligibility list omitted honey movement. Private checks cover stable
wall idle and 50-frame live windows of either/both wall jumps, plus exact
player/target matching across reload. Longer input-only falls in this vertical
section encounter terrain damage or pit death; those phases are outside the
live midpoint assertions. Slide $44 and carrying $7A have synthetic coverage;
private slide/carry and physical controller camera checks remain unverified.

### 2026-10-25 — Developer level-clear evidence
Native set_current_level_as_cleared ($BB:8158) uses map-level $08A8, not room
$D3. get_complete_bit_for_level ($BB:8169) selects a mask from $BB:817F and
word index (level >> 4) * 2. This is HiROM: the table is at external ROM file
offset $3B817F. set_level_end_exit_state ($B8:A5CA) parks the inactive Kong in
$13 and sets active $34; fade ($B8:A5DA/$80:8C03) sets $0512=$830F, clears
$0514 and sets $08C2 bit 2. These frame-boundary changes retain CPU state.
Copied normal-level checkpoint SHA-256:
`81d5fa4246639dffd1d05fb292347febc099b5823ab5bd19db55b551eb768108`.
The command sets the previously clear map-level $11 completion bit and reaches
world-map room kind 5, preserving the death-exit value $059D=0. A subsequent
map command is refused. A hidden Windows test loads that copied checkpoint
and delivers the configured F8 WM_KEYDOWN through the real event handler.
Subsequent copied-state verification corrects the initial boss experiment:
world maps retain boss room/camera fields, so those fields alone do not prove
that a transition failed. The map continuation and rendered scene prove exit.
Boss F8 selects kind-0 destination dispatch before fading; kind 3 instead
restores sublevel metadata. Native $BB:8125 keys reward bits at $7E:59B2 by
room $D3, and $BE:B88A/$B8A7 caps the Kremcoin counter $08CC at 75.
$08C4 bit 2 signals collection; the native transition recalculates percentage.
Bonus $059D=1 is an exit selector and must be accepted; its native return
restores the parent level and Rambi form. Map commands additionally require
rejection by the gameplay continuation guard ($0020=$8608).
Final-boss cinematics and physical SDL-keyboard play remain unverified.

### 2026-10-25 - Team throw wait after hanger attachment
The copied Hornet Hole checkpoint starts with P2 carrying P1 beneath a $14C
hanger. Holding Up through the throw animation yields passenger $2B and
thrower $2C. The passenger enters hook transition $30 then $3C at the native
anchor, and hook ownership promotes it. The old thrower still runs $2C, whose
update does terrain probes but suppresses movement; a jumping throw therefore
freezes it above the floor. Native horizontal wait $20 has the same dependency
on team reunion. The dispatch fix releases those waits only after confirmed
hook attachment, preserving the thrower's own velocity and gravity record.
Private input-only ground/jump routes and an old frozen-state reload prove
walking/jumping controls while the passenger remains attached.

### 2026-10-25 - Sticky-wall rescue destination evidence
The copied Hornet Hole checkpoint has P1 attached at (432,2997), state $41,
terrain flags $0002, with P2 standing at (482,3185). The old rescue movement
whitelist rejected the attached source and destination; its destination-ground
requirement separately rejected the wall. Native wall reaction $08 selects
state $41 and animation $2B, clears +$0E and motion, and lets the attached
state retain the established wall anchor. Sharing those coordinates with the
wall facing (+$12 bit $4000) and side contact flags preserves attachment and
independent wall jumps. The private Select route verifies the compiled 121-
tick hold (current tuning 2000 ms), floor return and shared-wall arrival, then
constructs the opposite player roles using only actual teleport/jump inputs.

### 2026-10-25 - Pit loss with a honey survivor
In the reported Hornet Hole checkpoint P1 stands in honey state $43 and P2
hangs on a wall in state $41. P1's leftward jump falls into a pit: the native
active-player exit ran because the survivor whitelist excluded $41. P2 first
jumping onto P1's platform then leftward into the pit instead took inactive
parking without a host lost bit, leaving it teleportable. Accepting live
honey survivor states routes both accepted pit losses through the same host
lost-player handoff, without changing the survivor's honey physics or pose.
The faller stays in $13 with zero collision flags until a DK barrel revives it.

### 2026-10-25 - Squitter rider pit evidence
The Hornet Hole save has P1 riding actor $0EFC, type $190, via state $09.
Moving off either platform edge changes the riding action to airborne $0A
before the pit path parks the Kong in $13. The previous mount cleanup accepted
only $194/$19C, leaving $6C=$0EFC and $6E=$190 after control passed to P2.
The shared native dismount palette exchange ($B8:94BB/$954E) applies to this
mount too: swap only attribute bits $0E00 before clearing mount ownership.
Input-only X dismount followed by P2's jump constructs the reverse rider case.
Both rider identities now lose the mount cleanly, retain the survivor palette
and allow the survivor's own ordinary jump; Select cannot revive the lost Kong.

### 2026-10-25 - Midpoint target versus viewport placement
The reported Squitter/hanger save already targets midpoint (429,1756), but
its native scroll was near (419,1620), placing the pair close to the left edge.
Target operand checks alone therefore missed the visible camera issue.
Desired scroll values $0ADE/$0AE2 are 24.8; camera_handler $B5:E549 passes
them into bank_B5_E7B7 before final scroll commit. Centering those desired
coordinates around the live pair yields a settled scroll of (301,1644),
placing the same midpoint at (128,112). Native smoothing takes time to reach
it and region bounds can still constrain placement. Private idle, both jump
routes and exact mid-jump reload checks verify target and actual scroll; a
rendered private frame confirms both players remain visible.

### 2026-10-26 - Pole regrab after returning or respawning
The first supplied snapshot records host lost mask 3 while P2 is alive in
state 0 with collision flags $1E and the both-present flag clear. The later
checkpoint has both players alive but +$4C=$0EFC for both, pointing to a
previously jumped-from pole. The current pole reuses slot $0EFC at X=3024.
Contact at X=3022/3025 is within the native width and its $FF:BAE0 clipping
range (-96,92); the stale reference makes pending/current pole values equal
and suppresses entry. Native CODE_B8B644's no-contact branch skips the current
pole callback, so clearing departure only there was insufficient. Expire the
reference in the pending pass instead. Native jump release takes three frames
in this fixture; tests wait for $06 then check no immediate regrab.

### 2026-10-26 - Rollercoaster rider ownership
The copied room $000E checkpoint (SHA-256
8068d9a4c62e4a92cd4f4ab8027780cc83367bde1cdaeee68fa3753589828070)
starts both Kongs in shot $12, then enters cart $0158: active $5B, inactive
$5C, each +$42 pointing to the cart. Cart +$42/+$44 hold front/rear riders;
$BE:D9F2 applies seat poses and ROM slope offsets. $BE:DBD7 uses only active
work +$22 and queues paired jump $16. Independent jumps instead preserve the
seat records and retain each character's native $67 physics. $BC:FB1D normally
adds the cart body to the active collision cache even while the rider jumps;
skipping this union restores the rider's actual airborne hitbox. Accepted
reaction $23 ownership is captured before later checks overwrite $6A.
Native hurt normally places the partner in $26 and freezes sprites with
$0A36=7/$0A38=$0280. Keep the survivor seated/in flight, clear that wait,
and grant native $9E flags plus work +$14=$60/+$16=$70 grace. Native cart-hit
$21 instead enters $6A and a confirmation prompt; use its $6B lost-rider arc
when a teammate survives. Final loss spends one team life. A controlled copy
with only $08BE changed from 4 to 0 verifies that one rider's loss keeps the
last life intact, while final loss sets $08BE=$FFFF and continuation $FA80.
The rendered output displays GAME OVER. Original saves are preserved.

### 2026-10-26 - Coaster stomp and gap corrections
Reported checkpoint SHA-256:
3993e5b4b083ba0915a20b967f3a9acce1d556ff4c685f4898400281433477c5.
The native enemy classifier sees $0D5E and treats descending coaster Kongs as
cart-body attacks, destroying the enemy without queuing bounce $1B. Bounded
collision operand adapters now suppress that cart classification only for
the contacted live $67/$68 Kong. Native bounce selects $68, which both the
seat-placement and shared-stomp policies must preserve. Native $B8:B038 also
writes horizontal correction at active work +$0E even for the inactive Kong.
Read its actual work selector and clear old corrections on jump/landing/
departure. Native departure otherwise clears the front cart seat regardless
of which rider left; release only the departing Kong and inherit cart speed.
A cart's old seat pointer can outlive a new cart's accepted landing, so seat
placement must verify the Kong's own +$42 still names that exact cart.
The native offscreen cart cutoff is camera Y+$0120. Reconcile riders there
and at native level-loss reaction $29, preserving safe airborne/new-cart
players while marking only actual fallers lost.

The later user-supplied gap checkpoint has both players alive at X=2891/2905,
room $000E, active P2; SHA-256
4dda3037cb39727c1e27c4b8964ec597a0a7b3d4fc3862fbbb9e8b68bd64ca4f.
Its inactive air movement exposed $BE:D88B's native follower pretranslation
when a cart seat becomes empty. Skip to the real contact test at $D8AA in
simultaneous TEAM. Guard reaction $15 against recruiting a rising/lost Kong
or cancelling an existing same-cart arc. Both player orders now cross the
gap to a new cart with native jump/right input while the non-jumper is lost.
Both source checkpoints are unchanged; no invincibility, revival, actor
position or geometry patches are used in the final acceptance routes.

### Later single-survivor coaster contact
Preserved source SHA-256:
d6291c1b38a8b7f20f11961678bc959f5461b4ab8b0a4692cada4e8c9d83003b.
The surviving Kong is $0E40/$5B on cart $0FB8 near X=5718; $0DE2 is lost.
The next $0158 cart is near X=5984,Y=525. Native foot contact accepts a
descending actor origin above the seat. Rejecting reaction $15 at an arbitrary
24-pixel origin gap consumes recruitment while leaving the rider in $67.
Accepting it also requires immediate vertical seating before cart validation;
otherwise the earlier X=3488 cart can eject the newly seated survivor.

### Paired coaster next-cart acceptance
Preserved checkpoint SHA-256:
82a05e4841000c0404549458d40a5dc85fb821f965c189de09d23b85a6ce934a.
Both riders are alive, active $0E40, on $0F5A near X=9660. At a simultaneous
55-frame jump, $0E40 boards the next cart near X=9888,Y=493 while $0DE2
trails the rear seat by about 22 X pixels and reaches 23 Y pixels below it.
Its subsequent miss is fatal despite the nearby accepted partner landing.
Other timings also expose rear-first recruitment with an unfilled front seat.
A bounded descending catch fills only the native empty seat. Non-jumpers and
well-below-cart fallers retain native loss. Controller-input acceptance uses
simultaneous/staggered jumps and either own jump after boarding.

### Shared stomp between carts
Copied checkpoint SHA-256:
1307e675b78d0a3ab8a7a21abfd9413ae95f2ab65f36813f03eb2f4b293e2a1a.
Both Kongs start on cart $0EFC near X=8920. A jump after 60 neutral frames
hits the $0220 enemy at frame 98, with both +$42 pointers already zero.
Before correction only P1 received native $68 bounce; the post-reaction
shared gate required CartRider ownership despite detached $67 eligibility.
Flight-based validation now gives both riders the same negative Y impulse
and preserves $68. Input-only routes confirm both subsequently board the
next cart. At a 70/60 stagger the vertical separation exceeds the compiled
32-pixel threshold, so only the actual collider receives its bounce.

### Early jump into cannon beside a falling cart
Preserved checkpoint SHA-256:
3d3931add0643994abf8b6c2bb854c49e4b5aacbc344947358298e2744d95667.
Only $0E40 is alive, riding $0E9E near X=14100. Early Jump+Right+Run at
frames 66-70 reaches the cannon at X=14472,Y=624 on frame 112 while the old
cart is still nearby. Originally the next frame changes cannon $11 back to
cart $5B; placement pulls it down and frame 119 begins death. Filtering that
recruitment alone still leaves $0D5E stale: later actor reuse produces a fake
cart landing mid-shot and another premature death. Capture must release both
seat ownership and the unused global cart context. Later Jump+Right entries
serve as an unmodified success comparison. Bananas on this route award extra
lives, so actual missed-jump life accounting is compared immediately before
death rather than against the original pre-award counter.

### Coaster exit rider freezes while cart continues
Preserved checkpoint SHA-256:
3ad569ae7cdee48ac190974509aa51bfb0aaba1b81b8697fc0df4cf89c8808cb.
Only $0E40 is alive, seated in $1016 near X=17000 in room $0E. Native exit
sets $70 near X=17210; the old placement policy stops carrying this state,
leaving X fixed while the cart reaches X=17471. $B8:ADF1 waits for the rider
to pass the viewport before native fade, so it never finishes. Including
live owned $70 in placement advances native $66 fade, transition to room
$1E, normal $0160 goal contact and world-map return (room $2E/submode $10).
No developer skip or guest-memory edits are used in the acceptance replay.

### Ceiling thorn kills the wrong player
Preserved checkpoint SHA-256:
f73747c1137cc1db821028a54011895ac285aecb17c23e91b63fbd5fef228568.
Both players are alive in room $2E, active $0E40. P1/$0DE2 jumps from
X=750,Y=1529 in state $6E and reaches the ceiling at Y=1440 on frame 19.
Accepted $23 records source $0DE2, while $6A/current_sprite later name $0E40.
Before repair native hurt initializes $0E40/$24 and leaves touching $0DE2
in survivor $26. Recording the self-sourced victim and promoting it at native
hurt initialization correctly assigns $24/$25 to $0DE2, then returns control
to $0E40. P2 jumping alone still damages $0E40.

### Shared kart native jump evidence (2026-10-27)
In the copied room-$0F checkpoint the player kart is type $0158 state $0202;
ordinary coaster checkpoints use $0201. $BE:DB91 consumes a 16-tick jump
timestamp window and grounded $0101 contact, takes the jump velocity from
the guest property pointer and advances the cart's high state byte. Co-op
selects the front rider's work timestamp rather than supplying guessed
physics. Rear-seat ROM slope offsets continue to follow the kart in flight.

### Hook framing and thrown collision (2026-10-27)
Copied room-$2C save has type-$014C hooks with +$4E=$E8E8/$00E8. Native
$B3:9891 uses the high byte for camera offsets, so it is not a motion-type
discriminator. Horizontal team flight $1F has collision mask $0006 rather
than the hook-required $0008 bit. Upward flight follows a different native
state path. The hook grab filter now admits real overlap for horizontal
flight in co-op. $B3:9968 is a long-call wrapper ending RTL, including when
invoked from within bank $B3; its shared scan must use a three-byte frame.

### Post-throw hook collision mask (2026-10-27)
The reported room-$2C save has P1 hanging at (1704,535), P2 at (1600,455).
P2 remains mask $0016 from thrown attachment, missing the hook-required
$0008 interaction bit even after jumping. Ordinary grounded throw recovery
would set $0018; a hook interrupts that recovery. Restore those bits for a
live hanging actor to preserve subsequent native hook contact eligibility.

### Native paired cat grab (2026-10-27)
$B8:9282 interaction $13 gives the active Kong state $48 and forces the
inactive Kong to follower state $2F. $B3:CE5A positions the active Kong on
the cat's spin and $B3:CEB3 duplicates position/velocity/facing to $0597
without distance checks. Co-op skips that duplication and recruitment.
The camera selects $0AE8 (the cat) ahead of $0593 while spinning; routing
this selection through the captured Kong allows the existing midpoint
logic to retain a distant independently controlled teammate.

### Packaging validation follow-up (2026-10-29)
Synthetic packaging validation passes through CTest (58/58 public suite). This tooling change makes no new emulated or physical hardware claim.

### Item replay acceptance follow-up (2026-10-29)
Both native input ports pass chest ownership isolation after correcting the
input-only pickup setup. No hardware model or physical-controller claim
changed; complete public CTest remains 58/58.
