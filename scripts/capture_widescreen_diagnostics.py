#!/usr/bin/env python3
"""Capture a repeatable DKC2 widescreen layer/object evidence bundle."""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
import html
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import sys
import time

import capture_tcp_screenshot as tcp


LAYER_MASKS = {
    "composite": 0x1F,
    "bg1": 0x01,
    "bg2": 0x02,
    "bg3": 0x04,
    "bg4": 0x08,
    "obj": 0x10,
}
DEFAULT_LAYERS = ("composite", "bg1", "bg2", "bg3", "obj")


def _number(value, default=None):
    if isinstance(value, int):
        return value
    if isinstance(value, str):
        try:
            return int(value, 0)
        except ValueError:
            return default
    return default


def classify_dkc2_screen(game_state: dict, ppu: dict) -> dict:
    """Describe the live screen policy using only directly observed state.

    Unknown configurations deliberately remain unclassified. This lets the
    diagnostic work across levels without applying one level's assumptions to
    a different type of screen.
    """
    configuration = game_state.get("screen_configuration") or {}
    game_sub_mode = _number(configuration.get("game_sub_mode"), -1)
    horizontal_sub_modes = {
        0x01, 0x06, 0x07, 0x09, 0x0D, 0x0E,
        0x0F, 0x12, 0x15, 0x18, 0x1A, 0x1F,
    }
    vertical_sub_modes = {0x08, 0x0C, 0x16, 0x1E}
    square_sub_modes = {0x10}
    if game_sub_mode in horizontal_sub_modes:
        level_map_layout = "column_major_horizontal"
    elif game_sub_mode in vertical_sub_modes:
        level_map_layout = "row_major_vertical"
    elif game_sub_mode in square_sub_modes:
        level_map_layout = "row_major_square_96_byte_stride"
    elif game_sub_mode >= 0:
        level_map_layout = "square_or_special"
    else:
        level_map_layout = "unknown"
    mode = _number(ppu.get("bgmode"), -1)
    target = _number(configuration.get("terrain_vram_word_address"))
    enabled = _number((ppu.get("screenEnabled") or [0])[0], 0)
    bg_sc = ppu.get("bgXsc") or []
    candidates = []
    for index in range(min(2, len(bg_sc))):
        register = _number(bg_sc[index])
        if register is None:
            continue
        base = (register & 0xFC) << 8
        if enabled & (1 << index):
            candidates.append({
                "layer": f"bg{index + 1}",
                "tilemap_word_address": f"0x{base:04x}",
                "matches_terrain_target": target == base,
            })
    owners = [item["layer"] for item in candidates
              if item["matches_terrain_target"]]
    owner = owners[0] if len(owners) == 1 else None
    level = _number(game_state.get("level_number"), 0)
    bg3_base = None
    if len(bg_sc) > 2 and _number(bg_sc[2]) is not None:
        bg3_base = (_number(bg_sc[2]) & 0xFC) << 8
    known_bg3_repeat = level == 0x002C and bg3_base == 0x6C00

    if mode != 1:
        kind = "special_or_bounded"
        reason = f"PPU mode {mode} has no proven general widescreen policy"
    elif owner:
        kind = "standard_rolling_terrain"
        reason = (
            f"{owner.upper()} is enabled and its live tilemap base matches "
            "DKC2's terrain destination")
    else:
        kind = "unclassified_mode1"
        reason = (
            "no unique enabled BG1/BG2 tilemap base matches DKC2's live "
            "terrain destination")
    return {
        "kind": kind,
        "terrain_owner": owner,
        "level_map_layout": level_map_layout,
        "terrain_candidates": candidates,
        "bg3_policy": (
            "rendered_scanline_repeat" if known_bg3_repeat
            else "bounded_unclassified"),
        "safe_for_object_widening": (
            kind == "standard_rolling_terrain" and
            level_map_layout not in {"square_or_special", "unknown"}),
        "reason": reason,
        "raw_screen_configuration": configuration,
    }


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--frame", required=True, type=int)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--input-recording", type=Path)
    parser.add_argument("--sram", type=Path)
    parser.add_argument("--savestate", type=Path)
    parser.add_argument("--port", type=int, default=4382)
    parser.add_argument("--timeout", type=float, default=120.0)
    parser.add_argument(
        "--scan-oam-margins", action="store_true",
        help=("collect render-consumed OAM samples in both widescreen "
              "margins throughout the replay"))
    parser.add_argument(
        "--function-watch",
        help="capture the first trace entry for this generated function")
    parser.add_argument(
        "--layers", default=",".join(DEFAULT_LAYERS),
        help="comma-separated subset of composite,bg1,bg2,bg3,bg4,obj")
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def read_bmp_margin_metrics(path: Path, margin_width: int = 43) -> dict:
    data = path.read_bytes()
    if len(data) < 54 or data[:2] != b"BM":
        raise RuntimeError(f"not a supported BMP: {path}")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    width = struct.unpack_from("<i", data, 18)[0]
    raw_height = struct.unpack_from("<i", data, 22)[0]
    bits = struct.unpack_from("<H", data, 28)[0]
    compression = struct.unpack_from("<I", data, 30)[0]
    if width <= 0 or raw_height == 0 or bits != 24 or compression != 0:
        raise RuntimeError(
            f"expected an uncompressed 24-bit BMP, got "
            f"{width}x{raw_height} {bits}bpp compression={compression}")
    height = abs(raw_height)
    row_stride = ((width * 3 + 3) // 4) * 4
    if pixel_offset + row_stride * height > len(data):
        raise RuntimeError(f"truncated BMP pixel data: {path}")
    left_end = min(margin_width, width)
    right_start = max(width - margin_width, 0)
    regions = {
        "left_margin": [0, left_end, 0, height],
        "native_view": [left_end, right_start, 0, height],
        "right_margin": [right_start, width, 0, height],
        # A semantic regression region for the late Pirate Panic vertical
        # camera transition. Logical Y is top-down even when BMP storage is
        # bottom-up.
        "upper_left_margin": [0, left_end, 0, min(64, height)],
    }
    image_colors = Counter()
    for row in range(height):
        base = pixel_offset + row * row_stride
        for x in range(width):
            image_colors[bytes(data[base + x * 3:base + x * 3 + 3])] += 1
    backdrop, backdrop_pixels = image_colors.most_common(1)[0]
    output = {
        "width": width,
        "height": height,
        # Layer-isolated SNES frames retain the fixed/backdrop color. Measuring
        # only black would therefore call an empty OBJ image completely full.
        "dominant_backdrop_bgr": backdrop.hex(),
        "dominant_backdrop_pixels": backdrop_pixels,
        "regions": {},
    }
    for name, (start, end, top, bottom) in regions.items():
        non_black = 0
        non_backdrop = 0
        colors = set()
        for logical_y in range(top, bottom):
            storage_row = (
                height - 1 - logical_y if raw_height > 0 else logical_y)
            base = pixel_offset + storage_row * row_stride
            for x in range(start, end):
                color = bytes(data[base + x * 3:base + x * 3 + 3])
                if color != b"\0\0\0":
                    non_black += 1
                    if len(colors) < 4096:
                        colors.add(color)
                if color != backdrop:
                    non_backdrop += 1
        pixels = (end - start) * (bottom - top)
        output["regions"][name] = {
            "bounds": {
                "x": [start, end],
                "y": [top, bottom],
            },
            "pixels": pixels,
            "non_black_pixels": non_black,
            "non_black_fraction": non_black / pixels if pixels else 0.0,
            "non_backdrop_pixels": non_backdrop,
            "non_backdrop_fraction": non_backdrop / pixels if pixels else 0.0,
            "sampled_unique_non_black_colors": len(colors),
        }
    return output


def capture_process(
        executable: Path, rom: Path, frame: int, output_dir: Path,
        layer: str, environment: dict[str, str], port: int,
        timeout: float, capture_private_state: bool,
        scan_oam_margins: bool, function_watch: str | None) -> dict:
    layer_dir = output_dir / layer
    layer_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = layer_dir / "stdout.log"
    stderr_path = layer_dir / "stderr.log"
    screenshot_path = layer_dir / f"frame_{frame:06d}_{layer}.bmp"
    deadline = time.monotonic() + timeout
    run_environment = os.environ.copy()
    run_environment.update(environment)
    run_environment["DKC2_WIDESCREEN"] = "1"
    run_environment["SNESRECOMP_TRACE_HOLD"] = "1"
    run_environment["SNESRECOMP_LAYER_MASK"] = str(LAYER_MASKS[layer])

    with stdout_path.open("w", encoding="utf-8") as stdout, \
            stderr_path.open("w", encoding="utf-8") as stderr:
        process = subprocess.Popen(
            [str(executable), str(rom), str(frame + 1)],
            cwd=str(executable.parent), env=run_environment,
            stdout=stdout, stderr=stderr)
        connection = None
        try:
            connection = tcp.connect(port, deadline)
            if function_watch:
                response = tcp.query(
                    connection, f"func_watch_arm {function_watch}")
                if response.get("ok") != 1:
                    raise RuntimeError(
                        f"could not arm function watch: {response}")
            margin_sprites_by_key = {}
            next_oam_scan = 0
            while True:
                history = tcp.query(connection, "history").get("history", {})
                newest = int(history.get("newest", -1))
                if scan_oam_margins and newest >= next_oam_scan:
                    tcp.collect_oam_margin_sprites(
                        tcp.query(connection, "oam_render_get 128 128"),
                        margin_sprites_by_key)
                    next_oam_scan = newest + 96
                if newest >= frame:
                    break
                if process.poll() is not None:
                    raise RuntimeError(
                        f"{layer} process exited before frame {frame}; "
                        f"see {stderr_path}")
                if time.monotonic() >= deadline:
                    raise TimeoutError(
                        f"{layer} did not reach frame {frame}; newest={newest}")
                time.sleep(0.02)

            ppu = tcp.query(connection, "get_ppu_state")
            shadow = tcp.query(connection, "ws_shadow_stats")
            oam_response = tcp.query(connection, "oam_render_get 1 128")
            oam = tcp.collect_oam_sprites(oam_response)
            if scan_oam_margins:
                tcp.collect_oam_margin_sprites(
                    tcp.query(connection, "oam_render_get 256 128"),
                    margin_sprites_by_key)
            function_watch_result = (
                tcp.query(connection, "func_watch_get")
                if function_watch else None)
            state = None
            if capture_private_state:
                wram_response = tcp.query(
                    connection, f"dump_frame_wram {frame} 0 131072")
                wram = bytes.fromhex(wram_response.get("hex", ""))
                if len(wram) != 0x20000:
                    raise RuntimeError(
                        f"WRAM snapshot is {len(wram)} bytes; expected 131072")
                (layer_dir / f"frame_{frame:06d}.wram").write_bytes(wram)
                vram_response = tcp.query(
                    connection, f"dump_frame_vram {frame} 0 65536")
                vram = bytes.fromhex(vram_response.get("hex", ""))
                if len(vram) != 0x10000:
                    raise RuntimeError(
                        f"VRAM snapshot is {len(vram)} bytes; expected 65536")
                (layer_dir / f"frame_{frame:06d}.vram").write_bytes(vram)
                state = tcp.decode_dkc2_wram(wram)

            screenshot_raw = tcp.query_raw(
                connection, f"screenshot {screenshot_path.resolve()}")
            if '"ok":true' not in screenshot_raw:
                raise RuntimeError(f"screenshot failed: {screenshot_raw}")
            tcp.query(connection, "continue")
            connection.close()
            connection = None
            remaining = max(deadline - time.monotonic(), 0.1)
            process.wait(timeout=remaining)
            if process.returncode != 0:
                raise RuntimeError(
                    f"{layer} process exited with {process.returncode}; "
                    f"see {stderr_path}")
            return {
                "mask": LAYER_MASKS[layer],
                "image": str(screenshot_path.relative_to(output_dir)),
                "image_metrics": read_bmp_margin_metrics(screenshot_path),
                "ppu": ppu,
                "widescreen_shadow": shadow,
                "rendered_oam": oam,
                "oam_margin_sprites": [
                    margin_sprites_by_key[key]
                    for key in sorted(margin_sprites_by_key)
                ],
                "function_watch": function_watch_result,
                "game_state": state,
                "stdout_log": str(stdout_path.relative_to(output_dir)),
                "stderr_log": str(stderr_path.relative_to(output_dir)),
                "runtime_events": scan_runtime_events(stderr_path),
            }
        finally:
            if connection is not None:
                try:
                    tcp.query(connection, "continue")
                except (ConnectionError, OSError, ValueError):
                    pass
                connection.close()
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


def scan_runtime_events(stderr_path: Path) -> dict:
    text = stderr_path.read_text(encoding="utf-8", errors="replace")
    markers = {
        "unresolved_stub_traps": "[UNRESOLVED-STUB TRAP HIT]",
        "unresolved_abandons": "[unresolved-abandon]",
        "interpreter_caps": "[interp_cap]",
        "fatal_errors": "[FATAL]",
    }
    return {name: text.count(marker) for name, marker in markers.items()}


def build_findings(layers: dict, screen_profile: dict | None = None) -> list[dict]:
    findings = []
    if screen_profile and screen_profile["kind"].startswith("unclassified"):
        findings.append({
            "stage": "screen_policy_unclassified",
            "evidence": screen_profile["reason"],
            "interpretation": (
                "Do not widen this screen by analogy. Capture its isolated "
                "layers and map its scroll method before changing runtime code."),
        })
    expected_backgrounds = {"bg1", "bg2", "bg3", "bg4"}
    if screen_profile:
        expected_backgrounds = set()
        owner = screen_profile.get("terrain_owner")
        if owner:
            expected_backgrounds.add(owner)
        if screen_profile.get("bg3_policy") == "rendered_scanline_repeat":
            expected_backgrounds.add("bg3")
    for name in expected_backgrounds:
        if name not in layers:
            continue
        regions = layers[name]["image_metrics"]["regions"]
        native = regions["native_view"]["non_backdrop_pixels"]
        for side in ("left_margin", "right_margin"):
            margin = regions[side]["non_backdrop_pixels"]
            if native and margin == 0:
                findings.append({
                    "stage": "background_load_or_render",
                    "layer": name,
                    "side": side,
                    "evidence": (
                        f"{name.upper()} has visible native-view pixels but "
                        f"zero non-black pixels in the {side.replace('_', ' ')}."),
                    "interpretation": (
                        "Inspect tilemap streaming/source reconstruction and "
                        "that layer's widened draw policy before presenter code."),
                })
    composite = layers.get("composite", {})
    game_state = composite.get("game_state") or {}
    game_margin = [
        sprite for sprite in game_state.get("active_sprites", [])
        if sprite["region"] in ("left_margin", "right_margin")
    ]
    oam_margin = [
        sprite for sprite in composite.get("rendered_oam", [])
        if sprite["region"] in ("left_margin", "right_margin")
    ]
    if game_margin and not oam_margin:
        findings.append({
            "stage": "object_render_or_cull",
            "evidence": (
                f"{len(game_margin)} active game sprite(s) project into a "
                "widescreen margin, but no render-consumed OAM entry does."),
            "interpretation": (
                "The object exists in WRAM; inspect sprite render bounds, "
                "display mode, render table membership, and OAM submission."),
        })
    if oam_margin and "obj" in layers:
        obj_regions = layers["obj"]["image_metrics"]["regions"]
        visible_obj_margin = sum(
            obj_regions[side]["non_backdrop_pixels"]
            for side in ("left_margin", "right_margin"))
        if visible_obj_margin == 0:
            findings.append({
                "stage": "object_ppu_or_presenter",
                "evidence": (
                    f"{len(oam_margin)} OAM entry/entries are in the margins, "
                    "but the OBJ-only image has no non-black margin pixels."),
                "interpretation": (
                    "The game submitted sprites; inspect PPU OBJ evaluation, "
                    "tile availability, clipping/windows, and presentation."),
            })
    for layer_name, layer in layers.items():
        events = layer.get("runtime_events", {})
        blocking = (
            events.get("unresolved_abandons", 0) +
            events.get("interpreter_caps", 0) +
            events.get("fatal_errors", 0))
        if blocking:
            findings.append({
                "stage": "runtime_integrity",
                "layer": layer_name,
                "evidence": f"The run logged {blocking} blocking runtime event(s).",
                "interpretation": (
                    "Treat this capture as invalid until the unresolved, "
                    "interpreter-cap, or fatal event is reconciled."),
            })
    return findings


def write_html_report(output_dir: Path, report: dict) -> None:
    cards = []
    for name, layer in report["layers"].items():
        cards.append(
            f"<section><h2>{html.escape(name.upper())}</h2>"
            f"<img src='{html.escape(layer['image'])}' alt='{html.escape(name)}'>"
            f"<pre>{html.escape(json.dumps(layer['image_metrics'], indent=2))}"
            f"</pre></section>")
    findings = "".join(
        f"<li><strong>{html.escape(item['stage'])}</strong>: "
        f"{html.escape(item['evidence'])} "
        f"{html.escape(item['interpretation'])}</li>"
        for item in report["findings"]) or "<li>No automatic red flag. Compare the isolated images manually.</li>"
    document = f"""<!doctype html>
<meta charset="utf-8">
<title>DKC2 widescreen diagnostic frame {report['frame']}</title>
<style>
body {{ background:#111; color:#eee; font:16px system-ui; margin:2rem }}
.grid {{ display:grid; grid-template-columns:repeat(auto-fit,minmax(380px,1fr)); gap:1rem }}
section {{ background:#1c1c1c; padding:1rem; border-radius:.5rem }}
img {{ width:100%; image-rendering:pixelated; border:1px solid #555 }}
pre {{ white-space:pre-wrap; font-size:12px }}
</style>
<h1>DKC2 widescreen diagnostic — frame {report['frame']}</h1>
<p>Same deterministic frame, isolated by PPU layer. Raw WRAM/VRAM stays private in this ignored bundle.</p>
<h2>Live screen profile</h2>
<pre>{html.escape(json.dumps(report.get('screen_profile', {}), indent=2))}</pre>
<h2>Automatic evidence</h2><ul>{findings}</ul>
<div class="grid">{''.join(cards)}</div>
"""
    with (output_dir / "index.html").open(
            "w", encoding="utf-8", newline="\n") as output:
        output.write(document)


def main() -> int:
    args = parse_args()
    if args.frame < 0:
        raise RuntimeError("--frame must be non-negative")
    executable = args.executable.expanduser().resolve()
    rom = args.rom.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    for path, label in ((executable, "executable"), (rom, "ROM")):
        if not path.is_file():
            raise RuntimeError(f"{label} does not exist: {path}")
    selected = tuple(
        item.strip().lower() for item in args.layers.split(",") if item.strip())
    if not selected or len(set(selected)) != len(selected):
        raise RuntimeError("--layers must contain unique layer names")
    unknown = [item for item in selected if item not in LAYER_MASKS]
    if unknown:
        raise RuntimeError(f"unknown layer(s): {', '.join(unknown)}")
    if output_dir.exists() and any(output_dir.iterdir()):
        raise RuntimeError(
            f"output directory is not empty; evidence is never overwritten: "
            f"{output_dir}")
    output_dir.mkdir(parents=True, exist_ok=True)

    environment = {}
    private_inputs = {}
    for path, variable, label in (
            (args.input_recording, "SNESRECOMP_INPUT_PLAY", "input_recording"),
            (args.sram, "DKC2_SRAM_INPUT", "sram"),
            (args.savestate, "DKC2_SAVESTATE_INPUT", "savestate")):
        if path:
            resolved = path.expanduser().resolve()
            if not resolved.is_file():
                raise RuntimeError(f"{label} does not exist: {resolved}")
            environment[variable] = str(resolved)
            private_inputs[label] = {
                "basename": resolved.name,
                "sha256": sha256_file(resolved),
            }

    report = {
        "schema": "dkc2-widescreen-diagnostic-v1",
        "frame": args.frame,
        "rom": {
            "basename": rom.name,
            "sha256": sha256_file(rom),
        },
        "private_inputs": private_inputs,
        "layer_meanings": {
            "bg1": "foreground/terrain candidate",
            "bg2": "background/parallax candidate",
            "bg3": "additional background/status candidate",
            "bg4": "Mode 0 additional background candidate",
            "obj": "render-consumed SNES sprites; not automatically an object ID",
        },
        "pipeline": [
            "background tile/object data exists in WRAM or ROM-derived state",
            "required graphics/tilemap data is loaded into VRAM",
            "game object is active and projects into the extended viewport",
            "object emits render-consumed OAM or background layer draws a tile",
            "PPU layer isolation produces pixels in the extended margin",
        ],
        "layers": {},
    }
    for layer in selected:
        print(f"capturing {layer} at frame {args.frame}...", flush=True)
        report["layers"][layer] = capture_process(
            executable, rom, args.frame, output_dir, layer, environment,
            args.port, args.timeout,
            capture_private_state=(layer == "composite"),
            scan_oam_margins=args.scan_oam_margins,
            function_watch=args.function_watch)
    composite = report["layers"].get("composite", {})
    report["screen_profile"] = classify_dkc2_screen(
        composite.get("game_state") or {}, composite.get("ppu") or {})
    report["findings"] = build_findings(
        report["layers"], report["screen_profile"])
    with (output_dir / "report.json").open(
            "w", encoding="utf-8", newline="\n") as output:
        output.write(json.dumps(report, indent=2) + "\n")
    write_html_report(output_dir, report)
    print(json.dumps({
        "report": str(output_dir / "report.json"),
        "html": str(output_dir / "index.html"),
        "findings": len(report["findings"]),
    }))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ConnectionError, OSError, RuntimeError, TimeoutError,
            subprocess.SubprocessError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
