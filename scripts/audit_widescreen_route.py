#!/usr/bin/env python3
"""Capture and automatically audit a deterministic DKC2 widescreen route.

The audit is deliberately evidence-oriented. It reports candidates and their
confidence; it does not claim that image heuristics can replace a reference
emulator or game-specific semantic rules.
"""

from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import html
import json
import os
from pathlib import Path
import struct
import subprocess
import sys


LAYER_MASKS = {
    "composite": 0x1F,
    "bg1": 0x01,
    "bg2": 0x02,
    "bg3": 0x04,
    "bg4": 0x08,
    "obj": 0x10,
}
BACKGROUND_LAYERS = ("bg1", "bg2", "bg3", "bg4")
FRAME_PREFIX = "widescreen_frame="
EXTRA_LEFT = 43
NATIVE_WIDTH = 256
WIDE_WIDTH = 342
HEIGHT = 224


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--output-dir", required=True, type=Path)
    parser.add_argument("--input-recording", type=Path)
    parser.add_argument("--sram", type=Path)
    parser.add_argument("--savestate", type=Path)
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--end", type=int)
    parser.add_argument("--step", type=int, default=4)
    parser.add_argument(
        "--layers", default="composite,bg1,bg2,bg3,obj",
        help="comma-separated subset of composite,bg1,bg2,bg3,bg4,obj")
    parser.add_argument("--timeout", type=float, default=600.0)
    parser.add_argument("--max-findings", type=int, default=500)
    parser.add_argument(
        "--reuse-capture", action="store_true",
        help="reanalyze the existing raw/ capture without rerunning the game")
    return parser.parse_args()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


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

    try:
        width = int(token())
        height = int(token())
        maximum = int(token())
    except ValueError as error:
        raise RuntimeError(f"invalid PPM header: {path}") from error
    # P6 has one whitespace separator after maxval. Do not skip an arbitrary
    # run here: the first binary pixel byte is allowed to equal ASCII space,
    # tab, CR, or LF. Treat CRLF as one logical separator for tolerant input.
    if pos >= len(data) or data[pos] not in b" \t\r\n":
        raise RuntimeError(f"missing PPM pixel separator: {path}")
    if data[pos:pos + 2] == b"\r\n":
        pos += 2
    else:
        pos += 1
    pixels = data[pos:]
    if maximum != 255 or width <= 0 or height <= 0 or \
            len(pixels) != width * height * 3:
        raise RuntimeError(f"unsupported or truncated PPM: {path}")
    return width, height, pixels


def write_bmp24(path: Path, width: int, height: int, rgb: bytes) -> None:
    row_stride = (width * 3 + 3) & ~3
    image_size = row_stride * height
    header = bytearray(54)
    header[0:2] = b"BM"
    struct.pack_into("<I", header, 2, 54 + image_size)
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<IiiHHIIiiII", header, 14, 40, width, -height, 1, 24,
                     0, image_size, 2835, 2835, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as stream:
        stream.write(header)
        padding = b"\0" * (row_stride - width * 3)
        for y in range(height):
            row = rgb[y * width * 3:(y + 1) * width * 3]
            bgr = bytearray(len(row))
            bgr[0::3] = row[2::3]
            bgr[1::3] = row[1::3]
            bgr[2::3] = row[0::3]
            stream.write(bgr)
            stream.write(padding)


def pixel_difference(a: bytes, b: bytes) -> float:
    if len(a) != len(b) or not a:
        return 1.0
    changed = 0
    pixels = len(a) // 3
    for offset in range(0, len(a), 3):
        if (abs(a[offset] - b[offset]) +
                abs(a[offset + 1] - b[offset + 1]) +
                abs(a[offset + 2] - b[offset + 2])) >= 24:
            changed += 1
    return changed / pixels


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


def tile_patch(pixels: bytes, width: int, x: int, y: int) -> bytes:
    output = bytearray()
    for row in range(8):
        start = ((y + row) * width + x) * 3
        output.extend(pixels[start:start + 24])
    return bytes(output)


def tile_region(center_x: int) -> str:
    if center_x < EXTRA_LEFT:
        return "left_margin"
    if center_x >= EXTRA_LEFT + NATIVE_WIDTH:
        return "right_margin"
    return "native"


