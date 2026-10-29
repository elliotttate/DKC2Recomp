#!/usr/bin/env python3
"""Private input-only equal barrel force, upward steering and landing-animation checks."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-barrel-momentum-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        # Use native team-up/put-down to cover the other controller as leader.
        swapped = directory / "p1-leader.sav"
        rows = run("swap", [(0x100, 1), (0, 50), (0x100, 1), (0, 80)], source, swapped)
        check(rows[-1]["active"] == "0de2" and
              rows[-1]["a"]["s"] == rows[-1]["b"]["s"] == "0000",
              "native reverse-leadership setup did not complete")
        for owner, checkpoint, entrant, delay in ((1, source, 1, 0), (0, swapped, 0, 60)):
            inside = directory / f"inside-{owner}.sav"
            rows = run(f"entry-{owner}", [(0x81 << (entrant*12), 50), (0, 90)], checkpoint, inside)
            check(rows[-1]["active"] == ("0de2" if owner == 0 else "0e40"),
                  "barrel changed the tested control owner")
            prefix = [(0, delay)] if delay else []
            for shooter in range(2):
                rows = run(f"up-right-{owner}-{shooter}", prefix +
                           [(1 << (shooter*12), 1), (0x80080, 55)], inside)
                flight = rows[delay:]
                check(any(r["a"]["s"] == r["b"]["s"] == "0012" for r in flight),
                      "both Kongs did not enter independent native shot physics")
                peaks = [min(r[p]["y"] for r in flight) for p in ("a", "b")]
                check(max(peaks) < 5870 and abs(peaks[0] - peaks[1]) <= 1,
                      "upward shot gives the players different maximum heights")
                early = flight[5]
                check(abs(early["a"]["x"] - early["b"]["x"]) <= 8 and
                      abs(early["a"]["y"] - early["b"]["y"]) <= 8,
                      "initial launch force differs by more than one native update")
                check(all(r[p]["s"] == "0012" for r in flight[5:40] for p in ("a", "b")),
                      "one player reverted to ordinary falling during upward flight")
                if owner == 1:
                    check(flight[45]["a"]["x"] > 405 and flight[45]["b"]["x"] > 405 and
                          abs(flight[45]["a"]["y"] - flight[45]["b"]["y"]) <= 1,
                          "one player cannot steer right while preserving upward force")
            airborne = directory / f"air-{owner}.sav"
            run(f"save-flight-{owner}", prefix + [(1, 1), (0x80080, 30)], inside, airborne)
            rows = run(f"reload-flight-{owner}", [(0x80080, 10)], airborne)
            check(all(r["a"]["s"] == r["b"]["s"] == "0012" for r in rows),
                  "paired native shot physics did not persist across flight reload")
        print("Both control owners and shooters: equal upward height, right steering and flight reload passed")

        # The earlier approach catches the rotating barrel aiming up-left.
        # Steer right onto the floor, then verify each player's left animation.
        inside = directory / "landing-inside.sav"
        run("landing-entry", [(0x81, 50), (0, 90)], source, inside)
        landed = directory / "landed.sav"
        rows = run("land", [(1, 1), (0x80080, 100)], inside, landed)
        last = rows[-1]
        check(last["a"]["s"] == last["b"]["s"] == "0000" and
              last["a"]["y"] == last["b"]["y"] == 6129,
              "native shot landing did not return both players to normal control")
        for player, mover in enumerate(("a", "b")):
            rows = run(f"walk-left-{player}", [(0x40 << (player*12), 35), (0, 15)], landed)
            check(rows[34][mover]["x"] < last[mover]["x"] - 25 and
                  rows[-1][mover]["s"] == "0000" and
                  all(r[mover]["s"] in ("0000", "0022") for r in rows),
                  "landed player cannot walk left normally")
            graphics = {r["sprites"][player]["graphic"] for r in rows[:35]}
            check(len(graphics) >= 4 and
                  all(r["sprites"][player]["animation"] not in (0x1C, 0xBC, 0x40, 0xE3)
                      for r in rows[10:]),
                  "landed player remained in a frozen barrel/roll animation")
        print("Both landing transitions, leftward walking animation and landing reload passed")
    check(state.read_bytes() == original, "external checkpoint changed")


if __name__ == "__main__":
    main()
