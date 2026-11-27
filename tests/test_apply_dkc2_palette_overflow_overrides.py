import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc2_palette_overflow_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc2_palette_overflow_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)

# The generated-C contract, spelled out here rather than imported from the
# patcher, so drift in the patcher's hook spelling or source PC fails.
CALL = "    Dkc2SpritePaletteOverflowHook(cpu);"
ANCHOR = "    cpu_trace_block(cpu, 0xBB8A8D);"
INCLUDE = '#include "dkc2_palette_overflow.h"'

# Synthetic generated C shaped like the v2 emitter's request_palette_direct:
# the overflow block at $BB:8A8D reads $05A7 and stores it to $05F7; another
# routine has its own $8A8D-looking trace that must stay untouched.
FIXTURE = """\
#include "cpu_state.h"
#include "funcs.h"
RecompReturn other_M0X0(CpuState *cpu) {
""" + ANCHOR + """
}
RecompReturn request_palette_direct_M0X0(CpuState *cpu) {
  L_8A86_M0X0:
    cpu_trace_block(cpu, 0xBB8A86);
    goto L_8A8D_M0X0; /* implicit fall-through */
  L_8A8D_M0X0:
""" + ANCHOR + """
    WatchdogCheck();
    uint16 _v9 = cpu_read16(cpu, cpu->DB, (uint16)(0x05a7));
    uint16 _v10 = cpu_read_a16(cpu);
    cpu_write16(cpu, cpu->DB, (uint16)(0x05f7), _v10);
  L_8AA1_M0X0:
    cpu_trace_block(cpu, 0xBB8AA1);
}
"""


class PatchTextTests(unittest.TestCase):
    def test_inserts_hook_at_overflow_block_only(self):
        patched = MODULE.patch_text(FIXTURE)
        self.assertEqual(patched.count(CALL), 1)
        self.assertEqual(patched.count(ANCHOR), 2)
        self.assertIn(ANCHOR + "\n" + CALL + "\n    WatchdogCheck();", patched)
        self.assertLess(patched.index("request_palette_direct_M0X0"),
                        patched.index(CALL))
        self.assertIn('#include "funcs.h"\n' + INCLUDE, patched)

    def test_idempotent(self):
        once = MODULE.patch_text(FIXTURE)
        self.assertEqual(MODULE.patch_text(once), once)

    def test_rejects_changed_shapes(self):
        cases = {
            "missing routine": FIXTURE.replace("request_palette_direct", "x"),
            "missing block": FIXTURE.replace(
                "  L_8A8D_M0X0:\n" + ANCHOR, "  L_8A8D_M0X0:"),
            "no $05A7 read": FIXTURE.replace("0x05a7", "0x05a9"),
            "no $05F7 write": FIXTURE.replace("0x05f7", "0x05f9"),
            "read after block": FIXTURE.replace(
                "    uint16 _v9 = cpu_read16(cpu, cpu->DB, (uint16)(0x05a7));\n",
                "").replace(
                "    cpu_trace_block(cpu, 0xBB8AA1);\n",
                "    cpu_trace_block(cpu, 0xBB8AA1);\n"
                "    uint16 _v9 = cpu_read16(cpu, cpu->DB, (uint16)(0x05a7));\n"),
        }
        for name, text in cases.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    MODULE.patch_text(text)

    def test_rejects_misplaced_hook(self):
        broken = FIXTURE.replace(
            "    cpu_trace_block(cpu, 0xBB8AA1);\n",
            "    cpu_trace_block(cpu, 0xBB8AA1);\n" + CALL + "\n")
        with self.assertRaises(ValueError):
            MODULE.patch_text(broken)


class ApplyTests(unittest.TestCase):
    def test_applies_to_the_single_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            unit = directory / "bankbb_parta6_v2.c"
            unit.write_bytes(FIXTURE.encode("utf-8"))
            (directory / "bankbb_part000_v2.c").write_text(
                "#include \"funcs.h\"\n", encoding="utf-8")
            self.assertEqual(MODULE.apply(directory), unit)
            first = unit.read_bytes()
            self.assertIn(CALL.encode(), first)
            self.assertNotIn(b"\r\n", first)
            MODULE.apply(directory)
            self.assertEqual(unit.read_bytes(), first)

    def test_requires_exactly_one_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                MODULE.apply(Path(directory))

    def test_rejects_multiple_units(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            for name in ("bankbb_parta6_v2.c", "bankbb_parta7_v2.c"):
                (directory / name).write_bytes(FIXTURE.encode("utf-8"))
            with self.assertRaises(ValueError):
                MODULE.apply(directory)
            for name in ("bankbb_parta6_v2.c", "bankbb_parta7_v2.c"):
                self.assertEqual((directory / name).read_bytes(),
                                 FIXTURE.encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
