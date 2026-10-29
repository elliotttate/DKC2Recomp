#!/usr/bin/env python3
"""Private held-beetle isolation during either player's water entry and reload."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-water-held-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        for port, player in enumerate(("a", "b")):
            swimmer = "b" if port == 0 else "a"
            hold = 2 << (12*port)
            carrying = directory / f"carrying-{port}.sav"
            rows = run(f"pickup-{port}", [(0x42 << (12*port), 24 if port == 0 else 48),
                                         (hold, 25)], save=carrying)
            item = rows[-1]["held_slot"]
            check(item >= 0xE9E and rows[-1][player]["s"] == "000c" and
                  any(s["id"] == 0x1DC and s["slot"] == (item - 0xDE2)//0x5E
                      for s in rows[-1]["sprites"]), "native beetle pickup setup failed")
            direction = 0x40 if port == 0 else 0x80
            movement = hold | (direction << (12*(1-port)))
            # Stop at real water entry rather than an old camera-dependent
            # frame count. The actual replay still checks every frame for a
            # dropped item, so an ownership failure cannot pass this probe.
            probe = run(f"find-water-{port}", [(movement, 240)], carrying)
            entry = next((i for i, row in enumerate(probe) if row[swimmer]["s"] == "003d"), None)
            check(entry is not None, "partner never reached water in the bounded approach")
            prefix = [(movement, entry + 1)]
            wet = directory / f"water-entry-{port}.sav"
            rows = run(f"partner-water-{port}", prefix, carrying, wet)
            check(rows[-1][swimmer]["s"] == "003d" and
                  all(r["held_slot"] == item and r[player]["s"] == "000c" for r in rows),
                  "partner water entry dropped the carrier's beetle")
            resumed = run(f"water-reload-{port}", [(hold, 5)], wet)
            direct = run(f"water-direct-{port}", prefix + [(hold, 5)], carrying)
            keys = ("a", "b", "held_slot")
            check([[r[k] for k in keys] for r in direct[-5:]] ==
                  [[r[k] for k in keys] for r in resumed] and
                  all(r["held_slot"] == item for r in resumed),
                  "water-entry reload lost object ownership")
            # Verify intentional release from the dry checkpoint; the nearby
            # water enemy can interrupt a later throw with damage handoff.
            thrown = run(f"intentional-throw-{port}", [(0, 35)], carrying)
            check(any(r[player]["s"] == "000f" for r in thrown) and
                  any(r["held_slot"] == 0 for r in thrown),
                  "carrier's intentional throw no longer releases the item")
            own_water = run(f"carrier-water-{port}", [(0x42 << (12*port), 60)], carrying)
            check(any(r[player]["s"] == "003d" and r["held_slot"] == 0 for r in own_water),
                  "carrier's own water entry no longer performs native drop")
            print(f"carrier port {port+1}: partner swimming retains beetle, reload matches, own throw/water drop pass")
    check(state.read_bytes() == original, "original checkpoint changed")


if __name__ == "__main__":
    main()
