#!/usr/bin/env python3
"""Private input-only pit survival checks from the reported shared-pole save."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-pole-survival-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        for fallen, player in enumerate(("a", "b")):
            survivor = 1 - fallen
            other = "b" if fallen == 0 else "a"
            word = 0x81 << (12*fallen)
            lost = directory / f"lost-{fallen}.sav"
            route = [(word, 12), (0, 288)]
            rows = run(f"fall-{fallen}", route, save=lost)
            check(all(r["sub"] == "0f" and r["level_id"] == 41 for r in rows),
                  "pit fall ended the level with a live pole climber")
            last = rows[-1]
            check(last["active"] == ("0de2" if survivor == 0 else "0e40") and
                  last[player]["s"] == "0013" and last["sprites"][fallen]["flags"] == 0 and
                  all(r[other]["s"] == "0045" and r[other]["x"] == 3024 for r in rows),
                  "pit loss displaced the pole survivor or did not park the fallen Kong")
            reloaded = run(f"lost-reload-{fallen}", [(word, 30), (0, 30)], lost)
            check(all(r[other] == last[other] and r[player]["s"] == "0013" and
                      r["sprites"][fallen]["flags"] == 0 for r in reloaded),
                  "lost reload/input changed survivor or revived fallen player")
            up = run(f"survivor-up-{fallen}", [(0x10 << (12*survivor), 20)], lost)
            down = run(f"survivor-down-{fallen}", [(0x20 << (12*survivor), 20)], lost)
            check(up[-1][other]["y"] + 12 < down[-1][other]["y"] and
                  all(r[other]["s"] == "0045" for r in up + down),
                  "survivor's own controller cannot climb")
            jump = run(f"survivor-jump-{fallen}", [(0x81 << (12*survivor), 12)], lost)
            check(any(r[other]["s"] == "0006" for r in jump),
                  "survivor cannot jump off the pole")
            last_fall = run(f"last-fall-{fallen}", [(0x81 << (12*survivor), 12), (0, 350)], lost)
            check(any(r["sub"] == "00" for r in last_fall),
                  "last player's death no longer ends the level")
            midfall = directory / f"midfall-{fallen}.sav"
            run(f"midfall-{fallen}", [(word, 12), (0, 21)], save=midfall)
            resumed = run(f"midfall-reload-{fallen}", [(0, 267)], midfall)
            keys = ("a", "b", "active", "sub", "level_id")
            check([[r[k] for k in keys] for r in rows[-267:]] ==
                  [[r[k] for k in keys] for r in resumed],
                  "midfall reload changed survival or player trajectory")
            print(f"fallen port {fallen+1}: pole survivor, own controls, lost/midfall reload and final loss pass")
    check(state.read_bytes() == original, "original snapshot changed")


if __name__ == "__main__":
    main()
