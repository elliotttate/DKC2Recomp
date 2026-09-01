#!/usr/bin/env python3
"""Synthetic checks for the widescreen state-corpus regression tool."""

from __future__ import annotations

import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "check_widescreen_state_corpus",
    ROOT / "scripts" / "check_widescreen_state_corpus.py")
CORPUS = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = CORPUS
SPEC.loader.exec_module(CORPUS)

WIDTH = CORPUS.NATIVE_WIDTH
HEIGHT = 16


def gradient(width: int, height: int, seed: int = 0) -> bytearray:
    pixels = bytearray(width * height * 3)
    for y in range(height):
        for x in range(width):
            offset = (y * width + x) * 3
            pixels[offset] = (x + seed) & 0xFF
            pixels[offset + 1] = (y * 7 + seed) & 0xFF
            pixels[offset + 2] = (x * 3 + y + seed) & 0xFF
    return pixels


def embed(native: bytes, extra: int, offset: int,
          margin_color: bytes = b"\x10\x20\x30") -> bytes:
    """Place a native frame inside a wide frame at the given column."""
    width = WIDTH + 2 * extra
    wide = bytearray(margin_color * (width * HEIGHT))
    for y in range(HEIGHT):
        src = y * WIDTH * 3
        dst = (y * width + offset) * 3
        wide[dst:dst + WIDTH * 3] = native[src:src + WIDTH * 3]
    return bytes(wide)


def trace_frame(bias: int, left: int, right: int, ready: int = 1,
                raw: int = 0) -> dict:
    return {
        "frame": 0, "level": 7, "game_sub_mode": 7,
        "camera": [500, 400], "camera_max": [2000, 800],
        "terrain_source": {"ready": ready},
        "ppu": {"mode": 1, "main": 0x17, "sub": 0x00,
                "wide": 3, "repeat": 4, "bias": bias,
                "left": left, "right": right, "bands": 1},
        "shadow": [{"west_raw": raw, "east_raw": 0},
                   {"west_raw": 0, "east_raw": 0}],
    }


def make_run(aspect: str, layer: str, frames: dict, trace: list) -> object:
    run = CORPUS.Run(aspect=aspect, layer=layer, directory=Path("."))
    run.returncode = 0
    run.completed = True
    run.frames = frames
    run.trace = {index: record for index, record in enumerate(trace)}
    return run