def iter_world_tiles(width: int, height: int, pixels: bytes,
                     h_scroll: int, v_scroll: int):
    # output X 43 is authentic SNES X 0. Align extraction to the layer's
    # world-space 8x8 grid so a cell can be compared as it crosses a margin.
    first_x = (-(h_scroll - EXTRA_LEFT)) % 8
    first_y = (-v_scroll) % 8
    for y in range(first_y, height - 7, 8):
        world_y = (v_scroll + y) // 8
        for x in range(first_x, width - 7, 8):
            logical_x = x - EXTRA_LEFT
            world_x = (h_scroll + logical_x) // 8
            patch = tile_patch(pixels, width, x, y)
            yield {
                "world": (world_x, world_y),
                "region": tile_region(x + 4),
                "hash": hashlib.blake2s(patch, digest_size=8).hexdigest(),
                "patch": patch,
                "x": x,
                "y": y,
            }


def sprite_identity(level: int, sprite: dict) -> tuple | None:
    placement = int(sprite.get("placement", 0))
    if placement:
        return level, int(sprite["type"]), placement
    return None


def signed_delta(value: int) -> int:
    return value - 0x10000 if value & 0x8000 else value


def classify_sprite_x(sprite: dict, metadata: dict) -> int:
    return signed_delta((int(sprite["world"][0]) -
                         int(metadata["camera"][0])) & 0xFFFF)


def finding(kind: str, confidence: str, frame: int, layer: str | None,
            evidence: str, **extra) -> dict:
    item = {
        "kind": kind,
        "confidence": confidence,
        "frame": frame,
        "evidence": evidence,
    }
    if layer:
        item["layer"] = layer
    item.update(extra)
    return item


def build_layer_age(metadata: list[dict]) -> dict[tuple[int, int], int]:
    ages = {}
    previous_signature = [None] * 4
    start_frame = [0] * 4
    for state in metadata:
        frame = int(state["frame"])
        ppu = state["ppu"]
        enabled = int(ppu["main"]) | int(ppu["sub"])
        for layer in range(4):
            signature = (
                int(state["level"]), int(state["game_mode"]),
                int(state["game_sub_mode"]), int(ppu["mode"]),
                int(ppu["bg_sc"][layer]), bool(enabled & (1 << layer)),
                bool(int(ppu["wide"]) & (1 << layer)),
                bool(int(ppu["repeat"]) & (1 << layer)),
                bool(int(ppu["mirror"]) & (1 << layer)),
                bool(int(ppu["clamp"]) & (1 << layer)),
            )
            if signature != previous_signature[layer]:
                previous_signature[layer] = signature
                start_frame[layer] = frame
            ages[(frame, layer)] = frame - start_frame[layer]
    return ages


def display_is_stable(state: dict, layer: int, ages: dict) -> bool:
    inidisp = int(state["ppu"].get("inidisp", 15))
    return not inidisp & 0x80 and (inidisp & 0x0F) == 0x0F and \
        ages.get((int(state["frame"]), layer), 0) >= 60


def terrain_owner(state: dict) -> int | None:
    ppu = state["ppu"]
    enabled = int(ppu["main"]) | int(ppu["sub"])
    target = int(state["terrain_vram"])
    owners = [layer for layer in range(2)
              if enabled & (1 << layer) and
              ((int(ppu["bg_sc"][layer]) & 0xFC) << 8) == target]
    return owners[0] if len(owners) == 1 else None


def terrain_margin_is_exact(state: dict) -> bool:
    values = state.get("terrain_source", {}).get("margin_prefill", [])
    if len(values) != 3:
        return False
    expected, present, _matching = (int(value) for value in values)
    # Every requested margin cell is authoritative once it is present. A
    # nonmatching static-map value means a newer cartridge tilemap write won,
    # which is preferable for animated/destructible terrain and must not be
    # compared against a different animation phase several frames later.
    return expected > 0 and expected == present


def terrain_margin_matches_static_source(state: dict) -> bool:
    values = state.get("terrain_source", {}).get("margin_prefill", [])
    if len(values) != 3:
        return False
    expected, present, matching = (int(value) for value in values)
    return expected > 0 and expected == present == matching


