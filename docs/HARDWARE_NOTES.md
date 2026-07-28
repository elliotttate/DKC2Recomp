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
never source-controlled.

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

`$0AFC` is the maximum horizontal scroll after the level-camera initializer
subtracts the native `$0100`-pixel viewport at `$B5:E36C-$B5:E373`. The
streamer keeps one additional 32-pixel metatile staged beyond that limit.
Reading the following metatile crosses into unrelated decompressed WRAM and
caused the colorful strip at the far-right room boundary. The host retains the
guard column, then uses an all-zero 4bpp character discovered in live VRAM so
the lower parallax layer remains visible.

Pirate Panic's 32-column BG2 sky/ocean map is intentionally wrapping, so the
host repeats the rendered native BG2 scanline. Collision-bearing BG1 is never
repeated. BG3 is centered because its tilemap is shared with HUD/staging uses.
Bounded 32-column menus and rooms are also centered and cleared until an
explicit reconstruction exists.
