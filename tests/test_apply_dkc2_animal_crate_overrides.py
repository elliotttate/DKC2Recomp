import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc2_animal_crate_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc2_animal_crate_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)

BRANCH = MODULE.BRANCH
CALL = MODULE.CALL

# Synthetic generated C shaped like the v2 emitter's dismount_animal_action:
# the Squawks block reads $4A,x, shifts it, and branches on carry; an
# unrelated carry branch elsewhere must stay untouched.
FIXTURE = """\
#include "cpu_state.h"
#include "funcs.h"
RecompReturn other_M0X0(CpuState *cpu) {
""" + BRANCH + """
}
RecompReturn dismount_animal_action_M0X0(CpuState *cpu) {
  L_C461_M0X0:
    cpu_trace_block(cpu, 0xB8C461);
    uint16 _v9 = cpu_read16(cpu, 0x00, (uint16)(cpu->D + 0x004a + cpu->X));
    cpu->_flag_C = (((_v9 & 0xFFFF)) & 1) ? 1 : 0;
""" + BRANCH + """
    goto L_C466_M0X0; /* fall-through */
  L_C466_M0X0:
    cpu_trace_block(cpu, 0xB8C466);
""" + BRANCH + """
}
"""


class PatchTextTests(unittest.TestCase):
    def test_inserts_gate_before_squawks_branch_only(self):
        patched = MODULE.patch_text(FIXTURE)
        self.assertEqual(patched.count(CALL), 1)
        self.assertEqual(patched.count(BRANCH), 3)
        self.assertIn(CALL + "\n" + BRANCH, patched)
        call = patched.index(CALL)
        self.assertLess(patched.index("0xB8C461"), call)
        self.assertLess(call, patched.index("L_C466_M0X0:"))
        self.assertIn('#include "funcs.h"\n' + MODULE.INCLUDE, patched)

    def test_idempotent(self):
        once = MODULE.patch_text(FIXTURE)
        self.assertEqual(MODULE.patch_text(once), once)

    def test_rejects_changed_shapes(self):
        cases = {
            "missing routine": FIXTURE.replace("dismount_animal_action", "x"),
            "missing block": FIXTURE.replace("0xB8C461", "0xB8C462"),
            "no $4A read": FIXTURE.replace("0x004a", "0x004c"),
            "block outside routine": FIXTURE.replace(
                "RecompReturn other_M0X0(CpuState *cpu) {\n",
                "RecompReturn other_M0X0(CpuState *cpu) {\n"
                "    cpu_trace_block(cpu, 0xB8C461);\n").replace(
                "    cpu_trace_block(cpu, 0xB8C461);\n    uint16 _v9",
                "    uint16 _v9"),
            "no branch": FIXTURE.replace(
                "    cpu->_flag_C = (((_v9 & 0xFFFF)) & 1) ? 1 : 0;\n" + BRANCH,
                "    cpu->_flag_C = (((_v9 & 0xFFFF)) & 1) ? 1 : 0;"),
        }
        for name, text in cases.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    MODULE.patch_text(text)

    def test_rejects_gate_after_branch(self):
        broken = FIXTURE.replace(
            BRANCH + "\n    goto L_C466_M0X0;",
            BRANCH + "\n" + CALL + "\n    goto L_C466_M0X0;")
        with self.assertRaises(ValueError):
            MODULE.patch_text(broken)


class ApplyTests(unittest.TestCase):
    def test_applies_to_the_single_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            unit = directory / "bankb8_part444_v2.c"
            unit.write_bytes(FIXTURE.encode("utf-8"))
            (directory / "bankb8_part000_v2.c").write_text(
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
            for name in ("bankb8_part444_v2.c", "bankb8_part445_v2.c"):
                (directory / name).write_bytes(FIXTURE.encode("utf-8"))
            with self.assertRaises(ValueError):
                MODULE.apply(directory)
            for name in ("bankb8_part444_v2.c", "bankb8_part445_v2.c"):
                self.assertEqual((directory / name).read_bytes(),
                                 FIXTURE.encode("utf-8"))


if __name__ == "__main__":
    unittest.main()
