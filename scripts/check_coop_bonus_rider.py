#!/usr/bin/env python3
"""Private mounted bonus return, independent controls and reload regression."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-bonus-rider-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        landed = directory / "landed.sav"
        rows = run("timeout-return", [(0, 1100)], save=landed)
        check(rows[0]["level_id"] == 0x8B and rows[0]["animal_type"] == 0x190,
              "checkpoint is not the reported mounted Rambi bonus")
        check(any(r["level_id"] == 0x29 and r["a"]["s"] == "0012" for r in rows),
              "return did not exercise native barrel flight")
        check(all(r["level_id"] == 0x29 and r["animal_type"] == 0x190 and
                  r["active"] == "0de2" and r["a"]["s"] == "0009" and
                  r["b"]["s"] == "0000" and r["a"]["x"] == r["b"]["x"] == 5192 and
                  r["a"]["y"] == r["b"]["y"] == 645 for r in rows[-100:]),
              "bonus return stranded the rider, lost its animal or moved without input")
        last = rows[-1]
        check(any(s["id"] == 0x190 and s["slot"] == (last["animal_slot"] - 0xDE2) // 0x5E
                  for s in last["sprites"]), "mount selector does not identify the restored animal")
        entrance = directory / "return-flight.sav"
        run("save-return", [(0, 900)], save=entrance)
        resumed = run("reload-return", [(0, 200)], entrance)
        keys = ("a", "b", "active", "animal_slot", "animal_type", "level_id")
        check([[r[k] for k in keys] for r in rows[-200:]] ==
              [[r[k] for k in keys] for r in resumed], "return reload changed arrival or mount ownership")
        idle = run("landed-reload", [(0, 100)], landed)
        check(all(r["a"] == last["a"] and r["b"] == last["b"] and
                  r["animal_type"] == 0x190 and
                  all(r["sprites"][p]["colors"] == last["sprites"][p]["colors"] for p in (0, 1))
                  for r in idle), "landed reload changed rider, partner or palettes")
        for port, player in enumerate(("a", "b")):
            other = "b" if player == "a" else "a"
            move = run(f"move-{port}", [(0x80 << (12*port), 10)], landed)
            check(move[-1][player]["x"] > last[player]["x"] + 4 and
                  all(r[other] == last[other] and r["animal_type"] == 0x190 for r in move),
                  "rider or on-foot partner cannot move independently")
            jump = run(f"jump-{port}", [(1 << (12*port), 20)], landed)
            check(min(r[player]["y"] for r in jump) < 620 and
                  all(r["animal_type"] == 0x190 for r in jump),
                  "rider or partner cannot jump after bonus return")
        print("Mounted timeout return: native flight, stable rider/partner, own movement/jumps, palettes and flight/landed reload pass")
    check(state.read_bytes() == original, "original checkpoint changed")


if __name__ == "__main__":
    main()
