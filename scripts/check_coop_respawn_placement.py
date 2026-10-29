#!/usr/bin/env python3
"""Private DK-barrel landing placement from the reported narrow-platform save."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-respawn-placement-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        neutral = run("lost-wait", [(0, 30)])
        check(all(r["b"]["s"] == "0013" and r["sprites"][1]["flags"] == 0 for r in neutral),
              "waiting without a DK barrel revived the lost Kong")
        prefix = [(0x82, 15), (0, 5), (2, 1)]
        safe = directory / "landed.sav"
        rows = run("revive", prefix + [(0, 200)], save=safe)
        start = next(i for i, r in enumerate(rows) if r["b"]["s"] == "003e")
        finish = next(i for i in range(start + 1, len(rows)) if rows[i]["b"]["s"] != "003e")
        landed = rows[finish]
        check(landed["b"]["s"] == "0022" and
              all(landed["a"][axis] == landed["b"][axis] for axis in ("x", "y")),
              "returning player did not finish the entrance at the living player's current position")
        check(all(r["level_id"] == 41 and r["sub"] == "0f" and
                  r["b"]["s"] not in ("0013", "003b", "0024", "0025") for r in rows[finish:]),
              "returning player slid into the pit after landing")
        check(all(rows[-1][p]["s"] == "0000" and rows[-1][p]["y"] == 685 for p in ("a", "b")),
              "both players did not remain safely on the platform")
        airborne = directory / "entrance.sav"
        run("save-entrance", prefix + [(0, 39)], save=airborne)
        resumed = run("reload-entrance", [(0, 161)], airborne)
        keys = ("a", "b", "active", "sub", "level_id")
        check([[r[k] for k in keys] for r in rows[-161:]] ==
              [[r[k] for k in keys] for r in resumed], "entrance reload changed landing or trajectory")
        reloaded = run("reload-landed", [(0, 90)], safe)
        check(all(r["b"] == rows[-1]["b"] for r in reloaded),
              "landed reload restored entrance drift")
        for port, player in enumerate(("a", "b")):
            other = "b" if player == "a" else "a"
            controls = run(f"controls-{port}", [(1 << (12*port), 20)], safe)
            check(any(r[player]["s"] == "0006" and r[player]["y"] < 650 for r in controls) and
                  all(r[other] == rows[-1][other] for r in controls),
                  "players lost independent jump control after revival")
        print("DK-barrel return: exact survivor landing, no pit drift, entrance/landed reload and either-port controls pass")
    check(state.read_bytes() == original, "original snapshot changed")


if __name__ == "__main__":
    main()
