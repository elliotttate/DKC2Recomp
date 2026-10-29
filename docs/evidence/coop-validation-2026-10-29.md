# Co-op validation evidence - 2026-10-29

This records preservation and fresh checks for the changes since
`3b386cb` (v0.0.8). It includes successful checks, failed attempts and
remaining limitations. No ROM, save, game assets or binaries are included.

## Preserved private files

- External archive name: `2026-10-29-precommit-084625Z`.
- **585 files verified**, totaling 174,588,110 bytes.
- This includes 582 files from the development checkpoint folders and the
  live quicksave, SRAM and SRAM backup. Originals and derived test copies
  are inventoried together; this is not 585 independent regression scenarios.
- Files were copied rather than moved. Each copy was checked against its
  source SHA-256, then all archive hashes were checked again after replays.
- [Checkpoint inventory](checkpoints-2026-10-29.csv) records relative archive
  paths, byte sizes and SHA-256 values. The corresponding private files remain
  outside the repository under `checkpoints/` and `live-saves/`.
- Raw build/replay logs and initial failure logs are stored beside the private
  checkpoints. Their hashes and paths are in the machine-readable evidence.

## Results

| Check | Fresh result |
| --- | --- |
| Complete Release build | Passed |
| CTest before validation | 56 passed / 58 total |
| CTest after validation | 56 passed / 58 total |
| Targeted private checks, latest outcome per case | 44 passed / 45 total |
| Recorded initial attempts and rechecks | 50 attempts |
| External backup hash verification | 585 / 585 matched |

CTest still fails `create_windows_version` and `make_release`: their spawned
PowerShell cannot resolve `Get-FileHash`. These are not reported as passing.

The remaining private failure is `check_coop_item_input.py`: its blank-SRAM
P1 pickup setup finds no held item before the ownership assertions run.
This setup failure was already documented on 2026-10-22. It is retained as a
failure, not counted as a pass or presented as a proven gameplay regression.
Copied-save item/water checks and synthetic ownership checks pass.

## Replay maintenance and failed attempts

The wider validation initially exposed four additional setup/assertion issues.
The original logs and commands are retained in the JSON attempt history:

- The normal developer-command check initially received a non-gameplay
  checkpoint. Re-running with the actual normal-level checkpoint passes.
- The water route stopped five ticks before the swimmer entered water, while
  the dry carrier still held its beetle. The check now probes for real water
  entry within a bounded approach, then replays through that frame with all
  item-ownership assertions retained.
- Staggered pole entrants jumped from rest and missed the pole. The routes
  now give later entrants the same run-up used by the sole-climber check;
  shared attachment, climbing, animation, bounds, release and reload checks
  remain intact.
- Mounted pit cleanup restores Rattly to native unmounted idle; deletion is
  not required. The check now verifies that idle state while retaining cleared
  mount references, survivor palette, on-foot control and reload assertions.

A subsequent respawn-loss check was also initially given the different pole
regrab checkpoint rather than its documented shared-pole-death checkpoint.
The corrected replay passes and the rejected attempt remains recorded.

These corrections change replay setup/expectations, not gameplay policy.
Kleever and repeat-pole invincibility options affect temporary test copies
only; those flags are visible in each recorded command.

## Reproduce

Use the supported external headerless North American v1.0 ROM with SHA-256:

`35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633`

Build and run the public suite from the repository root:

```powershell
cmake --build build-game-vscode --config Release --parallel 1
ctest --test-dir build-game-vscode -C Release --output-on-failure
```

For a private replay, point the script at an archived checkpoint without
replacing the live quicksave. For example:

```powershell
$backup = "C:\private\2026-10-29-precommit-084625Z"
$rom = "C:\private\dkc2.sfc"
$runner = ".\build-game-vscode\Release\dkc2_snesrecomp_headless.exe"
python scripts/check_coop_cat_grab.py --runner $runner --rom $rom --state "$backup\checkpoints\enemy-grab-owner\before-grab.sav"
```

[Machine-readable evidence](coop-validation-2026-10-29.json) contains every
case command, checkpoint hash, outcome, log hash and selected output. Replace
`{REPO}`, `{BACKUP}` and `{ROM}` in command arguments with your local paths.
The inventory CSV hash uses UTF-8 text with LF-normalized line endings.

## Source and build identity

- Baseline commit: `3b386cb4a6b67472bc6693664ce61a11190e7b5d`.
- Final source fingerprint: `777222e0d9b22e40b5ac0b5b7c1fcac729c3ca866e21cc2553b5f614026da75c`.
- Fingerprint includes 317 tracked/index source entries under the roots listed in the JSON.
- Validated headless executable SHA-256: `eaf105b027540bd9e47cf1ffaee6ad50ed94fe0774c2913aebeb0d562d7e1603`.
- The binary hash identifies the local validation artifact; the binary stays private.

