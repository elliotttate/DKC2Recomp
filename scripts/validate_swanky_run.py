#!/usr/bin/env python3
"""Validate a focused Swanky playtest from privacy-safe diagnostic artifacts."""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import sys
from typing import Any


SWANKY_DISPATCHER = 0x349EDC
SWANKY_STATES = {
    0x34A3E0,
    0x34A475,
    0x34A4CB,
    0x34A5D9,
    0x34A665,
    0x34A7CA,
}
CORRUPT_EDGES = {
    (0x34A807, 0x3AC7E4),
    (0x34A816, 0x338007),
    (0x33802E, 0x3CFA78),
    (0x33804F, 0x330000),
    (0x349EDF, 0x34A807),
}
PERF_VALUE = re.compile(r"\b([a-z_]+)=([0-9]+(?:\.[0-9]+)?)")


def parse_address(value: Any) -> int:
    if isinstance(value, int):
        return value & 0xFFFFFF
    return int(str(value), 0) & 0xFFFFFF


def lorom_canonical(value: Any) -> int:
    address = parse_address(value)
    bank = (address >> 16) & 0xFF
    if 0x80 <= bank <= 0xBF:
        address ^= 0x800000
    return address


def is_mmio_code_address(address: int) -> bool:
    return (address >> 16) == 0 and 0x2100 <= (address & 0xFFFF) <= 0x21FF


def read_json(path: pathlib.Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8-sig") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError(f"{path} does not contain a JSON object")
    return value


def performance_summary(path: pathlib.Path | None) -> dict[str, float | int]:
    if path is None or not path.is_file():
        return {"samples": 0}
    fps_values: list[float] = []
    emulation_values: list[float] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if not line.startswith("perf "):
            continue
        fields = {name: float(value) for name, value in PERF_VALUE.findall(line)}
        if "fps" in fields:
            fps_values.append(fields["fps"])
        if "emulation_ms" in fields:
            emulation_values.append(fields["emulation_ms"])
    summary: dict[str, float | int] = {"samples": len(fps_values)}
    if fps_values:
        summary["minimum_fps"] = min(fps_values)
        summary["average_fps"] = sum(fps_values) / len(fps_values)
    if emulation_values:
        summary["maximum_emulation_ms"] = max(emulation_values)
    return summary


def validate(
    report: dict[str, Any],
    tier2: dict[str, Any],
    perf_path: pathlib.Path | None = None,
) -> tuple[dict[str, Any], list[str]]:
    errors: list[str] = []
    outcome = report.get("dkc2", {}).get("outcome")
    if outcome != "clean_exit":
        errors.append(f"run outcome is {outcome!r}, expected 'clean_exit'")

    dispatch_log = report.get("dispatch_log", {})
    events = dispatch_log.get("events", [])
    if not isinstance(events, list):
        events = []
        errors.append("last-run report has no usable dispatch event list")

    a4cb_hits = []
    missing_swanky_aot = []
    for event in events:
        if not isinstance(event, dict):
            continue
        try:
            target = lorom_canonical(event.get("pc24", 0))
            source = lorom_canonical(event.get("source_pc24", 0))
        except (TypeError, ValueError):
            continue
        if (
            target == 0x34A4CB
            and source == SWANKY_DISPATCHER
            and int(event.get("mx", -1)) == 0
        ):
            a4cb_hits.append(event)
        if target in SWANKY_STATES and not int(event.get("found", 0)):
            missing_swanky_aot.append((source, target))

    if not any(int(event.get("found", 0)) for event in a4cb_hits):
        errors.append(
            "no native M0X0 $B4:A4CB dispatch was retained; play through "
            "Swanky and close the game soon afterward"
        )
    if missing_swanky_aot:
        errors.append(
            "Swanky runtime target missed native dispatch: "
            + ", ".join(
                f"${source:06X}->${target:06X}"
                for source, target in missing_swanky_aot
            )
        )

    discoveries = tier2.get("discoveries", [])
    if not isinstance(discoveries, list):
        discoveries = []
        errors.append("tier-2 manifest has no usable discovery list")
    bail_hits = 0
    interpreted_swanky_hits = 0
    corrupt_edges = []
    mmio_code_hits = []
    for discovery in discoveries:
        if not isinstance(discovery, dict):
            continue
        try:
            site = lorom_canonical(discovery.get("site_pc24", 0))
            target = lorom_canonical(discovery.get("target_pc24", 0))
        except (TypeError, ValueError):
            continue
        clean = int(discovery.get("clean_hits", 0))
        bails = int(discovery.get("bail_hits", 0))
        bail_hits += bails
        if target in SWANKY_STATES:
            interpreted_swanky_hits += clean + bails
        if (site, target) in CORRUPT_EDGES:
            corrupt_edges.append((site, target))
        if is_mmio_code_address(site) or is_mmio_code_address(target):
            mmio_code_hits.append((site, target))

    if bail_hits:
        errors.append(f"tier-2 interpreter recorded {bail_hits} step-cap bailout(s)")
    if interpreted_swanky_hits:
        errors.append(
            f"Swanky used the interpreter {interpreted_swanky_hits} time(s) "
            "instead of its native entries"
        )
    if corrupt_edges:
        errors.append(
            "the original corrupt dispatch cascade reappeared: "
            + ", ".join(
                f"${site:06X}->${target:06X}" for site, target in corrupt_edges
            )
        )
    if mmio_code_hits:
        errors.append("SNES MMIO addresses were treated as executable code")

    summary = {
        "ok": not errors,
        "outcome": outcome,
        "dispatch_events_retained": len(events),
        "swanky_a4cb_native_hits": sum(
            1 for event in a4cb_hits if int(event.get("found", 0))
        ),
        "tier2_bail_hits": bail_hits,
        "tier2_swanky_hits": interpreted_swanky_hits,
        "corrupt_edge_hits": len(corrupt_edges),
        "mmio_code_hits": len(mmio_code_hits),
        "performance": performance_summary(perf_path),
    }
    return summary, errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--report", required=True, type=pathlib.Path)
    parser.add_argument("--tier2", required=True, type=pathlib.Path)
    parser.add_argument("--performance", type=pathlib.Path)
    args = parser.parse_args()

    summary, errors = validate(
        read_json(args.report), read_json(args.tier2), args.performance
    )
    print(json.dumps(summary, indent=2, sort_keys=True))
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
