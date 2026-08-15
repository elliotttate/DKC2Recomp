#!/usr/bin/env python3
"""Keep Swanky's runtime-pointer state machine statically dispatchable."""

from __future__ import annotations

import pathlib
import sys


EXPECTED = [
    ("CODE_B4A391", 0xA383, 0xA3E0),
    ("swanky_game_show_state_a3e0", 0xA3E0, 0xA475),
    ("swanky_game_show_state_a475", 0xA475, 0xA4CB),
    ("swanky_game_show_state_a4cb", 0xA4CB, 0xA5D9),
    ("swanky_game_show_state_a5d9", 0xA5D9, 0xA665),
    ("swanky_game_show_state_a665", 0xA665, 0xA791),
    ("spawn_swanky_prize", 0xA791, 0xA7CA),
    ("swanky_prize_helper_a7ca", 0xA7CA, 0xA7D9),
]


def parse_functions(path: pathlib.Path) -> dict[int, tuple[str, int]]:
    functions: dict[int, tuple[str, int]] = {}
    for line_number, raw_line in enumerate(
        path.read_text(encoding="utf-8").splitlines(), start=1
    ):
        tokens = raw_line.split()
        if not tokens or tokens[0] != "func":
            continue
        if len(tokens) < 4:
            raise AssertionError(f"{path}:{line_number}: incomplete func entry")
        end_tokens = [token for token in tokens[3:] if token.startswith("end:")]
        if len(end_tokens) != 1:
            raise AssertionError(
                f"{path}:{line_number}: expected exactly one end boundary"
            )
        start = int(tokens[2], 16)
        end = int(end_tokens[0][len("end:") :], 16)
        if start in functions:
            raise AssertionError(
                f"{path}:{line_number}: duplicate function start ${start:04X}"
            )
        functions[start] = (tokens[1], end)
    return functions


def main() -> int:
    default_cfg = pathlib.Path(__file__).resolve().parents[1] / "recomp" / "bankb4.cfg"
    cfg_path = pathlib.Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else default_cfg
    functions = parse_functions(cfg_path)

    for name, start, end in EXPECTED:
        actual = functions.get(start)
        assert actual == (name, end), (
            f"${start:04X}: expected {name} ending at ${end:04X}, "
            f"found {actual!r}"
        )

    for previous, current in zip(EXPECTED, EXPECTED[1:]):
        assert previous[2] == current[1], (
            f"gap or overlap between {previous[0]} and {current[0]}"
        )

    print("Swanky static-entry map: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
