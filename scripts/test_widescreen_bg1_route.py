#!/usr/bin/env python3
"""Replay the private Pirate Panic fixture and check the late BG1 margin."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--capture-tool", required=True, type=Path)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--input-recording", required=True, type=Path)
    parser.add_argument("--sram", required=True, type=Path)
    parser.add_argument("--frame", type=int, default=6750)
    parser.add_argument("--camera-x", type=int, default=7103)
    parser.add_argument("--camera-y", type=int, default=298)
    parser.add_argument("--expected-upper-left-non-backdrop", type=int,
                        default=0)
    parser.add_argument("--expected-bg1-sha256")
    parser.add_argument(
        "--keep-output", type=Path,
        help="retain private capture evidence here instead of using a temporary directory")
    return parser.parse_args()


def validate_report(report: dict, output: Path, frame: int,
                    camera_x: int, camera_y: int,
                    expected_upper_left: int,
                    expected_bg1_sha256: str | None) -> None:
    if report.get("schema") != "dkc2-widescreen-diagnostic-v1":
        raise RuntimeError("unexpected diagnostic report schema")
    if report.get("frame") != frame:
        raise RuntimeError(
            f"diagnostic frame is {report.get('frame')}, expected {frame}")

    layers = report.get("layers", {})
    if set(layers) != {"composite", "bg1"}:
        raise RuntimeError(
            f"diagnostic layers are {sorted(layers)}, expected composite and bg1")
    camera = layers["composite"].get("game_state", {}).get("camera")
    expected_camera = {"x": camera_x, "y": camera_y}
    if camera != expected_camera:
        raise RuntimeError(
            f"camera is {camera}, expected {expected_camera}; "
            "the private fixture may not match this regression")

    regions = layers["bg1"].get("image_metrics", {}).get("regions", {})
    upper_left = regions.get("upper_left_margin", {})
    bad_pixels = upper_left.get("non_backdrop_pixels")
    if bad_pixels != expected_upper_left:
        raise RuntimeError(
            f"late Pirate Panic BG1 upper-left margin contains "
            f"{bad_pixels} non-backdrop pixels; expected "
            f"{expected_upper_left}")
    native_pixels = regions.get("native_view", {}).get(
        "non_backdrop_pixels", 0)
    if native_pixels <= 0:
        raise RuntimeError(
            "BG1 native view is empty; the margin check is not meaningful")

    if expected_bg1_sha256:
        relative_image = layers["bg1"].get("image")
        image = output / relative_image if relative_image else None
        if image is None or not image.is_file():
            raise RuntimeError("BG1 image required for the hash check is missing")
        actual_hash = hashlib.sha256(image.read_bytes()).hexdigest()
        if actual_hash.lower() != expected_bg1_sha256.lower():
            raise RuntimeError(
                f"BG1 image SHA-256 is {actual_hash}, expected "
                f"{expected_bg1_sha256}")

    for name, layer in layers.items():
        events = layer.get("runtime_events", {})
        blocking = {
            key: int(events.get(key, 0))
            for key in ("unresolved_abandons", "interpreter_caps", "fatal_errors")
            if int(events.get(key, 0)) != 0
        }
        if blocking:
            raise RuntimeError(f"{name} replay has blocking events: {blocking}")


def run_capture(args: argparse.Namespace, output: Path) -> dict:
    command = [
        sys.executable,
        str(args.capture_tool.resolve()),
        "--executable", str(args.executable.resolve()),
        "--rom", str(args.rom.resolve()),
        "--input-recording", str(args.input_recording.resolve()),
        "--sram", str(args.sram.resolve()),
        "--frame", str(args.frame),
        "--layers", "composite,bg1",
        "--output-dir", str(output.resolve()),
        "--timeout", "240",
    ]
    subprocess.run(command, check=True)
    report_path = output / "report.json"
    if not report_path.is_file():
        raise RuntimeError(f"capture did not produce {report_path}")
    return json.loads(report_path.read_text(encoding="utf-8"))


def main() -> int:
    args = parse_args()
    for path in (
            args.capture_tool, args.executable, args.rom,
            args.input_recording, args.sram):
        if not path.is_file():
            raise RuntimeError(f"required private regression input is missing: {path}")

    if args.keep_output:
        output = args.keep_output.resolve()
        if output.exists():
            raise RuntimeError(f"refusing to overwrite private evidence: {output}")
        report = run_capture(args, output)
        validate_report(
            report, output, args.frame, args.camera_x, args.camera_y,
            args.expected_upper_left_non_backdrop,
            args.expected_bg1_sha256)
        print(f"widescreen_bg1_route=passed report={output / 'report.json'}")
    else:
        with tempfile.TemporaryDirectory(
                prefix="dkc2-widescreen-bg1-route-") as temporary:
            output = Path(temporary) / "capture"
            report = run_capture(args, output)
            validate_report(
                report, output, args.frame, args.camera_x, args.camera_y,
                args.expected_upper_left_non_backdrop,
                args.expected_bg1_sha256)
        print("widescreen_bg1_route=passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"widescreen_bg1_route=failed error={error}", file=sys.stderr)
        raise SystemExit(1)
