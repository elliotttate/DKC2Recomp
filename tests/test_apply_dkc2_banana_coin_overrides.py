import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "apply_dkc2_banana_coin_overrides.py")
SPEC = importlib.util.spec_from_file_location(
    "apply_dkc2_banana_coin_overrides", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)

SCREEN, SAVE, LOAD = MODULE.SITES

# The generated-C contract, spelled out here rather than read from the
# patcher's SITES, so drift in a hook's spelling, its anchor line or the
# include fails these tests instead of passing them.
SCREEN_CALL = "    Dkc2BananaCoinsSavedHook(cpu);"
SAVE_CALL = "    Dkc2BananaCoinsSavedHook(cpu);"
LOAD_CALL = "    Dkc2BananaCoinsLoadedHook(cpu);"
INCLUDE = '#include "dkc2_banana_coins.h"'
SCREEN_BEFORE = "    goto L_A61D_M0X0; /* implicit fall-through */"
SAVE_BEFORE = ("    { uint16 _ret_s = cpu->S;  "
               "/* RTS pop hardware return frame */")
LOAD_BEFORE = ("    { uint16 _ret_s = cpu->S;  "
               "/* RTL pop hardware return frame */")

# Synthetic generated C shaped like the v2 emitter's two routines: the save
# copy reads the $56CA buffer and returns with RTS from block $BB:C5D8; the
# loader writes $08CC and returns with RTL from block $BB:C846. Unrelated
# returns in each routine must stay untouched.
SAVE_UNIT = """\
#include "cpu_state.h"
#include "funcs.h"
RecompReturn copy_save_to_sram_M0X0(CpuState *cpu) {
  L_C5CF_M0X0:
    cpu_trace_block(cpu, 0xBBC5CF);
    uint16 _v9 = cpu_read16(cpu, cpu->DB, (uint16)(0x56ca + cpu->Y));
  L_C5D8_M0X0:
    cpu_trace_block(cpu, 0xBBC5D8);
    WatchdogCheck();
""" + SAVE_BEFORE + """
      cpu->S = (uint16)(cpu->S + 1);
    }
}
"""

LOAD_UNIT = """\
#include "cpu_state.h"
#include "funcs.h"
RecompReturn CODE_BBC736_M0X0(CpuState *cpu) {
  L_C7A0_M0X0:
    cpu_trace_block(cpu, 0xBBC7A0);
    cpu_write8(cpu, cpu->DB, (uint16)(0x08cc), _v41);
  L_C846_M0X0:
    cpu_trace_block(cpu, 0xBBC846);
    WatchdogCheck();
""" + LOAD_BEFORE + """
      cpu->S = (uint16)(cpu->S + 1);
    }
  L_C745_M0X0:
    cpu_trace_block(cpu, 0xBBC745);
""" + LOAD_BEFORE + """
    }
}
"""


# The Kong Kollege "Save Game" screen copies the buffer in its own loop at
# $80:A613 and pulls DB in block $80:A61C before falling into $80:A61D. The
# skipped path jumps straight to $80:A61D and must stay untouched.
SCREEN_UNIT = """\
#include "cpu_state.h"
#include "funcs.h"
RecompReturn bank_80_A5B1_M0X0(CpuState *cpu) {
  L_A5CD_M0X0:
    cpu_trace_block(cpu, 0x80A5CD);
    if (cpu->_flag_Z == 1) { goto L_A61D_M0X0; }
  L_A613_M0X0:
    cpu_trace_block(cpu, 0x80A613);
    uint16 _v11 = cpu_read16(cpu, cpu->DB, (uint16)(0x56ca + cpu->Y));
    goto L_A61C_M0X0; /* fall-through */
  L_A61C_M0X0:
    cpu_trace_block(cpu, 0x80A61C);
    WatchdogCheck();
    cpu->DB = 0;
""" + SCREEN_BEFORE + """
  L_A61D_M0X0:
    cpu_trace_block(cpu, 0x80A61D);
""" + SCREEN_BEFORE.replace("A61D", "A6ED") + """
}
"""


class ContractTests(unittest.TestCase):
    def test_sites_match_the_contract(self):
        self.assertEqual(MODULE.INCLUDE, INCLUDE)
        self.assertEqual(
            [(site["call"], site["before"], site["block"])
             for site in MODULE.SITES],
            [(SCREEN_CALL, SCREEN_BEFORE,
              "    cpu_trace_block(cpu, 0x80A61C);"),
             (SAVE_CALL, SAVE_BEFORE, "    cpu_trace_block(cpu, 0xBBC5D8);"),
             (LOAD_CALL, LOAD_BEFORE, "    cpu_trace_block(cpu, 0xBBC846);")])


