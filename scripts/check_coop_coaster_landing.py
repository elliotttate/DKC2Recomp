#!/usr/bin/env python3
"""Private native cart landing with one rider already lost; input-only replay."""
import argparse
import hashlib
from pathlib import Path
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
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    original = state.read_bytes()
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    check(original[-5] == 0xC2 and original[-4] in (1, 2), "requires one lost rider")
    who = 1 if original[-4] == 1 else 0
    live, dead = ("a", "b") if who == 0 else ("b", "a")
    jump, forward = 0x81 << (who * 12), 0x80 << (who * 12)
    initial_cart = word(original, (0xDE2 if who == 0 else 0xE40) + 0x42)
    with tempfile.TemporaryDirectory(prefix="dkc2-coaster-landing-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"; source.write_bytes(original)

        def run(name, route, checkpoint=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=checkpoint, save=save, submode=None)

        for delay in (35, 40, 45, 50):
            final = directory / f"landed-{delay}.sav"
            rows = run(f"landing-{delay}", [(0, delay), (jump, 30), (forward, 100)], save=final)
            data = final.read_bytes()
            check(any(r[live]["s"] == "0067" for r in rows), "jump did not launch")
            check(rows[-1][live]["s"] == "005b" and rows[-1][live]["x"] > 6200,
                  "native accepted contact left the survivor falling past the cart")
            check(word(data, (0xDE2 if who == 0 else 0xE40) + 0x42) != initial_cart,
                  "survivor did not change carts")
            check(all(r[dead]["s"] == "0013" and r["sprites"][1-who]["flags"] == 0 for r in rows),
                  "lost teammate revived or retained a seat")
            check(data[-4] == original[-4] and word(data, 0x8BE) == word(original, 0x8BE),
                  "landing changed the lost rider or spent a team life")

        airborne = directory / "airborne.sav"
        prefix = [(0, 40), (jump, 20)]
        run("save-airborne", prefix, save=airborne)
        direct = run("direct", prefix + [(forward, 100)])[-100:]
        resumed = run("reload", [(forward, 100)], checkpoint=airborne)
        keys = ("a", "b", "active", "camera_x", "camera_y", "sprites", "kong_flags")
        check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in resumed],
              "midair reload changed the landing")
    check(state.read_bytes() == original, "external checkpoint changed")
    print("Survivor lands on next cart across four jump timings; dead teammate, lives and exact reload preserved")


if __name__ == "__main__":
    main()
