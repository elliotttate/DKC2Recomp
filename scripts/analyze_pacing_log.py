#!/usr/bin/env python3
"""Summarise a DKC2_PACING_LOG written by the macOS gameplay host.

Each line of the log is one presented frame: the pacing mode, the host time
of the present, the display tick it followed with that tick's target refresh
time and interval, the emulation time, the audio queue's average fill, the
stretch ratio, and the time spent in each stage of the loop. The summary
that matters is the estimate of how many refreshes each frame was shown
for: a frame appears on the first refresh at or after its present, so two
frames presented before the same refresh show as a dropped frame (0) and a
frame that spans two refreshes shows as a doubled one (2). A locked display
cadence shows every frame exactly once.
"""
import argparse
import math
import statistics
from collections import Counter

NATIVE_FRAME_SECONDS = 1.0 / 60.098811862
OFF_NOMINAL_SECONDS = 0.002

COLUMNS = ("frame", "mode", "present", "tick", "target", "interval",
           "emulate", "fill", "ratio", "pump", "work", "wait", "present_call")


def parse(lines):
    """Rows from log lines; the header and short lines are skipped."""
    rows = []
    for line in lines:
        parts = line.split()
        if len(parts) < 9 or not parts[0].isdigit():
            continue
        row = {"frame": int(parts[0]), "mode": parts[1], "tick": int(parts[3])}
        for index, name in enumerate(COLUMNS):
            if name in row:
                continue
            row[name] = float(parts[index]) if index < len(parts) else 0.0
        rows.append(row)
    return rows


def stats(values):
    if not values:
        return None
    return {"count": len(values), "mean": statistics.mean(values),
            "sd": statistics.pstdev(values), "min": min(values),
            "max": max(values)}


def refreshes_per_frame(rows, period):
    """Histogram of the refreshes each frame was shown for, estimated from
    the present times against the tick targets."""
    shown = []
    for row in rows:
        if row["target"] <= 0:
            continue
        target = row["target"]
        if row["present"] > target:
            target += period * math.ceil((row["present"] - target) / period)
        shown.append(target)
    histogram = Counter()
    for first, second in zip(shown, shown[1:]):
        histogram[int(round((second - first) / period))] += 1
    return dict(sorted(histogram.items()))


def summarize(rows, skip=0):
    rows = rows[skip:]
    summary = {"frames": len(rows), "modes": dict(Counter(r["mode"] for r in rows))}
    if not rows:
        return summary
    intervals = [b["present"] - a["present"] for a, b in zip(rows, rows[1:])]
    summary["present_interval"] = stats(intervals)
    summary["present_intervals_off_nominal"] = sum(
        1 for v in intervals if abs(v - NATIVE_FRAME_SECONDS) > OFF_NOMINAL_SECONDS)
    ticks = [r["interval"] for r in rows if r["interval"] > 0]
    summary["tick_interval"] = stats(ticks)
    if ticks:
        period = statistics.median(ticks)
        summary["tick_hz"] = 1.0 / period
        phase = [r["present"] - r["target"] for r in rows if r["target"] > 0]
        summary["present_minus_target"] = stats(phase)
        summary["late_presents"] = sum(1 for v in phase if v > 0)
        summary["refreshes_per_frame"] = refreshes_per_frame(rows, period)
    summary["emulate"] = stats([r["emulate"] for r in rows])
    for stage in ("pump", "work", "wait", "present_call"):
        summary[stage] = stats([r[stage] for r in rows])
    fills = [r["fill"] for r in rows if r["fill"] >= 0]
    if fills:
        summary["fill"] = {"first": fills[0], "last": fills[-1],
                           "min": min(fills), "max": max(fills)}
    ratios = [r["ratio"] for r in rows]
    summary["ratio"] = {"min": min(ratios), "max": max(ratios), "last": ratios[-1]}
    return summary


def describe(name, values, scale=1000.0, unit="ms"):
    if not values:
        return f"  {name}: none"
    return (f"  {name}: n={values['count']} mean={values['mean'] * scale:.3f}{unit} "
            f"sd={values['sd'] * scale:.3f}{unit} min={values['min'] * scale:.3f}{unit} "
            f"max={values['max'] * scale:.3f}{unit}")


def report(path, summary):
    lines = [f"{path}: {summary['frames']} frames; modes {summary['modes']}"]
    if not summary["frames"]:
        return "\n".join(lines)
    lines.append(describe("present interval", summary["present_interval"]))
    lines.append(f"  present intervals more than 2 ms off nominal: "
                 f"{summary['present_intervals_off_nominal']}")
    lines.append(describe("display tick interval", summary["tick_interval"]))
    if summary["tick_interval"]:
        lines.append(f"  tick rate about {summary['tick_hz']:.2f} Hz")
        lines.append(describe("present minus tick target (negative = before the refresh)",
                              summary["present_minus_target"]))
        lines.append(f"  frames presented after their tick's target refresh: "
                     f"{summary['late_presents']}")
        lines.append(f"  refreshes per frame (estimated): {summary['refreshes_per_frame']}")
    lines.append(describe("emulation time", summary["emulate"]))
    for stage in ("pump", "work", "wait", "present_call"):
        lines.append(describe(f"stage {stage}", summary[stage]))
    if "fill" in summary:
        fill = summary["fill"]
        lines.append(f"  audio fill average: first {fill['first']:.0f} last {fill['last']:.0f} "
                     f"min {fill['min']:.0f} max {fill['max']:.0f} frames")
    ratio = summary["ratio"]
    lines.append(f"  stretch ratio: min {ratio['min']:.5f} max {ratio['max']:.5f} "
                 f"last {ratio['last']:.5f}")
    return "\n".join(lines)


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("log", nargs="+", help="DKC2_PACING_LOG file(s)")
    parser.add_argument("--skip", type=int, default=240,
                        help="frames to skip at the start, past startup "
                             "priming (default 240)")
    args = parser.parse_args()
    for path in args.log:
        with open(path) as handle:
            rows = parse(handle)
        print(report(path, summarize(rows, args.skip)))


if __name__ == "__main__":
    main()
