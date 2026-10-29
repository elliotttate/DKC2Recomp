#!/usr/bin/env python3
"""Private independent pole entry, climb limits, release and reload regression."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-coop-poles-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        def midpoint(rows):
            for r in rows[-8:]:
                for axis in ("x", "y"):
                    # Integer trace positions omit the native subpixel words.
                    check(abs(r["camera_target_" + axis] -
                              (r["a"][axis] + r["b"][axis]) // 2) <= 1,
                          "pole camera target does not follow both players on " + axis)

        for port, player in enumerate(("a", "b")):
            single = directory / f"single-pole-{port}.sav"
            rows = run(f"single-pole-{port}",
                       [(0x82 << (12*port), 10), (0x93 << (12*port), 40),
                        (0x10 << (12*port), 10 if port == 0 else 25)], save=single)
            check(all(r[player]["s"] == "0045" and
                      r["b" if player == "a" else "a"]["s"] != "0045" for r in rows[-8:]),
                  "one-pole camera setup did not leave exactly one player climbing")
            midpoint(rows)
            midpoint(run(f"single-pole-reload-{port}", [(0, 5)], single))
        print("Either sole climber: midpoint on both axes and reload pass")

        # Give each later entrant the same native run-up as the sole-climber
        # route; jumping from rest can fall below the pole before reaching it.
        approaches = {
            "together": [(0x82082, 10), (0x93093, 40), (0x10010, 35)],
            "p1-first": [(0x82, 10), (0x93, 40), (0x82010, 10), (0x93010, 40), (0x10010, 15)],
            "p2-first": [(0x82000, 10), (0x93000, 40), (0x10082, 10), (0x10093, 40), (0x10010, 15)],
        }
        for name, route in approaches.items():
            shared = directory / (name + ".sav")
            rows = run(name, route, save=shared)
            check(rows[-1]["a"]["s"] == rows[-1]["b"]["s"] == "0045" and
                  rows[-1]["a"]["x"] == rows[-1]["b"]["x"] == 1704,
                  name + ": both players did not grab the reported pole")
            for player in ("a", "b"):
                start = next(i for i, r in enumerate(rows) if r[player]["s"] == "0045")
                check(all(r[player]["s"] == "0045" for r in rows[start:]),
                      name + ": climber alternated between hanging and falling")
            stable = run(name + "-idle", [(0, 30)], shared)
            check(all(r[p]["s"] == "0045" and 572 <= r[p]["y"] <= 700
                      for r in stable for p in ("a", "b")),
                  name + ": native pole limits or stable hanging failed")
            midpoint(stable)
            for port, player in enumerate(("a", "b")):
                other = "b" if player == "a" else "a"
                up = run(f"{name}-up-{port}", [(0x10 << (12*port), 20)], shared)
                down = run(f"{name}-down-{port}", [(0x20 << (12*port), 20)], shared)
                check(up[-1][player]["y"] + 12 < down[-1][player]["y"] and
                      all(a[other] == b[other] for a, b in zip(up, down)),
                      "climb direction or independent height clamp failed")
                animated = run(f"{name}-animation-{port}",
                               [(0x20 << (12*port), 25), (0x10 << (12*port), 25), (0, 90)], shared)
                for start in (0, 25):
                    check(len({r["sprites"][port]["graphic"]
                               for r in animated[start:start+25]}) >= 3,
                          "pole climbing sprite froze while moving up/down")
                idle = animated[-8:]
                midpoint(idle)
                check(len({r["sprites"][port]["graphic"] for r in idle}) == 1 and
                      all(r["sprites"][port]["animation"] == (0x2F if port == 0 else 0xD2)
                          for r in idle), f"{name} port {port}: pole animation did not return to native idle pose: " +
                      str([(r["sprites"][port]["animation"], r["sprites"][port]["graphic"])
                           for r in idle]))
                jumping = directory / f"jump-{name}-{port}.sav"
                word = 0x81 << (12*port)
                early = run(f"{name}-jump-start-{port}", [(word, 3)], shared, jumping)
                check(early[-1][player]["s"] == "0006" and early[-1][other]["s"] == "0045",
                      "first press did not release exactly its own player")
                released = directory / f"released-{name}-{port}.sav"
                reload = run(f"{name}-jump-reload-{port}", [(word, 17)], jumping, released)
                direct = run(f"{name}-jump-direct-{port}", [(word, 20)], shared)
                check([(r["a"], r["b"]) for r in direct[-17:]] ==
                      [(r["a"], r["b"]) for r in reload], "pole jump latch was lost on reload")
                check(all(r[player]["s"] == "0006" and r[other]["s"] == "0045" for r in reload) and
                      reload[-1][player]["x"] > 1720,
                      "jumping player regrabbed or pulled the partner off")
                last = run(f"{name}-last-release-{port}", [(0x81 << (12*(1-port)), 10)], released)
                check(any(r[other]["s"] == "0006" for r in last),
                      "last climber could not leave with one jump press")
            print(name + ": stable shared hanging, independent climbing/limits, up/down animation and idle, midpoint camera, either/last release and jump reload pass")
    check(state.read_bytes() == original, "original save changed")


if __name__ == "__main__":
    main()
