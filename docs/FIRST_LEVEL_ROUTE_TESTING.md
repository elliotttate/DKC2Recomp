# First-level route testing

Pirate Panic is the first deterministic gameplay route target. The goal is not
only to confirm that the game window can be played manually. The goal is to
turn one clean human playthrough into a replayable regression that can fail in
CI/local CTest whenever gameplay, rendering, audio, input, or save-state
behavior regresses.

## Current status

The headless snesrecomp host can replay a private input recording through the
`SNESRECOMP_INPUT_PLAY` environment variable. The Windows desktop host can
create that recording with `SNESRECOMP_INPUT_REC`.

The first private recording contains 11,275 frames and reaches Pirate Panic's
normal goal transition. Its native replay enters the level at frame 1,267,
keeps it active for 10,009 frames, observes 17 completion-flag changes and
three exit transitions, and reports no clipped samples. It is not yet an
accepted regression: frame 5,522 reaches the unresolved dispatch at
`$BA:B33F`, emits `[interp_cap]` and `[unresolved-abandon]`, and therefore
fails the route gate. The recording is evidence and a debugging fixture, not a
passing correctness claim.

The route gate is intentionally private because the recording is player input
for a copyrighted game and because it may depend on a private SRAM or file
state. Keep route recordings under `recordings/` or `private/`; both are
outside source control.

## Recording format

Each non-empty line is one emulated frame. Values are hexadecimal controller
masks. `#` and `;` begin comments. A decimal repeat count can follow the mask,
with or without `*`.

```text
# wait one frame
000000

# hold Start for 12 frames
000008 * 12

# Player 1 Right plus B for 60 frames
000101 60
```

The low 12 bits are Player 1. Bits 12-23 are Player 2. Frames after the end of
the file replay neutral input, which is useful for padding but should not be
used to prove a route.

## Create the first Pirate Panic recording

From PowerShell, launch the desktop build with recording enabled:

```powershell
$env:SNESRECOMP_INPUT_REC = "recordings\pirate-panic-entrance-to-goal.input"
.\build-snesrecomp\Release\DKC2Recomp.exe `
  ".\build-snesrecomp\Release\Donkey Kong Country 2 - Diddy's Kong Quest (U) (V1.0).smc"
Remove-Item Env:\SNESRECOMP_INPUT_REC
```

Then play a clean route:

1. Start a new game or load a known clean file state.
2. Enter Pirate Panic, level `$0003` in the v1.0 level table.
3. Finish the level through the normal goal.
4. Let the post-goal transition advance briefly.
5. Close the game cleanly so the recording file is flushed.

Do not edit the recording into the repository. If the route starts from a
specific SRAM or file-state fixture, keep that fixture private too and record
its provenance in the implementation journal.

## Replay and assert the route

The standalone gate is:

```powershell
.\scripts\test_pirate_panic_route.ps1 `
  -Rom ".\build-snesrecomp\Release\Donkey Kong Country 2 - Diddy's Kong Quest (U) (V1.0).smc" `
  -Executable ".\build-snesrecomp\Release\dkc2_snesrecomp_headless.exe" `
  -Input ".\recordings\pirate-panic-entrance-to-goal.input" `
  -Frames 45000
```

It verifies:

- the recording loads and the run completes;
- no interpreter-cap or unresolved-dispatch diagnostic occurs;
- the run enters Pirate Panic;
- Pirate Panic remains active for a meaningful number of frames;
- the per-file completion flags change while Pirate Panic is the active parent
  level;
- the level-exit destination transitions at least once;
- audio remains unclipped.

The test does not yet compare against a Snes9x reference route. First the
`$BA:B33F` dispatch must replay cleanly; reference comparison is the next
correctness upgrade after the native route is deterministic.

## Add the route to CTest

Configure with the private recording path:

```powershell
cmake -S . -B build-snesrecomp `
  -DDKC2_BUILD_SNESRECOMP=ON `
  -DDKC2_PIRATE_PANIC_INPUT="C:\private\pirate-panic-entrance-to-goal.input"
```

When `DKC2_ROM` and `DKC2_PIRATE_PANIC_INPUT` are both set, CTest registers:

```text
supplied_rom_pirate_panic_route
```

Run it with:

```powershell
ctest --test-dir build-snesrecomp -C Release -R pirate_panic --output-on-failure
```

## Reference comparison follow-up

Once the native replay is stable, capture the same input route in Snes9x or
the local snesref path and compare frame/event/audio checkpoints. The current
native gate proves "this route stayed playable and reached the clear path";
the reference gate should prove "this route matches the accurate emulator at
selected milestones."
