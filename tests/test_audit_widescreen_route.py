#!/usr/bin/env python3

from __future__ import annotations

import importlib.util
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "audit_widescreen_route", ROOT / "scripts" / "audit_widescreen_route.py")
AUDIT = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(AUDIT)


def state(frame: int, h_scroll: int = 0, sprites=None, west_miss: int = 0):
    return {
        "frame": frame,
        "level": 3,
        "game_mode": 1,
        "game_sub_mode": 15,
        "camera": [h_scroll, 0],
        "terrain_vram": 0x7000,
        "ppu": {
            "mode": 1,
            "inidisp": 15,
            "main": 1,
            "sub": 0,
            "h": [h_scroll, 0, 0, 0],
            "v": [0, 0, 0, 0],
            "bg_sc": [0x70, 0, 0, 0],
            "wide": 1,
            "clamp": 0,
            "mirror": 0,
            "repeat": 0,
        },
        "shadow": [
            {"west_hit": 0, "west_miss": west_miss,
             "east_hit": 0, "east_miss": 0},
            {"west_hit": 0, "west_miss": 0,
             "east_hit": 0, "east_miss": 0},
        ],
        "sprites": sprites or [],
    }


def image(color=(20, 30, 40)):
    return bytearray(color * (AUDIT.WIDE_WIDTH * AUDIT.HEIGHT))


def fill_patch(pixels: bytearray, x: int, y: int, color):
    for row in range(8):
        for col in range(8):
            offset = ((y + row) * AUDIT.WIDE_WIDTH + x + col) * 3
            pixels[offset:offset + 3] = bytes(color)


