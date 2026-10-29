#!/usr/bin/env python3
"""Private input-only shared barrel aiming, cancellation and firing regression."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def word(data, address):
    check(len(data) == 299458 and data[:4] == b"SLTR" and
          struct.unpack_from("<I", data, 4)[0] == 7, "unsupported snapshot layout")
    return struct.unpack_from("<H", data, 168255 + address)[0]


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
    with tempfile.TemporaryDirectory(prefix="dkc2-barrel-rotation-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for entrant, approach in enumerate((0x41, 0x81000)):
            inside = directory / f"inside-{entrant}.sav"
            rows = run(f"entry-{entrant}", [(approach, 12), (1 << (12*entrant), 30),
                                            (0, 40)], source, inside)
            check(rows[-1]["a"]["s"] == "0031" and rows[-1]["b"]["s"] == "0011",
                  "both players did not enter the reported barrel")
            data = inside.read_bytes()
            barrel = word(data, 0xA84)
            check(word(data, barrel) == 0x140 and word(data, barrel + 0x4E) == 0x400,
                  "expected the reported manually rotatable barrel")
            angles, shots = {}, {}
            for name, inputs in (("neutral", 0), ("p1-left", 0x40), ("p1-right", 0x80),
                                 ("p2-left", 0x40000), ("p2-right", 0x80000),
                                 ("opposed", 0x40080), ("both-right", 0x80080)):
                aimed = directory / f"aimed-{entrant}-{name}.sav"
                rows = run(f"aim-{entrant}-{name}", [(inputs, 12)], inside, aimed)
                check(all(r["a"]["s"] == "0031" and r["b"]["s"] == "0011" for r in rows),
                      "aiming unexpectedly fired or detached a player")
                angles[name] = word(aimed.read_bytes(), barrel + 0x48)
                if name.startswith("p") and name != "opposed":
                    for shooter in range(2):
                        fired = run(f"fire-{entrant}-{name}-{shooter}",
                                    [(1 << (12*shooter), 1), (0, 20)], aimed)
                        check(any(r["a"]["s"] == r["b"]["s"] == "0012" for r in fired),
                              "either controller failed to launch both players after aiming/reload")
                        shots[name, shooter] = [(r["a"], r["b"]) for r in fired]
            check(angles["p1-left"] == angles["p2-left"] == 0xDC00 and
                  angles["p1-right"] == angles["p2-right"] == 0x2400,
                  "the controllers did not rotate at the same native rate in both directions")
            check(angles["opposed"] == angles["neutral"] == 0 and
                  angles["both-right"] == angles["p1-right"],
                  "opposite inputs did not cancel or same-direction inputs doubled rotation")
            for direction in ("left", "right"):
                check(shots[f"p1-{direction}", 0] == shots[f"p2-{direction}", 0] ==
                      shots[f"p1-{direction}", 1] == shots[f"p2-{direction}", 1],
                      "aim/fire controller changed the paired shot trajectory")
            # The saved barrel initially points down: native clockwise and
            # counterclockwise turns therefore send shots left/right respectively.
            check(shots["p1-left", 0][-1][0]["x"] > 1140 and
                  shots["p1-right", 0][-1][0]["x"] < 1100,
                  "aiming did not change the actual launch direction")
            print(f"entrant P{entrant+1}: either controller aims/fires; native rate, "
                  "opposing cancellation, reload and paired trajectories pass")
    check(state.read_bytes() == original, "original snapshot changed")


if __name__ == "__main__":
    main()
