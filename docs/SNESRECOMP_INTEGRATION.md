# snesrecomp integration

## Decision

The project remains an independent Git repository and consumes
[`mstan/snesrecomp`](https://github.com/mstan/snesrecomp) as the `snesrecomp/`
Git submodule. The earlier interpreter-first runtime remains in this repository
as a validation harness; it is not discarded or presented as the final desktop
runner.

The submodule records exact commit
`ae92aad25a2a8b686c8fd1fa5d95a5a6f3db266d` from the dedicated
`dkc2/static-coverage-import` branch. The Git link makes builds reproducible
even though the framework continues to move. DKC2's own gates must validate
every behavior we depend on after each deliberate refresh.

Initialize a fresh checkout with:

```powershell
git submodule update --init --recursive
git -C snesrecomp rev-parse HEAD
```

The second command must print the commit recorded by the parent repository.
Do not develop framework changes on the submodule's detached HEAD. Create a
named branch such as `dkc2/<topic>`, make only game-agnostic changes there, and
submit those commits to upstream separately. The parent repository then updates
its Git link to the accepted or tested framework commit.

## Source and generated-data boundary

The supported ROM is private input. The repository contains source-only
`recomp/*.cfg` structural metadata and build/tooling glue, but not the ROM,
screenshots, memory dumps, extracted assets, assembly source, or ROM-derived
generated C.
Generation output is written under ignored `generated/` or `src/gen/` paths.

The CFG set was mechanically derived from the H4v0c21 address/symbol map and
contains labels, addresses, bounded ranges, data ranges, and finite dispatch
contracts. The source repository had no explicit license at the validated
revision. This repository remains private, and public redistribution of those
derived labels/contracts requires separate provenance and legal review.

The upstream repository currently declares no overall license. Its submodule
is suitable for local research and for contributing changes back to its owner,
but distributing combined binaries or claiming a generally redistributable
open-source release remains blocked until the applicable licenses and
permissions are clear. This is an engineering boundary, not legal advice.

## Bring-up workflow

1. Validate the private USA v1.0 ROM with `dkc2_verify`.
2. Run the existing synthetic and private regression suite unchanged.
3. Generate the imported structural program from `recomp/*.cfg`.
4. Stand up the game-specific runner around the shared hardware runtime.
5. Boot entirely through LLE, then promote measured hot or structurally proven
   routines to AOT without removing the interpreter fallback.
6. Compare frame, memory, CPU, controller, and audio checkpoints against an
   accurate emulator at matching logical events.
7. Move any genuinely game-agnostic framework fix to its own branch inside the
   submodule, add an upstream regression test, and prepare a focused draft PR.

The current framework-only scope and pre-publication checklist are maintained
in `docs/UPSTREAM_PR_PLAN.md`.

The current reproducible Windows commands are:

```powershell
git submodule update --init --recursive
.\scripts\generate_snesrecomp.ps1 -Rom "C:\private\dkc2.smc"
cmake -S . -B build-snesrecomp -DDKC2_BUILD_SNESRECOMP=ON
cmake --build build-snesrecomp --config Release `
    --target dkc2_snesrecomp_headless dkc2_snesrecomp_desktop
.\build-snesrecomp\Release\dkc2_snesrecomp_headless.exe `
    "C:\private\dkc2.smc" 1
.\scripts\run_snesrecomp_desktop.ps1 -Rom "C:\private\dkc2.smc"
.\scripts\test_snesrecomp_smoke.ps1 -Rom "C:\private\dkc2.smc"
```

`DKC2_BUILD_SNESRECOMP` defaults to `OFF`, so a fresh source-only
checkout can build and test the validation harness before private generated C
exists. Enabling it fails with an actionable message if either the submodule
or generated sources are absent. The old `DKC2_BUILD_SNESRECOMP_HEADLESS`
spelling remains a deprecated compatibility alias.

## Current native checkpoint

The experimental target compiles and links against the shared runner. The
private generator identifies HiROM correctly, reads reset `$83F7`, NMI `$F37D`,
and IRQ `$F3B9` from the HiROM vector table. The current structural import
defines 3,296 bounded entries, 497 finite runtime-pointer sites, 38 terminal
inline-table calls, and 313 exact data regions. It emits 3,460 exact CPU-mode
variants: 3,325 AOT eligible (96.10%) and 135 deliberate LLE fallbacks.

The first runtime failure was mapper routing, not the SPC700 IPL. DKC2's sample
descriptor at `$F0:09FC` is HiROM ROM but overlaps LoROM SRAM. The generic fix
selects only the active mapper's SRAM window, and a focused regression proves
that `$F0:09FE` reads ROM for HiROM and SRAM for LoROM.

The next failure was DKC2's non-returning NMI dispatcher. It jumps through
direct-page `$20`, resets the hardware stack, and ends at a new `WAI` rather
than returning through `RTI`. The adapter now treats that wait as the next
continuation. The rebuilt 600-frame gate reports advancing state, nonzero
palette/video, and generated audio samples:

```text
heartbeat frame=600 resume=$00b50b lle=1
run_stats video_active_frames=589 blank_frames=11
          audio_active_frames=553 audio_nonzero_samples=588046
result=completed frames=600
```

The first request beyond frame 3,047 then froze in an object-list sort.
`BRL -$72` at `$B5:F348` must use `$B5:F34B`, after its operand, as the PC
base. The shared interpreter changed `cpu->pc` in the same expression that
fetched the operand, allowing MSVC to use the pre-fetch PC and land two bytes
early at `STZ $44`. Both `BRA` and `BRL` now fetch into a temporary before
changing the PC, with positive and negative displacement regressions.

With that fix the runner completes 12,000 neutral-input frames and crosses the
same transition repeatedly. The next apparent sprite failure was traced to the
game adapter omitting the shared PPU's VBlank handler. DKC2's 544-byte WRAM OAM
source and its DMA request were already correct, but every transfer began from
the stale internal OAM-port address. Calling the handler after visible-line
rendering reloads that port before the following NMI.

Paced native frame 3,575 now matches the aligned Snes9x frame 3,578 byte-for-byte in
VRAM, CGRAM, and OAM. After normalizing Snes9x's RGB565 green expansion to the
native RGB555 expansion, all 57,344 pixels match.

The runner now enforces a `1364 * 262` master-clock frame budget and consumes
DSP output with a fractional 32,040 Hz / 60.098811862 Hz accumulator. The
12,000-frame gate observes 28 state transitions, six correctly ordered demo
starts and ends, two complete attract cycles, zero sequence errors, zero clipped
samples, and no suspicious discontinuity. It pins both the state-event SHA-256
and the audio FNV-1a stream fingerprint. A separate one-cycle private PCM
comparison against Snes9x passes duration, RMS, peak, maximum-delta, clipping,
and seven-region silence-envelope checks.

The shared interpreter originally cleared program-bank bit 7 on long
calls/jumps/returns. This mapped DKC2's `$80/$B5/$BB` FastROM execution onto
byte-identical `$00/$35/$3B` SlowROM banks and made each demo level load 14
frames late. Directed `JSL`, `JML`, `RTL`, and `JMP [abs]` tests now preserve
the complete PBR. The three native/reference loading durations are currently
152/152, 134/135, and 152/153 frames, and the complete first-cycle event delta
is bounded to six frames by `scripts/compare_state_traces.py`.

The project-owned Windows desktop host now presents that same frame/audio path
through GDI and waveOut, polls focused keyboard input, and maps the first
connected XInput pad. A 180-frame hidden startup gate covers ROM verification,
window creation, the game loop, renderer, audio initialization, and clean
shutdown. The gamepad mapper has a source-only synthetic regression. A real
human watch/listen/input pass remains open; automated startup is not perceptual
sign-off.

Verification at this checkpoint:

- the clean project build and complete configured CTest suite pass;
- the generator reproduces 3,325 AOT and 135 LLE exact variants;
- the focused upstream v2 suite passes 337/337 tests;
- the promoted build completes the formerly failing frame-3,330 attract path
  with active video/audio and no sequence error;
- the focused MSVC interpreter core passes 23/23 checks and its bridge harness
  passes 52/52 checks;
- the mapper/runtime-dispatch regression passes under MSVC;
- upstream's top-level test launcher has four pre-existing width-lint failures,
  reproduced unchanged in an untouched checkout of the same upstream commit;
- the 600-frame video/audio integrity gate passes;
- the aligned frame-3,575 display-state/reference regression passes;
- the earlier LLE-heavy 12,000-frame semantic gate completed two attract
  cycles; the full post-promotion 12,000-frame gate is still open; and
- three synthetic PCM-comparison cases pass, including rejection of clipping
  and an added long dropout;
- the synthetic desktop gamepad/trigger mapping regression passes;
- the synthetic atomic-presenter and bounded-rewind regressions pass; and
- the private 180-frame desktop gate performs a real in-memory restore and
  passes with user SRAM explicitly disabled.

`scripts/test_snesrecomp_smoke.ps1` enforces the short gate: the requested 600
frames must complete, intro state `$002A` must advance, CGRAM must be nonzero,
at least one nonzero framebuffer and audio stream must be observed, and audio
must not clip or exceed the discontinuity ceiling. PPU OAM and DKC2's WRAM OAM
staging table are both reported; equality is optional because some sampled
frames legitimately precede the next complete DMA. A second configured test
pins the aligned frame-3,575 frame/VRAM/CGRAM/OAM hashes. A third pins two
attract cycles, their state-event hash, and the audio fingerprint. The short check is also
registered as `supplied_rom_snesrecomp_smoke` whenever the private native target
and `DKC2_ROM` are enabled on Windows.

## Attract-demo success criterion

The first playable milestone is not "a window opened". It passes only when a
fresh process using the verified ROM and neutral host input:

- boots through every logo and title transition;
- enters the built-in attract demo without injected gameplay input;
- runs the complete demo sequence and returns to its normal title-loop state;
- completes at least two consecutive attract cycles;
- has no watchdog, unsupported-dispatch abandonment, interpreter bail, crash,
  freeze, or unexpected reset;
- produces frame checkpoints matching the reference at agreed events, with no
  visible corruption, missing layers, sprite artifacts, or timing-dependent
  flicker;
- produces continuous reference-matched music and sound effects without
  underruns, dropped commands, pitch errors, or transition gaps; and
- is deterministic: the same build, ROM, config, and neutral-input recording
  produce the same checkpoint sequence on two clean runs.

Manual watching and listening are still required for sign-off, but they are
backed by automated heartbeats, frame/memory hashes, audio counters, and a
bounded timeout so a silent freeze cannot be mistaken for success.

The desktop host also uses the framework's cartridge-RAM allocation for normal
DKC2 saves. It reads 2 KiB from `saves/save.srm` after `SnesInit`, writes it on
a clean exit, and rotates the previous file to `save.srm.bak`. An isolated
two-launch validation produced 2,048-byte current and backup files with
matching hashes; the configured CTest run disables persistence and the real
playable-build save directory was not touched.