class RouteAuditTests(unittest.TestCase):
    def test_ppm_reader_and_bmp_writer(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            ppm = root / "sample.ppm"
            ppm.write_bytes(b"P6\n2 1\n255\n" + bytes((1, 2, 3, 4, 5, 6)))
            width, height, pixels = AUDIT.read_ppm(ppm)
            self.assertEqual((width, height), (2, 1))
            self.assertEqual(pixels, bytes((1, 2, 3, 4, 5, 6)))
            bmp = root / "sample.bmp"
            AUDIT.write_bmp24(bmp, width, height, pixels)
            self.assertEqual(bmp.read_bytes()[:2], b"BM")

    def test_ppm_reader_preserves_whitespace_valued_first_pixel(self):
        with tempfile.TemporaryDirectory() as directory:
            ppm = Path(directory) / "space-first.ppm"
            pixels = bytes((0x20, 0x1A, 0x20, 4, 5, 6))
            ppm.write_bytes(b"P6\n2 1\n255\n" + pixels)
            width, height, decoded = AUDIT.read_ppm(ppm)
            self.assertEqual((width, height), (2, 1))
            self.assertEqual(decoded, pixels)

    def test_capture_integrity_error_does_not_abort_other_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            raw = root / "raw" / "composite"
            raw.mkdir(parents=True)
            (raw / "frame_000000.ppm").write_bytes(
                b"P6\n1 1\n255\n" + bytes((1, 2, 3)))
            (raw / "frame_000001.ppm").write_bytes(b"P6\n1 1\n255\n")
            images, findings = AUDIT.load_images(
                root, ("composite",), 0, 2, 1)
            self.assertEqual(set(images["composite"]), {0})
            self.assertEqual(len(findings), 2)
            self.assertTrue(all(item["kind"] == "capture_integrity"
                                for item in findings))

    def test_old_native_boundary_seam_is_detected(self):
        pixels = image()
        for y in range(AUDIT.HEIGHT):
            for x in range(AUDIT.EXTRA_LEFT):
                offset = (y * AUDIT.WIDE_WIDTH + x) * 3
                pixels[offset:offset + 3] = b"\xff\xff\xff"
        images = {"bg1": {0: (
            AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, bytes(pixels), Path("unused")),
            1: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, bytes(pixels),
                Path("unused2"))}}
        findings = AUDIT.analyze_seams(
            images, {0: state(0), 1: state(1)},
            {(0, 0): 60, (1, 0): 61})
        self.assertTrue(any(item["kind"] == "native_boundary_seam" and
                            item["side"] == "left" for item in findings))

    def test_isolated_authored_edge_is_not_called_a_seam(self):
        plain = bytes(image())
        edged = image()
        for y in range(AUDIT.HEIGHT):
            for x in range(AUDIT.EXTRA_LEFT):
                offset = (y * AUDIT.WIDE_WIDTH + x) * 3
                edged[offset:offset + 3] = b"\xff\xff\xff"
        images = {"composite": {
            0: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, plain, Path("a")),
            1: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, bytes(edged), Path("b")),
            2: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, plain, Path("c")),
        }}
        metadata = {frame: state(frame) for frame in range(3)}
        ages = {(frame, 0): 60 + frame for frame in range(3)}
        self.assertEqual(AUDIT.analyze_seams(images, metadata, ages), [])

    def test_same_world_tile_margin_native_mismatch_is_detected(self):
        margin = image()
        native = image()
        # World tile 36 appears at output x=331 when hScroll=0 (right margin)
        # and at x=267 when hScroll=64 (native view).
        fill_patch(margin, 331, 0, (255, 0, 0))
        fill_patch(margin, 331, 8, (255, 0, 0))
        fill_patch(native, 267, 0, (0, 255, 0))
        fill_patch(native, 267, 8, (0, 255, 0))
        images = {"bg1": {
            0: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, bytes(margin), Path("a")),
            16: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, bytes(native), Path("b")),
        }}
        margin_state = state(0, 0)
        native_state = state(16, 64)
        margin_state["terrain_tiles"] = [
            [288, 0, 36, 0, 0x1111], [288, 8, 36, 1, 0x1112]]
        native_state["terrain_tiles"] = [
            [224, 0, 36, 0, 0x2221], [224, 8, 36, 1, 0x2222]]
        findings = AUDIT.analyze_world_tiles(
            {0: margin_state, 16: native_state},
            {(0, 0): 60, (16, 0): 76})
        self.assertTrue(any(item["kind"] == "margin_world_tile_mismatch" and
                            [36, 0] in item["worlds"] for item in findings))

    def test_shadow_miss_delta_is_exact_evidence(self):
        first = state(0, west_miss=2)
        second = state(64, west_miss=11)
        first["shadow"][0]["west_blank"] = 2
        second["shadow"][0]["west_blank"] = 11
        findings = AUDIT.analyze_shadow_misses(
            [first, second], {(0, 0): 0, (64, 0): 64})
        self.assertEqual(len(findings), 1)
        self.assertEqual(findings[0]["west_blank"], 9)
        self.assertEqual(findings[0]["confidence"], "strong")

    def test_large_blank_burst_is_retained_during_camera_motion(self):
        first = state(0)
        second = state(1)
        first["shadow"][0]["east_miss"] = 0
        second["shadow"][0]["east_miss"] = 1120
        first["shadow"][0]["east_blank"] = 0
        second["shadow"][0]["east_blank"] = 1120
        findings = AUDIT.analyze_shadow_misses(
            [first, second], {(0, 0): 0, (1, 0): 0})
        self.assertEqual(len(findings), 1)
        self.assertEqual(
            findings[0]["kind"], "large_verified_blank_margin_fallback")
        self.assertEqual(findings[0]["east_blank"], 1120)

    def test_raw_vram_fallback_is_exact_evidence(self):
        first = state(0)
        second = state(4)
        first["shadow"][0]["west_raw"] = 0
        second["shadow"][0]["west_raw"] = 7
        findings = AUDIT.analyze_shadow_misses(
            [first, second], {(0, 0): 0, (4, 0): 4})
        self.assertEqual(findings[0]["kind"], "raw_vram_margin_fallback")
        self.assertEqual(findings[0]["confidence"], "exact")

    def test_placed_object_spawn_inside_wide_view_is_detected(self):
        sprite = {
            "slot": 4, "type": 0x1234, "world": [270, 20],
            "graphic": 1, "display": 0, "state": 1, "sub_state": 0,
            "placement": 77, "despawn_time": 8, "despawn_countdown": 8,
        }
        findings = AUDIT.analyze_object_lifetimes(
            [state(0), state(1, sprites=[sprite]),
             state(2, sprites=[sprite]), state(3)], 1,
            {(1, 0): 60, (2, 0): 61})
        kinds = {item["kind"] for item in findings}
        self.assertIn("object_spawn_inside_wide_view", kinds)
        self.assertIn("object_despawn_inside_wide_view", kinds)

    def test_coarse_samples_do_not_claim_object_lifecycle_edges(self):
        metadata = [
            state(0, h_scroll=100, sprites=[]),
            state(12, h_scroll=100, sprites=[{
                "slot": 0, "type": 7, "world": [90, 10],
                "graphic": 1, "display": 0, "state": 0,
                "sub_state": 0, "placement": 4,
                "despawn_time": 0, "despawn_countdown": 0,
            }]),
        ]
        ages = {(0, 0): 60, (12, 0): 72}
        self.assertEqual(
            AUDIT.analyze_object_lifetimes(metadata, 12, ages), [])

    def test_active_margin_object_without_obj_pixels_is_detected(self):
        sprite = {
            "slot": 4, "type": 0x1234, "world": [270, 20],
            "graphic": 7, "display": 0, "state": 1, "sub_state": 0,
            "placement": 77, "despawn_time": 8, "despawn_countdown": 8,
        }
        pixels = bytes(image())
        images = {"obj": {4: (
            AUDIT.WIDE_WIDTH, AUDIT.HEIGHT, pixels, Path("unused"))}}
        findings = AUDIT.analyze_object_visibility(
            images, {4: state(4, sprites=[sprite])})
        self.assertEqual(findings[0]["kind"],
                         "active_margin_object_without_obj_pixels")

        visible = image()
        fill_patch(visible, 310, 16, (255, 255, 255))
        findings = AUDIT.analyze_object_visibility(
            {"obj": {4: (AUDIT.WIDE_WIDTH, AUDIT.HEIGHT,
                          bytes(visible), Path("unused"))}},
            {4: state(4, sprites=[sprite])})
        self.assertEqual(findings, [])

    def test_trace_parser_ignores_other_stderr(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "stderr.log"
            path.write_text(
                "noise\n" + AUDIT.FRAME_PREFIX +
                '{"frame":4,"ppu":{},"shadow":[],"sprites":[]}\n',
                encoding="utf-8")
            parsed = AUDIT.parse_trace(path, 0, 8)
            self.assertEqual([item["frame"] for item in parsed], [4])


if __name__ == "__main__":
    unittest.main()
