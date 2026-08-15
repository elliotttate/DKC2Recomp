import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPTS = Path(__file__).resolve().parents[1] / "scripts"
sys.path.insert(0, str(SCRIPTS))
import build_dkc2_symbol_database as symbols


SYMBOLS = """\
schema_version = 1
rom_revision = "test revision"
rom_sha256 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"

[[function]]
address = "B5:8000"
name = "update_test_object"
aliases = ["CODE_B58000"]
confidence = "confirmed"
tags = ["object", "test"]
provenance = "synthetic fixture"
note = "Updates the synthetic test object."
"""

LAYOUTS = """\
schema_version = 1
rom_revision = "test revision"

[[object]]
address = "WRAM:0100"
name = "g_test_objects"
constant = "DKC2_TEST_OBJECTS"
type = "TestObject"
size = 8
count = 2
stride = 4
count_constant = "DKC2_TEST_OBJECT_COUNT"
stride_constant = "DKC2_TEST_OBJECT_SIZE"
confidence = "confirmed"
tags = ["object", "test"]
provenance = "synthetic fixture"
note = "Two test records."

[[structure]]
name = "TestObject"
size = 4
confidence = "confirmed"
provenance = "synthetic fixture"
note = "Synthetic record."

[[field]]
structure = "TestObject"
offset = 0
name = "state"
constant = "DKC2_TEST_OBJECT_STATE_OFFSET"
type = "u16"
confidence = "confirmed"
note = "Synthetic state."
"""


class SymbolDatabaseTests(unittest.TestCase):
    def create_fixture(self, root: Path, symbols_text: str = SYMBOLS,
                       layouts_text: str = LAYOUTS) -> None:
        recomp = root / "recomp"
        recomp.mkdir()
        (recomp / "bankb5.cfg").write_text(
            "bank = 0xB5\n"
            "func CODE_B58000 8000 end:8010 entry_mx:0,0\n"
            "func untouched 8010 end:8020 entry_mx:0,0\n",
            encoding="utf-8")
        (recomp / "symbols.toml").write_text(symbols_text, encoding="utf-8")
        (recomp / "layouts.toml").write_text(layouts_text, encoding="utf-8")

    def run_tool(self, root: Path, *extra: str) -> int:
        with contextlib.redirect_stdout(io.StringIO()), \
             contextlib.redirect_stderr(io.StringIO()):
            return symbols.main(["--root", str(root), *extra])

    def test_apply_generates_and_check_is_idempotent(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.create_fixture(root)
            self.assertEqual(self.run_tool(root, "--apply-cfg"), 0)
            cfg = (root / "recomp" / "bankb5.cfg").read_text(encoding="utf-8")
            self.assertIn("func update_test_object 8000", cfg)
            self.assertTrue((root / "scripts" / "dkc2_symbols_generated.py").is_file())
            docs = (root / "docs" / "SYMBOL_DATABASE.md").read_text(encoding="utf-8")
            self.assertIn("Structural CFG inventory: **2 functions**", docs)
            inventory = root / ".cache" / "dkc2-symbols.json"
            self.assertIn('"cfg_function_count": 2', inventory.read_text(encoding="utf-8"))
            self.assertEqual(self.run_tool(root, "--check"), 0)

    def test_refuses_unapplied_cfg_and_stale_generated_output(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.create_fixture(root)
            self.assertEqual(self.run_tool(root), 1)
            self.assertEqual(self.run_tool(root, "--apply-cfg"), 0)
            output = root / "scripts" / "dkc2_symbols_generated.py"
            output.write_text("stale\n", encoding="utf-8")
            self.assertEqual(self.run_tool(root, "--check"), 1)

    def test_refuses_semantic_address_that_is_not_cfg_boundary(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.create_fixture(root, SYMBOLS.replace("B5:8000", "B5:8001"))
            self.assertEqual(self.run_tool(root, "--apply-cfg"), 1)

    def test_refuses_overlapping_structure_fields(self):
        second_field = """
[[field]]
structure = "TestObject"
offset = 1
name = "overlap"
constant = "DKC2_TEST_OBJECT_OVERLAP_OFFSET"
type = "u16"
confidence = "guessed"
note = "Invalid overlap."
"""
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            self.create_fixture(root, layouts_text=LAYOUTS + second_field)
            self.assertEqual(self.run_tool(root, "--apply-cfg"), 1)


if __name__ == "__main__":
    unittest.main()
