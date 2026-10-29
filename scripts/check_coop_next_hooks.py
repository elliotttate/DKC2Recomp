#!/usr/bin/env python3
"""Private input-only saved thrown-occupant recovery and successive shared hooks."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-next-hooks-") as tmp:
        directory = Path(tmp)
        source = directory/"original.sav"; source.write_bytes(original)
        def run(name, route, checkpoint=source, save=None):
            rows = run_route(runner, rom, directory, name, route, state=checkpoint,
                             save=save, submode=None)
            check(all(r["mode"] == 1 for r in rows), "left gameplay during hook traversal")
            return rows
        second = directory/"second.sav"
        route = [(0x83000, 35), (0, 65)]
        rows = run("second", route, save=second)
        check(all(r["a"] == {"x":1704,"y":535,"s":"003c"} for r in rows),
              "P2 progression disturbed the first occupant")
        check(rows[-1]["a"] == rows[-1]["b"], "saved thrown player cannot use next hook")
        # The snapshot contains the old thrown mask. It must repair itself
        # without editing that save, including a reload halfway through flight.
        mid = directory/"mid.sav"
        run("save-mid", [(0x83000, 20)], save=mid)
        resumed = run("resume-mid", [(0x83000, 15), (0, 65)], mid)
        keys = ("a", "b", "active", "camera_x", "camera_y", "sprites")
        check([[r[k] for k in keys] for r in rows[20:]] ==
              [[r[k] for k in keys] for r in resumed], "mid-jump reload diverged")
        for first in range(2):
            mover, waiting = ("a","b") if first == 0 else ("b","a")
            next_save = directory/f"third-first-{first}.sav"
            rows = run(f"third-{first}", [(0x83 << (first*12),25),(0,50)], second, next_save)
            check(rows[-1][mover] == {"x":1784,"y":495,"s":"003c"},
                  "hanging teammate blocks a different hook")
            check(all(r[waiting] == {"x":1704,"y":535,"s":"003c"} for r in rows),
                  "different-hook grab recruited teammate")
            other = 1-first
            rows = run(f"share-third-{first}", [(0x83 << (other*12),25),(0,60)], next_save)
            check(rows[-1]["a"] == rows[-1]["b"] == {"x":1784,"y":495,"s":"003c"},
                  "third hook cannot be shared in either grab order")
    check(state.read_bytes() == original, "source checkpoint changed")
    print("Saved thrown mask recovery, next-hook sharing, either third-hook grab order and exact flight reload pass")


if __name__ == "__main__":
    main()
