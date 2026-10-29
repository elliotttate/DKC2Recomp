#!/usr/bin/env python3
"""Private shared-kart input/reload checks; reversed seating is a controlled fixture."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_coaster import word
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
    cart = word(original, 0xDE2 + 0x42)
    check(word(original, cart) == 0x158 and word(original, cart + 0x2E) == 0x202,
          "requires grounded mode-02 shared kart")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-shared-kart-") as tmp:
        directory = Path(tmp)
        for who in range(2):
            # Source is used untouched for P1. Reverse native seat ownership and
            # seat states only for the controlled P2-first-boarded fixture.
            data = bytearray(original)
            leader, peer = (0xDE2, 0xE40) if who == 0 else (0xE40, 0xDE2)
            if who:
                for address, value in ((cart+0x42, leader), (cart+0x44, peer),
                                       (leader+0x2E, 0x5B), (peer+0x2E, 0x5C)):
                    struct.pack_into("<H", data, 168255 + address, value)
            source = directory / f"seating-{who}.sav"; source.write_bytes(data)
            def run(name, route, checkpoint=source, save=None):
                return run_route(runner, rom, directory, f"{who}-{name}", route,
                                 state=checkpoint, save=save, submode=None)
            idle = run("idle", [(0, 75)])
            passenger = run("passenger", [(0, 2), (1 << ((1-who)*12), 1), (0, 72)])
            keys = ("a", "b", "sprites", "active")
            check([[r[k] for k in keys] for r in idle] ==
                  [[r[k] for k in keys] for r in passenger], "passenger moved vehicle or left seat")
            prefix = [(0, 2), (1 << (who*12), 1), (0, 12)]
            mid = directory / f"mid-{who}.sav"
            jump = run("jump", prefix + [(0, 60)])
            run("save", prefix, save=mid)
            resumed = run("reload", [(0, 60)], mid)
            check([[r[k] for k in keys] for r in jump[-60:]] ==
                  [[r[k] for k in keys] for r in resumed], "mid-jump reload diverged")
            vehicle = lambda r: next(s for s in r["sprites"] if s["slot"] == (cart-0xDE2)//0x5E)
            check(min(vehicle(r)["y"] for r in jump) < vehicle(idle[0])["y"] - 35,
                  "leader did not jump whole kart")
            check(all(r["a"]["s"] in ("005b", "005c") and
                      r["b"]["s"] in ("005b", "005c") for r in jump), "rider jumped out")
            check(all(abs(r[p]["y"]-vehicle(r)["y"]) <= 8 for r in jump for p in ("a", "b")),
                  "rider separated from jumping kart")
            check(vehicle(jump[-1])["state"] == 0x202, "kart did not land")
            print(f"leader P{who+1}: native vehicle jump, seated passenger, passenger input blocked, exact reload pass")
    check(state.read_bytes() == original, "source checkpoint changed")


if __name__ == "__main__":
    main()