The source fingerprint is SHA-256 over lexicographically sorted UTF-8 lines
`<git-mode> <git-object-id> <path>` followed by LF, for the recorded source
roots. The object IDs come from the staged source entries and match the
committed tree. Documentation/evidence files are excluded to avoid a
self-referential hash. Initial failed attempts used the earlier replay
setups; the latest outcomes correspond to the corrected final source.

## Latest private-check outcomes

| Case | Latest result | Checkpoint inputs |
| --- | --- | --- |
| `animal_turns` | PASS | `snake-health-inspection/original.sav` |
| `barrel_gravity` | PASS | `barrel-gravity-inspection/original.sav` |
| `barrel_momentum` | PASS | `barrel-momentum-inspection/original.sav` |
| `barrel_rotation` | PASS | `barrel-rotation-inspection/original.sav` |
| `barrel_sharing` | PASS | `shared-barrel-inspection/original.sav`, `barrel-air-inspection/original.sav` |
| `barrel_stomp` | PASS | `barrel-stomp-inspection/original.sav` |
| `bonus_hooks` | PASS | `bonus-hang-inspection/hang.sav` |
| `bonus_rider` | PASS | `bonus-rider-inspection/original.sav` |
| `boss_intro` | PASS | `boss-intro-inspection/original.sav` |
| `camera_center` | PASS | `spider-camera-inspection/original.sav` |
| `cat_grab` | PASS | `enemy-grab-owner/before-grab.sav` |
| `ceiling_thorns` | PASS | `ceiling-thorn-owner/original.sav` |
| `coaster` | PASS | `coaster-inspection/original.sav` |
| `coaster_cannon_capture` | PASS | `coaster-early-jump/original.sav` |
| `coaster_detached_stomp` | PASS | `coaster-pair-stomp-check/original.sav` |
| `coaster_exit` | PASS | `coaster-exit-stuck/original.sav` |
| `coaster_landing` | PASS | `coaster-landing-inspection/original.sav` |
| `coaster_pair_landing` | PASS | `coaster-pair-landing/original.sav` |
| `coaster_stomp` | PASS | `coaster-stomp-inspection/original.sav`, `coaster-stomp-inspection/reported-gap.sav` |
| `developer_bonus` | PASS | `developer-special-inspection/bonus-original.sav` |
| `developer_boss` | PASS | `developer-special-inspection/boss-original.sav` |
| `developer_normal` | PASS | `developer-inspection/original.sav` |
| `enemy_push` | PASS | `enemy-push-inspection/original.sav` |
| `goal` | PASS | `goal-inspection/original.sav` |
| `ground_shake` | PASS | `ground-shake-inspection/original.sav` |
| `honey` | PASS | `honey-movement-inspection/original.sav` |
| `honey_pits` | PASS | `pit-honey-inspection/original.sav` |
| `honey_teleport` | PASS | `honey-teleport-inspection/original.sav` |
| `honey_wall` | PASS | `honey-wall-inspection/original.sav`, `honey-wall-camera-inspection/original.sav` |
| `item_input` | FAIL | Blank SRAM / scripted startup |
| `kleever_hooks` | PASS | `hanger-orders-inspection/original.sav` |
| `next_hooks` | PASS | `next-hook/original.sav` |
| `pole_regrab` | PASS | `pole-dead-inspection/both-alive.sav` |
| `pole_survival` | PASS | `pole-death-inspection/original.sav` |
| `poles` | PASS | `pole-hang-inspection/original.sav` |
| `respawn_lost` | PASS | `pole-dead-inspection/original.sav`, `pole-death-inspection/original.sav` |
| `respawn_placement` | PASS | `respawn-placement-inspection/original.sav` |
| `shared_kart` | PASS | `shared-kart-control/original.sav` |
| `shared_mounts` | PASS | `barrel-parrot-inspection/both-parrot-original.sav`, `rattly-death-inspection/original.sav` |
| `shared_stomp` | PASS | `shared-stomp-inspection/original.sav`, `stomp-camera-inspection/original.sav` |
| `spider_pits` | PASS | `spider-pit-inspection/original.sav` |
| `thorns` | PASS | `thorn-death-inspection/original.sav` |
| `throw_hanger` | PASS | `throw-hanger-inspection/original.sav` |
| `thrown_hooks` | PASS | `hook-side-share/original.sav` |
| `water_held` | PASS | `water-held-inspection/original.sav` |

## Unverified scope

- This is targeted evidence, not full-game traversal or a claim that every
  enemy/terrain arrangement works.
- Physical controller buttons and rumble sensation require owner acceptance.
- Older optional rescue scripts with a hard-coded three-second threshold
  were not rerun against the current two-second compiled configuration.
- A fresh input-only first-boarding approach for the special kart, concurrent
  captures by multiple cats and final ending cinematics remain unverified.

See [release notes](../../RELEASE-NOTES-unreleased.md) and the
[implementation journal](../IMPLEMENTATION_JOURNAL.md) for feature scope
and earlier verification history.
