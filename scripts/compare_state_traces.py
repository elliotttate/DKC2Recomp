#!/usr/bin/env python3
"""Compare ordered DKC2 semantic state events from native and reference logs."""

from __future__ import annotations

import argparse
import pathlib
import re
import sys
from dataclasses import dataclass


EVENT_RE = re.compile(
    r"(?:reference_)?state_event\s+frame=(?P<frame>\d+)\s+"
    r"game_mode=\$(?P<game_mode>[0-9a-fA-F]{4})\s+"
    r"game_sub_mode=\$(?P<sub_mode>[0-9a-fA-F]{4})\s+"
    r"demo_status=\$(?P<demo_status>[0-9a-fA-F]{4})\s+"
    r"demo_sequence=\$(?P<demo_sequence>[0-9a-fA-F]{4})\s+"
    r"demo_index=\$(?P<demo_index>[0-9a-fA-F]{4})\s+"
    r"demo_timer=\$(?P<demo_timer>[0-9a-fA-F]{4})\s+"
    r"level=\$(?P<level>[0-9a-fA-F]{4})\s+"
    r"active_frame=\$(?P<active_frame>[0-9a-fA-F]{4})\s+"
    r"continuation=\$(?P<continuation>[0-9a-fA-F]{4})",
    re.MULTILINE,
)


@dataclass(frozen=True)
class Event:
    frame: int
    values: tuple[int, ...]

    @property
    def game_mode(self) -> int:
        return self.values[0]

    @property
    def demo_sequence(self) -> int:
        return self.values[3]

    @property
    def semantic_values(self) -> tuple[int, ...]:
        # active_frame is a sampling-phase diagnostic, not a state-transition
        # identity. At attract reset one implementation may clear it later in
        # the same video frame without producing another semantic event.
        return self.values[:7] + self.values[8:]


def parse_events(path: pathlib.Path) -> list[Event]:
    raw = path.read_bytes()
    if raw.startswith((b"\xff\xfe", b"\xfe\xff")):
        text = raw.decode("utf-16")
    else:
        text = raw.decode("utf-8", errors="replace")
    events: list[Event] = []
    names = (
        "game_mode",
        "sub_mode",
        "demo_status",
        "demo_sequence",
        "demo_index",
        "demo_timer",
        "level",
        "active_frame",
        "continuation",
    )
    for match in EVENT_RE.finditer(text):
        values = tuple(int(match.group(name), 16) for name in names)
        events.append(Event(int(match.group("frame")), values))
    if not events:
        raise ValueError(f"no state events found in {path}")
    return events


def loading_durations(events: list[Event]) -> list[int]:
    durations: list[int] = []
    for previous, current in zip(events, events[1:]):
        if (
            previous.game_mode == 0x87E1
            and current.game_mode == 0x8819
            and previous.demo_sequence == current.demo_sequence
        ):
            durations.append(current.frame - previous.frame)
    return durations


def compare(
    native: list[Event],
    reference: list[Event],
    max_absolute_delta: int,
    max_loading_difference: int,
) -> list[str]:
    failures: list[str] = []
    native_values = [event.semantic_values for event in native]
    reference_values = [event.semantic_values for event in reference]
    if native_values != reference_values:
        failures.append("ordered semantic event sequences differ")
        return failures

    deltas = [n.frame - r.frame for n, r in zip(native, reference)]
    if max(abs(delta) for delta in deltas) > max_absolute_delta:
        failures.append(
            f"maximum event-frame delta {max(abs(d) for d in deltas)} "
            f"exceeds {max_absolute_delta}"
        )

    native_loading = loading_durations(native)
    reference_loading = loading_durations(reference)
    if len(native_loading) != len(reference_loading):
        failures.append("number of level-loading windows differs")
    elif native_loading:
        difference = max(
            abs(n - r) for n, r in zip(native_loading, reference_loading)
        )
        if difference > max_loading_difference:
            failures.append(
                f"maximum loading-duration difference {difference} "
                f"exceeds {max_loading_difference}"
            )
    return failures


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("native", type=pathlib.Path)
    parser.add_argument("reference", type=pathlib.Path)
    parser.add_argument("--max-absolute-delta", type=int, default=6)
    parser.add_argument("--max-loading-difference", type=int, default=1)
    args = parser.parse_args()

    try:
        native = parse_events(args.native)
        reference = parse_events(args.reference)
        failures = compare(
            native,
            reference,
            args.max_absolute_delta,
            args.max_loading_difference,
        )
    except (OSError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    print("event  native  reference  delta  mode  demo  level")
    for index, (n, r) in enumerate(zip(native, reference), 1):
        print(
            f"{index:5d} {n.frame:7d} {r.frame:10d} "
            f"{n.frame-r.frame:+6d}  ${n.values[0]:04x} "
            f"{n.values[3]:5d}  ${n.values[6]:04x}"
        )
    print(f"native_loading_frames={loading_durations(native)}")
    print(f"reference_loading_frames={loading_durations(reference)}")
    if failures:
        for failure in failures:
            print(f"failure: {failure}", file=sys.stderr)
        print("result=fail")
        return 1
    print("result=pass")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
