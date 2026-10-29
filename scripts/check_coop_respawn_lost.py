#!/usr/bin/env python3
"""Private stale-loss recovery; optional real-pole replay with synthetic old loss metadata."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def word(data, address):
    return struct.unpack_from("<H", data, 168255 + address)[0]


def loss(data):
    check(data[:4] == b"SLTR" and data[-5] == 0xC2, "expected recorded co-op snapshot metadata")
    return data[-4]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--pole-state", type=Path,
                        help="earlier shared-pole death checkpoint for controlled stale-metadata replay")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    originals = {state: state.read_bytes()}
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    with tempfile.TemporaryDirectory(prefix="dkc2-respawn-loss-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(originals[state])

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        active = word(originals[state], 0x593)
        check(loss(originals[state]) == 3 and active in (0xDE2, 0xE40), "expected both old loss bits")
        active_bit = 1 if active == 0xDE2 else 2
        other = "b" if active_bit == 1 else "a"
        output = directory / "reconciled.sav"
        rows = run("reconcile", [(0, 2)], save=output)
        check(loss(output.read_bytes()) == 3 & ~active_bit, "live respawn retained stale loss")
        check(all(r[other]["s"] == "0013" for r in rows), "repair revived dead teammate")
        if args.pole_state:
            pole_path = args.pole_state.resolve(strict=True)
            originals[pole_path] = pole_path.read_bytes()
            poles = directory / "poles.sav"
            poles.write_bytes(originals[pole_path])
            for fallen in range(2):
                survivor, dead = ("b", "a") if fallen == 0 else ("a", "b")
                lost = directory / f"pole-loss-{fallen}.sav"
                run(f"pole-fall-{fallen}", [(0x81 << (12*fallen), 12), (0, 288)], poles, lost)
                # Recreate only the stale host bits from the reported save;
                # preserve the cartridge's pole geometry, actors and gameplay state.
                data = bytearray(lost.read_bytes())
                check(loss(data) == 1 << fallen, "pole fall did not leave one survivor")
                data[-4] = 3
                lost.write_bytes(data)
                up = directory / f"up-{fallen}.sav"
                climbed = run(f"climb-{fallen}", [(0x10 << (12*(1-fallen)), 20)], lost, up)
                down = run(f"down-{fallen}", [(0x20 << (12*(1-fallen)), 20)], lost)
                check(all(r[survivor]["s"] == "0045" and r[dead]["s"] == "0013"
                          for r in climbed + down), "stale-loss repair broke pole/dead-player state")
                check(climbed[-1][survivor]["y"] + 12 < down[-1][survivor]["y"],
                      "survivor cannot climb with dead teammate")
                check(loss(up.read_bytes()) == 1 << fallen, "pole repair cleared dead teammate's loss")
    check(all(p.read_bytes() == data for p, data in originals.items()), "external source changed")
    print("state_sha256=" + hashlib.sha256(originals[state]).hexdigest())
    print("Live respawn loss repair and controlled real-pole stale-metadata checks pass; sources unchanged")


if __name__ == "__main__":
    main()
