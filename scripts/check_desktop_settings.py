"""Check that desktop startup, pause overlay, and restart preserve settings.

Requires the owner's external supported ROM. All host output is isolated in
an OS temporary directory; no ROM, save, or game pixels are written to Git.
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", type=Path, required=True)
    parser.add_argument("--sdl", type=Path, help="Optional portable SDL host executable")
    parser.add_argument("--rom", type=Path, required=True)
    args = parser.parse_args()
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("DKC2_", "SNESRECOMP_"))}
    env.update(DKC2_DESKTOP_TEST_HIDDEN="1", DKC2_DESKTOP_TEST_FRAMES="65",
               DKC2_DESKTOP_TEST_OVERLAY="1", DKC2_DESKTOP_DISABLE_SRAM="1",
               SNESRECOMP_NO_LAUNCHER="1")
    hosts = [("WGL", args.native, 1), ("GDI fallback", args.native, 0)]
    if args.sdl:
        hosts.append(("SDL", args.sdl, 1))
    for label, executable, renderer in hosts:
        expected = dict(Renderer=renderer, Upscaler=2, ReconstructMode=4,
                        ReconstructStrength=73, ReconstructSoftness=29,
                        ReconstructShading=81, HapticsEnabled=0,
                        AspectIndex=3, ScreenKind=3, WidescreenEdge=1,
                        CoopMode=1, Player1Source=2, Player2Source=2,
                        Player1Device="030044f05e040000e002000000007200",
                        Player2Device="03009761c82d00000161000000000000",
                        Display=1, CrtPreset=3, CrtScanlines=63, CrtSharpness=91,
                        CrtMask=3, CrtMaskStrength=42, CrtGlow=71,
                        CrtHalation=68, CrtCurvature=21)
        with tempfile.TemporaryDirectory(prefix="DKC2-settings-") as folder:
            # Launcher paths are anchored beside the executable on Windows.
            runtime = Path(folder) / executable.name
            shutil.copy2(executable, runtime)
            for library in executable.parent.glob("*.dll"):
                shutil.copy2(library, Path(folder) / library.name)
            config = Path(folder) / "launcher.cfg"
            config.write_text("SkipLauncher=1\nEnableAudio=0\n" + "".join(
                f"{key}={value}\n" for key, value in expected.items()),
                encoding="utf-8")
            for launch in range(2):
                run = subprocess.run([str(runtime),
                                      str(args.rom.resolve())],
                                     cwd=folder, env=env, capture_output=True,
                                     text=True, timeout=45)
                if run.returncode or "result=desktop_completed frames=65" not in run.stdout:
                    raise RuntimeError(f"{label} launch {launch + 1} failed: "
                                       f"{run.returncode}\n{run.stdout}\n{run.stderr}")
                saved = dict(line.split("=", 1) for line in
                             config.read_text(encoding="utf-8").splitlines()
                             if "=" in line)
                for key, value in expected.items():
                    if saved.get(key) != str(value):
                        raise AssertionError(f"{label} launch {launch + 1}: "
                                             f"{key} expected {value}, got {saved.get(key)}")
            print(f"{label}: reconstruction, CRT, rumble, aspect and input preferences "
                  "and both device GUIDs preserved through pause and two launches", flush=True)


if __name__ == "__main__":
    main()
