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
to an SNES controller register. Both actions, plus file save states, are
suppressed unless the user explicitly enables Assist Tools in the host
launcher or overlay. The overlay's five-slot selector and snapshot files are
host state and never appear on the SNES bus. Opening the overlay also replaces
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
macOS behavior remains unverified until native hardware acceptance; no SNES
hardware result is synthesized to cover that missing host evidence.

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

Visible OpenGL hosts now request a one-buffer swap interval and publish the
accepted VSync state in the diagnostic presentation-backend string. This is a
host/display synchronization request, not SNES timing: it neither alters the
master-clock schedule nor fabricates a successful response when the graphics
driver lacks the extension. Hidden automation uses interval zero to avoid a
driver wait in noninteractive tests; GDI remains synchronized, if at all, by
the Windows compositor. The SDL/NVIDIA path accepted interval one during the
local integration check. Visible Win32 WGL tearing and the interaction between
60.000 Hz displays and DKC2's 60.098811862 Hz host cadence remain owner-visible
acceptance items.

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

Save-state compatibility is also a host ABI concern, not SNES behavior. DKC2's
pre-v7 file includes a raw `CpuState` continuation whose layout is not stable
across the August framework refresh. Loading it as the newer layout can create
an invalid interpreter entry such as `$4012A20` and later stop at a misleading
cartridge PC. DKC2 now rejects that version before any guest state is changed.

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
and are suppressed by the Assist Tools gate. No binding value is written to
WRAM, controller registers, snapshots, SRAM, or deterministic recordings.
Opening the pause menu suppresses the resulting controller word; remap
capture therefore cannot become an in-game input on the same frame.
Pre-boot navigation to Assist Tools and Credits changes only the host launcher's
view enum. It does not run a console frame or touch SNES-visible state.

## Widescreen is not SNES hardware

The cartridge still programs a 256-column SNES viewport. The optional
342-column output is a host PPU capability that exposes tilemap and OBJ data
outside that authentic center. It does not change dot clocks, H/V counters,
DMA timing, VRAM size, OAM layout, or the 65816 camera coordinate system.

DKC2's central placement-radius function at `$BB:BB07` stores a left allowance
and a complete horizontal span in several indexed records. Some default
deactivation callers add eight to a radius index, but other live-object checks
use fixed indices such as `$50`; an index bit therefore cannot identify the
caller's lifecycle purpose. The generated-code adapter marks explicit
activation, live/deactivation, and mixed reset call sites. The host adaptation
adds 43 pixels to the activation left allowance and 86 pixels to its span, but
deliberately leaves deactivation native. Widening live checks retained
already-passed objects off-left and changed later sprite allocation/collision
state. The two
world-sprite visibility paths beginning at `$B5:9FC9` receive the corresponding
transformation of their native `$30` and `$160` constants. Disabled mode
returns all four native
values exactly. This expands activation and rendering boundaries; it
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
repeated. BG3 is centered because its tilemap is shared with HUD/staging uses.
Bounded 32-column menus and rooms are also centered and cleared until an
explicit reconstruction exists.

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

The visible debugger later exposed the same native-viewport seam in two World
5 effects. The disassembly identifies Web Woods level `$0017` with
`forest_misty_ppu_config` and Gusty Glade level `$0018` with
`forest_windy_ppu_config`; both use streamed BG2 `$6800`-class terrain,
bounded BG1 `$5800`, and bounded BG3 `$5C00`. Owner layer-isolation captures
established that the fog and windblown leaves are BG1, correcting the initial
BG3 classification. BG3 supplies the supporting forest backdrop. Repeating
both completed native scanlines fills the margins without reading unpopulated
tilemap columns. The selector requires the exact level, both enabled layers,
both bases, and widened BG2, so similar forest rooms remain clamped.

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

## Host-only live source provenance

The Visible Widescreen Debugger observes the final shadow-tile selection. For
each BG1/BG2 margin fetch, it records whether the entry came from an
authoritative game VRAM capture, DKC2's decompressed-map prefill, an exact
periodic fold, a verified transparent fallback, or unproven wrapped VRAM. The
record is indexed by output scanline and host screen X and is cleared before
every rendered frame. It is neither SNES state nor proof that a plausible tile
has the correct priority or artistic intent.

The renderer can blend these classes onto only the added margin pixels. This
keeps the native 256-pixel region as a visual reference and makes a red raw
fallback distinct from a gray safe-but-missing tile. Deliberately repeated
native backdrop rows are classified by DKC2 after shadow lookup. BG3 and
sprites can be isolated, but do not yet carry per-pixel provenance.
