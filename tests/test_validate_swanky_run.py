#!/usr/bin/env python3
"""Synthetic regressions for the focused Swanky diagnostic validator."""

from __future__ import annotations

import importlib.util
import pathlib


SCRIPT = (
    pathlib.Path(__file__).resolve().parents[1]
    / "scripts"
    / "validate_swanky_run.py"
)
SPEC = importlib.util.spec_from_file_location("validate_swanky_run", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def report(found: int = 1) -> dict:
    return {
        "dkc2": {"outcome": "clean_exit"},
        "dispatch_log": {
            "events": [
                {
                    "pc24": 0xB4A4CB,
                    "source_pc24": 0xB49EDC,
                    "mx": 0,
                    "found": found,
                }
            ]
        },
    }


def main() -> int:
    summary, errors = MODULE.validate(report(), {"discoveries": []})
    assert not errors
    assert summary["ok"]
    assert summary["swanky_a4cb_native_hits"] == 1

    bad_tier2 = {
        "discoveries": [
            {
                "site_pc24": "0x349EDC",
                "target_pc24": "0x34A4CB",
                "clean_hits": 29,
                "bail_hits": 1,
            },
            {
                "site_pc24": "0x34A807",
                "target_pc24": "0x3AC7E4",
                "clean_hits": 1,
                "bail_hits": 0,
            },
            {
                "site_pc24": "0x00213B",
                "target_pc24": "0x00213F",
                "clean_hits": 1,
                "bail_hits": 0,
            },
        ]
    }
    summary, errors = MODULE.validate(report(found=0), bad_tier2)
    assert not summary["ok"]
    assert summary["tier2_bail_hits"] == 1
    assert summary["tier2_swanky_hits"] == 30
    assert summary["corrupt_edge_hits"] == 1
    assert summary["mmio_code_hits"] == 1
    assert len(errors) >= 5

    summary, errors = MODULE.validate(
        {"dkc2": {"outcome": "clean_exit"}, "dispatch_log": {"events": []}},
        {"discoveries": []},
    )
    assert not summary["ok"]
    assert any("$B4:A4CB" in error for error in errors)

    print("Swanky diagnostic validator: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
