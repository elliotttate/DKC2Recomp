# DKC2 Private Diagnostic Version

This is a private, self-contained widescreen testing kit. It contains the
owner's ROM and may contain saves, recordings, WRAM, VRAM, and screenshots.
Do not upload it, commit it, or attach it to a GitHub release.

## Record a complete Pirate Panic run

Right-click `Record-Pirate-Panic.ps1`, choose **Run with PowerShell**, and play
the complete route. The script:

- uses an absolute path derived from this folder;
- preserves the starting `save.srm` beside the recording;
- refuses to overwrite earlier evidence;
- waits for the game to close;
- verifies that recording exists;
- preserves a recording-specific tier-2 coverage manifest, performance log,
  and last-run report under `diagnostics/`; and
- reports the exact frame count.

From PowerShell, a custom evidence name is:

```powershell
.\Record-Pirate-Panic.ps1 `
  -RecordingName "pirate-panic-test-01.input"
```

Close the game normally. If recording cannot start or stops writing, the game
now displays an error instead of silently discarding the session.

For a focused Swanky validation run:

```powershell
.\Record-Pirate-Panic.ps1 `
  -RecordingName "swanky-validation-01.input"
```

Start from a normal save and enter Swanky's Bonus Bonanza. Play through one
complete game-show attempt, then close the game normally. Do not use rewind or
load a save state during this recording: controller frames are recorded, but
those host-only actions are not yet part of the input stream. Fast-forward is
safe because it only changes presentation cadence.

After the script reports success, validate the run with:

```powershell
python .\tools\validate_swanky_run.py `
  --report .\diagnostics\swanky-validation-01.last_run_report.json `
  --tier2 .\diagnostics\swanky-validation-01.tier2_coverage.json `
  --performance .\diagnostics\swanky-validation-01.performance.log
```

The validator requires a retained native `$B4:A4CB` dispatch, zero interpreter
bailouts, zero interpreted Swanky state hits, and no recurrence of the corrupt
dispatch/MMIO-execution sequence from Version 11.

## Diagnose one frame

```powershell
.\Diagnose-Frame.ps1 `
  -Recording ".\recordings\pirate-panic-test-01.input" `
  -Frame 5499 `
  -OpenReport
```

Each capture gets a timestamped folder beneath `captures/`; previous evidence
is never overwritten. The script automatically supplies the preserved
starting SRAM when one exists.

Capture a frame immediately before an object appears and the first frame after
it appears. Inspect BG1 first for floors/walls. For objects, follow active game
sprite → render-consumed OAM → OBJ pixels.

## Verify the kit

This hidden smoke test records 120 frames, replays them through the
trace-enabled runner, and creates composite/BG1/OBJ evidence:

```powershell
.\Verify-Diagnostic-Kit.ps1
```

Python 3 must be installed and available as `python.exe`; the diagnostic tools
use only the Python standard library.
