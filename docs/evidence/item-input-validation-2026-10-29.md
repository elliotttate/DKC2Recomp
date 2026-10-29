# Item-input replay follow-up - 2026-10-29

Follow-up to [the original co-op evidence](coop-validation-2026-10-29.md).
The original 44/45 private result remains a historical record.

The blank-SRAM pickup setup had two problems. P1 reached the enemy during
roll recovery and took damage; P2 picked up the chest, then released Run
and threw it before the ownership checks. Mirroring the approach also
assumed identical Kong jump/roll arcs. The replay now uses a restarted P1
roll and a closer P2 approach, followed by held Run while both settle.
It additionally verifies the held sprite is the native chest (type $01A8).
Only controller inputs change guest state. No gameplay code changed.

Both player slots now pass all existing assertions: a grounded carrier and
independent partner; every-frame ownership during partner Run/Pick and Jump;
no partner carry/throw state; owner airborne carrying; item retention after
ground/air snapshot loading; and intentional release by the owner.

| Check | Result |
| --- | --- |
| Item-input private replay | Passed for P1 and P2 |
| Complete CTest before replay maintenance | 58 / 58 passed |
| Complete CTest after replay maintenance | 58 / 58 passed |
| Packaging tests directly in PowerShell 7 | Both passed |

Reproduce from the repository root:

```powershell
python scripts/check_coop_item_input.py --runner build-game-vscode/Release/dkc2_snesrecomp_headless.exe --rom C:/private/dkc2.sfc
ctest --test-dir build-game-vscode -C Release --output-on-failure
```

The optional `--kongs-pack` interface remains available but was not exercised
in this follow-up. The runner starts with blank SRAM in a temporary folder;
no existing save is loaded or modified. Earlier checkpoints remain copied
outside Git. Original failed probe traces and new validation logs are retained
in the private evidence archive under `validation-fixes/`.

This follow-up reruns the previously failing private case, not all 45 cases.
The other 44 passed in the original validation; gameplay code is unchanged.
Physical controller behavior, every level and complete game runs remain
outside this evidence. [JSON metadata](item-input-validation-2026-10-29.json)
records the ROM, runner, script and log hashes.
