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
RecompReturn CODE_B59F40_M0X0(CpuState *cpu) {
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


class WidescreenOverrideTests(unittest.TestCase):
    def make_generated_dir(self, root: Path) -> Path:
        generated = root / "generated"
        generated.mkdir()
        (generated / "radius.c").write_text(
            RADIUS_FIXTURE, encoding="utf-8")
        (generated / "renderer.c").write_text(
            RENDER_FIXTURE, encoding="utf-8")
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
