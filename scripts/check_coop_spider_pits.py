#!/usr/bin/env python3
"""Private Squitter rider pit handoff, palette cleanup and snapshot acceptance."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def word(data, address):
    return struct.unpack_from("<H", data, 168255 + address)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--broken-state", type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    preserved = {state: original}
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    check(word(original, 0x593) == 0xDE2 and word(original, 0x6E) == 0x190,
          "expected reported P1 riding Squitter checkpoint")
    with tempfile.TemporaryDirectory(prefix="dkc2-spider-pits-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        # X dismount, then P2 jumps onto the idle spider: no guest patches.
        reverse = directory / "p2-rider.sav"
        run("reverse-rider", [(0x200, 1), (0, 60), (0x1000, 35), (0, 60)], save=reverse)
        check(word(reverse.read_bytes(), 0x593) == 0xE40 and
              word(reverse.read_bytes(), 0x6E) == 0x190, "input-only rider swap failed")
        for who, checkpoint in enumerate((source, reverse)):
            player, other = ("a", "b") if who == 0 else ("b", "a")
            before = checkpoint.read_bytes()
            animal = word(before, 0x6C)
            expected_palette = word(before, animal + 0x12) & 0xE00
            other_palette = word(before, (0xE40 if who == 0 else 0xDE2) + 0x12) & 0xE00
            for direction in (0x40, 0x80):
                name = f"rider-{who}-{direction}"
                route = [(direction << (12*who), 80), (0, 300)]
                lost = directory / (name + ".sav")
                rows = run(name, route, checkpoint, lost)
                last = rows[-1]
                check(all(r["level_id"] == word(original, 0xD3) for r in rows), "rider loss ended level")
                check(last[player]["s"] == "0013" and last["sprites"][who]["flags"] == 0 and
                      last["active"] == ("0e40" if who == 0 else "0de2"), "rider was not marked lost")
                check(last["animal_slot"] == last["animal_type"] == 0 and
                      last["kong_flags"] & 0x200 == 0, "mount leaked to survivor")
                check(last["sprites"][who]["attr"] & 0xE00 == expected_palette and
                      last["sprites"][1-who]["attr"] & 0xE00 == other_palette, "pit handoff corrupted palettes")
                check(last[other]["s"] == "0000", "survivor inherited rider animation")
                controls = run(name + "-controls", [(0x81 << (12*(1-who)), 15)], lost)
                check(any(r[other]["s"] == "0006" for r in controls) and
                      controls[-1][other]["y"] < last[other]["y"] - 20, "survivor cannot jump normally")
                loaded = run(name + "-reload", [(4 << (12*who), 180)], lost)
                check(all(r[player]["s"] == "0013" and r["animal_type"] == 0 for r in loaded),
                      "reload or Select revived rider/transferred spider")
                if direction == 0x40:
                    middle = directory / f"midfall-{who}.sav"
                    run(name + "-midfall", [(direction << (12*who), 30)], checkpoint, middle)
                    resumed = run(name + "-resume", [(direction << (12*who), 50), (0, 300)], middle)
                    keys = ("a", "b", "active", "animal_slot", "animal_type", "kong_flags")
                    check([[r[k] for k in keys] for r in rows[30:]] ==
                          [[r[k] for k in keys] for r in resumed], "midfall reload changed handoff")
        if args.broken_state:
            broken = args.broken_state.resolve(strict=True)
            preserved[broken] = broken.read_bytes()
            copy = directory / "broken.sav"
            copy.write_bytes(preserved[broken])
            rows = run("old-broken-repair", [(0, 100)], copy)
            check(rows[-1]["animal_slot"] == rows[-1]["animal_type"] == 0,
                  "old mounted-loss save retained stale spider context")
    check(all(p.read_bytes() == data for p, data in preserved.items()), "external source changed")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Either Squitter rider, both pit edges: lost rider, clean mount/palette handoff, survivor controls and reload pass; saves unchanged")


if __name__ == "__main__":
    main()
