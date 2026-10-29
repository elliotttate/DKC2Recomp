#!/usr/bin/env python3
"""Private Kleever hook sharing; optional invincibility affects temporary copies only."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def snapshot_word(data, address):
    # The version-7 native snapshot layout is validated before any cheat write.
    check(len(data) == 299458 and data[:4] == b"SLTR" and
          struct.unpack_from("<I", data, 4)[0] == 7, "unsupported snapshot layout")
    return struct.unpack_from("<H", data, 168255 + address)[0]


def invincible_copy(source, target):
    data = bytearray(source.read_bytes())
    check([snapshot_word(data, slot) for slot in (0xDE2, 0xE40)] == [0xE4, 0xE8],
          "invincibility copy requires native Diddy/Dixie slots")
    work = [snapshot_word(data, a) for a in (0x595, 0x599)]
    check(set(work) == {0x16B2, 0x16D8}, "unexpected native player work selectors")
    # Native update_invincibility decrements work+$16 and supplies mask $9E.
    # Do not alter positions, velocities, hook ownership or input in the test.
    for address in work:
        struct.pack_into("<H", data, 168255 + address + 0x16, 0x7FFF)
    for slot in (0xDE2, 0xE40):
        struct.pack_into("<H", data, 168255 + slot + 0x30, 0x9E)
    target.write_bytes(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--invincible", action="store_true",
                        help="Use native invincibility on copied shared checkpoints for long release tests")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-kleever-hooks-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for first in range(2):
            mover, other = ("a", "b") if first == 0 else ("b", "a")
            held = directory / f"first-{first}.sav"
            rows = run(f"grab-{first}", [(0x81 << (first*12), 24), (0, 50)], source, held)
            check(rows[-1][mover]["s"] == "003c" and
                  all(r[other]["s"] not in ("003c", "0030", "0071") for r in rows),
                  "first grab recruited the wrong player")
            shared = directory / f"shared-{first}.sav"
            rows = run(f"second-{first}", [(0x81 << ((1-first)*12), 40), (0, 20)], held, shared)
            check(rows[-1]["a"] == rows[-1]["b"] and rows[-1]["a"]["s"] == "003c" and
                  all(r[mover]["s"] == "003c" for r in rows),
                  "second grab displaced the first player or failed to share")
            check(rows[-1]["a"]["x"] == 1008 and rows[-1]["a"]["y"] == 559,
                  "shared players did not use the native hook anchor")
            camera = snapshot_word(shared.read_bytes(), 0xAB8)
            check(camera & 0x6000 == 0x6000, "shared scan bypassed native hook camera flags")
            # Initial grab-order checks above always use the unmodified checkpoint.
            if args.invincible:
                protected = directory / f"protected-{first}.sav"
                invincible_copy(shared, protected)
                shared = protected
            rows = run(f"shared-reload-{first}", [(0, 30)], shared)
            check(all(r["a"] == r["b"] and r["a"]["s"] == "003c" for r in rows),
                  "shared hook did not survive save/load")
            for jumper in range(2):
                moving, hanging = ("a", "b") if jumper == 0 else ("b", "a")
                departed = directory / f"departed-{first}-{jumper}.sav"
                rows = run(f"first-release-{first}-{jumper}",
                           [(0x41 << (jumper*12), 1), (0x40 << (jumper*12), 54)], shared, departed)
                check(rows[0][moving]["s"] == "0006" and
                      rows[15][moving]["y"] < rows[0][moving]["y"] - 40 and
                      all(r[hanging]["s"] == "003c" for r in rows),
                      "one jump did not release independently or detached the other player")
                last = 1-jumper
                rows = run(f"last-release-{first}-{jumper}",
                           [(0x41 << (last*12), 1), (0x40 << (last*12), 20)], departed)
                check(rows[0][hanging]["s"] == "0006" and
                      rows[15][hanging]["y"] < rows[0][hanging]["y"] - 40 and
                      all(r[hanging]["s"] != "003c" for r in rows[:20]),
                      "last occupant needed another press or immediately regrabbed")
    check(state.read_bytes() == original, "external checkpoint changed")
    print("Both clean grab orders, native camera, shared reload and either/last single-press releases passed")
    if args.invincible:
        print("Temporary copied invincibility used only after clean shared-grab proof; original unchanged")


if __name__ == "__main__":
    main()
