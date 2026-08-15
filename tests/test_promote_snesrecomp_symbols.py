import importlib.util
from pathlib import Path
import sys
import tempfile
import unittest


SCRIPT = (
    Path(__file__).resolve().parents[1]
    / "scripts"
    / "promote_snesrecomp_symbols.py"
)
SPEC = importlib.util.spec_from_file_location(
    "promote_snesrecomp_symbols", SCRIPT
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


CFG = """\
bank = 0x80
func CODE_80D77A D73A end:D744 entry_mx:0,0
func CODE_80ABCD ABE0 end:ABF0 entry_mx:0,0
func existing_name ABF0 end:AC00 entry_mx:0,0
"""

SYMBOLS = """\
; private test fixture
[labels]
80:D77A handle_mine_glint_CODE_80D77A
80:ABCD DATA_80ABCD
80:ABCD unqualified_name
"""


class SymbolPromotionTests(unittest.TestCase):
    def make_fixture(self, root: Path) -> tuple[Path, Path]:
        cfg_dir = root / "recomp"
        cfg_dir.mkdir()
        (cfg_dir / "bank80.cfg").write_text(CFG, encoding="utf-8")
        symbols = root / "private.sym"
        symbols.write_text(SYMBOLS, encoding="utf-8")
        return cfg_dir, symbols

    def test_finds_only_context_that_preserves_generic_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg_dir, symbols = self.make_fixture(Path(directory))
            promotions = MODULE.find_promotions(cfg_dir, symbols)
            self.assertEqual(len(promotions), 1)
            self.assertEqual(promotions[0].entry.name, "CODE_80D77A")
            self.assertEqual(
                promotions[0].new_name,
                "handle_mine_glint_CODE_80D77A",
            )

    def test_apply_is_exact_and_idempotent(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg_dir, symbols = self.make_fixture(Path(directory))
            promotions = MODULE.find_promotions(cfg_dir, symbols)
            MODULE.apply_promotions(promotions)
            updated = (cfg_dir / "bank80.cfg").read_text(encoding="utf-8")
            self.assertIn(
                "func handle_mine_glint_CODE_80D77A D73A", updated
            )
            self.assertIn("func CODE_80ABCD ABE0", updated)
            self.assertEqual(MODULE.find_promotions(cfg_dir, symbols), [])

    def test_rejects_ambiguous_contextual_aliases(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg_dir, symbols = self.make_fixture(Path(directory))
            symbols.write_text(
                SYMBOLS
                + "80:D77A other_context_CODE_80D77A\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "ambiguous contextual"):
                MODULE.find_promotions(cfg_dir, symbols)

    def test_rejects_bank_mismatch(self):
        with tempfile.TemporaryDirectory() as directory:
            cfg_dir, symbols = self.make_fixture(Path(directory))
            cfg_path = cfg_dir / "bank80.cfg"
            cfg_path.write_text(
                CFG.replace("CODE_80D77A", "CODE_81D77A"),
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "encodes bank 81"):
                MODULE.find_promotions(cfg_dir, symbols)


if __name__ == "__main__":
    unittest.main()
