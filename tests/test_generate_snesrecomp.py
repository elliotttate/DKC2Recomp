import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).resolve().parents[1] / "scripts" / "generate_snesrecomp.py"
SPEC = importlib.util.spec_from_file_location("generate_snesrecomp", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class GenerateSnesrecompTests(unittest.TestCase):
    def test_integer_accepts_decimal_and_hex(self):
        self.assertEqual(MODULE.integer("4096"), 4096)
        self.assertEqual(MODULE.integer("0x10"), 16)

    def test_rom_size_is_checked_before_private_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            rom = Path(directory) / "synthetic.smc"
            rom.write_bytes(b"not a game")
            with self.assertRaisesRegex(ValueError, "Unsupported ROM size"):
                MODULE.validate_rom(rom)


if __name__ == "__main__":
    unittest.main()
