#!/usr/bin/env python3
"""Private input-only selected Kleever level intro and subsequent co-op controls."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-boss-intro-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        ready = directory / "ready.sav"
        rows = run("enter", [(1, 1), (0, 429)], save=ready)
        check(all(r["level_id"] == 0x21 for r in rows), "selected boss level changed")
        intro = rows[95:408]
        check({"0074", "0075"}.issubset({r["a"]["s"] for r in intro}),
              "native boss intro hold/walk sequence did not run")
        check(all(r["a"]["y"] == 657 and r["b"]["y"] <= 657 for r in intro),
              "boss intro lets a player sink into the lava")
        check(all(r["b"]["s"] == "0022" for r in rows[206:228]),
              "partner left native follower handling before the intro ended")

        def alive(row):
            return row["a"]["s"] == row["b"]["s"] == "0000" and all(
                row["sprites"][p]["flags"] & 0x10 for p in range(2))

        check(alive(rows[-1]) and rows[-1]["a"]["x"] == 996 and
              rows[-1]["b"]["x"] == 950,
              "intro did not restore two live players at native floor positions")
        for name, held in (("p2-held", 0x83000), ("both-held", 0x83083)):
            guarded = run(name, [(1, 1), (held, 407)])
            check(all(r["a"] == rows[i]["a"] and r["b"] == rows[i]["b"]
                      for i, r in enumerate(guarded[95:], start=95)),
                  name + ": held inputs escaped scripted intro handling")
        middle = directory / "middle.sav"
        run("save-intro", [(1, 1), (0, 299)], save=middle)
        resumed = run("reload-intro", [(0, 130)], middle)
        check(all(r["a"]["y"] == r["b"]["y"] == 657 for r in resumed) and
              alive(resumed[-1]), "intro reload lost or stranded the partner")
        for player, mover in enumerate(("a", "b")):
            other = "b" if player == 0 else "a"
            word = 0x41 if player == 0 else 0x81
            control = run(f"controls-{player}", [(word << (player*12), 20)], ready)
            check(any(r[mover]["s"] == "0006" for r in control) and
                  control[-1][mover]["y"] < 627 and
                  all(r[other]["y"] == 657 for r in control),
                  "post-intro player cannot independently move/jump after reload")
    check(state.read_bytes() == original, "external checkpoint changed")
    print("Selected boss entry, native intro, held-input protection, intro reload and both controls passed")


if __name__ == "__main__":
    main()
