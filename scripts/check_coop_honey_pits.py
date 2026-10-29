#!/usr/bin/env python3
"""Private input-only pit handling with a sticky-wall/honey-floor survivor."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-honey-pits-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        ready = run("inspect", [(0, 1)])[0]
        check(ready["a"]["s"] == "0043" and ready["b"]["s"] == "0041",
              "expected reported P1 honey-floor/P2 sticky-wall checkpoint")
        for fallen, player in enumerate(("a", "b")):
            other = ("b", "a")[fallen]
            # P2 first jumps off its wall onto the same platform as P1.
            prefix = [(0x81000, 45), (0, 60)] if fallen else []
            route = prefix + [(0x41 << (12*fallen), 45), (0, 400)]
            lost = directory / f"lost-{fallen}.sav"
            rows = run(f"fall-{fallen}", route, save=lost)
            last = rows[-1]
            check(all(r["level_id"] == ready["level_id"] for r in rows),
                  "one pit fall ended the level with a live honey teammate")
            check(last["active"] == ("0e40" if fallen == 0 else "0de2") and
                  last[player]["s"] == "0013" and last["sprites"][fallen]["flags"] == 0,
                  "fallen player was not consistently recorded as lost")
            check(all(r[other] == ready[other] for r in rows), "pit loss disturbed honey survivor")
            # Neither ordinary input nor Select can revive a dead player.
            reloaded = run(f"reload-{fallen}", [(4 << (12*fallen), 180),
                                                  (0x81 << (12*fallen), 30)], lost)
            check(all(r[player]["s"] == "0013" and r["sprites"][fallen]["flags"] == 0
                      for r in reloaded), "reload/Select revived a lost player")
            jumped = run(f"survivor-{fallen}", [(0x81 << (12*(1-fallen)), 25)], lost)
            check(any(r[other]["s"] in ("0006", "0042", "0044") for r in jumped),
                  "survivor's controller cannot jump from honey")
    check(state.read_bytes() == original, "external source changed")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Both pit fallers: level continues, honey survivor controls and persistent lost state pass; save unchanged")


if __name__ == "__main__":
    main()
