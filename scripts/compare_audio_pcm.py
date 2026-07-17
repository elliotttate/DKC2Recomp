#!/usr/bin/env python3
"""Compare private native/reference SNES stereo PCM without embedding audio.

Inputs are headerless signed 16-bit little-endian stereo streams at 32,040 Hz.
The comparison intentionally measures artifact indicators and timing envelopes,
not bit identity: independent S-DSP implementations can differ by a few integer
levels while still producing equivalent clean audio.
"""

from __future__ import annotations

import argparse
import array
import math
import sys
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True)
class SilenceRun:
    start_seconds: float
    duration_seconds: float


@dataclass(frozen=True)
class Metrics:
    frames: int
    duration_seconds: float
    rms: float
    peak: int
    clipped_samples: int
    max_delta: int
    zero_frames: int
    longest_zero_seconds: float
    long_silences: tuple[SilenceRun, ...]


def read_stereo_s16le(path: Path) -> array.array:
    size = path.stat().st_size
    if size == 0 or size % 4 != 0:
        raise ValueError(f"{path}: expected a nonempty multiple of 4 bytes")
    samples = array.array("h")
    with path.open("rb") as stream:
        samples.fromfile(stream, size // samples.itemsize)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def analyze(path: Path, sample_rate: float, block_frames: int,
            silence_threshold: float, minimum_silence: float) -> Metrics:
    samples = read_stereo_s16le(path)
    frames = len(samples) // 2
    square_sum = 0
    peak = 0
    clipped = 0
    max_delta = 0
    previous = [0, 0]
    have_previous = False
    zero_frames = 0
    zero_run = 0
    longest_zero_run = 0
    block_square_sum = 0
    block_sample_count = 0
    block_active: list[bool] = []

    for frame in range(frames):
        left = samples[frame * 2]
        right = samples[frame * 2 + 1]
        if left == 0 and right == 0:
            zero_frames += 1
            zero_run += 1
            longest_zero_run = max(longest_zero_run, zero_run)
        else:
            zero_run = 0
        for channel, sample in enumerate((left, right)):
            magnitude = abs(sample)
            peak = max(peak, magnitude)
            clipped += magnitude >= 32760
            square = sample * sample
            square_sum += square
            block_square_sum += square
            block_sample_count += 1
            if have_previous:
                max_delta = max(max_delta, abs(sample - previous[channel]))
            previous[channel] = sample
        have_previous = True
        if (frame + 1) % block_frames == 0:
            rms = math.sqrt(block_square_sum / block_sample_count)
            block_active.append(rms > silence_threshold)
            block_square_sum = 0
            block_sample_count = 0

    long_silences: list[SilenceRun] = []
    run_start: int | None = None
    for index, active in enumerate((*block_active, True)):
        if not active and run_start is None:
            run_start = index
        elif active and run_start is not None:
            run_seconds = (index - run_start) * block_frames / sample_rate
            if run_seconds >= minimum_silence:
                long_silences.append(SilenceRun(
                    run_start * block_frames / sample_rate, run_seconds))
            run_start = None

    return Metrics(
        frames=frames,
        duration_seconds=frames / sample_rate,
        rms=math.sqrt(square_sum / len(samples)),
        peak=peak,
        clipped_samples=clipped,
        max_delta=max_delta,
        zero_frames=zero_frames,
        longest_zero_seconds=longest_zero_run / sample_rate,
        long_silences=tuple(long_silences),
    )


def ratio(value: float, reference: float) -> float:
    return value / reference if reference else math.inf


