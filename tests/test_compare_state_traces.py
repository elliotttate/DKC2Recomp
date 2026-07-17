#!/usr/bin/env python3

import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "scripts"))
from compare_state_traces import Event, compare, loading_durations  # noqa: E402


def event(frame: int, mode: int, demo: int, marker: int = 0) -> Event:
    return Event(frame, (mode, marker, 1, demo, 6, 0, demo, marker, 0x8608))


class StateTraceComparisonTests(unittest.TestCase):
    def test_accepts_aligned_loading_windows(self):
        native = [event(100, 0x87E1, 1), event(252, 0x8819, 1)]
        reference = [event(103, 0x87E1, 1), event(255, 0x8819, 1)]
        self.assertEqual(loading_durations(native), [152])
        self.assertEqual(compare(native, reference, 6, 1), [])

    def test_rejects_semantic_sequence_difference(self):
        native = [event(100, 0x87E1, 1)]
        reference = [event(100, 0x87E1, 2)]
        self.assertTrue(compare(native, reference, 6, 1))

    def test_rejects_excess_event_drift(self):
        native = [event(110, 0x87E1, 1)]
        reference = [event(100, 0x87E1, 1)]
        self.assertTrue(compare(native, reference, 6, 1))

    def test_rejects_slow_loading_window(self):
        native = [event(100, 0x87E1, 1), event(114, 0x8819, 1)]
        reference = [event(100, 0x87E1, 1), event(110, 0x8819, 1)]
        self.assertTrue(compare(native, reference, 6, 1))


if __name__ == "__main__":
    unittest.main()
