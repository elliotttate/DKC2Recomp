#!/usr/bin/env python3
"""Replay the private Bramble fixture and verify square-layout widescreen."""

from __future__ import annotations

import argparse
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
    parser.add_argument("--frame", type=int, default=1600)
    parser.add_argument("--camera-x", type=int, default=2653)
    parser.add_argument("--camera-y", type=int, default=2456)
    parser.add_argument("--keep-output", type=Path)
    return parser.parse_args()


def validate_report(report: dict, args: argparse.Namespace) -> None:
    if report.get("schema") != "dkc2-widescreen-diagnostic-v1":
        raise RuntimeError("unexpected diagnostic report schema")
    if report.get("frame") != args.frame:
        raise RuntimeError(
            f"diagnostic frame is {report.get('frame')}, expected {args.frame}")

    layers = report.get("layers", {})
    if set(layers) != {"composite", "bg1"}:
        raise RuntimeError(
            f"diagnostic layers are {sorted(layers)}, expected composite and bg1")
    game_state = layers["composite"].get("game_state", {})
    if game_state.get("level_number") != 0x002E:
        raise RuntimeError(
            f"level is {game_state.get('level_number')}, expected Bramble $002E")
    expected_camera = {"x": args.camera_x, "y": args.camera_y}
    if game_state.get("camera") != expected_camera:
        raise RuntimeError(
            f"camera is {game_state.get('camera')}, expected {expected_camera}")

    profile = report.get("screen_profile", {})
    expected_profile = {
        "kind": "standard_rolling_terrain",
        "terrain_owner": "bg1",
        "level_map_layout": "row_major_square_192_byte_stride",
        "safe_for_object_widening": True,
    }
    for key, value in expected_profile.items():
        if profile.get(key) != value:
            raise RuntimeError(
                f"screen profile {key} is {profile.get(key)!r}, expected {value!r}")

    for layer_name in ("composite", "bg1"):
        regions = layers[layer_name].get(
            "image_metrics", {}).get("regions", {})
        for region_name in ("left_margin", "right_margin"):
            pixels = regions.get(region_name, {}).get("non_backdrop_pixels", 0)
            if pixels <= 0:
                raise RuntimeError(
                    f"{layer_name} {region_name} is empty; widescreen regressed")
        events = layers[layer_name].get("runtime_events", {})
        blocking = {
            key: int(events.get(key, 0))
            for key in ("unresolved_abandons", "interpreter_caps", "fatal_errors")
            if int(events.get(key, 0)) != 0
        }
        if blocking:
            raise RuntimeError(
                f"{layer_name} replay has blocking events: {blocking}")

    shadow = layers["composite"].get(
        "widescreen_shadow", {}).get("layers", [])
    if not shadow or not shadow[0].get("active"):
        raise RuntimeError("Bramble BG1 widescreen shadow is inactive")
    if int(shadow[0].get("eastHit", 0)) <= 0:
        raise RuntimeError("Bramble BG1 produced no right-margin terrain hits")
    if report.get("findings"):
        raise RuntimeError(f"diagnostic findings remain: {report['findings']}")


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
            raise RuntimeError(
                f"required private regression input is missing: {path}")

    if args.keep_output:
        output = args.keep_output.resolve()
        if output.exists():
            raise RuntimeError(f"refusing to overwrite private evidence: {output}")
        report = run_capture(args, output)
        validate_report(report, args)
        print(f"widescreen_bramble_route=passed report={output / 'report.json'}")
    else:
        with tempfile.TemporaryDirectory(
                prefix="dkc2-widescreen-bramble-route-") as temporary:
            output = Path(temporary) / "capture"
            report = run_capture(args, output)
            validate_report(report, args)
        print("widescreen_bramble_route=passed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError) as error:
        print(f"widescreen_bramble_route=failed error={error}", file=sys.stderr)
        raise SystemExit(1)