class CorpusAnalysisTest(unittest.TestCase):
    def test_center_check_follows_presentation_bias(self):
        extra = 43
        native = bytes(gradient(WIDTH, HEIGHT))
        bias = 43
        wide = embed(native, extra, extra - bias)
        runs = {
            ("4:3", "composite"): make_run(
                "4:3", "composite", {0: (WIDTH, HEIGHT, native)},
                [trace_frame(0, 0, 0)]),
            ("16:9", "composite"): make_run(
                "16:9", "composite", {0: (WIDTH + 2 * extra, HEIGHT, wide)},
                [trace_frame(bias, 43, 43)]),
        }
        result = CORPUS.analyze_state("lava", runs, 1, 3.0, 18.0)
        composite = result["runs"]["16:9/composite"]
        self.assertEqual(composite["center_differences"], 0)
        self.assertEqual(composite["center_edge_differences"], 0)
        self.assertEqual(composite["presentation_biases"], [43])
        self.assertEqual(
            [f["kind"] for f in result["findings"]
             if f["severity"] == "error"], [])

    def test_interior_and_edge_differences_are_separated(self):
        extra = 26
        width = WIDTH + 2 * extra
        # One continuous gradient across the whole wide frame, with the
        # native frame as its exact center crop, so no seam or blank finding
        # competes with the center check.
        wide = bytearray(gradient(width, HEIGHT))
        native = CORPUS.crop(bytes(wide), width, HEIGHT, extra, extra + WIDTH)
        # Corrupt one interior pixel and one pixel inside the edge band.
        interior = (3 * width + extra + 100) * 3
        edge = (5 * width + extra + 2) * 3
        wide[interior:interior + 3] = b"\xff\xff\xff"
        wide[edge:edge + 3] = b"\xff\xff\xff"
        runs = {
            ("4:3", "composite"): make_run(
                "4:3", "composite", {0: (WIDTH, HEIGHT, native)},
                [trace_frame(0, 0, 0)]),
            ("16:10", "composite"): make_run(
                "16:10", "composite", {0: (width, HEIGHT, bytes(wide))},
                [trace_frame(0, 26, 26)]),
        }
        result = CORPUS.analyze_state("ship", runs, 1, 3.0, 18.0)
        composite = result["runs"]["16:10/composite"]
        self.assertEqual(composite["center_differences"], 1)
        self.assertEqual(composite["center_edge_differences"], 1)
        self.assertEqual(
            [f["kind"] for f in result["findings"]],
            ["center_mismatch"])

    def test_boot_capture_skips_the_center_check(self):
        extra = 43
        native = bytes(gradient(WIDTH, HEIGHT))
        wide = embed(bytes(gradient(WIDTH, HEIGHT, seed=5)), extra, extra)
        runs = {
            ("4:3", "composite"): make_run(
                "4:3", "composite", {3300: (WIDTH, HEIGHT, native)},
                [trace_frame(0, 0, 0)]),
            ("16:9", "composite"): make_run(
                "16:9", "composite", {3300: (WIDTH + 2 * extra, HEIGHT, wide)},
                [trace_frame(20, 43, 43)]),
        }
        runs[("16:9", "composite")].trace = {3300: trace_frame(20, 43, 43)}
        result = CORPUS.analyze_state("boot", runs, 1, 3.0, 18.0,
                                      center_check=False)
        composite = result["runs"]["16:9/composite"]
        self.assertEqual(composite["center_differences"], 0)
        self.assertEqual(composite["presentation_biases"], [20])
        self.assertNotIn("center_mismatch",
                         [f["kind"] for f in result["findings"]])
        strict = CORPUS.analyze_state("boot", runs, 1, 3.0, 18.0)
        self.assertIn("center_mismatch",
                      [f["kind"] for f in strict["findings"]])

    def test_legacy_bias_reconstructs_the_pre_trace_formula(self):
        self.assertEqual(CORPUS.legacy_bias(0x100, 0x800, 43), 43)
        self.assertEqual(CORPUS.legacy_bias(0x800, 0x800, 43), -43)
        self.assertEqual(CORPUS.legacy_bias(0x300, 0x800, 43), 0)
        self.assertEqual(CORPUS.legacy_bias(0x100, 0x120, 43), 0)
        legacy_trace = [{
            "camera": [0x100, 0], "camera_max": [0x800, 0],
            "terrain_source": {"ready": 1}, "ppu": {"mode": 1}}]
        legacy_trace = {0: legacy_trace[0]}
        self.assertEqual(CORPUS.frame_bias(legacy_trace, 0, 43), 43)
        self.assertEqual(CORPUS.frame_bias({0: trace_frame(-10, 43, 43)}, 0,
                                           43), -10)
        self.assertEqual(CORPUS.visible_margins({0: trace_frame(0, 5, 43)}, 0,
                                                43), (5, 43))
        self.assertEqual(CORPUS.visible_margins(legacy_trace, 0, 43),
                         (43, 43))
        # A sparse boot capture resolves each frame to the latest record.
        sparse = {3300: trace_frame(0, 43, 43), 3350: trace_frame(7, 43, 43)}
        self.assertEqual(CORPUS.frame_bias(sparse, 3350, 43), 7)
        self.assertEqual(CORPUS.frame_bias(sparse, 3349, 43), 0)
        self.assertEqual(CORPUS.parse_trace(
            'widescreen_frame={"frame":12,"ppu":{"bias":3}}\n')[12]["ppu"]
            ["bias"], 3)

    def test_blank_visible_margin_and_raw_fallback_are_reported(self):
        extra = 43
        width = WIDTH + 2 * extra
        native = bytes(gradient(WIDTH, HEIGHT, seed=9))
        # The right margin is blank (backdrop color) while the left has art.
        wide = bytearray(embed(native, extra, extra, b"\x00\x00\x00"))
        for y in range(HEIGHT):
            for x in range(extra):
                offset = (y * width + x) * 3
                wide[offset:offset + 3] = b"\x40\x50\x60"
        # Make the backdrop the dominant color of the frame so the margin
        # classifier treats the left margin as content.
        for y in range(HEIGHT):
            for x in range(extra + 20, extra + WIDTH):
                offset = (y * width + x) * 3
                wide[offset:offset + 3] = b"\x00\x00\x00"
        runs = {
            ("4:3", "composite"): make_run(
                "4:3", "composite", {0: (WIDTH, HEIGHT, native)},
                [trace_frame(0, 0, 0)]),
            ("16:9", "composite"): make_run(
                "16:9", "composite", {0: (width, HEIGHT, bytes(wide))},
                [trace_frame(0, 43, 43, raw=3)]),
            ("16:9", "bg1"): make_run(
                "16:9", "bg1", {0: (width, HEIGHT, bytes(wide))},
                [trace_frame(0, 43, 43)]),
        }
        result = CORPUS.analyze_state("marsh", runs, 1, 3.0, 18.0)
        kinds = {(f["kind"], f.get("layer"), f.get("side"))
                 for f in result["findings"]}
        self.assertIn(("raw_vram_margin_fallback", "composite", None), kinds)
        self.assertIn(("blank_margin", "bg1", "right"), kinds)
        self.assertNotIn(("blank_margin", "bg1", "left"), kinds)
        # A clamped (zero-width) margin is never reported as blank.
        runs[("16:9", "bg1")].trace = {0: trace_frame(0, 43, 0)}
        result = CORPUS.analyze_state("marsh", runs, 1, 3.0, 18.0)
        kinds = {(f["kind"], f.get("layer"), f.get("side"))
                 for f in result["findings"]}
        self.assertNotIn(("blank_margin", "bg1", "right"), kinds)

    def test_reference_comparison_counts_center_and_margin_changes(self):
        extra = 43
        width = WIDTH + 2 * extra
        native = bytes(gradient(WIDTH, HEIGHT))
        wide = embed(native, extra, extra)
        changed = bytearray(wide)
        changed[0:3] = b"\xff\x00\x00"                     # left margin
        center = (2 * width + extra + 50) * 3
        changed[center:center + 3] = b"\x00\xff\x00"       # center
        with tempfile.TemporaryDirectory() as temp:
            reference = Path(temp) / "ref"
            frame_dir = reference / "lava" / "16x9" / "composite"
            frame_dir.mkdir(parents=True)
            header = f"P6\n{width} {HEIGHT}\n255\n".encode()
            (frame_dir / "frame_000000.ppm").write_bytes(header + wide)
            runs = {("16:9", "composite"): make_run(
                "16:9", "composite", {0: (width, HEIGHT, bytes(changed))},
                [trace_frame(0, 43, 43)])}
            comparison = CORPUS.compare_with_reference(
                "lava", runs, reference, 1)
        entry = comparison["16:9/composite"]
        self.assertEqual(entry["frames_compared"], 1)
        self.assertEqual(entry["center_pixels_changed"], 1)
        self.assertEqual(entry["margin_pixels_changed"], 1)

    def test_state_discovery_deduplicates_identical_states(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "a").mkdir()
            (root / "b").mkdir()
            (root / "a" / "one.sav").write_bytes(b"state-one")
            (root / "b" / "copy.sav").write_bytes(b"state-one")
            (root / "b" / "two.sav").write_bytes(b"state-two")
            states = CORPUS.discover_states([root])
            self.assertEqual([s.name for s in states], ["one.sav", "two.sav"])
            self.assertEqual(CORPUS.state_name(states[1], root), "b_two")


if __name__ == "__main__":
    unittest.main()