def all_enabled_backgrounds_have_wide_source(state: dict) -> bool:
    ppu = state.get("ppu", {})
    enabled = (int(ppu.get("main", 0)) | int(ppu.get("sub", 0))) & 0x0F
    policies = (int(ppu.get("wide", 0)) | int(ppu.get("repeat", 0)) |
                int(ppu.get("mirror", 0)) | int(ppu.get("clamp", 0))) & 0x0F
    return enabled != 0 and enabled & ~policies == 0


def analyze_shadow_misses(metadata: list[dict], ages: dict) -> list[dict]:
    output = []
    previous = None
    for state in metadata:
        if previous:
            ppu = state["ppu"]
            wide = int(ppu["wide"])
            padded = (int(ppu.get("repeat", 0)) |
                      int(ppu.get("mirror", 0)) |
                      int(ppu.get("clamp", 0)))
            for layer in range(2):
                bit = 1 << layer
                if not wide & bit or padded & bit:
                    continue
                current_stats = state["shadow"][layer]
                old_stats = previous["shadow"][layer]
                west = int(current_stats["west_miss"]) - int(old_stats["west_miss"])
                east = int(current_stats["east_miss"]) - int(old_stats["east_miss"])
                west_raw = int(current_stats.get("west_raw", 0)) - \
                    int(old_stats.get("west_raw", 0))
                east_raw = int(current_stats.get("east_raw", 0)) - \
                    int(old_stats.get("east_raw", 0))
                west_blank = int(current_stats.get("west_blank", 0)) - \
                    int(old_stats.get("west_blank", 0))
                east_blank = int(current_stats.get("east_blank", 0)) - \
                    int(old_stats.get("east_blank", 0))
                if west_raw > 0 or east_raw > 0:
                    output.append(finding(
                        "raw_vram_margin_fallback", "exact", int(state["frame"]),
                        f"bg{layer + 1}",
                        f"renderer consumed wrapped VRAM for {west_raw} west "
                        f"and {east_raw} east margin samples",
                        west_raw=west_raw, east_raw=east_raw,
                        west_misses=west, east_misses=east))
                else:
                    blank_samples = west_blank + east_blank
                    large_blank_burst = blank_samples >= 64
                    if blank_samples <= 0 or (
                            not large_blank_burst and
                            not display_is_stable(state, layer, ages)):
                        continue
                    kind = ("large_verified_blank_margin_fallback"
                            if large_blank_burst else
                            "verified_blank_margin_fallback")
                    output.append(finding(
                        kind, "strong",
                        int(state["frame"]), f"bg{layer + 1}",
                        f"missing world content was replaced with a verified "
                        f"blank tile for {west_blank} west and {east_blank} "
                        f"east margin samples",
                        west_blank=west_blank, east_blank=east_blank,
                        west_misses=west, east_misses=east))
        previous = state
    return output


def analyze_seams(images: dict, metadata_by_frame: dict, ages: dict) -> list[dict]:
    candidates = []
    for layer, frames in images.items():
        if layer not in BACKGROUND_LAYERS and layer != "composite":
            continue
        for frame, (width, height, pixels, _) in frames.items():
            if width != WIDE_WIDTH:
                continue
            state = metadata_by_frame.get(frame)
            if not state or int(state["ppu"]["mode"]) not in (0, 1, 2, 3, 5):
                continue
            layer_index = int(layer[-1]) - 1 if layer.startswith("bg") else 0
            if not display_is_stable(state, layer_index, ages):
                continue
            for side, x in (("left", EXTRA_LEFT),
                            ("right", EXTRA_LEFT + NATIVE_WIDTH)):
                score = edge_score(pixels, width, height, x)
                if score["ratio"] >= 3.0 and score["excess"] >= 18.0:
                    candidates.append(finding(
                        "native_boundary_seam", "strong", frame, layer,
                        f"{side} old-4:3 boundary discontinuity is "
                        f"{score['ratio']:.2f}x nearby column edges",
                        side=side, score=score))
    # A mast, wall, rope, or other authored vertical edge can coincide with
    # the old 4:3 boundary for one sampled frame. A stale-margin seam remains
    # fixed at that screen coordinate as the camera moves. Require the signal
    # on adjacent captured samples so isolated geometry is not promoted to a
    # defect automatically.
    candidate_keys = {
        (item["layer"], item["side"], item["frame"])
        for item in candidates
    }
    neighboring_frames = {}
    for layer, frames in images.items():
        ordered = sorted(frames)
        for index, frame in enumerate(ordered):
            neighboring_frames[(layer, frame)] = {
                ordered[index - 1] if index else None,
                ordered[index + 1] if index + 1 < len(ordered) else None,
            }
    output = []
    for item in candidates:
        neighbors = neighboring_frames.get(
            (item["layer"], item["frame"]), set())
        if any((item["layer"], item["side"], frame) in candidate_keys
               for frame in neighbors if frame is not None):
            output.append(item)
    return output


