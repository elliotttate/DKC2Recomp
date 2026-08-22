import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))

TCP_SPEC = importlib.util.spec_from_file_location(
    "capture_tcp_screenshot", SCRIPTS / "capture_tcp_screenshot.py")
TCP = importlib.util.module_from_spec(TCP_SPEC)
assert TCP_SPEC.loader is not None
TCP_SPEC.loader.exec_module(TCP)

sys.modules["capture_tcp_screenshot"] = TCP
BUNDLE_SPEC = importlib.util.spec_from_file_location(
    "capture_widescreen_diagnostics",
    SCRIPTS / "capture_widescreen_diagnostics.py")
BUNDLE = importlib.util.module_from_spec(BUNDLE_SPEC)
assert BUNDLE_SPEC.loader is not None
BUNDLE_SPEC.loader.exec_module(BUNDLE)
ROUTE_SPEC = importlib.util.spec_from_file_location(
    "test_widescreen_bg1_route",
    SCRIPTS / "test_widescreen_bg1_route.py")
ROUTE = importlib.util.module_from_spec(ROUTE_SPEC)
assert ROUTE_SPEC.loader is not None
ROUTE_SPEC.loader.exec_module(ROUTE)


def write_test_bmp(path: Path, width: int, height: int, lit_pixels: set) -> None:
    stride = ((width * 3 + 3) // 4) * 4
    pixel_bytes = bytearray(stride * height)
    for x, y in lit_pixels:
        # Positive-height BMPs store the bottom row first; callers use logical
        # top-down coordinates to match diagnostic report bounds.
        offset = (height - 1 - y) * stride + x * 3
        pixel_bytes[offset:offset + 3] = b"\x10\x20\x30"
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<I", header, 2, 54 + len(pixel_bytes))
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<I", header, 14, 40)
    struct.pack_into("<i", header, 18, width)
    struct.pack_into("<i", header, 22, height)
    struct.pack_into("<H", header, 26, 1)
    struct.pack_into("<H", header, 28, 24)
    path.write_bytes(header + pixel_bytes)


class WidescreenDiagnosticTests(unittest.TestCase):
    def test_dkc2_wram_decodes_active_sprite_and_margin(self):
        wram = bytearray(0x20000)
        wram[TCP.DKC2_CAMERA_X:TCP.DKC2_CAMERA_X + 2] = (1000).to_bytes(2, "little")
        wram[TCP.DKC2_CAMERA_Y:TCP.DKC2_CAMERA_Y + 2] = (200).to_bytes(2, "little")
        wram[TCP.DKC2_LEVEL_TYPE:TCP.DKC2_LEVEL_TYPE + 2] = (
            9).to_bytes(2, "little")
        wram[TCP.DKC2_GAME_SUB_MODE:TCP.DKC2_GAME_SUB_MODE + 2] = (
            0x0F).to_bytes(2, "little")
        wram[TCP.DKC2_TERRAIN_VRAM:TCP.DKC2_TERRAIN_VRAM + 2] = (
            0x7800).to_bytes(2, "little")
        wram[TCP.DKC2_GAME_MODE:TCP.DKC2_GAME_MODE + 2] = (
            0x87E1).to_bytes(2, "little")
        wram[TCP.DKC2_DEMO_STATUS:TCP.DKC2_DEMO_STATUS + 2] = (
            1).to_bytes(2, "little")
        wram[TCP.DKC2_DEMO_SEQUENCE:TCP.DKC2_DEMO_SEQUENCE + 2] = (
            2).to_bytes(2, "little")
        base = TCP.DKC2_SPRITE_TABLE + 3 * TCP.DKC2_SPRITE_SIZE
        wram[base:base + 2] = (0xBEEF).to_bytes(2, "little")
        wram[base + 6:base + 8] = (1260).to_bytes(2, "little")
        wram[base + 10:base + 12] = (220).to_bytes(2, "little")
        wram[base + 0x1A:base + 0x1C] = (0x1234).to_bytes(2, "little")
        state = TCP.decode_dkc2_wram(bytes(wram))
        self.assertEqual(state["camera"], {"x": 1000, "y": 200})
        self.assertEqual(state["game_mode"], "0x87e1")
        self.assertEqual(state["attract_demo"], {
            "status": 1,
            "sequence": 2,
            "active": True,
        })
        self.assertEqual(state["screen_configuration"]["level_type"], 9)
        self.assertEqual(
            state["screen_configuration"]["terrain_vram_word_address"],
            "0x7800")
        self.assertEqual(len(state["active_sprites"]), 1)
        sprite = state["active_sprites"][0]
        self.assertEqual(sprite["slot"], 3)
        self.assertEqual(sprite["screen_x"], 260)
        self.assertEqual(sprite["region"], "right_margin")
        self.assertEqual(sprite["current_graphic"], "0x1234")

    def test_screen_classifier_follows_live_bg2_terrain_owner(self):
        state = {
            "level_number": 0x002C,
            "screen_configuration": {
                "terrain_vram_word_address": "0x7800",
                "game_sub_mode": 0x0F,
            },
        }
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x71", "0x79", "0x6d", "0x00"],
            "screenEnabled": ["0x17", "0x10"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["kind"], "standard_rolling_terrain")
        self.assertEqual(profile["terrain_owner"], "bg2")
        self.assertEqual(
            profile["level_map_layout"], "column_major_horizontal")
        self.assertEqual(profile["bg3_policy"], "rendered_scanline_repeat")
        self.assertTrue(profile["safe_for_object_widening"])

    def test_attract_demo_profile_retains_true_widescreen_classification(self):
        state = {
            "level_number": 0x000C,
            "attract_demo": {
                "status": 1,
                "sequence": 1,
                "active": True,
            },
            "screen_configuration": {
                "terrain_vram_word_address": "0x7000",
                "game_sub_mode": 0x0F,
            },
        }
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x71", "0x78", "0x00", "0x00"],
            "screenEnabled": ["0x01", "0x00"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["kind"], "standard_rolling_terrain")
        self.assertEqual(profile["terrain_owner"], "bg1")
        self.assertTrue(profile["safe_for_object_widening"])
        self.assertEqual(profile["attract_demo"]["sequence"], 1)

    def test_screen_classifier_fails_closed_when_target_does_not_match(self):
        state = {"screen_configuration": {
            "terrain_vram_word_address": "0x6400"}}
        ppu = {
            "bgmode": "0x01",
            "bgXsc": ["0x71", "0x79", "0x00", "0x00"],
            "screenEnabled": ["0x03", "0x00"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["kind"], "unclassified_mode1")
        self.assertIsNone(profile["terrain_owner"])
        self.assertFalse(profile["safe_for_object_widening"])

    def test_classifier_combines_main_and_sub_screen_layers(self):
        state = {
            "level_number": 0x0013,
            "screen_configuration": {
                "terrain_vram_word_address": "0x7800",
                "game_sub_mode": 0x03,
            },
        }
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x6c", "0x79", "0x68", "0x00"],
            "screenEnabled": ["0x01", "0x16"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["kind"], "standard_rolling_terrain")
        self.assertEqual(profile["terrain_owner"], "bg2")
        self.assertEqual(
            profile["level_map_layout"], "narrow_vertical_row_major")
        self.assertTrue(profile["safe_for_object_widening"])

    def test_screen_classifier_identifies_vertical_map_layout(self):
        state = {"screen_configuration": {
            "game_sub_mode": 0x0C,
            "terrain_vram_word_address": "0x7000",
        }}
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x71", "0x78", "0x00", "0x00"],
            "screenEnabled": ["0x01", "0x00"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["terrain_owner"], "bg1")
        self.assertEqual(profile["level_map_layout"], "row_major_vertical")

    def test_screen_classifier_identifies_bramble_square_layout(self):
        state = {"screen_configuration": {
            "game_sub_mode": 0x10,
            "terrain_vram_word_address": "0x7800",
        }}
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x79", "0x70", "0x74", "0x00"],
            "screenEnabled": ["0x17", "0x00"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["terrain_owner"], "bg1")
        self.assertEqual(
            profile["level_map_layout"],
            "row_major_square_192_byte_stride")
        self.assertTrue(profile["safe_for_object_widening"])

    def test_screen_classifier_identifies_standard_wasp_hive_square_layout(self):
        state = {
            "level_number": 0x0002,
            "screen_configuration": {
                "game_sub_mode": 0x03,
                "terrain_vram_word_address": "0x7800",
            },
        }
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x79", "0x70", "0x74", "0x00"],
            "screenEnabled": ["0x17", "0x00"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(
            profile["level_map_layout"],
            "row_major_square_192_byte_stride",
        )
        self.assertEqual(profile["terrain_owner"], "bg1")
        self.assertTrue(profile["safe_for_object_widening"])

    def test_unproven_square_or_special_profile_fails_closed(self):
        state = {"screen_configuration": {
            "game_sub_mode": 0x02,
            "terrain_vram_word_address": "0x7800",
        }}
        ppu = {
            "bgmode": 1,
            "bgXsc": ["0x79", "0x70", "0x74", "0x00"],
            "screenEnabled": ["0x17", "0x00"],
        }
        profile = BUNDLE.classify_dkc2_screen(state, ppu)
        self.assertEqual(profile["level_map_layout"], "square_or_special")
        self.assertFalse(profile["safe_for_object_widening"])

    def test_full_oam_inventory_classifies_both_margins(self):
        response = {"snaps": [{"f": 9, "slot": [
            [10, 250, 1, 1, 2],
            [20, 5, 1, 3, 4],
            [30, 100, 0, 5, 6],
            [240, 10, 0, 7, 8],
        ]}]}
        sprites = TCP.collect_oam_sprites(response)
        self.assertEqual(
            [item["region"] for item in sprites],
            ["left_margin", "right_margin", "native_view"])
        self.assertEqual(sprites[0]["screen_x"], -6)

    def test_bmp_metrics_measure_each_viewport_region(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "test.bmp"
            write_test_bmp(path, 342, 2, {(0, 0), (50, 0), (341, 1)})
            metrics = BUNDLE.read_bmp_margin_metrics(path)
            regions = metrics["regions"]
            self.assertEqual(regions["left_margin"]["non_black_pixels"], 1)
            self.assertEqual(regions["native_view"]["non_black_pixels"], 1)
            self.assertEqual(regions["right_margin"]["non_black_pixels"], 1)
            self.assertEqual(regions["left_margin"]["non_backdrop_pixels"], 1)
            self.assertEqual(regions["right_margin"]["non_backdrop_pixels"], 1)

    def test_bmp_metrics_measure_logical_upper_left_margin(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "top-left.bmp"
            write_test_bmp(
                path, 342, 224,
                {(0, 0), (27, 45), (0, 64), (43, 0), (341, 223)})
            region = BUNDLE.read_bmp_margin_metrics(path)[
                "regions"]["upper_left_margin"]
            self.assertEqual(region["bounds"], {
                "x": [0, 43],
                "y": [0, 64],
            })
            self.assertEqual(region["pixels"], 43 * 64)
            self.assertEqual(region["non_backdrop_pixels"], 2)

    def test_findings_distinguish_game_sprite_from_oam_stage(self):
        empty_metrics = {"regions": {
            "left_margin": {"non_backdrop_pixels": 0},
            "native_view": {"non_backdrop_pixels": 10},
            "right_margin": {"non_backdrop_pixels": 0},
        }}
        layers = {
            "composite": {
                "game_state": {"active_sprites": [
                    {"region": "right_margin"}]},
                "rendered_oam": [],
            },
            "bg1": {"image_metrics": empty_metrics},
        }
        findings = BUNDLE.build_findings(layers)
        self.assertEqual(
            {item["stage"] for item in findings},
            {"background_load_or_render", "object_render_or_cull"})

    def test_runtime_event_scan_separates_trap_from_abandon(self):
        with tempfile.TemporaryDirectory() as temp:
            log = Path(temp) / "stderr.log"
            log.write_text(
                "[UNRESOLVED-STUB TRAP HIT]\n[unresolved-abandon]\n",
                encoding="utf-8")
            events = BUNDLE.scan_runtime_events(log)
            self.assertEqual(events["unresolved_stub_traps"], 1)
            self.assertEqual(events["unresolved_abandons"], 1)
            self.assertEqual(events["interpreter_caps"], 0)

    def test_private_bg1_route_validator_targets_semantic_region(self):
        clean_events = {
            "unresolved_abandons": 0,
            "interpreter_caps": 0,
            "fatal_errors": 0,
        }
        report = {
            "schema": "dkc2-widescreen-diagnostic-v1",
            "frame": 6750,
            "layers": {
                "composite": {
                    "game_state": {"camera": {"x": 7103, "y": 298}},
                    "runtime_events": clean_events,
                },
                "bg1": {
                    "image_metrics": {"regions": {
                        "upper_left_margin": {"non_backdrop_pixels": 0},
                        "native_view": {"non_backdrop_pixels": 100},
                    }},
                    "runtime_events": clean_events,
                },
            },
        }
        ROUTE.validate_report(
            report, Path("."), 6750, 7103, 298, 0, None)
        report["layers"]["bg1"]["image_metrics"]["regions"][
            "upper_left_margin"]["non_backdrop_pixels"] = 224
        with self.assertRaisesRegex(RuntimeError, "contains 224"):
            ROUTE.validate_report(
                report, Path("."), 6750, 7103, 298, 0, None)


if __name__ == "__main__":
    unittest.main()
