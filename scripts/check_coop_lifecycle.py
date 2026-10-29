#!/usr/bin/env python3
"""Private TEAM damage, saved loss, DK-barrel revival and repeat-damage route."""
import argparse
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import TEAM_START, SEPARATE_PLAYERS, check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    args = parser.parse_args()
    runner, rom = args.runner.resolve(strict=True), args.rom.resolve(strict=True)
    with tempfile.TemporaryDirectory(prefix="dkc2-coop-lifecycle-") as tmp:
        directory = Path(tmp)
        state = directory / "lost.sav"
        first = run_route(runner, rom, directory, "hurt",
                          TEAM_START + SEPARATE_PLAYERS +
                          # Keep P1 out of the first enemy contact; midpoint scrolling
                          # still applies the native screen-edge clamp.
                          [(0x80000, 120), (0x40000, 60)], save=state)
        check(any(r["b"]["s"] == "0024" for r in first[3338:]), "P2 never took damage")
        check(first[-1]["b"]["s"] == "0013" and not (first[-1]["kong_flags"] & 0x4000),
              "P2 was not lost after its hurt animation")
        second = run_route(runner, rom, directory, "rescue",
                           # Roll past the first enemy and throw the DK barrel, then
                           # let the revived P2 jump into the next enemy.
                           [(0x40082, 145), (0, 80), (0x80000, 120),
                            (0x81000, 30), (0x80000, 80), (0, 100)],
                           state=state, save=directory / "lost-again.sav")
        check(all(r["b"]["s"] == "0013" and r["sprites"][1]["flags"] == 0
                  for r in second[:100]), "input or reloading revived the lost P2")
        check(second[79]["a"]["x"] - second[0]["a"]["x"] > 140,
              "surviving P1 cannot move")
        check(any(r["b"]["s"] == "003e" and r["kong_flags"] & 0x4000
                  for r in second[100:180]), "DK barrel did not revive P2")
        check(second[280]["b"]["x"] - second[224]["b"]["x"] > 60 and
              second[280]["sprites"][1]["flags"] & 0x18 == 0x18,
              "revived P2 cannot move or interact")
        check(second[-1]["b"]["s"] == "0013" and not (second[-1]["kong_flags"] & 0x4000),
              "second contact did not remove P2 again")
        third = run_route(runner, rom, directory, "still-lost", [(0x41040, 60)],
                          state=directory / "lost-again.sav")
        check(all(r["b"]["s"] == "0013" and r["sprites"][1]["flags"] == 0
                  for r in third), "lost P2 rejoined after the second save/load")
        print("TEAM lifecycle passed: hurt, held input, save/load, P1 survival, DK-barrel rescue, P2 control, repeat loss")


if __name__ == "__main__":
    main()
