# Cross-platform desktop foundation

## Current status

The game/runtime code and the new `dkc2_snesrecomp_sdl` gameplay host are
written for Windows, Linux, and macOS. The SDL2 host provides the same minimum
playable surface on each platform:

- accelerated, resizable 4:3 video with nearest or bilinear scaling;
- Raw, CRT, Composite, and Trinitron screen-color modes;
- keyboard and two hot-pluggable SDL game controllers;
- queued 32,040 Hz signed-16 stereo audio;
- SRAM, slot-0 save/load states, 3x fast-forward, and bounded rewind;
- the shared `recomp-ui` launcher; and
- the FPS window-title readout.

This is a portability foundation, not yet a three-platform release claim. The
SDL target compiles and completes its private-ROM integration test on Windows.
Linux and macOS still require native compiler/runtime acceptance, packaging,
and visible controller/audio testing on those operating systems.

The established Win32 `dkc2_snesrecomp_desktop` host remains in place during
that acceptance work. It is not silently replaced by the SDL host.

## Private source generation

The portable generator uses only Python, Rust/Cargo, and the pinned
`snesrecomp` submodule:

```sh
python3 scripts/generate_snesrecomp.py --rom /private/path/dkc2.sfc
```

The PowerShell entry point remains supported on Windows. Both verify the exact
4 MiB payload and SHA-256 before generating ignored C under
`generated/snesrecomp/`. Neither copies the ROM into the repository.

## Linux build

Install a C/C++ compiler, CMake, Python 3, Rust/Cargo, OpenGL development
headers, and SDL2 development files. For example, Debian/Ubuntu package names
include `build-essential`, `cmake`, `ninja-build`, `python3`, `cargo`,
`libsdl2-dev`, and `libgl1-mesa-dev`.

```sh
git submodule update --init --recursive
python3 scripts/generate_snesrecomp.py --rom /private/path/dkc2.sfc
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKC2_BUILD_SNESRECOMP=ON
cmake --build build-release --target dkc2_snesrecomp_sdl
./build-release/DKC2Recomp /private/path/dkc2.sfc
```

## macOS build

Install the Xcode command-line tools plus CMake, Python 3, Rust/Cargo, and
SDL2. Homebrew can provide the non-Xcode dependencies.

```sh
git submodule update --init --recursive
python3 scripts/generate_snesrecomp.py --rom /private/path/dkc2.sfc
cmake -S . -B build-release -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DDKC2_BUILD_SNESRECOMP=ON
cmake --build build-release --target dkc2_snesrecomp_sdl
./build-release/DKC2Recomp /private/path/dkc2.sfc
```

CMake prefers an installed SDL2 package. If none is found,
`DKC2_FETCH_SDL2=ON` (the default) fetches the pinned SDL 2.30.9 source. Set it
to `OFF` for fully offline/system-package-only builds.

## Diagnostics portability

Both desktop hosts write the same rolling JSON report and allowlisted support
bundle format. Windows unhandled exceptions include a minidump. On Linux and
macOS, a fatal-signal handler writes only a small marker using signal-safe
operations; the next launch turns it into the complete bundle. Native Linux
and macOS acceptance must deliberately exercise this recovery path as well as
normal clean-exit reporting. Reports exclude ROM paths/data and all saves, but
include loaded-module paths and basic machine/OS information.

## Native acceptance required

Before checking off either operating system, run these checks on that system:

1. Configure and compile a clean Release build with strict project warnings.
2. Run the complete public test suite.
3. Run the private hidden SDL smoke test with video, audio, fast-forward, and a
   real rewind restore.
4. Watch and listen through at least one complete attract cycle.
5. Test keyboard plus two representative controllers, hot-plugging, SRAM,
   state save/load, fullscreen, resize, and every screen-color mode.
6. Produce a source-clean package that contains no ROM, generated C, saves, or
   captures.
7. Exercise clean requested diagnostics and the platform crash path; confirm
   that the bundle allowlist contains no ROM or save artifact.

Native `.app`/code-signing work on macOS and AppImage/Flatpak-style packaging
on Linux remain separate roadmap tasks. Save/config migration to the platform
user-data directory should be completed before installing outside a writable
portable folder.