def analyze_world_tiles(metadata_by_frame: dict, ages: dict) -> list[dict]:
    observations = defaultdict(list)
    for frame, state in metadata_by_frame.items():
        layer_index = terrain_owner(state)
        if layer_index is None:
            continue
        ppu = state["ppu"]
        if int(ppu["repeat"]) & (1 << layer_index) or \
                int(ppu["mirror"]) & (1 << layer_index) or \
                int(ppu["clamp"]) & (1 << layer_index) or \
                not display_is_stable(state, layer_index, ages):
            continue
        layer = f"bg{layer_index + 1}"
        scene = (int(state["level"]), int(state["game_mode"]),
                 int(state["game_sub_mode"]), int(ppu["bg_sc"][layer_index]))
        for screen_x, screen_y, world_x, world_y, entry in \
                state.get("terrain_tiles", []):
            if screen_x < 0:
                region = "left_margin"
            elif screen_x >= NATIVE_WIDTH:
                region = "right_margin"
            else:
                region = "native"
            key = scene, layer, (world_x, world_y)
            observations[key].append({
                "frame": frame, "region": region, "entry": entry,
                "x": screen_x, "y": screen_y,
                "margin_exact": terrain_margin_is_exact(state),
            })

    mismatches = defaultdict(list)
    for (scene, layer, world), samples in observations.items():
        native = [sample for sample in samples if sample["region"] == "native"]
        margins = [sample for sample in samples if sample["region"] != "native"]
        if not native or not margins:
            continue
        for margin in margins:
            if margin["margin_exact"]:
                continue
            nearest = min(native, key=lambda item: abs(item["frame"] - margin["frame"]))
            distance = abs(nearest["frame"] - margin["frame"])
            if distance > 72 or nearest["entry"] == margin["entry"]:
                continue
            mismatches[(margin["frame"], layer, margin["region"])].append({
                "world": world, "reference_frame": nearest["frame"],
                "distance": distance, "margin_entry": margin["entry"],
                "native_entry": nearest["entry"], "scene": scene,
                "x": margin["x"], "y": margin["y"],
            })
            break
    output = []
    for (frame, layer, region), samples in mismatches.items():
        if len(samples) < 2:
            continue
        reference = min(samples, key=lambda item: item["distance"])
        worlds = [list(item["world"]) for item in samples[:12]]
        output.append(finding(
            "margin_world_tile_mismatch", "strong", frame, layer,
            f"{len(samples)} exact terrain entries disagreed between the "
            f"{region.replace('_', ' ')} and their later/earlier native-view "
            f"tilemap identity",
            reference_frame=reference["reference_frame"], worlds=worlds,
            scene=list(reference["scene"]), region=region))
    return output


