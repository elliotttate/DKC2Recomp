#!/usr/bin/env python3
"""Private input-only pit survival acceptance from the reported Gangplank save."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--state", required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-pits-") as tmp:
        directory = Path(tmp)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        # Native team-up and put-down swap leadership without changing input
        # ownership or patching guest memory. Both players finish on safe ground.
        swapped = directory / "p1-leader.sav"
        rows = run("swap", [(0x80, 10), (0, 8), (0x100, 1), (0, 50),
                            (0x100, 1), (0, 80)], state, swapped)
        check(rows[-1]["active"] == "0de2" and
              rows[-1]["a"]["s"] == rows[-1]["b"]["s"] == "0000",
              "native leadership swap did not finish")
        for leader, source in ((0, swapped), (1, state)):
            for fallen in range(2):
                survivor = 1 - fallen
                mover, other = ("a", "b") if fallen == 0 else ("b", "a")
                lost = directory / f"lost-{leader}-{fallen}.sav"
                rows = run(f"fall-{leader}-{fallen}",
                           [(0x80 << (fallen*12), 100), (0, 140)], source, lost)
                check(all(r["level_id"] == 4 and r["sub"] == "18" for r in rows),
                      "pit fall ended the level with a live partner")
                last = rows[-1]
                check(last["active"] == ("0de2" if survivor == 0 else "0e40") and
                      last[mover]["s"] == "0013" and
                      last["sprites"][fallen]["flags"] == 0 and
                      last[other]["s"] == "0000" and last[other]["y"] == 593,
                      "pit did not park fallen player and preserve grounded survivor")
                rows = run(f"reload-{leader}-{fallen}",
                           [(0x81 << (fallen*12), 45), (0, 40)], lost)
                check(all(r[mover]["s"] == "0013" and
                          r["sprites"][fallen]["flags"] == 0 for r in rows),
                      "held input or reload revived fallen player")
                rows = run(f"controls-{leader}-{fallen}",
                           [(0x41 << (survivor*12), 20), (0, 70)], lost)
                check(any(r[other]["s"] == "0006" for r in rows) and
                      rows[19][other]["y"] < 553,
                      "surviving player's own controller cannot jump")
                rows = run(f"last-fall-{leader}-{fallen}",
                           [(0x80 << (survivor*12), 180), (0, 250)], lost)
                check(any(r[other]["s"] == "003b" for r in rows) and
                      any(r["sub"] == "00" for r in rows),
                      "last surviving player's pit fall did not retain native level loss")
        rows = run("both-fall", [(0x80080, 100), (0, 300)], state)
        check(any(r["sub"] == "00" for r in rows),
              "both falling players incorrectly preserved the level")
    check(state.read_bytes() == original, "external snapshot changed")
    print("Both leaders and fallers: survivor controls, lost save/load, final and simultaneous loss passed")


if __name__ == "__main__":
    main()
