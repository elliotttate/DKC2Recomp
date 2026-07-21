# DKC2Recomp

> This recompilation is a byproduct of developing
> [snesrecomp](https://github.com/mstan/snesrecomp): the games are the proving
> ground, while the reusable framework is the larger goal. This is an early
> preview, not an official port. Expect rough edges and please report any
> reproducible gameplay, video, or audio regressions.

Static recompilation of *Donkey Kong Country 2: Diddy's Kong Quest* for SNES
into a native Windows application, using the `snesrecomp` framework.

The 65816 game program is translated to native C where analysis can prove an
exact entry state. The remaining CPU variants use the shared 65816 interpreter
as a correctness fallback. SNES hardware outside the main CPU—the PPU,
SPC700/S-DSP, DMA/HDMA, controllers, and cartridge mapping—is modeled by the
shared runtime.

## Quick start

1. Download `DKC2Recomp-windows-x64-v0.0.1.zip` from
   [Releases](../../releases) and extract the complete archive.
2. Run `DKC2Recomp.exe`.
3. In the Dear ImGui launcher, select your own legally obtained North American
   v1.0 ROM and choose **Play**.

The selected external path is remembered in `rom.cfg` beside the executable.
The ROM is never copied into the release. Saves are written to
`saves/save.srm`, with the previous clean save retained as `save.srm.bak`.

## Supported ROM

The launcher and runtime verify the ROM after removing an optional 512-byte
copier header.

| Property | Expected value |
| --- | --- |
| Size | 4,194,304 bytes (headerless body) |
| CRC32 | `006364DB` |
| SHA-256 | `35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633` |
| Internal name | `DIDDY'S KONG QUEST` |
| Region/version | North America v1.0 |

You must supply your own lawfully obtained dump. ROMs, extracted in-game
graphics, music, level data, save files, and generated ROM-derived C are not
distributed in this repository or its release archives. The launcher includes
North American retail cover art solely to identify the supported game and
region; its source and copyright notice are documented in
`recomp/launcher/README.md`.

## Controls

| SNES control | Keyboard |
| --- | --- |
| D-Pad | Arrow keys |
| A | X |
| B | Z |
| X | S |
| Y | A |
| L | Q |
| R | W |
| Start | Enter |
| Select | Shift |
| Rewind | Hold 1 |
| Fast-forward | Hold 2 |
| Save state (slot 0) | F5 |
| Load state (slot 0) | F9 |
| Toggle performance log | F |
| Quit | Escape |

The first connected XInput controller is detected automatically. The left
trigger rewinds and the right trigger fast-forwards.

Save states are stored beside the executable as `saves/dkc20.sav`. They are
separate from the cartridge SRAM files used for normal in-game saves.

The game window title reports the measured presentation rate once per second.
Press `F` to write per-phase main-thread timings to `performance.log` beside
the executable; press it again to stop. The current gameplay presenter uses
GDI, which has no GPU timestamp interface, so the log explicitly reports GPU
time as unavailable instead of inventing a value.

## Static recompilation coverage

The 0.0.1 sound analysis profile emits 3,425 of 3,467 exact CPU-mode variants
as static C (98.79%). The other 42 variants use the interpreter. This is a
compile-time structural count, not a percentage of dynamically executed CPU
instructions.

The generated C remains ignored because it is derived from the user's ROM.
Only source-owned configuration and structural metadata are committed.

## Building from source

Prerequisites on Windows:

- CMake and Ninja;
- a MinGW-w64 C/C++ toolchain with SDL2;
- Python 3.9+; and
- Rust/Cargo for the native whole-program analyzer.

```powershell
git clone --recurse-submodules https://github.com/mstan/DKC2Recomp
cd DKC2Recomp

.\scripts\generate_snesrecomp.ps1 -Rom "C:\private\dkc2.sfc"

$env:Path = "C:\msys64\mingw64\bin;$env:Path"
cmake -S . -B build-release -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DDKC2_BUILD_SNESRECOMP=ON `
  -DSDL2_DIR=C:\msys64\mingw64\lib\cmake\SDL2
cmake --build build-release --target dkc2_snesrecomp_desktop
```

Project-owned desktop and headless code is compiled for speed in Release
builds (`-O3` with GCC/Clang and `/O2`, MSVC's maximum speed preset, with
MSVC). To embed a private Windows `.ico` without adding it to Git, configure
with `-DDKC2_DESKTOP_ICON="C:\private\dkc2.ico"`.

Package the source-clean Windows release with:

```powershell
.\scripts\make_release.ps1 -Version 0.0.1
```

The packaging script allowlists the documented launcher cover and refuses ROM,
save, generated, screenshot, and audio artifacts. Its output is written under
the ignored `release-stage/` directory.

## Repository layout

- `recomp/` — source-owned CFG and structural metadata.
- `runner/` — DKC2 host adapters, input, presentation, rewind, and ROM checks.
- `snesrecomp/` — pinned shared recompiler and SNES runtime.
- `recomp-ui/` — pinned shared Dear ImGui launcher.
- `docs/RECONCILIATION.md` — provenance and disposition of the pre-upstream
  working tree.
- `scripts/` — regeneration, testing, packaging, and launch helpers.
- `generated/`, `private/`, and build directories — ignored local artifacts.

## Acknowledgements

- [H4v0c21's DKC2 disassembly](https://github.com/H4v0c21/DKC2-disassembly)
  provides the independently verified symbols and structural boundaries used
  during analysis. No disassembly or ROM-derived assets are redistributed.
- [snesrecomp](https://github.com/mstan/snesrecomp) provides the static
  recompiler, interpreter fallback, and shared SNES runtime.
- [recomp-ui](https://github.com/mstan/recomp-ui) provides the shared Dear
  ImGui launcher.
- The SNES hardware implementation derives from LakeSnes, with additional
  algorithms credited to Snes9x in the relevant source files.

## License

Project-owned source is available under the [MIT License](LICENSE). Vendored
dependencies and submodules retain their own licenses. Nintendo and Rare own
their respective game content and trademarks; no license in this repository
grants rights to that content.