class PatchTextTests(unittest.TestCase):
    def test_inserts_each_hook_before_its_return(self):
        saved = MODULE.patch_text(SAVE_UNIT, [SAVE])
        self.assertEqual(saved.count(SAVE_CALL), 1)
        self.assertIn(SAVE_CALL + "\n" + SAVE_BEFORE, saved)
        self.assertLess(saved.index("0xBBC5D8"), saved.index(SAVE_CALL))
        self.assertIn('#include "funcs.h"\n' + INCLUDE, saved)

        loaded = MODULE.patch_text(LOAD_UNIT, [LOAD])
        self.assertEqual(loaded.count(LOAD_CALL), 1)
        self.assertEqual(loaded.count(LOAD_BEFORE), 2)
        call = loaded.index(LOAD_CALL)
        self.assertLess(loaded.index("0xBBC846"), call)
        self.assertLess(call, loaded.index("0xBBC745"))

    def test_save_screen_hook_follows_the_copy(self):
        patched = MODULE.patch_text(SCREEN_UNIT, [SCREEN])
        self.assertEqual(patched.count(SCREEN_CALL), 1)
        self.assertIn("    cpu->DB = 0;\n" + SCREEN_CALL + "\n" +
                      SCREEN_BEFORE, patched)
        self.assertLess(patched.index("0x80A61C"), patched.index(SCREEN_CALL))
        self.assertLess(patched.index(SCREEN_CALL), patched.index("0x80A61D"))
        self.assertEqual(MODULE.patch_text(patched, [SCREEN]), patched)
        for broken in (SCREEN_UNIT.replace("0x56ca", "0x56cc"),
                       SCREEN_UNIT.replace("0x80A61C", "0x80A61E")):
            with self.assertRaises(ValueError):
                MODULE.patch_text(broken, [SCREEN])

    def test_both_sites_in_one_unit(self):
        text = SAVE_UNIT + LOAD_UNIT.replace(
            '#include "cpu_state.h"\n#include "funcs.h"\n', "")
        patched = MODULE.patch_text(text, [SAVE, LOAD])
        self.assertEqual(patched.count(SAVE_CALL), 1)
        self.assertEqual(patched.count(LOAD_CALL), 1)
        self.assertEqual(patched.count(INCLUDE), 1)

    def test_idempotent(self):
        for unit, site in ((SAVE_UNIT, SAVE), (LOAD_UNIT, LOAD)):
            with self.subTest(site["name"]):
                once = MODULE.patch_text(unit, [site])
                self.assertEqual(MODULE.patch_text(once, [site]), once)

    def test_rejects_changed_shapes(self):
        cases = {
            "missing save routine": (
                SAVE_UNIT.replace("copy_save_to_sram", "x"), SAVE),
            "save block moved": (
                SAVE_UNIT.replace("0xBBC5D8", "0xBBC5D9"), SAVE),
            "no buffer read": (SAVE_UNIT.replace("0x56ca", "0x56cc"), SAVE),
            "save block has no return": (
                SAVE_UNIT.replace(SAVE_BEFORE, "    {"), SAVE),
            "loader without $08CC": (
                LOAD_UNIT.replace("0x08cc", "0x08ce"), LOAD),
            "loader block moved": (
                LOAD_UNIT.replace("0xBBC846", "0xBBC847"), LOAD),
        }
        for name, (text, site) in cases.items():
            with self.subTest(name):
                with self.assertRaises(ValueError):
                    MODULE.patch_text(text, [site])

    def test_rejects_misplaced_hook(self):
        broken = SAVE_UNIT.replace(
            "    WatchdogCheck();\n",
            "    WatchdogCheck();\n" + SAVE_CALL + "\n    cpu->X = 0;\n")
        with self.assertRaises(ValueError):
            MODULE.patch_text(broken, [SAVE])


class ApplyTests(unittest.TestCase):
    def write_units(self, directory):
        self.screen = directory / "bank80_part25b_v2.c"
        self.screen.write_bytes(SCREEN_UNIT.encode("utf-8"))
        save = directory / "bankbb_part45b_v2.c"
        load = directory / "bankbb_part472_v2.c"
        save.write_bytes(SAVE_UNIT.encode("utf-8"))
        load.write_bytes(LOAD_UNIT.encode("utf-8"))
        (directory / "bankbb_part000_v2.c").write_bytes(
            b'#include "funcs.h"\n')
        return save, load

    def test_applies_to_every_unit(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            save, load = self.write_units(directory)
            self.assertEqual(MODULE.apply(directory),
                             sorted([self.screen, save, load]))
            first = (self.screen.read_bytes(), save.read_bytes(),
                     load.read_bytes())
            self.assertIn(SCREEN_CALL.encode(), first[0])
            self.assertIn(SAVE_CALL.encode(), first[1])
            self.assertIn(LOAD_CALL.encode(), first[2])
            self.assertNotIn(b"\r\n", b"".join(first))
            MODULE.apply(directory)
            self.assertEqual((self.screen.read_bytes(), save.read_bytes(),
                              load.read_bytes()), first)

    def test_requires_both_units(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            save, load = self.write_units(directory)
            load.unlink()
            with self.assertRaises(ValueError):
                MODULE.apply(directory)
            self.assertEqual(self.screen.read_bytes(),
                             SCREEN_UNIT.encode("utf-8"))
            self.assertEqual(save.read_bytes(), SAVE_UNIT.encode("utf-8"))

    def test_failed_site_writes_nothing(self):
        with tempfile.TemporaryDirectory() as directory:
            directory = Path(directory)
            save, load = self.write_units(directory)
            broken = LOAD_UNIT.replace("0x08cc", "0x08ce").encode("utf-8")
            load.write_bytes(broken)
            with self.assertRaises(ValueError):
                MODULE.apply(directory)
            # Every unit write_units wrote, the screen unit patched first
            # among them, is still byte-for-byte what it was.
            self.assertEqual(
                {path.name: path.read_bytes()
                 for path in sorted(directory.glob("*.c"))},
                {self.screen.name: SCREEN_UNIT.encode("utf-8"),
                 save.name: SAVE_UNIT.encode("utf-8"),
                 load.name: broken,
                 "bankbb_part000_v2.c": b'#include "funcs.h"\n'})


if __name__ == "__main__":
    unittest.main()
