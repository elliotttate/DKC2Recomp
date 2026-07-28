import importlib.util
from pathlib import Path
import tempfile
import unittest


SCRIPT = (Path(__file__).resolve().parents[1] / "scripts" /
          "capture_tcp_screenshot.py")
SPEC = importlib.util.spec_from_file_location(
    "capture_tcp_screenshot", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class TcpScreenshotTests(unittest.TestCase):
    def test_margin_report_decodes_signed_oam_x_and_deduplicates(self):
        response = {
            "snaps": [{
                "f": 321,
                "slot": [
                    [100, 20, 0, 1, 2],   # authentic center
                    [80, 5, 1, 3, 4],     # right margin: x=261
                    [90, 250, 1, 5, 6],   # left margin: x=506 => -6
                    [240, 1, 1, 7, 8],    # parked / not visibly active
                ],
            }],
        }
        found = {}
        MODULE.collect_oam_margin_sprites(response, found)
        MODULE.collect_oam_margin_sprites(response, found)
        self.assertEqual(len(found), 2)
        self.assertEqual(found[(321, 1)]["side"], "right")
        self.assertEqual(found[(321, 1)]["x"], 261)
        self.assertEqual(found[(321, 2)]["side"], "left")
        self.assertEqual(found[(321, 2)]["x"], -6)

    def test_hex_snapshot_writes_exact_binary_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            destination = Path(temp) / "snapshot.bin"
            MODULE.write_hex_snapshot(
                {"hex": "00 7f 80 ff"}, destination, 4)
            self.assertEqual(destination.read_bytes(), b"\x00\x7f\x80\xff")

    def test_hex_snapshot_rejects_truncated_payload(self):
        with tempfile.TemporaryDirectory() as temp:
            with self.assertRaisesRegex(RuntimeError, "expected 4"):
                MODULE.write_hex_snapshot(
                    {"hex": "0011"}, Path(temp) / "short.bin", 4)


if __name__ == "__main__":
    unittest.main()