def analyze_object_lifetimes(metadata: list[dict], step: int,
                             ages: dict) -> list[dict]:
    # A placed object can be absent on one coarse sample and present again on
    # the next because its slot/placement fields are updated between those
    # samples.  Treating that aliasing as a lifecycle edge produced false
    # spawn/despawn reports in the attract demo at step 12.  A lifecycle claim
    # is only exact when the immediately preceding/following frame was traced;
    # callers can rerun a small suspect range at step 1.
    if step != 1:
        return []
    tracks = defaultdict(list)
    for state in metadata:
        level = int(state["level"])
        for sprite in state.get("sprites", []):
            identity = sprite_identity(level, sprite)
            if identity:
                tracks[identity].append((state, sprite, classify_sprite_x(sprite, state)))

    frames = {int(state["frame"]): state for state in metadata}
    output = []
    left, right = -EXTRA_LEFT + 8, NATIVE_WIDTH + EXTRA_LEFT - 8
    def boundary_relevant(x: int) -> bool:
        return x < 0 or x >= NATIVE_WIDTH or abs(x) <= 24 or \
            abs(x - NATIVE_WIDTH) <= 24

    for identity, samples in tracks.items():
        first_state, first_sprite, first_x = samples[0]
        first_frame = int(first_state["frame"])
        prior = frames.get(first_frame - step)
        owner = terrain_owner(first_state)
        age_layer = owner if owner is not None else 0
        if prior and int(prior["level"]) == identity[0] and \
                ages.get((first_frame, age_layer), 0) >= 60 and \
                left < first_x < right and boundary_relevant(first_x):
            output.append(finding(
                "object_spawn_inside_wide_view", "strong", first_frame, "obj",
                f"placed object {identity[2]} first became active at screen "
                f"X={first_x}, already inside the 16:9 view",
                object={"level": identity[0], "type": identity[1],
                        "placement": identity[2], "screen_x": first_x,
                        "slot": int(first_sprite["slot"])},
                reference_frame=first_frame - step))
        last_state, last_sprite, last_x = samples[-1]
        last_frame = int(last_state["frame"])
        following = frames.get(last_frame + step)
        owner = terrain_owner(last_state)
        age_layer = owner if owner is not None else 0
        if following and int(following["level"]) == identity[0] and \
                ages.get((last_frame, age_layer), 0) >= 60 and \
                left < last_x < right and boundary_relevant(last_x):
            output.append(finding(
                "object_despawn_inside_wide_view", "strong", last_frame, "obj",
                f"placed object {identity[2]} stopped being active at screen "
                f"X={last_x}, still inside the 16:9 view",
                object={"level": identity[0], "type": identity[1],
                        "placement": identity[2], "screen_x": last_x,
                        "slot": int(last_sprite["slot"])},
                reference_frame=last_frame + step))
    return output


def analyze_object_visibility(images: dict, metadata_by_frame: dict) -> list[dict]:
    output = []
    for frame, (width, height, pixels, _) in images.get("obj", {}).items():
        state = metadata_by_frame.get(frame)
        if not state:
            continue
        colors = Counter(
            pixels[offset:offset + 3] for offset in range(0, len(pixels), 3))
        backdrop = colors.most_common(1)[0][0]
        camera_x, camera_y = (int(value) for value in state["camera"])
        for sprite in state.get("sprites", []):
            if not int(sprite.get("placement", 0)) or \
                    not int(sprite.get("graphic", 0)):
                continue
            screen_x = signed_delta((int(sprite["world"][0]) - camera_x) & 0xFFFF)
            screen_y = signed_delta((int(sprite["world"][1]) - camera_y) & 0xFFFF)
            if not (-EXTRA_LEFT <= screen_x < 0 or
                    NATIVE_WIDTH <= screen_x < NATIVE_WIDTH + EXTRA_LEFT):
                continue
            output_x = screen_x + EXTRA_LEFT
            x0, x1 = max(0, output_x - 48), min(width, output_x + 49)
            y0, y1 = max(0, screen_y - 48), min(height, screen_y + 49)
            visible = False
            for y in range(y0, y1):
                for x in range(x0, x1):
                    offset = (y * width + x) * 3
                    if pixels[offset:offset + 3] != backdrop:
                        visible = True
                        break
                if visible:
                    break
            if not visible:
                output.append(finding(
                    "active_margin_object_without_obj_pixels", "strong",
                    frame, "obj",
                    f"placed object {int(sprite['placement'])} is active at "
                    f"({screen_x},{screen_y}) with graphic "
                    f"{int(sprite['graphic'])}, but the OBJ-only image has no "
                    f"nearby pixels",
                    object={"level": int(state["level"]),
                            "type": int(sprite["type"]),
                            "placement": int(sprite["placement"]),
                            "screen_x": screen_x, "screen_y": screen_y,
                            "slot": int(sprite["slot"])}))
    return output


