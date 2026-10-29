#!/usr/bin/env python3
"""Private mounted-Rattly hanger, offscreen survival and Select rescue acceptance."""
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
    parser.add_argument("--waiting-state", type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    originals = {state: state.read_bytes()}
    if args.waiting_state:
        args.waiting_state = args.waiting_state.resolve(strict=True)
        originals[args.waiting_state] = args.waiting_state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires supported USA v1.0 ROM")
    print("state_sha256=" + hashlib.sha256(originals[state]).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-snake-hooks-") as tmp:
        directory = Path(tmp)

        def run(name, route, source=state, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        def mounted(rows):
            check(all(r["active"] == "0de2" and r["animal_type"] == 0x194 and
                      r["animal_slot"] == 0x0E9E for r in rows),
                  "follower action changed Rattly rider ownership")

        hanging = directory / "hanging.sav"
        rows = run("grab", [(0x81000, 22), (0x1000, 45), (0, 80)], save=hanging)
        mounted(rows)
        check(rows[-1]["b"] == {"x": 880, "y": 3495, "s": "003c"},
              "on-foot partner cannot finish native hanger attachment")
        rows = run("hang-reload", [(0, 60)], hanging)
        mounted(rows)
        check(all(r["b"]["s"] == "003c" for r in rows), "hanger attachment lost on reload")
        rows = run("ride-while-hanging", [(0x80, 20)], hanging)
        mounted(rows)
        check(all(r["b"]["s"] == "003c" for r in rows) and
              rows[-1]["sprites"][2]["x"] > rows[0]["sprites"][2]["x"] + 8,
              "rider cannot move independently of hanging partner")
        rows = run("release", [(0x81000, 1), (0x80000, 24), (0, 30)], hanging)
        mounted(rows)
        check(all(r["b"]["s"] == "0006" for r in rows[:15]) and
              rows[14]["b"]["y"] < rows[0]["b"]["y"] - 30,
              "hanging partner cannot jump off on first press")

        upper = directory / "upper.sav"
        rows = run("superjump", [(0x100, 100), (0, 180)], save=upper)
        mounted(rows)
        check(min(r["camera_y"] for r in rows) < rows[0]["b"]["y"] - 0x120,
              "replay did not scroll partner below native screen cutoff")
        check(all(r["b"]["s"] == "0000" and r["sprites"][1]["flags"] & 0x10 for r in rows),
              "Rattly superjump parked or killed grounded partner")
        rows = run("offscreen-jump", [(0x1000, 1), (0, 70)], upper)
        check(any(r["b"]["s"] == "0006" for r in rows) and
              all(r["b"]["s"] not in ("0013", "003b") for r in rows),
              "offscreen partner loses independent movement")
        rescued = directory / "rescued.sav"
        rows = run("rescue", [(0x4000, 181), (0, 60)], upper, rescued)
        mounted(rows)
        check(all(r["b"]["x"] == 840 for r in rows[:180]), "Select rescue fired before three seconds")
        check(abs(rows[180]["a"]["x"] - rows[180]["b"]["x"]) == 16 and
              abs(rows[180]["a"]["y"] - rows[180]["b"]["y"]) <= 16,
              "Select rescue cannot reach mounted Rattly")
        rows = run("rescued-controls", [(0x40000, 20), (0, 5), (0x1000, 1), (0, 35)], rescued)
        check(any(r["b"]["s"] == "0006" for r in rows[25:]) and
              all(r["sprites"][1]["flags"] & 0x10 for r in rows),
              "rescued partner cannot walk/jump after reload")
        if args.waiting_state:
            rows = run("old-wait-rescue", [(0x4000, 181), (0, 60)], args.waiting_state)
            mounted(rows)
            check(abs(rows[180]["a"]["x"] - rows[180]["b"]["x"]) == 16,
                  "older offscreen waiting snapshot cannot use Select rescue")
    check(all(p.read_bytes() == data for p, data in originals.items()), "external snapshot changed")
    print("Mounted hanger attachment/release, rider controls, superjump survival, rescue and reload passed")


if __name__ == "__main__":
    main()
