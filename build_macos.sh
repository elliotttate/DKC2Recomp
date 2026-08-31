#!/usr/bin/env bash
set -euo pipefail

if [[ "$(uname -s)" != "Darwin" ]]; then
  echo "error: build_macos.sh must run on macOS" >&2
  exit 2
fi

repo_dir="$(cd "$(dirname "$0")" && pwd)"
build_dir="$repo_dir/build/macos"
rom_path="${1:-${DKC2_ROM:-}}"

for tool in cmake ninja sdl2-config python3 install_name_tool otool codesign; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "error: missing required tool: $tool" >&2
    echo "Install CMake, Ninja, and SDL2 (for example: brew install cmake ninja sdl2)." >&2
    exit 2
  fi
done

requested_target="${DKC2_MACOS_DEPLOYMENT_TARGET:-14.0}"
system_sdl="$(sdl2-config --prefix)/lib/libSDL2-2.0.0.dylib"
if [[ ! -f "$system_sdl" ]]; then
  echo "error: unable to locate the SDL2 dynamic library at $system_sdl" >&2
  exit 2
fi
sdl_target="$(otool -l "$system_sdl" | awk '
  $1 == "cmd" && $2 == "LC_BUILD_VERSION" { build_version = 1; next }
  build_version && $1 == "minos" { print $2; exit }
')"
deployment_target="$(python3 - "$requested_target" "$sdl_target" <<'PY'
import sys

def version(value):
    return tuple(int(part) for part in value.split("."))

requested, dependency = sys.argv[1:3]
print(dependency if dependency and version(dependency) > version(requested)
      else requested)
PY
)"
if [[ "$deployment_target" != "$requested_target" ]]; then
  echo "macOS deployment target raised from $requested_target to $deployment_target for bundled SDL2"
fi

if ! compgen -G "$repo_dir/generated/snesrecomp/*.c" >/dev/null; then
  if [[ -z "$rom_path" ]]; then
    echo "error: private generated sources are missing" >&2
    echo "usage: ./build_macos.sh '/path/to/DKC2-USA-v1.0.sfc'" >&2
    exit 2
  fi
  python3 "$repo_dir/scripts/generate_snesrecomp.py" --rom "$rom_path"
fi

cmake -E remove_directory "$build_dir/DKC2Recomp.app"
cmake -S "$repo_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_DEPLOYMENT_TARGET="$deployment_target" \
  -DCMAKE_PREFIX_PATH="$(sdl2-config --prefix)" \
  -DDKC2_BUILD_SNESRECOMP=ON \
  -DDKC2_FETCH_SDL2=OFF \
  -DDKC2_ROM="$rom_path"
cmake --build "$build_dir" \
  --target dkc2_snesrecomp_sdl dkc2_snesrecomp_headless --parallel

app="$build_dir/DKC2Recomp.app"
executable="$app/Contents/MacOS/DKC2Recomp"
frameworks="$app/Contents/Frameworks"
linked_sdl="$(otool -L "$executable" | awk '/libSDL2.*dylib/ {print $1; exit}')"
if [[ -z "$linked_sdl" || ! -f "$linked_sdl" ]]; then
  echo "error: unable to resolve the linked SDL2 dynamic library" >&2
  exit 3
fi
sdl_name="$(basename "$linked_sdl")"
sdl_bundle="$frameworks/$sdl_name"
mkdir -p "$frameworks"
cp -fL "$linked_sdl" "$sdl_bundle"
chmod u+w "$sdl_bundle"
install_name_tool -id "@rpath/$sdl_name" "$sdl_bundle"
install_name_tool -change "$linked_sdl" \
  "@executable_path/../Frameworks/$sdl_name" "$executable"

codesign --force --sign - "$sdl_bundle"
codesign --force --deep --sign - "$app"
codesign --verify --deep --strict "$app"
touch "$app"

# Older local instructions used build-macos-native/DKC2Recomp.app. Keep that
# path as a symlink to the canonical app so a saved Finder alias cannot reopen
# a stale executable and LaunchServices cannot discover a second bundle with
# the same identity.
compat_app="$repo_dir/build-macos-native/DKC2Recomp.app"
launch_services="/System/Library/Frameworks/CoreServices.framework/Frameworks/LaunchServices.framework/Support/lsregister"
if [[ -d "$(dirname "$compat_app")" ]]; then
  if [[ -x "$launch_services" && -d "$compat_app" && ! -L "$compat_app" ]]; then
    "$launch_services" -u "$compat_app" >/dev/null 2>&1 || true
  fi
  if [[ -L "$compat_app" ]]; then
    cmake -E rm -f "$compat_app"
  else
    cmake -E remove_directory "$compat_app"
  fi
  cmake -E create_symlink "$app" "$compat_app"
  codesign --verify --deep --strict "$compat_app"
fi
if [[ -x "$launch_services" ]]; then
  # Keep diagnostics and the compatibility copy directly launchable by path,
  # but remove their duplicate bundle registrations so `open -b` resolves to
  # the canonical signed app rather than an arbitrary older local build.
  for noncanonical_app in \
      "$repo_dir/build-macos-diagnostics/DKC2Recomp.app" \
      "$compat_app"; do
    if [[ -d "$noncanonical_app" ]]; then
      "$launch_services" -u "$noncanonical_app" >/dev/null 2>&1 || true
    fi
  done
  # LaunchServices applies unregister requests asynchronously. Give those
  # targeted removals one bounded moment to settle before making the canonical
  # bundle the final registration.
  sleep 1
  "$launch_services" -f "$app" >/dev/null
fi

echo "MACOS_BUILD_OK"
echo "$app"
