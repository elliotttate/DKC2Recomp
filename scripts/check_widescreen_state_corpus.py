#!/usr/bin/env python3
"""Replay every preserved DKC2 save state and check its widescreen output.

Each state is rendered at the authentic 4:3 aspect and at one or more wide
aspects, in composite and in per-layer isolation. The checks are the same
machine facts the route auditor already uses, applied to a corpus instead of
one route:

* the native 256-pixel center of every wide frame must equal the 4:3 frame;
* an enabled background that is visibly non-blank in the native center must
  also be non-blank in both host margins;
* the old 4:3 boundary must not be a much larger pixel discontinuity than
  its neighboring columns;
* the world-keyed terrain store must never serve raw rolling VRAM; and
* every replay must complete without a runtime failure.

An optional reference directory produced by an earlier run enables a
before/after comparison: per-state pixel differences in the margins and the
center are reported so a generalized presentation rule can be validated
against every previously accepted case at once.

ROMs, save states, and rendered frames stay under the private output
directory; nothing here writes to the repository.
"""

from __future__ import annotations

import argparse
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

NATIVE_WIDTH = 256
HEIGHT = 224
LAYER_MASKS = {
    "composite": 0x1F,
    "bg1": 0x01,
    "bg2": 0x02,
    "bg3": 0x04,
    "obj": 0x10,
}
ASPECT_EXTRA = {"16:9": 43, "16:10": 26}
DEFAULT_LAYERS = ("composite", "bg1", "bg2", "bg3")
TRACE_PREFIX = "widescreen_frame="


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_ppm(path: Path) -> tuple[int, int, bytes]:
    data = path.read_bytes()
    if not data.startswith(b"P6"):
        raise RuntimeError(f"not a binary PPM: {path}")
    pos = 2

    def token() -> bytes:
        nonlocal pos
        while pos < len(data):
            if data[pos] == ord("#"):
                pos = data.find(b"\n", pos)
                if pos < 0:
                    raise RuntimeError(f"truncated PPM comment: {path}")
            elif data[pos] in b" \t\r\n":
                pos += 1
            else:
                break
        end = pos
        while end < len(data) and data[end] not in b" \t\r\n":
            end += 1
        value = data[pos:end]
        pos = end
        return value

    width = int(token())
    height = int(token())
    maximum = int(token())
    if pos >= len(data) or data[pos] not in b" \t\r\n":
        raise RuntimeError(f"missing PPM pixel separator: {path}")
    pos += 2 if data[pos:pos + 2] == b"\r\n" else 1
    pixels = data[pos:]
    if maximum != 255 or len(pixels) != width * height * 3:
        raise RuntimeError(f"unsupported or truncated PPM: {path}")
    return width, height, pixels


def crop(pixels: bytes, width: int, height: int, x0: int, x1: int) -> bytes:
    rows = []
    for y in range(height):
        start = (y * width + x0) * 3
        rows.append(pixels[start:start + (x1 - x0) * 3])
    return b"".join(rows)


def count_pixel_differences(a: bytes, b: bytes) -> int:
    if len(a) != len(b):
        return max(len(a), len(b)) // 3
    changed = 0
    for offset in range(0, len(a), 3):
        if a[offset:offset + 3] != b[offset:offset + 3]:
            changed += 1
    return changed


def dominant_color(pixels: bytes) -> bytes:
    counts: dict[bytes, int] = {}
    for offset in range(0, len(pixels), 3):
        color = pixels[offset:offset + 3]
        counts[color] = counts.get(color, 0) + 1
    return max(counts, key=counts.get) if counts else b"\0\0\0"


def non_backdrop_fraction(pixels: bytes, backdrop: bytes) -> float:
    total = len(pixels) // 3
    if total == 0:
        return 0.0
    other = sum(1 for offset in range(0, len(pixels), 3)
                if pixels[offset:offset + 3] != backdrop)
    return other / total


def edge_score(pixels: bytes, width: int, height: int, edge_x: int) -> dict:
    def difference(x: int) -> float:
        total = 0
        for y in range(height):
            left = (y * width + x - 1) * 3
            right = left + 3
            total += abs(pixels[left] - pixels[right])
            total += abs(pixels[left + 1] - pixels[right + 1])
            total += abs(pixels[left + 2] - pixels[right + 2])
        return total / (height * 3)

    edge = difference(edge_x)
    nearby = [difference(x) for x in range(max(1, edge_x - 5),
                                            min(width, edge_x + 6))
              if x != edge_x]
    baseline = sum(nearby) / len(nearby) if nearby else 0.0
    return {
        "edge": round(edge, 3),
        "nearby": round(baseline, 3),
        "ratio": round(edge / max(baseline, 1.0), 3),
        "excess": round(edge - baseline, 3),
    }


