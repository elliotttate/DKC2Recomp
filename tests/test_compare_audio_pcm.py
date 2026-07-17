#!/usr/bin/env python3
"""Synthetic tests for the source-only private PCM comparison gate."""

from __future__ import annotations

import argparse
import array
import importlib.util
import math
from pathlib import Path
import sys
import tempfile
import unittest


REPOSITORY = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "compare_audio_pcm", REPOSITORY / "scripts" / "compare_audio_pcm.py")
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)


def comparison_arguments() -> argparse.Namespace:
    return argparse.Namespace(
        max_duration_delta=0.25,
        min_rms_ratio=0.90,
        max_rms_ratio=1.10,
        min_peak_ratio=0.85,
        max_peak_ratio=1.15,
        min_delta_ratio=0.70,
        max_delta_ratio=1.30,
        max_silence_duration_delta=0.30,
        max_silence_start_delta=1.25,
    )


def make_pcm(path: Path, sample_rate: int, amplitude: int,
             extra_silence: bool = False, clipped: bool = False) -> None:
    samples = array.array("h")
    for frame in range(sample_rate * 3):
        seconds = frame / sample_rate
        silent = seconds < 0.75 or 1.75 <= seconds < 2.5
        if extra_silence and 1.25 <= seconds < 1.9:
            silent = True
        value = 0 if silent else int(amplitude * math.sin(frame * 0.17))
        if clipped and frame == sample_rate:
            value = 32767
        samples.extend((value, value))
    if sys.byteorder != "little":
        samples.byteswap()
    with path.open("wb") as stream:
        samples.tofile(stream)


class AudioComparisonTests(unittest.TestCase):
    sample_rate = 1000

    def analyze(self, path: Path):
        return MODULE.analyze(path, self.sample_rate, 20, 10.0, 0.5)

    def test_similar_clean_streams_pass(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "native.pcm"
            reference = root / "reference.pcm"
            make_pcm(native, self.sample_rate, 950)
            make_pcm(reference, self.sample_rate, 1000)
            self.assertEqual(
                MODULE.compare(self.analyze(native), self.analyze(reference),
                               comparison_arguments()), [])

    def test_clipping_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "native.pcm"
            reference = root / "reference.pcm"
            make_pcm(native, self.sample_rate, 1000, clipped=True)
            make_pcm(reference, self.sample_rate, 1000)
            failures = MODULE.compare(
                self.analyze(native), self.analyze(reference),
                comparison_arguments())
            self.assertTrue(any("clipped" in failure for failure in failures))

    def test_extra_long_dropout_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            native = root / "native.pcm"
            reference = root / "reference.pcm"
            make_pcm(native, self.sample_rate, 1000, extra_silence=True)
            make_pcm(reference, self.sample_rate, 1000)
            failures = MODULE.compare(
                self.analyze(native), self.analyze(reference),
                comparison_arguments())
            self.assertTrue(any("silence" in failure for failure in failures))


if __name__ == "__main__":
    unittest.main()