def compare(native: Metrics, reference: Metrics, args: argparse.Namespace) -> list[str]:
    failures: list[str] = []
    if native.clipped_samples:
        failures.append(f"native has {native.clipped_samples} clipped samples")
    if abs(native.duration_seconds - reference.duration_seconds) > args.max_duration_delta:
        failures.append("stream duration differs by more than "
                        f"{args.max_duration_delta:.3f} seconds")
    rms_ratio = ratio(native.rms, reference.rms)
    if not args.min_rms_ratio <= rms_ratio <= args.max_rms_ratio:
        failures.append(f"RMS ratio {rms_ratio:.6f} is outside allowed range")
    peak_ratio = ratio(native.peak, reference.peak)
    if not args.min_peak_ratio <= peak_ratio <= args.max_peak_ratio:
        failures.append(f"peak ratio {peak_ratio:.6f} is outside allowed range")
    delta_ratio = ratio(native.max_delta, reference.max_delta)
    if not args.min_delta_ratio <= delta_ratio <= args.max_delta_ratio:
        failures.append(f"maximum-delta ratio {delta_ratio:.6f} is outside allowed range")
    if len(native.long_silences) != len(reference.long_silences):
        failures.append("long-silence count differs: "
                        f"native={len(native.long_silences)} "
                        f"reference={len(reference.long_silences)}")
    else:
        for index, (actual, expected) in enumerate(
                zip(native.long_silences, reference.long_silences), 1):
            if abs(actual.duration_seconds - expected.duration_seconds) > \
                    args.max_silence_duration_delta:
                failures.append(f"silence {index} duration differs by more than "
                                f"{args.max_silence_duration_delta:.3f} seconds")
            if abs(actual.start_seconds - expected.start_seconds) > \
                    args.max_silence_start_delta:
                failures.append(f"silence {index} starts more than "
                                f"{args.max_silence_start_delta:.3f} seconds apart")
    return failures


def describe(label: str, metrics: Metrics) -> None:
    silences = ", ".join(
        f"{run.start_seconds:.3f}+{run.duration_seconds:.3f}"
        for run in metrics.long_silences)
    print(f"{label}: frames={metrics.frames} duration={metrics.duration_seconds:.6f}s "
          f"rms={metrics.rms:.6f} peak={metrics.peak} clipped={metrics.clipped_samples} "
          f"max_delta={metrics.max_delta} zero_frames={metrics.zero_frames} "
          f"longest_zero={metrics.longest_zero_seconds:.6f}s")
    print(f"{label}_long_silences=[{silences}]")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native", type=Path)
    parser.add_argument("reference", type=Path)
    parser.add_argument("--sample-rate", type=float, default=32040.0)
    parser.add_argument("--block-frames", type=int, default=256)
    parser.add_argument("--silence-threshold", type=float, default=10.0)
    parser.add_argument("--minimum-silence", type=float, default=0.5)
    parser.add_argument("--max-duration-delta", type=float, default=0.25)
    parser.add_argument("--min-rms-ratio", type=float, default=0.90)
    parser.add_argument("--max-rms-ratio", type=float, default=1.10)
    parser.add_argument("--min-peak-ratio", type=float, default=0.85)
    parser.add_argument("--max-peak-ratio", type=float, default=1.15)
    parser.add_argument("--min-delta-ratio", type=float, default=0.70)
    parser.add_argument("--max-delta-ratio", type=float, default=1.30)
    parser.add_argument("--max-silence-duration-delta", type=float, default=0.30)
    parser.add_argument("--max-silence-start-delta", type=float, default=1.25)
    args = parser.parse_args()
    if args.sample_rate <= 0 or args.block_frames <= 0:
        parser.error("sample rate and block size must be positive")

    try:
        native = analyze(args.native, args.sample_rate, args.block_frames,
                         args.silence_threshold, args.minimum_silence)
        reference = analyze(args.reference, args.sample_rate, args.block_frames,
                            args.silence_threshold, args.minimum_silence)
    except (OSError, ValueError) as error:
        parser.error(str(error))
    describe("native", native)
    describe("reference", reference)
    failures = compare(native, reference, args)
    if failures:
        for failure in failures:
            print(f"failure: {failure}", file=sys.stderr)
        return 1
    print("result=pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
