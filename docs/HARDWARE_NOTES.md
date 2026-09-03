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
word. D-pad and left-stick directions share the directional bits. Synthetic
tests cover every face, menu, shoulder, D-pad, analog direction, trigger
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
menu inputs enter an SNES controller register. Rumble, DirectInput, and native PlayStation
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
interval zero as well: its host waits the 60.098811862 Hz absolute Mach
deadline before submitting the complete frame, with a short final spin and
stall re-anchor. This prevents a blocking 60/120 Hz OpenGL swap from becoming a
second cadence authority. `DKC2_KEEP_OPENGL_VSYNC=1` retains the old path as a
diagnostic comparison; it is not the default.

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
