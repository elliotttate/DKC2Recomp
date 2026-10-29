#!/usr/bin/env python3
"""Private TEAM item-input isolation replay using an external supported ROM.

Only controller inputs change game state. Temporary traces and snapshots stay
outside Git; both fixed player slots must retain ownership across ground/air
carrying, partner run/pick and jump inputs, and save/load.
"""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import TEAM_START, SEPARATE_PLAYERS, check


def swap_ports(route):
    return [(((word & 0xFFF) << 12) | ((word >> 12) & 0xFFF), duration)
            for word, duration in route]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--kongs-pack", type=Path)
    args = parser.parse_args()
    runner, rom = args.runner.resolve(strict=True), args.rom.resolve(strict=True)
    pack = args.kongs_pack.resolve(strict=True) if args.kongs_pack else None
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "item input replay requires the supported headerless USA v1.0 ROM")
    with tempfile.TemporaryDirectory(prefix="dkc2-coop-item-input-") as tmp:
        directory = Path(tmp)
        for owner in range(2):
            name = f"p{owner + 1}"
            carrier, partner = ("a", "b") if owner == 0 else ("b", "a")
            owner_y = 2 << (owner * 12)
            partner_shift = (1 - owner) * 12
            ground, air = directory / f"{name}-ground.sav", directory / f"{name}-air.sav"

            def run(label, route, *, state=None, save=None):
                return run_route(runner, rom, directory, name + "-" + label,
                                 route, state=state, save=save,
                                 kongs_pack=pack)

            # The Kongs' jump/roll arcs differ: a mirrored fixed approach can
            # hit the enemy during roll recovery. P1 restarts its roll before
            # contact; P2 walks closer first. Keep Run held after the chest
            # pickup rather than accidentally testing an intentional throw.
            approach = (swap_ports(SEPARATE_PLAYERS) +
                        [(0x80082, 90), (0x80080, 1), (0x80082, 38)]
                        if owner == 0 else SEPARATE_PLAYERS +
                        [(0x80080, 90), (0x82080, 30)])
            data = run("pickup", TEAM_START + approach + [(owner_y, 30)], save=ground)
            held = data[-1]["held_slot"]
            check(held >= 0x0E9E and (held - 0x0DE2) % 0x5E == 0,
                  name + ": no held item")
            check(data[-1][carrier]["s"] == "000c" and data[-1][partner]["s"] == "0000",
                  name + ": pickup did not establish separate carrier/partner states")
            check(any(sprite["slot"] == (held - 0x0DE2) // 0x5E and
                      sprite["id"] == 0x01A8 for sprite in data[-1]["sprites"]),
                  name + ": held item is not the native chest")

            # Run/pick both while grounded and after jumping. The original
            # shared-item read made the empty-handed jumper enter carry/throw.
            for action, route in {
                "run-pick": [(owner_y, 3), (owner_y | (2 << partner_shift), 12),
                             (owner_y, 50)],
                "jump-run-pick": [(owner_y | (1 << partner_shift), 1),
                                  (owner_y, 15), (owner_y | (2 << partner_shift), 1),
                                  (owner_y, 70)],
            }.items():
                rows = run(action, route, state=ground)
                check(all(row["held_slot"] == held for row in rows),
                      name + ": partner " + action + " released the owner's item")
                check(all(row[partner]["s"] not in ("000c", "000d", "000e", "000f", "003f")
                          for row in rows), name + ": partner adopted a carry/throw state")

            airborne = run("owner-jump", [(owner_y | (1 << (owner * 12)), 1),
                                         (owner_y, 15)], state=ground, save=air)
            check(airborne[-1][carrier]["s"] in ("000d", "000e"),
                  name + ": owner did not enter airborne carrying")
            rows = run("air-reload", [(owner_y | (2 << partner_shift), 1),
                                      (owner_y, 80)], state=air)
            check(all(row["held_slot"] == held for row in rows),
                  name + ": airborne reload/partner input lost ownership")
            release = run("owner-release", [(owner_y, 2), (0, 80)], state=ground)
            check(any(row["held_slot"] == 0 for row in release),
                  name + ": owner can no longer release its item")
            print(name + ": partner run/pick and jump isolated; ground/air save/load and owner release passed")


if __name__ == "__main__":
    main()
