#!/usr/bin/env python3
"""Private input-only sideways throw and camera-framed shared hook acceptance."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--"+name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-thrown-hooks-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"; source.write_bytes(original)
        def run(name, route, checkpoint=source, save=None):
            rows = run_route(runner, rom, directory, name, route, state=checkpoint,
                             save=save, submode=None)
            check(all(r["mode"] == 1 for r in rows), "gameplay reset during hook handling")
            return rows
        # This low ground throw passes below the hook. Do not invent a grab
        # without overlap, or alter ordinary throw motion to force contact.
        miss = run("miss", [(0x82, 1), (0, 90)])
        check(all(r["b"]["s"] != "003c" for r in miss), "non-contact throw attached")
        first = directory / "first.sav"
        prefix = [(0x81, 22), (0x82, 1), (0, 85)]
        rows = run("side", prefix, save=first)
        check(any(r["b"]["s"] == "001f" for r in rows), "not a horizontal throw")
        check(rows[-1]["b"]["s"] == "003c" and rows[-1]["a"]["s"] == "0000",
              "sideways hook contact failed or stranded thrower")
        shared = directory / "shared.sav"
        approach = [(0x83, 45), (0, 30)]
        rows = run("join", approach, first, shared)
        check(rows[-1]["a"] == rows[-1]["b"] and rows[-1]["a"]["s"] == "003c",
              "camera-framed hook cannot be shared")
        idle = run("shared-idle", [(0, 150)], shared)
        check(all(r["a"] == r["b"] and r["a"]["s"] == "003c" for r in idle),
              "shared pose or ownership did not persist")
        direct = run("continuous", prefix + approach + [(0, 150)])[-150:]
        keys = ("a", "b", "active", "camera_x", "camera_y", "sprites")
        check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in idle],
              "shared save/reload diverged")
        for who in range(2):
            mover, other = ("a", "b") if who == 0 else ("b", "a")
            departed = directory / f"departed-{who}.sav"
            rows = run(f"leave-{who}", [(0x43 << (who*12), 40), (0, 20)], shared, departed)
            check(rows[0][mover]["s"] == "0006" and
                  rows[15][mover]["y"] < rows[0][mover]["y"]-40,
                  "one Jump did not release occupant into native arc")
            check(all(r[other]["s"] == "003c" for r in rows), "departure detached partner")
            second = 1-who
            rows = run(f"last-leaves-{who}", [(0x43 << (second*12), 40)], departed)
            check(rows[0][other]["s"] == "0006" and
                  all(r[other]["s"] not in ("0030", "003c") for r in rows[:25]),
                  "last occupant needs a second press or is immediately regrabbed")
        rejoin = run("regrab", [(0x82, 14), (0x83, 45), (0, 25)], directory/"departed-0.sav")
        check(rejoin[-1]["a"] == rejoin[-1]["b"] and rejoin[-1]["a"]["s"] == "003c",
              "remaining occupant prevents later regrab")
    check(state.read_bytes() == original, "source save changed")
    print("Real sideways contact, non-contact rejection, shared framed hook, independent departures, regrab and exact reload pass")


if __name__ == "__main__":
    main()
