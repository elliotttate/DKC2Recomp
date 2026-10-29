#!/usr/bin/env python3
"""Private Klobber knockback/reload and intentional pickup regression."""
import argparse
import hashlib
from pathlib import Path
import struct
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
    with tempfile.TemporaryDirectory(prefix="dkc2-enemy-push-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        for name, word, duration in (("neutral", 0, 80), ("p1", 0x80, 80),
                                     ("p2", 0x80000, 55), ("both", 0x80080, 80)):
            rows = run(name, [(word, duration)])
            check(any(r[p]["s"] == "0015" for r in rows for p in ("a", "b")),
                  name + ": no actual native push")
            check(all(r[p]["s"] not in ("0017", "0018", "001A", "0040")
                      for r in rows for p in ("a", "b")), name + ": enemy push caused team pickup")
            if name == "neutral":
                check(all(r["b"] == rows[0]["b"] for r in rows), "unhit player moved during the partner's recoil")
                check(rows[-1]["a"]["s"] == "0000" and rows[-1]["a"]["x"] < 3700,
                      "pushed partner did not finish native recoil")
        pushed = directory / "pushed.sav"
        rows = run("before-reload", [(0, 60)], save=pushed)
        check(rows[-1]["a"]["s"] == "0015", "snapshot not in native knockback")
        data = pushed.read_bytes()
        check(data[:4] == b"SLTR" and len(data) == 299458 and
              struct.unpack_from("<H", data, 168255 + 0xD7A)[0] == 0,
              "push created a held object")
        reload = run("reload", [(0, 20)], pushed)
        direct = run("direct", [(0, 80)])
        check([(r["a"], r["b"]) for r in reload] ==
              [(r["a"], r["b"]) for r in direct[-20:]], "knockback reload changed ownership or movement")
        for port in range(2):
            rows = run(f"intentional-pickup-{port}", [(0x100 << (12*port), 1), (0, 32)])
            check(any(r["a"]["s"] == "0018" or r["b"]["s"] == "0018" for r in rows),
                  "intentional player pickup stopped working")
        print("Both ports: native recoil, no forced carrying, reload and intentional pickup pass")
    check(state.read_bytes() == original, "original save changed")


if __name__ == "__main__":
    main()
