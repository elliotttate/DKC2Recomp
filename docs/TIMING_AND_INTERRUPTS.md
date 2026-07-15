# Timing, interrupts, HDMA, and controllers

## Purpose and accuracy boundary

Version 0.5 adds the first shared master-cycle timeline. The scheduler accepts
SNES master cycles and advances the beam position, interrupt latches, HDMA,
automatic controller polling, and the SPC700/S-DSP clock domain from one
source of time.

The W65C816 core still executes a complete instruction without reporting its
exact internal and external cycles. The private boot probe therefore feeds the
scheduler **eight master cycles per host-visible A-bus byte access**. This
counts opcode, operand, data, stack, vector, and synchronous general-DMA source
accesses, but it omits internal cycles and dummy bus accesses and does not yet
select 6/8/12-master-cycle bus speeds by address.

Consequently, the scheduler has real master-cycle units and deterministic
event ordering, but the CPU-to-master-clock conversion is provisional. Frame,
beam, and APU cycle counts from `--with-timing` are regression evidence, not a
claim that the run is synchronized to a console trace.

## Compatibility modes

`dkc2_boot` intentionally keeps the older checkpoints:

- no option: stop at the original APU handshake boundary;
- `--with-apu`: retain the version-0.4 port-access APU scheduler and stop at
  `$4211`; and
- `--with-timing`: use the new timeline, timed APU stepping, interrupts, HDMA,
  and neutral controller input.

Keeping the old modes makes it possible to detect a regression in an earlier
layer without confusing it with later timing work.

## NTSC beam model

The current non-overscan NTSC geometry is:

| Event | Position |
| --- | --- |
| Scanline length | 1,364 master cycles |
| Frame length | 262 scanlines |
| HBlank start | master cycle 1,096 |
| VBlank start | scanline 225 |

`$4212` reports the derived HBlank and VBlank states. Interlace, overscan,
short scanlines, PAL timing, DRAM refresh stalls, and PPU dot-level behavior
are not yet modeled.

## Interrupt registers and delivery

The timed path implements the subset required by DKC2:

- `$4200` stores NMI enable, H/V IRQ mode, and automatic-joypad enable;
- `$4207-$420A` store the 9-bit H/V timer positions;
- `$4210` reports the VBlank NMI latch and clears it on read;
- `$4211` reports TIMEUP and clears it on read; and
- `$4212` reports VBlank, HBlank, and automatic-joypad busy.

VBlank latches one NMI edge when enabled. H-only, V-only, and combined H/V
timer matches latch TIMEUP. The boot runner checks those latches between
logical CPU instructions and enters the existing `dkc2_cpu_nmi` or
`dkc2_cpu_irq` path. A CPU stopped in `WAI` advances one scanline at a time
until an enabled source can wake it. Exact within-instruction interrupt
sampling and the masked-IRQ wake nuance remain future CPU timing work.

## APU scheduling

The timed path converts master-cycle credit to SPC700 cycles at a nominal
21:1 ratio. Because the imported core can advance only whole SPC700
instructions, the wrapper carries an overrun as negative credit and repays it
before running the next instruction. This avoids accumulating a whole-opcode
rounding error after every 65816 instruction.

The old `$2140-$2143` access scheduler remains only in `--with-apu` mode for
the version-0.4 regression. The ARAM hash at the later timing checkpoint is
expected to differ because the audio CPU continues running for many frames.

## HDMA

HDMA channels are initialized from `$420C` at the start of a frame and run at
HBlank for visible scanlines. The implementation includes:

- all eight B-bus transfer patterns;
- direct and indirect source tables;
- repeat and write-once line descriptors, including the 128-line encoding;
- table, indirect-address, and line-counter register write-back; and
- an explicit barrier for unsupported B-to-A transfers.

The intro uses direct, mode-0, write-once tables on channels 2 through 4. A
synthetic test checks descriptor expiry and termination. The private timing
probe executes 1,071 one-byte HDMA line transfers before the next barrier.
HDMA currently takes effect at the HBlank event without consuming its own bus
duration, and general DMA remains synchronous at one logical CPU instruction.

## Controllers

The runtime accepts a 16-bit SNES button mask for each of two controllers.
`$4016/$4017` expose the manual serial stream after a `$4016` latch/strobe.
When `$4200` bit 0 is set, VBlank begins a 4,224-master-cycle automatic poll;
`$4212` bit 0 reports busy and `$4218-$421B` receive the two results. Joypads
3 and 4 currently return zero. The boot probe supplies neutral input until a
desktop input backend is added.

## Verification checkpoint

The synthetic timing suite checks NMI/TIMEUP clear-on-read behavior, H/V blank
status, frame wrap, H-timer matching, direct intro-style HDMA, manual serial
input, and automatic polling.

The private command is:

```powershell
.\build\Release\dkc2_boot.exe "C:\private\dkc2.smc" 5000000 --with-timing
```

It passes the old `$4211` boundary, resumes from `WAI`, performs repeated NMI,
DMA, HDMA, and controller work, and stops at `$2135`. DKC2 is reading the
middle byte of the PPU Mode-7 multiplication result while building the intro
palette. That multiplication result is the next deliberately unsupported
hardware behavior.
