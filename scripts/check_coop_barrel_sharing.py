#!/usr/bin/env python3
"""Private input-only manual-barrel entry, either-port firing and landing checks."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--air-state", type=Path,
                        help="External reported occupied barrel with delayed partner air control")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    air_original = args.air_state.read_bytes() if args.air_state else None
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-shared-barrel-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for entrant in range(2):
            inside = directory / f"inside-{entrant}.sav"
            approach = [(0x40, 15), (0, 5)] if entrant == 0 else []
            rows = run(f"entry-{entrant}", approach +
                       [(0x41 << (entrant*12), 50), (0, 100)], source, inside)
            last = rows[-1]
            check(last["a"]["s"] == "0031" and last["b"]["s"] == "0011" and
                  last["a"]["x"] == last["b"]["x"] == 472 and
                  last["a"]["y"] == last["b"]["y"] == 6336,
                  "touching barrel did not bring both players inside")
            check(any(s["id"] == 0x140 for s in last["sprites"]),
                  "expected the reported manual barrel")
            for shooter in range(2):
                landed = directory / f"landed-{entrant}-{shooter}.sav"
                # This rotating barrel aims at the upper-left platform at
                # different times for the two approaches. Preserve its timing.
                delay = [(0, 60)] if entrant == 1 else []
                rows = run(f"shoot-{entrant}-{shooter}", delay +
                           [(1 << (shooter*12), 1), (0, 90)], inside, landed)
                check(any(r["a"]["s"] == r["b"]["s"] == "0012" and
                          abs(r["a"]["y"] - r["b"]["y"]) < 32 for r in rows),
                      "either controller did not launch both players")
                last = rows[-1]
                check(last["a"]["s"] == last["b"]["s"] == "0000" and
                      last["a"]["y"] == last["b"]["y"] == 6257,
                      "paired launch did not land both players on the upper platform")
                for player, mover in enumerate(("a", "b")):
                    other = "b" if player == 0 else "a"
                    controls = run(f"controls-{entrant}-{shooter}-{player}",
                                   [(0x81 << (player*12), 20), (0, 20)], landed)
                    check(any(r[mover]["s"] == "0006" for r in controls) and
                          controls[-1][mover]["y"] < last[mover]["y"] - 20 and
                          all(r[other]["s"] == "0000" and
                              r[other]["x"] == last[other]["x"] for r in controls),
                          "players did not regain separate controls after landing/reload")
        if args.air_state:
            air = directory / "air-original.sav"
            air.write_bytes(air_original)
            print("air_state_sha256=" + hashlib.sha256(air_original).hexdigest())
            routes = {"neutral": 0, "p1-left": 0x40, "p1-right": 0x80,
                      "p2-left": 0x40000, "p2-right": 0x80000,
                      "opposing": 0x40080}
            data = {name: run("air-" + name, [(1, 1), (word, 45)], air,
                              directory / ("air-" + name + ".sav"))
                    for name, word in routes.items()}
            baseline = data["neutral"]
            for name, rows in data.items():
                check(all(r["a"] == baseline[i]["a"] and r["b"] == baseline[i]["b"]
                          for i, r in enumerate(rows[:25])),
                      name + ": steering bypassed the native launch lock")
                check(all(r["a"]["y"] > 5200 and r["a"]["y"] < 5400 and
                          r["b"]["s"] == "0012" for r in rows[25:36]),
                      name + ": control check was not in flight")
            check(data["p1-left"][35]["a"]["x"] + 15 < data["p1-right"][35]["a"]["x"] and
                  data["p1-left"][26]["a"]["s"] == data["p1-right"][26]["a"]["s"] == "0012",
                  "partner cannot steer in both directions as soon as native lock ends")
            check(data["p2-left"][35]["b"]["x"] + 30 < data["p2-right"][35]["b"]["x"],
                  "original occupant lost left/right air control")
            check(all(data[name][i]["b"] == baseline[i]["b"]
                      for name in ("p1-left", "p1-right") for i in range(25, 36)),
                  "partner steering changed the other player's flight")
            check(data["opposing"][35]["a"]["x"] > data["opposing"][35]["b"]["x"] + 20,
                  "opposing controllers cannot separate during flight")
            detached = directory / "detached.sav"
            run("air-detach", [(1, 1), (0x80, 30)], air, detached)
            left = run("air-reload-left", [(0x40, 15)], detached)
            right = run("air-reload-right", [(0x80, 15)], detached)
            check(left[-1]["a"]["x"] + 30 < right[-1]["a"]["x"] and
                  all(l["b"] == r["b"] for l, r in zip(left, right)),
                  "detached partner lost independent air control after reload")
            print("Native launch lock, both air directions, opposing controls and in-flight reload passed")
    check(state.read_bytes() == original, "external snapshot changed")
    if args.air_state:
        check(args.air_state.read_bytes() == air_original, "external air snapshot changed")
    print("Both entrants x both shooters: shared entry, paired flight, landing and independent controls passed")


if __name__ == "__main__":
    main()
