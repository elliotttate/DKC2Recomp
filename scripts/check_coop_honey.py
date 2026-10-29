#!/usr/bin/env python3
"""Private honey-floor ownership, independent controls and jump reload checks."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-honey-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for port, player in enumerate(("a", "b")):
            other = "b" if port == 0 else "a"
            stuck = directory / f"stuck-{port}.sav"
            rows = run(f"contact-{port}", [(0x80 << (12*port), 140)], save=stuck)
            last = rows[-1]
            check(last[player]["s"] == "0043" and last[other]["s"] == "0000",
                  "honey contact affected the wrong Kong")
            walk = run(f"dry-walk-{port}", [(0x80 << (12*(1-port)), 25)], stuck)
            check(walk[-1][other]["x"] > last[other]["x"] + 15 and
                  all(r[player] == last[player] for r in walk), "honey locked the dry teammate")
            word = 0x81 << (12*port)
            jump = run(f"escape-{port}", [(word, 25)], stuck)
            check(jump[-1][player]["s"] == "0006" and
                  jump[-1][player]["y"] < last[player]["y"] - 50 and
                  all(r[other]["s"] == "0000" for r in jump), "own controller cannot jump out of honey")
            middle = directory / f"jump-{port}.sav"
            run(f"mid-jump-{port}", [(word, 10)], stuck, middle)
            resumed = run(f"reload-jump-{port}", [(word, 15)], middle)
            check([(r["a"], r["b"]) for r in jump[10:]] ==
                  [(r["a"], r["b"]) for r in resumed], "honey jump reload changed trajectory")
            print(f"P{port+1} honey contact: dry teammate walks, own escape jump and reload pass")

        both = directory / "both.sav"
        rows = run("both-contact", [(0x80080, 150)], save=both)
        check(all(rows[-1][p]["s"] == "0043" for p in ("a", "b")), "both players did not stick to honey")
        for port, player in enumerate(("a", "b")):
            other = "b" if port == 0 else "a"
            word = 0x81 << (12*port)
            jump = run(f"both-escape-{port}", [(word, 25)], both)
            check(jump[-1][player]["s"] == "0006" and
                  all(r[other] == rows[-1][other] for r in jump), "honey escape controls are coupled")
            hops = run(f"repeat-hop-{port}", [(word, 25), (0, 60), (word, 25)], both)
            check(hops[-1][player]["s"] == "0006" and
                  hops[-1][player]["y"] < rows[-1][player]["y"] - 50,
                  "native repeated honey hop failed")
        print("Both on honey: either player escapes independently and can hop again")
    check(state.read_bytes() == original, "external snapshot changed")


if __name__ == "__main__":
    main()
