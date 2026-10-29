#!/usr/bin/env python3
"""Private midpoint target and actual viewport centering from a copied gameplay save."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def midpoint(rows):
    for row in rows:
        for axis in ("x", "y"):
            check(abs(row["camera_target_" + axis] -
                      (row["a"][axis] + row["b"][axis]) // 2) <= 1,
                  "camera target lost player midpoint on " + axis)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    with tempfile.TemporaryDirectory(prefix="dkc2-camera-center-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        settled = directory / "settled.sav"
        rows = run("settle", [(0, 180)], save=settled)
        midpoint(rows)
        last = rows[-1]
        check(last["a"]["s"] == "0009" and last["b"]["s"] == "003c",
              "expected rider and hanger to remain alive")
        check(abs(last["camera_x"] + 128 - last["camera_target_x"]) <= 2 and
              abs(last["camera_y"] + 112 - last["camera_target_y"]) <= 2,
              "actual viewport did not settle around midpoint")
        loaded = run("settled-reload", [(0, 30)], settled)
        midpoint(loaded)
        check(all(r["camera_x"] == last["camera_x"] and r["camera_y"] == last["camera_y"]
                  for r in loaded), "idle reload changed centered viewport")
        for name, word in (("rider", 1), ("hanger", 0x81000)):
            rows = run(name, [(word, 25), (0, 30)], settled)
            midpoint(rows)
            check(any(r[("a" if name == "rider" else "b")]["s"] in ("000a", "000b", "0006")
                      for r in rows), "camera route did not exercise moving player")
            middle = directory / (name + "-middle.sav")
            run(name + "-save", [(word, 10)], settled, middle)
            resumed = run(name + "-resume", [(word, 15), (0, 30)], middle)
            keys = ("a", "b", "camera_target_x", "camera_target_y", "camera_x", "camera_y")
            check([[r[k] for k in keys] for r in rows[10:]] ==
                  [[r[k] for k in keys] for r in resumed], "mid-jump reload changed camera/players")
    check(state.read_bytes() == original, "external source save changed")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Actual viewport midpoint, either player moving and exact camera reload pass; source unchanged")


if __name__ == "__main__":
    main()
