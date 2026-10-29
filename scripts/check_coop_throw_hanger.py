#!/usr/bin/env python3
"""Private input-only team throw into a fixed hanger; external saves stay intact."""
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
    parser.add_argument("--stuck-state", type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires supported ROM")
    carrier = word(original, 0x593)
    check(carrier in (0xDE2, 0xE40) and word(original, carrier + 0x2E) == 0x17,
          "save while carrying the partner below the reported hanger")
    shift = 12 if carrier == 0xE40 else 0
    mover, passenger = ("b", "a") if shift else ("a", "b")
    preserved = {state: original}
    with tempfile.TemporaryDirectory(prefix="dkc2-throw-hanger-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        cases = [("ground", [], source),
                 ("jump", [(1 << shift, 1), (0, 15)], source)]
        if args.stuck_state:
            path = args.stuck_state.resolve(strict=True)
            preserved[path] = path.read_bytes()
            stuck = directory / "stuck.sav"
            stuck.write_bytes(preserved[path])
            cases.append(("stuck-reload", None, stuck))
        for name, prefix, checkpoint in cases:
            output = directory / (name + ".sav")
            route = [(0, 80)] if prefix is None else prefix + [(0x12 << shift, 50), (0, 80)]
            rows = run(name, route, checkpoint, output)
            last = rows[-1]
            check(last[passenger]["s"] == "003c", "passenger did not stay attached")
            check(last[mover]["s"] == "0000", "thrower remained in team wait")
            controls = run(name + "-controls", [(0x80 << shift, 18), (0x41 << shift, 18)], output)
            check(controls[17][mover]["x"] > last[mover]["x"] + 10,
                  "thrower could not walk after reload")
            check(controls[-1][mover]["y"] < last[mover]["y"] - 40 and
                  any(r[mover]["s"] == "0006" for r in controls),
                  "thrower could not jump after reload")
            check(all(r[passenger]["s"] == "003c" for r in controls),
                  "thrower movement disturbed hanger occupant")
        # Releasing Up before launch keeps the ordinary horizontal team throw.
        rows = run("ordinary-throw", [(0x12 << shift, 1), (0, 100)], source)
        check(any(r[passenger]["s"] == "001f" for r in rows), "ordinary throw flight changed")
        check(rows[-1][mover]["s"] == "0000", "ordinary throw stranded carrier")
    check(all(p.read_bytes() == data for p, data in preserved.items()), "external save changed")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Ground/jumping hanger throws, free carrier controls, reload and ordinary throw pass; saves unchanged")


if __name__ == "__main__":
    main()
