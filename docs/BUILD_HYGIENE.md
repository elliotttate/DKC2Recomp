# Build and version hygiene

The repository has one source checkout. Compiler workspaces, generated private
code, and user-testable versions are different kinds of output and must not be
mixed together.

## Canonical locations

| Location | Purpose | Keep in Git | Safe to recreate |
| --- | --- | --- | --- |
| repository root | Source, tests, scripts, and documentation | Yes | No |
| `snesrecomp/` | Pinned SNESRecomp source submodule | Gitlink | No |
| `recomp-ui/` | Pinned launcher source submodule | Gitlink | No |
| `generated/snesrecomp/` | Private C generated from the user's verified ROM | No | Yes, with the ROM |
| `build-snesrecomp/` | Canonical local Windows compiler workspace | No | Yes |
| `versions/Version NN/` | Append-only builds intended for manual testing | No | Yes, from the matching source commit |
| `diagnostics/` | Local crash reports and support bundles | No | Yes |

Use `build-snesrecomp` for routine Windows development. A build directory is a
CMake cache plus intermediate object files; it is not a release and should not
be opened to choose whichever executable looks newest. The supported Win32
executable is `build-snesrecomp/Release/DKC2Recomp.exe`; the portable SDL host
is `build-snesrecomp/Release/DKC2RecompSDL.exe`.

Use `versions/Version NN` for play testing. Each folder is an immutable,
self-described snapshot with only the two executables, launcher assets,
documentation, and `VERSION.txt`. The manifest identifies the source branch,
commit, dirty state, creation time, and executable hashes.

## Why the old build folders exist

Earlier milestones used a new CMake directory for isolation while testing
different generators, toolchains, upstream baselines, and reconciliation
attempts. This was useful during investigation but left several similarly named
folders:

- `build/` and `build-verify/` are early Visual Studio verification trees.
- `build-reconcile/` contains a temporary nested checkout used during the
  public-repository reconciliation; it is not the active source repository.
- `build-upstream-baseline*` are disposable upstream-comparison trees made with
  different Python/toolchain environments.
- `build-snesrecomp/` is the current, canonical Visual Studio Release tree.

All of these are ignored by Git. Except for `build-snesrecomp`, they are
historical compiler or comparison output and may be deleted after no process is
using them. Deleting them does not delete source, Git history, the private ROM,
generated source under `generated/`, normal SRAM, or numbered test versions.

## Routine workflow

Configure and build in the canonical workspace:

```powershell
cmake -S . -B build-snesrecomp -DDKC2_ROM="C:\private\dkc2.smc"
cmake --build build-snesrecomp --config Release
ctest --test-dir build-snesrecomp -C Release --output-on-failure
```

After the source is committed and the complete test gate passes, create the
next manual-test snapshot:

```powershell
.\scripts\create_windows_version.ps1
```

Never copy files manually into an older numbered folder. The script discovers
the highest existing number, creates the next one through a temporary staging
folder, refuses overwrites, and rejects ROMs, saves, generated code, runtime
configuration, captures, logs, diagnostics, and unrelated executables.

## Naming policy

- Do not introduce another permanent `build-*` name for normal work.
- Temporary investigations should use `_scratch/<purpose>` or an external
  temporary directory and be removed after their result is recorded.
- Do not place testable builds in compiler directories or the repository root.
- Do not put ROMs or persistent saves in a numbered version folder.
- Git commits identify source versions; `Version NN` identifies local test
  handoffs. They solve different problems and both are recorded in
  `VERSION.txt`.

## Safe consolidation boundary

Keep the repository root, `snesrecomp/`, `recomp-ui/`, `generated/`,
`build-snesrecomp/`, and `versions/`. The other existing `build*` directories
are disposable only after the current canonical build and its tests are
confirmed. This document deliberately does not automate deletion: build trees
can contain an investigator's unrecorded logs, so cleanup remains an explicit
local choice.