def deduplicate_findings(items: list[dict], maximum: int) -> list[dict]:
    rank = {"exact": 0, "strong": 1, "suspect": 2}
    items.sort(key=lambda item: (rank.get(item["confidence"], 9),
                                 item["frame"], item["kind"],
                                 item.get("layer", "")))
    output = []
    seen = set()
    for item in items:
        bucket = item["frame"] // 60 if item["kind"] == \
            "margin_world_tile_mismatch" else None
        key = (item["kind"], item.get("layer"), item.get("side"),
               item.get("region"), bucket,
               (item.get("object") or {}).get("placement"))
        if key in seen:
            continue
        seen.add(key)
        output.append(item)
        if len(output) >= maximum:
            break
    return output


def parse_trace(stderr_path: Path, start: int, end: int) -> list[dict]:
    output = []
    for line in stderr_path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith(FRAME_PREFIX):
            continue
        state = json.loads(line[len(FRAME_PREFIX):])
        if start <= int(state["frame"]) <= end:
            output.append(state)
    return output


def capture_layer(executable: Path, rom: Path, output_dir: Path, layer: str,
                  start: int, end: int, step: int, environment: dict,
                  timeout: float) -> tuple[Path, Path]:
    layer_dir = output_dir / "raw" / layer
    layer_dir.mkdir(parents=True, exist_ok=True)
    stdout_path = layer_dir / "stdout.log"
    stderr_path = layer_dir / "stderr.log"
    run_env = os.environ.copy()
    run_env.update(environment)
    run_env.update({
        "DKC2_WIDESCREEN": "1",
        "SNESRECOMP_LAYER_MASK": str(LAYER_MASKS[layer]),
        "DKC2_FRAME_PPM_PREFIX": str((layer_dir / "frame").resolve()),
        "DKC2_FRAME_PPM_START": str(start),
        "DKC2_FRAME_PPM_END": str(end),
        "DKC2_FRAME_PPM_STEP": str(step),
    })
    if layer == "composite":
        run_env["DKC2_WIDESCREEN_TRACE"] = "1"
        run_env["DKC2_WIDESCREEN_TRACE_START"] = str(start)
        run_env["DKC2_WIDESCREEN_TRACE_STEP"] = str(step)
    with stdout_path.open("w", encoding="utf-8") as stdout, \
            stderr_path.open("w", encoding="utf-8") as stderr:
        result = subprocess.run(
            [str(executable), str(rom), str(end + 1)],
            cwd=str(executable.parent), env=run_env, stdout=stdout,
            stderr=stderr, timeout=timeout, check=False)
    if result.returncode != 0:
        raise RuntimeError(
            f"{layer} capture exited with {result.returncode}; see {stderr_path}")
    return stdout_path, stderr_path


def load_images(output_dir: Path, layers: tuple[str, ...], start: int,
                end: int, step: int) -> tuple[dict, list[dict]]:
    output = {}
    integrity_findings = []
    for layer in layers:
        frames = {}
        for frame in range(start, end + 1, step):
            path = output_dir / "raw" / layer / f"frame_{frame:06d}.ppm"
            if not path.is_file():
                integrity_findings.append({
                    "kind": "capture_integrity",
                    "confidence": "exact",
                    "frame": frame,
                    "layer": layer,
                    "evidence": f"{path.name} is missing; detectors skipped this sample",
                })
                continue
            try:
                width, height, pixels = read_ppm(path)
            except RuntimeError as error:
                integrity_findings.append({
                    "kind": "capture_integrity",
                    "confidence": "exact",
                    "frame": frame,
                    "layer": layer,
                    "evidence": f"{path.name} could not be decoded; detectors skipped "
                                f"this sample ({error.args[0].split(':', 1)[0]})",
                })
                continue
            frames[frame] = (width, height, pixels, path)
        output[layer] = frames
    return output, integrity_findings


