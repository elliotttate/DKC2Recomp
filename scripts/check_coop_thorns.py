#!/usr/bin/env python3
"""Private input-only thorn damage, survivor and save/reload acceptance."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-thorns-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for port, player in enumerate(("a", "b")):
            other = "b" if port == 0 else "a"
            lost = directory / f"lost-{port}.sav"
            route = [(0x40 << (12*port), 90), (0, 240)]
            rows = run(f"thorn-{port}", route, save=lost)
            hurt = next((i for i, row in enumerate(rows) if row[player]["s"] == "0024"), None)
            check(hurt is not None, "touching player did not enter native hurt animation")
            check(rows[hurt][other]["s"] == "0026", "wrong player received thorn damage")
            arc = [row for row in rows if row[player]["s"] == "0025"]
            check(len(arc) > 20 and min(row[player]["y"] for row in arc) < rows[hurt][player]["y"] - 30,
                  "native death arc did not play")
            check(rows[-1][player]["s"] == "0013" and rows[-1]["sprites"][port]["flags"] == 0 and
                  rows[-1][other]["s"] == "0000" and all(row["sub"] == "10" for row in rows),
                  "death did not preserve the surviving teammate")
            middle = directory / f"middle-{port}.sav"
            run(f"mid-hurt-{port}", [(0x40 << (12*port), 90), (0, 5)], save=middle)
            resumed = run(f"resume-{port}", [(0, 235)], middle)
            check(all((a["a"], a["b"], a["active"], a["sub"]) ==
                      (b["a"], b["b"], b["active"], b["sub"])
                      for a, b in zip(rows[95:], resumed)), "mid-death reload changed the sequence")
            controls = run(f"survivor-jump-{port}", [(1 << (12*(1-port)), 20), (0, 40)], lost)
            check(any(row[other]["s"] == "0006" for row in controls), "survivor cannot jump")
            check(all(row[player]["s"] == "0013" for row in controls), "lost player revived on reload")
            final = run(f"last-thorn-{port}", [(0x40 << (12*(1-port)), 160), (0, 340)], lost)
            check(any(row[other]["s"] == "0005" for row in final) and
                  any(row["sub"] == "00" for row in final), "final thorn death did not end the level")
            print(f"P{port + 1}: native hurt/arc, survivor controls, mid-death reload and final loss passed")
    check(state.read_bytes() == original, "external snapshot changed")


if __name__ == "__main__":
    main()
