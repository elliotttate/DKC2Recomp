# Packaging validation follow-up - 2026-10-29

Follow-up to [the original co-op evidence](coop-validation-2026-10-29.md).
The original 56/58 result remains a historical record.

Windows PowerShell launched directly by PowerShell 7 normalizes its module
path, but launching through native CTest retains the other edition's paths.
Automatic module discovery then misses the bundled Get-FileHash function.
Both packaging scripts now import Utility from the executing host's PSHOME;
the release script also explicitly imports its bundled Archive module.

The synthetic snapshot test verifies both executable hashes against known
byte sequences. The release test checks executable and ZIP checksums using
independent .NET hashing, along with both runtime variants, required contents,
private-content exclusion, invalid version rejection and append-only snapshots.

| Check | Result |
| --- | --- |
| Complete CTest before change | 56 / 58 passed |
| Both packaging tests through CTest | 2 / 2 passed |
| Complete CTest after change | 58 / 58 passed |

Reproduce with `ctest --test-dir build-game-vscode -C Release --output-on-failure`.
These tests package synthetic files; this does not certify a distributed game
package, physical controllers or full-game completion. No gameplay code changed.

Log hashes are recorded in [the follow-up JSON](packaging-validation-2026-10-29.json).