def attach_evidence(output_dir: Path, findings: list[dict], images: dict) -> None:
    evidence_dir = output_dir / "evidence"
    for item in findings:
        layer = item.get("layer", "composite")
        if layer not in images:
            layer = "composite"
        paths = []
        for evidence_layer in dict.fromkeys((layer, "composite")):
            if evidence_layer not in images:
                continue
            for label, frame in (("event", item["frame"]),
                                 ("reference", item.get("reference_frame"))):
                if frame is None or frame not in images[evidence_layer]:
                    continue
                width, height, pixels, _ = images[evidence_layer][frame]
                path = evidence_dir / \
                    f"{evidence_layer}_{frame:06d}_{label}.bmp"
                if not path.exists():
                    write_bmp24(path, width, height, pixels)
                paths.append(str(path.relative_to(output_dir)).replace("\\", "/"))
        item["images"] = paths


def write_html(output_dir: Path, report: dict) -> None:
    rows = []
    for item in report["findings"]:
        pictures = "".join(
            f"<a href='{html.escape(path)}'><img src='{html.escape(path)}'></a>"
            for path in item.get("images", []))
        rows.append(
            "<tr>"
            f"<td>{html.escape(item['confidence'])}</td>"
            f"<td>{html.escape(item['kind'])}</td>"
            f"<td>{item['frame']}</td>"
            f"<td>{html.escape(item.get('layer', ''))}</td>"
            f"<td>{html.escape(item['evidence'])}</td>"
            f"<td class='shots'>{pictures}</td></tr>")
    if not rows:
        rows.append("<tr><td colspan='6'>No automatic candidate was found. "
                    "This is not a correctness proof.</td></tr>")
    observations = []
    for item in report.get("safe_observations", []):
        observations.append(
            "<tr>"
            f"<td>{html.escape(item['kind'])}</td>"
            f"<td>{item['frame']}</td>"
            f"<td>{html.escape(item.get('layer', ''))}</td>"
            f"<td>{html.escape(item['evidence'])}</td></tr>")
    summary = html.escape(json.dumps(report["summary"], indent=2))
    document = f"""<!doctype html>
<meta charset="utf-8"><title>DKC2 widescreen route audit</title>
<style>
body{{background:#111;color:#eee;font:15px system-ui;margin:2rem}}
table{{border-collapse:collapse;width:100%}}th,td{{border:1px solid #555;padding:.5rem;vertical-align:top}}
th{{background:#282828;position:sticky;top:0}}img{{width:260px;image-rendering:pixelated;margin:.2rem}}
.shots{{min-width:280px}}code,pre{{background:#1d1d1d;padding:1rem;white-space:pre-wrap}}
</style>
<h1>DKC2 widescreen route audit</h1>
<p>This report ranks deterministic candidates. Exact means the runtime observed
a concrete margin-source miss; strong means two independent observations
disagree. Neither label proves artistic intent without a reference oracle.</p>
<pre>{summary}</pre>
<table><thead><tr><th>Confidence</th><th>Detector</th><th>Frame</th><th>Layer</th><th>Evidence</th><th>Images</th></tr></thead>
<tbody>{''.join(rows)}</tbody></table>
<h2>Safe diagnostic observations</h2>
<p>These events are retained for provenance but are not actionable defects.</p>
<table><thead><tr><th>Detector</th><th>Frame</th><th>Layer</th><th>Evidence</th></tr></thead>
<tbody>{''.join(observations) if observations else "<tr><td colspan='4'>None</td></tr>"}</tbody></table>
"""
    (output_dir / "index.html").write_text(document, encoding="utf-8")


