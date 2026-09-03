#!/usr/bin/env python3
"""Tests for scripts/analyze_pacing_log.py on synthetic pacing logs."""
import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "scripts"))

import analyze_pacing_log as pacing  # noqa: E402

HEADER = ("frame mode present_s tick target_s interval_s emulate_s "
          "fill_frames ratio pump_s work_s wait_s present_call_s")
TICK = 1.0 / 60.0


def locked_log(frames):
    """A display-locked run: tick n targets refresh n, each frame presented
    13 ms before its tick's target."""
    lines = [HEADER]
    for n in range(1, frames + 1):
        target = 100.0 + n * TICK
        lines.append(f"{n} display {target - 0.013:.6f} {n} {target:.6f} "
                     f"{TICK:.6f} 0.001000 1500.0 1.00100 0.0001 0.0011 "
                     f"0.0130 0.0015")
    return lines


def drifting_log(frames):
    """A host-clock run at 60.0988 Hz against 60-Hz ticks: the phase drifts
    across a refresh about every ten seconds, so frames double or drop."""
    lines = [HEADER]
    native = pacing.NATIVE_FRAME_SECONDS
    for n in range(1, frames + 1):
        present = 100.0 + n * native
        tick = int((present - 100.0) / TICK)
        target = 100.0 + (tick + 1) * TICK
        lines.append(f"{n} clock {present:.6f} {tick} {target:.6f} "
                     f"{TICK:.6f} 0.001000 1500.0 1.00000 0.0001 0.0011 "
                     f"0.0130 0.0015")
    return lines


class ParseTest(unittest.TestCase):
    def test_parses_rows_and_skips_header(self):
        rows = pacing.parse(locked_log(3))
        self.assertEqual(len(rows), 3)
        self.assertEqual(rows[0]["frame"], 1)
        self.assertEqual(rows[0]["mode"], "display")
        self.assertEqual(rows[0]["tick"], 1)
        self.assertAlmostEqual(rows[0]["wait"], 0.013)

    def test_tolerates_short_rows(self):
        rows = pacing.parse([HEADER, "1 clock 100.0 0 0.0 0.0 0.001 -1.0 1.0"])
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]["pump"], 0.0)


class SummaryTest(unittest.TestCase):
    def test_locked_run_shows_every_frame_once(self):
        summary = pacing.summarize(pacing.parse(locked_log(600)), skip=0)
        self.assertEqual(summary["modes"], {"display": 600})
        # The log carries six decimals, so the rate is within a hundredth.
        self.assertAlmostEqual(summary["tick_hz"], 60.0, places=1)
        self.assertEqual(summary["refreshes_per_frame"], {1: 599})
        self.assertEqual(summary["late_presents"], 0)
        self.assertLess(summary["present_minus_target"]["max"], 0.0)

    def test_drifting_run_drops_frames(self):
        summary = pacing.summarize(pacing.parse(drifting_log(1800)), skip=0)
        histogram = summary["refreshes_per_frame"]
        # Thirty seconds at 60.0988 Hz against 60 Hz is about three frames
        # more than the refreshes available, each one a dropped frame.
        self.assertGreaterEqual(histogram.get(0, 0), 2)
        self.assertEqual(sum(histogram.values()), 1799)

    def test_skip_and_empty(self):
        rows = pacing.parse(locked_log(5))
        self.assertEqual(pacing.summarize(rows, skip=5)["frames"], 0)
        self.assertEqual(pacing.summarize(rows, skip=2)["frames"], 3)

    def test_report_mentions_the_histogram(self):
        summary = pacing.summarize(pacing.parse(locked_log(20)), skip=0)
        text = pacing.report("run.log", summary)
        self.assertIn("refreshes per frame (estimated): {1: 19}", text)
        self.assertIn("tick rate about 60.00 Hz", text)


if __name__ == "__main__":
    unittest.main()
