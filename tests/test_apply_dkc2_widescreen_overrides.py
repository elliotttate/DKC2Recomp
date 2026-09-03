import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc2_widescreen_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc2_widescreen_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


RADIUS_FIXTURE = """\
#include "funcs.h"
RecompReturn check_placement_spawning_radius_M0X0(CpuState *cpu) {
  uint16 left = cpu_read16(cpu, 0, (uint16)(0xbbb92f + cpu->X));
  uint16 span = cpu_read16(cpu, 0, (uint16)(0xbbb931 + cpu->X));
}
"""

RENDER_FIXTURE = """\
#include "funcs.h"
RecompReturn render_world_sprites_CODE_B59F40_M0X0(CpuState *cpu) {
L_9FC9_M0X0:
  cpu_trace_block(cpu, 0xB59FC9);
  uint16 left_a = 0x30;
  uint16 span_a = 0x160;
L_9FDA_M0X0:
  cpu_trace_block(cpu, 0xB59FDA);
L_A00E_M0X0:
  cpu_trace_block(cpu, 0xB5A00E);
  uint16 left_b = 0x30;
  uint16 span_b = 0x160;
L_A021_M0X0:
  cpu_trace_block(cpu, 0xB5A021);
}
"""

BANANA_INDEX_FIXTURE = """\
#include "funcs.h"
RecompReturn update_banana_visibility_CODE_B5F3E9_M0X0(CpuState *cpu) {
L_F3C5_M0X0:
  cpu_trace_block(cpu, 0xB5F3C5);
  uint16 left_edge = 0x107;
L_F3CE_M0X0:
  cpu_trace_block(cpu, 0xB5F3CE);
}
"""

BANANA_RENDER_FIXTURE = """\
#include "funcs.h"
RecompReturn prepare_banana_render_bounds_CODE_B5F540_M0X0(CpuState *cpu) {
L_F51C_M0X0:
  cpu_trace_block(cpu, 0xB5F51C);
  uint16 right_edge = 0x100;
L_F534_M0X0:
  cpu_trace_block(cpu, 0xB5F534);
L_F545_M0X0:
  cpu_trace_block(cpu, 0xB5F545);
  uint16 total_span = 0x10f;
L_F54E_M0X0:
  cpu_trace_block(cpu, 0xB5F54E);
}
"""

BANANA_CLIP_FIXTURE = """\
#include "funcs.h"
RecompReturn render_banana_tiles_CODE_B5F5E1_M0X0(CpuState *cpu) {
L_F5F4_M0X0:
  cpu_trace_block(cpu, 0xB5F5F4);
  uint16 left_clip = 0xf;
L_F5F9_M0X0:
  cpu_trace_block(cpu, 0xB5F5F9);
L_F61B_M0X0:
  cpu_trace_block(cpu, 0xB5F61B);
  uint16 right_clip = 0x107;
L_F62B_M0X0:
  cpu_trace_block(cpu, 0xB5F62B);
L_F672_M0X1:
  cpu_trace_block(cpu, 0xB5F672);
  cpu_write_a_m(cpu, (uint16)(base_a));
  cpu_write_a_m(cpu, (uint16)(screen_x_a));
L_F6A4_M1X1:
  cpu_trace_block(cpu, 0xB5F6A4);
L_F6D5_M0X1:
  cpu_trace_block(cpu, 0xB5F6D5);
  cpu_write_a_m(cpu, (uint16)(base_b));
  cpu_write_a_m(cpu, (uint16)(screen_x_b));
L_F707_M1X1:
  cpu_trace_block(cpu, 0xB5F707);
}
"""


DESPAWN_FIXTURE = '''#include "funcs.h"

RecompReturn CODE_B59C52_M0X0(CpuState *cpu) {
L_9C63_M0X0:
  uint16 y_left = 0x80;
  uint16 y_span = 0x130;
L_9C79_M0X0:
  uint16 x_offset = 0x80;
  uint16 x_left = 0xb0;
  uint16 x_span = 0x160;
L_9C8F_M0X0:
  return RECOMP_RETURN_NORMAL;
}
'''


class WidescreenOverrideTests(unittest.TestCase):
    def make_generated_dir(self, root: Path) -> Path:
        generated = root / "generated"
        generated.mkdir()
        (generated / "radius.c").write_text(
            RADIUS_FIXTURE, encoding="utf-8")
        (generated / "renderer.c").write_text(
            RENDER_FIXTURE, encoding="utf-8")
        (generated / "banana_index.c").write_text(
            BANANA_INDEX_FIXTURE, encoding="utf-8")
        (generated / "banana_renderer.c").write_text(
            BANANA_RENDER_FIXTURE, encoding="utf-8")
        (generated / "banana_clip.c").write_text(
            BANANA_CLIP_FIXTURE, encoding="utf-8")
        (generated / "despawn.c").write_text(
            DESPAWN_FIXTURE, encoding="utf-8")
        return generated

    def test_applies_expected_adaptations_and_is_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            MODULE.apply_overrides(generated)
            first = {
                path.name: path.read_text(encoding="utf-8")
                for path in generated.glob("*.c")
            }
            MODULE.apply_overrides(generated)
            second = {
                path.name: path.read_text(encoding="utf-8")
                for path in generated.glob("*.c")
            }
            self.assertEqual(first, second)
            self.assertIn(MODULE.INCLUDE, first["radius.c"])
            self.assertIn(MODULE.INCLUDE, first["despawn.c"])
            self.assertIn(
                "uint16 x_left = Dkc2VideoExpandCullLeft(0xb0);",
                first["despawn.c"])
            self.assertIn(
                "uint16 x_span = Dkc2VideoExpandCullSpan(0x160);",
                first["despawn.c"])
            self.assertIn("uint16 y_span = 0x130;", first["despawn.c"])
            self.assertIn("uint16 x_offset = 0x80;", first["despawn.c"])
            self.assertIn(
                "Dkc2VideoExpandCullLeft(cpu_read16", first["radius.c"])
            self.assertIn(
                "Dkc2VideoExpandCullSpan(cpu_read16", first["radius.c"])
            self.assertEqual(
                first["renderer.c"].count(
                    "Dkc2VideoExpandCullLeft(0x30)"), 2)
            self.assertEqual(
                first["renderer.c"].count(
                    "Dkc2VideoExpandCullSpan(0x160)"), 2)
            self.assertIn(
                "Dkc2VideoExpandCullLeft(0x107)",
                first["banana_index.c"])
            self.assertIn(
                "Dkc2VideoExpandCullLeft(0x100)",
                first["banana_renderer.c"])
            self.assertIn(
                "Dkc2VideoExpandCullSpan(0x10f)",
                first["banana_renderer.c"])
            self.assertIn(
                "Dkc2VideoExpandCullLeft(0xf)",
                first["banana_clip.c"])
            self.assertIn(
                "Dkc2VideoExpandCullLeft(0x107)",
                first["banana_clip.c"])
            self.assertEqual(
                first["banana_clip.c"].count(
                    "Dkc2VideoPromoteOamXHigh"), 2)

    def test_fails_closed_when_an_anchor_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            generated = self.make_generated_dir(Path(directory))
            renderer = generated / "renderer.c"
            renderer.write_text(
                RENDER_FIXTURE.replace("0x160", "0x161", 1),
                encoding="utf-8")
            with self.assertRaisesRegex(ValueError, "native cull constant"):
                MODULE.apply_overrides(generated)


if __name__ == "__main__":
    unittest.main()