def main() -> int:
    args = parse_args()
    executable = args.executable.expanduser().resolve()
    rom = args.rom.expanduser().resolve()
    output_dir = args.output_dir.expanduser().resolve()
    if not executable.is_file() or not rom.is_file():
        raise RuntimeError("the executable and ROM must both exist")
    if args.start < 0 or args.step < 1 or args.max_findings < 1:
        raise RuntimeError("start must be non-negative; step/max-findings positive")
    end = args.end
    if end is None:
        if not args.input_recording:
            raise RuntimeError("--end is required without --input-recording")
        end = sum(1 for line in args.input_recording.read_text(
            encoding="ascii").splitlines() if line.strip()) - 1
    if end < args.start:
        raise RuntimeError("--end must not precede --start")
    layers = tuple(item.strip().lower() for item in args.layers.split(",")
                   if item.strip())
    if not layers or len(set(layers)) != len(layers):
        raise RuntimeError("--layers must contain unique names")
    unknown = [layer for layer in layers if layer not in LAYER_MASKS]
    if unknown:
        raise RuntimeError(f"unknown layer(s): {', '.join(unknown)}")
    if "composite" not in layers:
        raise RuntimeError("the route audit requires the composite layer")
    if args.reuse_capture and not (output_dir / "raw" / "composite" /
                                   "stderr.log").is_file():
        raise RuntimeError("--reuse-capture requires an existing raw capture")
    if not args.reuse_capture and output_dir.exists() and any(output_dir.iterdir()):
        raise RuntimeError(f"output directory is not empty: {output_dir}")
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

    composite_stderr = output_dir / "raw" / "composite" / "stderr.log"
    if not args.reuse_capture:
        for layer in layers:
            print(
                f"capturing {layer} frames {args.start}..{end} step {args.step}...",
                flush=True)
            _, stderr_path = capture_layer(
                executable, rom, output_dir, layer, args.start, end, args.step,
                environment, args.timeout)
            if layer == "composite":
                composite_stderr = stderr_path
    metadata = parse_trace(composite_stderr, args.start, end)
    expected_frames = len(range(args.start, end + 1, args.step))
    if len(metadata) != expected_frames:
        raise RuntimeError(
            f"widescreen trace has {len(metadata)} samples; expected {expected_frames}")
    images, integrity_findings = load_images(
        output_dir, layers, args.start, end, args.step)
    metadata_by_frame = {int(item["frame"]): item for item in metadata}

    all_findings = list(integrity_findings)
    ages = build_layer_age(metadata)
    all_findings.extend(analyze_shadow_misses(metadata, ages))
    all_findings.extend(analyze_seams(images, metadata_by_frame, ages))
    all_findings.extend(analyze_world_tiles(metadata_by_frame, ages))
    all_findings.extend(analyze_object_lifetimes(metadata, args.step, ages))
    all_findings.extend(analyze_object_visibility(images, metadata_by_frame))
    safe_kinds = {"verified_blank_margin_fallback"}
    safe_observations = deduplicate_findings(
        [item for item in all_findings if item["kind"] in safe_kinds],
        args.max_findings)
    findings = deduplicate_findings(
        [item for item in all_findings if item["kind"] not in safe_kinds],
        args.max_findings)
    attach_evidence(output_dir, findings, images)
    counts = defaultdict(int)
    for item in findings:
        counts[item["kind"]] += 1
    report = {
        "schema": "dkc2-widescreen-route-audit-v1",
        "range": {"start": args.start, "end": end, "step": args.step,
                  "samples": expected_frames},
        "layers": list(layers),
        "rom": {"basename": rom.name, "sha256": sha256_file(rom)},
        "private_inputs": private_inputs,
        "summary": {
            "findings_retained": len(findings),
            "safe_observations_retained": len(safe_observations),
            "findings_before_deduplication": len(all_findings),
            "capture_integrity_errors": len(integrity_findings),
            "by_detector": dict(sorted(counts.items())),
            "limitations": [
                "No image-only heuristic can prove artistic correctness.",
                "Per-scanline HDMA scroll can lower confidence for affected layers.",
                "Unknown object intent requires a reference trace or semantic rule.",
                "A clean report means no encoded detector fired, not no defect exists.",
            ],
        },
        "findings": findings,
        "safe_observations": safe_observations,
    }
    (output_dir / "report.json").write_text(
        json.dumps(report, indent=2) + "\n", encoding="utf-8")
    write_html(output_dir, report)
    print(json.dumps({
        "report": str(output_dir / "report.json"),
        "html": str(output_dir / "index.html"),
        "findings": len(findings),
        "safe_observations": len(safe_observations),
        "by_detector": dict(sorted(counts.items())),
    }))
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.SubprocessError,
            json.JSONDecodeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