# The renderer may rebuild up to this many pixels at each native edge of a
# periodic bounded backdrop from that backdrop's proven period. Those columns
# are reported separately from the interior center check.
EDGE_REPAIR = 7


def split_center_differences(center: bytes, native: bytes,
                             height: int) -> tuple[int, int]:
    """Count differing pixels inside the native viewport, split into the
    interior and the seven-pixel endpoint bands; `height` is the row count
    of both buffers and is only used to validate their sizes."""
    interior = 0
    edges = 0
    for offset in range(0, min(len(center), len(native)), 3):
        if center[offset:offset + 3] == native[offset:offset + 3]:
            continue
        x = (offset // 3) % NATIVE_WIDTH
        if x < EDGE_REPAIR or x >= NATIVE_WIDTH - EDGE_REPAIR:
            edges += 1
        else:
            interior += 1
    expected = NATIVE_WIDTH * height * 3
    if len(center) != len(native) or len(native) != expected:
        interior += abs(len(center) - len(native)) // 3
    return interior, edges


def legacy_bias(camera_x: int, maximum_scroll_x: int, extra: int) -> int:
    """Presentation bias used by binaries that predate the trace field."""
    lower, upper = 0x100, maximum_scroll_x
    if upper < lower or upper - lower < 2 * extra:
        return 0
    target = min(max(camera_x, lower + extra), upper - extra)
    return target - camera_x


def frame_bias(trace: dict, frame: int, extra: int) -> int:
    state = trace_record(trace, frame)
    if not state:
        return 0
    ppu = state.get("ppu") or {}
    if "bias" in ppu:
        return int(ppu["bias"])
    terrain = state.get("terrain_source") or {}
    if not terrain.get("ready"):
        return 0
    camera = state.get("camera") or [0, 0]
    camera_max = state.get("camera_max") or [0, 0]
    return legacy_bias(int(camera[0]), int(camera_max[0]), extra)


def visible_margins(trace: dict, frame: int, extra: int) -> tuple[int, int]:
    state = trace_record(trace, frame)
    ppu = (state or {}).get("ppu") or {}
    if "left" in ppu and "right" in ppu:
        return int(ppu["left"]), int(ppu["right"])
    return extra, extra


def parse_trace(text: str) -> dict:
    """Per-frame widescreen trace records keyed by frame number."""
    frames = {}
    for line in text.splitlines():
        if line.startswith(TRACE_PREFIX):
            try:
                record = json.loads(line[len(TRACE_PREFIX):])
            except json.JSONDecodeError:
                continue
            frames[int(record.get("frame", len(frames)))] = record
    return frames


def trace_record(trace: dict, frame: int) -> dict | None:
    if not trace:
        return None
    if frame in trace:
        return trace[frame]
    earlier = [key for key in trace if key <= frame]
    return trace[max(earlier)] if earlier else trace[min(trace)]


@dataclass
class Run:
    aspect: str
    layer: str
    directory: Path
    returncode: int = -1
    completed: bool = False
    trace: dict = field(default_factory=dict)
    frames: dict = field(default_factory=dict)


def run_headless(executable: Path, rom: Path, state: Path | None,
                 frames: int, aspect: str, layer: str, directory: Path,
                 extra_environment: dict | None = None,
                 sequence: tuple[int, int, int] | None = None) -> Run:
    """Replay `frames` frames and dump the PPM sequence (start, end, step);
    the default sequence dumps every frame."""
    directory.mkdir(parents=True, exist_ok=True)
    start, end, step = sequence or (0, frames - 1, 1)
    environment = dict(os.environ)
    environment.update({
        "DKC2_ASPECT": aspect,
        "SNESRECOMP_LAYER_MASK": str(LAYER_MASKS[layer]),
        "DKC2_FRAME_PPM_PREFIX": str((directory / "frame").resolve()),
        "DKC2_FRAME_PPM_START": str(start),
        "DKC2_FRAME_PPM_END": str(end),
        "DKC2_FRAME_PPM_STEP": str(step),
        "DKC2_WIDESCREEN_TRACE": "1",
        "DKC2_WIDESCREEN_TRACE_START": str(start),
        "DKC2_WIDESCREEN_TRACE_STEP": str(step),
    })
    if state is not None:
        environment["DKC2_SAVESTATE_INPUT"] = str(state.resolve())
    if extra_environment:
        environment.update(extra_environment)
    run = Run(aspect=aspect, layer=layer, directory=directory)
    with (directory / "stdout.log").open("wb") as stdout, \
            (directory / "stderr.log").open("wb") as stderr:
        process = subprocess.run(
            [str(executable), str(rom), str(frames)],
            env=environment, stdout=stdout, stderr=stderr, check=False)
    run.returncode = process.returncode
    stdout_text = (directory / "stdout.log").read_text(errors="replace")
    run.completed = process.returncode == 0 and \
        "result=completed" in stdout_text
    run.trace = parse_trace(
        (directory / "stderr.log").read_text(errors="replace"))
    for frame in range(start, end + 1, step):
        path = directory / f"frame_{frame:06d}.ppm"
        if path.exists():
            run.frames[frame] = read_ppm(path)
    return run


def analyze_state(name: str, runs: dict, frames: int,
                  seam_ratio: float, seam_excess: float,
                  center_check: bool = True) -> dict:
    """Analyze one state's runs.

    `center_check` compares each wide frame's presented native viewport with
    the 4:3 render of the same frame. It is exact only for short replays: over
    a long route the widened object activation window legitimately changes
    enemy behavior, so a boot capture uses the before/after reference
    comparison instead.
    """
    findings = []
    native = runs.get(("4:3", "composite"))
    result = {
        "state": name,
        "runs": {},
        "findings": findings,
        "profile": {},
    }
    for (aspect, layer), run in runs.items():
        result["runs"][f"{aspect}/{layer}"] = {
            "returncode": run.returncode,
            "completed": run.completed,
            "frames": len(run.frames),
        }
        if not run.completed:
            findings.append({
                "kind": "runtime_failure", "severity": "error",
                "aspect": aspect, "layer": layer,
                "detail": f"exit code {run.returncode}",
            })
    if native and native.trace:
        first = native.trace[min(native.trace)]
        result["profile"] = {
            "level": first.get("level"),
            "game_sub_mode": first.get("game_sub_mode"),
            "camera": first.get("camera"),
            "camera_max": first.get("camera_max"),
            "terrain_vram": first.get("terrain_vram"),
            "ppu": first.get("ppu"),
        }

    for aspect, extra in ASPECT_EXTRA.items():
        wide = runs.get((aspect, "composite"))
        if not wide or not native:
            continue
        wide_width = NATIVE_WIDTH + 2 * extra
        center_differences = 0
        edge_differences = 0
        biases = set()
        for frame in sorted(native.frames):
            if frame not in wide.frames:
                continue
            if not center_check:
                biases.add(frame_bias(wide.trace, frame, extra))
                continue
            width, height, pixels = wide.frames[frame]
            nwidth, nheight, npixels = native.frames[frame]
            if width != wide_width or nwidth != NATIVE_WIDTH:
                findings.append({
                    "kind": "unexpected_geometry", "severity": "error",
                    "aspect": aspect, "layer": "composite", "frame": frame,
                    "detail": f"wide {width}x{height}, native "
                              f"{nwidth}x{nheight}",
                })
                continue
            bias = frame_bias(wide.trace, frame, extra)
            biases.add(bias)
            # The presented viewport is shifted by the bias, so the exact
            # 4:3 image sits at wide column (extra - bias).
            offset = extra - bias
            if offset < 0 or offset + NATIVE_WIDTH > width:
                findings.append({
                    "kind": "unexpected_geometry", "severity": "error",
                    "aspect": aspect, "layer": "composite", "frame": frame,
                    "detail": f"presentation bias {bias} leaves the native "
                              "viewport outside the host frame",
                })
                continue
            center = crop(pixels, width, height, offset,
                          offset + NATIVE_WIDTH)
            interior, edges = split_center_differences(center, npixels,
                                                       nheight)
            center_differences += interior
            edge_differences += edges
        entry = result["runs"][f"{aspect}/composite"]
        entry["center_differences"] = center_differences
        entry["center_edge_differences"] = edge_differences
        entry["presentation_biases"] = sorted(biases)
        if center_differences:
            findings.append({
                "kind": "center_mismatch", "severity": "error",
                "aspect": aspect, "layer": "composite",
                "detail": f"{center_differences} interior center pixels "
                          "differ from the 4:3 render across the captured "
                          "frames",
            })

        ready_frames = [
            record for record in wide.trace.values()
            if (record.get("terrain_source") or {}).get("ready")]
        result["runs"][f"{aspect}/composite"]["terrain_ready_frames"] = \
            len(ready_frames)
        if wide.trace:
            last = wide.trace[max(wide.trace)]
            raw = 0
            for shadow in last.get("shadow") or []:
                raw += int(shadow.get("west_raw", 0)) + \
                    int(shadow.get("east_raw", 0))
            result["runs"][f"{aspect}/composite"]["raw_fallbacks"] = raw
            if raw:
                findings.append({
                    "kind": "raw_vram_margin_fallback", "severity": "error",
                    "aspect": aspect, "layer": "composite",
                    "detail": f"{raw} margin lookups fell through to raw "
                              "rolling VRAM",
                })
            result["runs"][f"{aspect}/composite"]["hdma_bands"] = \
                int((last.get("ppu") or {}).get("bands", 0))
            wide_mask = int((last.get("ppu") or {}).get("wide", 0))
            repeat_mask = int((last.get("ppu") or {}).get("repeat", 0))
            result["runs"][f"{aspect}/composite"]["wide_mask"] = wide_mask
            result["runs"][f"{aspect}/composite"]["repeat_mask"] = \
                repeat_mask

        for layer in ("composite", "bg1", "bg2", "bg3"):
            run = runs.get((aspect, layer))
            if not run:
                continue
            enabled_frames = 0
            blank_margin_frames = {"left": 0, "right": 0}
            seam_frames = {"left": 0, "right": 0}
            native_fill = 0.0
            for frame in sorted(run.frames):
                width, height, pixels = run.frames[frame]
                if width != wide_width:
                    continue
                backdrop = dominant_color(pixels)
                center = crop(pixels, width, height, extra,
                              extra + NATIVE_WIDTH)
                left_visible, right_visible = visible_margins(
                    run.trace, frame, extra)
                left = crop(pixels, width, height, extra - left_visible,
                            extra) if left_visible else b""
                right = crop(pixels, width, height, extra + NATIVE_WIDTH,
                             extra + NATIVE_WIDTH + right_visible) \
                    if right_visible else b""
                center_fill = non_backdrop_fraction(center, backdrop)
                native_fill = max(native_fill, center_fill)
                if layer != "composite":
                    trace = trace_record(run.trace, frame)
                    ppu = (trace or {}).get("ppu") or {}
                    enabled = (int(ppu.get("main", 0)) |
                               int(ppu.get("sub", 0)))
                    index = int(layer[-1]) - 1
                    if not enabled & (1 << index):
                        continue
                if center_fill < 0.05:
                    continue
                enabled_frames += 1
                for side, region in (("left", left), ("right", right)):
                    if region and \
                            non_backdrop_fraction(region, backdrop) == 0.0:
                        blank_margin_frames[side] += 1
                for side, x, visible in (("left", extra, left_visible),
                                         ("right", extra + NATIVE_WIDTH,
                                          right_visible)):
                    if visible < 8:
                        continue
                    score = edge_score(pixels, width, height, x)
                    if score["ratio"] >= seam_ratio and \
                            score["excess"] >= seam_excess:
                        seam_frames[side] += 1
            entry = result["runs"][f"{aspect}/{layer}"]
            entry["visible_frames"] = enabled_frames
            entry["blank_margin_frames"] = blank_margin_frames
            entry["seam_frames"] = seam_frames
            entry["native_fill"] = round(native_fill, 4)
            for side in ("left", "right"):
                if enabled_frames and \
                        blank_margin_frames[side] == enabled_frames:
                    findings.append({
                        "kind": "blank_margin", "severity": "warning",
                        "aspect": aspect, "layer": layer, "side": side,
                        "detail": f"{layer} is visible natively but the "
                                  f"{side} margin is blank in every frame",
                    })
                if enabled_frames and \
                        seam_frames[side] == enabled_frames:
                    findings.append({
                        "kind": "native_boundary_seam", "severity": "warning",
                        "aspect": aspect, "layer": layer, "side": side,
                        "detail": f"{side} old-4:3 boundary is a persistent "
                                  f"discontinuity in {layer}",
                    })
    return result


def compare_with_reference(name: str, runs: dict, reference_dir: Path,
                           frames: int) -> dict:
    comparison = {}
    for (aspect, layer), run in runs.items():
        extra = ASPECT_EXTRA.get(aspect, 0)
        center_diff = 0
        margin_diff = 0
        compared = 0
        for frame in sorted(run.frames):
            reference = reference_dir / name / aspect.replace(":", "x") / \
                layer / f"frame_{frame:06d}.ppm"
            if not reference.exists():
                continue
            width, height, pixels = run.frames[frame]
            rwidth, rheight, rpixels = read_ppm(reference)
            if (width, height) != (rwidth, rheight):
                continue
            compared += 1
            if extra:
                center_diff += count_pixel_differences(
                    crop(pixels, width, height, extra, extra + NATIVE_WIDTH),
                    crop(rpixels, width, height, extra,
                         extra + NATIVE_WIDTH))
                margin_diff += count_pixel_differences(
                    crop(pixels, width, height, 0, extra),
                    crop(rpixels, width, height, 0, extra))
                margin_diff += count_pixel_differences(
                    crop(pixels, width, height, extra + NATIVE_WIDTH, width),
                    crop(rpixels, width, height, extra + NATIVE_WIDTH,
                         width))
            else:
                center_diff += count_pixel_differences(pixels, rpixels)
        comparison[f"{aspect}/{layer}"] = {
            "frames_compared": compared,
            "center_pixels_changed": center_diff,
            "margin_pixels_changed": margin_diff,
        }
    return comparison


def write_contact_sheet(path: Path, runs: dict, aspects: list[str]) -> bool:
    try:
        from PIL import Image
    except ImportError:
        return False
    tiles = []
    for aspect in ["4:3"] + aspects:
        for layer in DEFAULT_LAYERS:
            run = runs.get((aspect, layer))
            if not run or not run.frames:
                continue
            width, height, pixels = run.frames[min(run.frames)]
            image = Image.frombytes("RGB", (width, height), pixels)
            tiles.append((f"{aspect} {layer}", image))
    if not tiles:
        return False
    columns = 4
    tile_w = max(image.width for _, image in tiles)
    tile_h = max(image.height for _, image in tiles) + 14
    rows = (len(tiles) + columns - 1) // columns
    sheet = Image.new("RGB", (tile_w * columns, tile_h * rows), (32, 32, 32))
    try:
        from PIL import ImageDraw
        draw = ImageDraw.Draw(sheet)
    except ImportError:
        draw = None
    for index, (label, image) in enumerate(tiles):
        x = (index % columns) * tile_w
        y = (index // columns) * tile_h
        sheet.paste(image, (x, y + 14))
        if draw:
            draw.text((x + 2, y + 1), label, fill=(255, 255, 255))
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)
    return True


def discover_states(sources: list[Path]) -> list[Path]:
    states = []
    for source in sources:
        if source.is_dir():
            states.extend(sorted(source.rglob("*.sav")))
        elif source.exists():
            states.append(source)
    seen = set()
    unique = []
    for state in states:
        digest = sha256_bytes(state.read_bytes())
        if digest in seen:
            continue
        seen.add(digest)
        unique.append(state)
    return unique


def state_name(state: Path, root: Path) -> str:
    try:
        relative = state.resolve().relative_to(root.resolve())
    except ValueError:
        relative = Path(state.name)
    return re.sub(r"[^A-Za-z0-9._-]+", "_", str(relative.with_suffix("")))


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--state", action="append", type=Path, default=[],
                        help="a save state, or a directory searched "
                             "recursively for *.sav")
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--frames", type=int, default=4)
    parser.add_argument("--aspect", action="append", default=[],
                        choices=sorted(ASPECT_EXTRA))
    parser.add_argument("--layers", default=",".join(DEFAULT_LAYERS))
    parser.add_argument("--reference-dir", type=Path)
    parser.add_argument("--jobs", type=int, default=4)
    parser.add_argument("--seam-ratio", type=float, default=3.0)
    parser.add_argument("--seam-excess", type=float, default=18.0)
    parser.add_argument("--strict-seams", action="store_true",
                        help="treat persistent boundary seams and blank "
                             "margins as failures instead of warnings")
    parser.add_argument("--no-contact-sheets", action="store_true")
    parser.add_argument("--boot-frames", type=int, default=0,
                        help="also replay a neutral boot (no state) for this "
                             "many frames; the attract demos start near "
                             "frame 3,276")
    parser.add_argument("--boot-start", type=int, default=3300)
    parser.add_argument("--boot-step", type=int, default=50)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    executable = args.executable.resolve()
    rom = args.rom.resolve()
    aspects = args.aspect or ["16:9"]
    layers = tuple(item.strip() for item in args.layers.split(",") if item)
    if "composite" not in layers:
        layers = ("composite",) + layers
    states = discover_states(args.state)
    if not states and not args.boot_frames:
        print("error: no save states found (pass --state or --boot-frames)",
              file=sys.stderr)
        return 2
    output_dir = args.output_dir.resolve()
    output_dir.mkdir(parents=True, exist_ok=True)
    common_root = Path(os.path.commonpath(
        [str(s.resolve()) for s in states])) if states else Path(".")
    if common_root.is_file():
        common_root = common_root.parent

    jobs = []
    for state in states:
        name = state_name(state, common_root)
        jobs.append((name, state, "4:3", "composite"))
        for aspect in aspects:
            for layer in layers:
                jobs.append((name, state, aspect, layer))
    if args.boot_frames:
        jobs.append(("boot", None, "4:3", "composite"))
        for aspect in aspects:
            for layer in layers:
                jobs.append(("boot", None, aspect, layer))
    boot_sequence = (args.boot_start,
                     max(args.boot_start, args.boot_frames - 1),
                     max(1, args.boot_step))

    def execute(job):
        name, state, aspect, layer = job
        directory = output_dir / name / aspect.replace(":", "x") / layer
        if state is None:
            return job, run_headless(executable, rom, None, args.boot_frames,
                                     aspect, layer, directory,
                                     sequence=boot_sequence)
        return job, run_headless(executable, rom, state, args.frames,
                                 aspect, layer, directory)

    runs_by_state: dict[str, dict] = {}
    with ThreadPoolExecutor(max_workers=max(1, args.jobs)) as pool:
        for (name, _, aspect, layer), run in pool.map(execute, jobs):
            runs_by_state.setdefault(name, {})[(aspect, layer)] = run

    report = {
        "executable": str(executable),
        "executable_sha256": sha256_bytes(executable.read_bytes()),
        "frames": args.frames,
        "aspects": aspects,
        "layers": list(layers),
        "states": [],
    }
    hard_failures = 0
    warnings = 0
    for name in sorted(runs_by_state):
        runs = runs_by_state[name]
        result = analyze_state(name, runs, args.frames,
                               args.seam_ratio, args.seam_excess,
                               center_check=name != "boot")
        if args.reference_dir:
            result["reference"] = compare_with_reference(
                name, runs, args.reference_dir.resolve(), args.frames)
        if not args.no_contact_sheets:
            write_contact_sheet(output_dir / name / "contact.png", runs,
                                aspects)
        for finding in result["findings"]:
            if finding["severity"] == "error" or args.strict_seams:
                hard_failures += 1
            else:
                warnings += 1
        report["states"].append(result)

    (output_dir / "report.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    lines = [f"widescreen state corpus: {len(report['states'])} states, "
             f"{hard_failures} failures, {warnings} warnings"]
    for result in report["states"]:
        profile = result.get("profile") or {}
        composite = result["runs"].get(f"{aspects[0]}/composite", {})
        lines.append(
            f"- {result['state']}: level=${int(profile.get('level') or 0):04x}"
            f" sub_mode=${int(profile.get('game_sub_mode') or 0):04x}"
            f" camera={profile.get('camera')}"
            f" center_diff={composite.get('center_differences')}"
            f" edge_diff={composite.get('center_edge_differences')}"
            f" bias={composite.get('presentation_biases')}"
            f" ready={composite.get('terrain_ready_frames')}"
            f" wide=${int(composite.get('wide_mask') or 0):02x}"
            f" repeat=${int(composite.get('repeat_mask') or 0):02x}")
        for finding in result["findings"]:
            lines.append(f"    {finding['severity']}: {finding['kind']} "
                         f"[{finding.get('aspect')}/{finding.get('layer')}"
                         f"{'/' + finding['side'] if 'side' in finding else ''}]"
                         f" {finding['detail']}")
        if "reference" in result:
            for key, value in result["reference"].items():
                if value["center_pixels_changed"] or \
                        value["margin_pixels_changed"]:
                    lines.append(
                        f"    changed vs reference [{key}]: center "
                        f"{value['center_pixels_changed']} px, margins "
                        f"{value['margin_pixels_changed']} px over "
                        f"{value['frames_compared']} frames")
    summary = "\n".join(lines)
    (output_dir / "summary.txt").write_text(summary + "\n", encoding="utf-8")
    print(summary)
    return 1 if hard_failures else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
