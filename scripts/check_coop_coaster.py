#!/usr/bin/env python3
"""Private coaster input acceptance, plus a controlled zero-spare-lives check."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def word(data, address):
    check(data[:4] == b"SLTR" and struct.unpack_from("<I", data, 4)[0] == 7,
          "requires a v7 checkpoint")
    return struct.unpack_from("<H", data, 168255 + address)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-coaster-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, checkpoint=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=checkpoint, save=save, submode=None)

        for who in range(2):
            player, partner = ("a", "b") if who == 0 else ("b", "a")
            prefix = [(0, 100), (1 << (who * 12), 15)]
            airborne = directory / f"jump-{who}.sav"
            rows = run(f"jump-{who}", prefix, save=airborne)
            check(all(r[player]["s"] == "0067" for r in rows[100:]), "own port did not jump")
            check(all(r[partner]["s"] in ("005b", "005c") for r in rows[100:]),
                  "jump displaced the other rider")
            check(rows[-1][player]["y"] < rows[-1][partner]["y"] - 60, "jump had no height")
            direct = run(f"direct-{who}", prefix + [(0, 80)])[-80:]
            resumed = run(f"resumed-{who}", [(0, 80)], airborne)
            keys = ("a", "b", "active", "camera_x", "camera_y", "sprites")
            check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in resumed],
                  "mid-jump reload changed the native trajectory")
            check(all(direct[-1][p]["s"] in ("005b", "005c") for p in ("a", "b")),
                  "jumper did not land back in its seat")
        for name, route in (("together", [(0, 100), (0x1001, 15), (0, 80)]),
                            ("staggered", [(0, 100), (1, 12), (0x1000, 15), (0, 80)])):
            rows = run(name, route)
            check(any(r["a"]["s"] == r["b"]["s"] == "0067" for r in rows),
                  "both riders cannot be airborne independently")
            check(all(rows[-1][p]["s"] in ("005b", "005c") for p in ("a", "b")),
                  "paired jumps did not land")
        print("Either port, simultaneous/staggered jumps, separate seats and exact midair reload pass")

        # No memory edits: standing into the first enemy loses P2; jumping both
        # through that section instead produces an accepted airborne P1 hit.
        cases = ((1, [(0, 430)]),
                 (0, [(0, 340), (0x1001, 25), (0, 90)]))
        for fallen, route in cases:
            survivor = 1 - fallen
            dead, live = ("a", "b") if fallen == 0 else ("b", "a")
            checkpoint = directory / f"hurt-{fallen}.sav"
            rows = run(f"hurt-{fallen}", route, save=checkpoint)
            check(any(r[dead]["s"] == "0024" for r in rows), "native hurt animation did not play")
            check(rows[-1][dead]["s"] == "0013" and rows[-1]["sprites"][fallen]["flags"] == 0,
                  "lost rider was not parked")
            check(rows[-1][live]["s"] == "005b" and
                  rows[-1]["active"] == ("0de2" if survivor == 0 else "0e40"),
                  "survivor lost its cart or control ownership")
            data = checkpoint.read_bytes()
            check(data[-4] == 1 << fallen, "damage lost the wrong player")
            check(word(data, 0x8BE) == word(original, 0x8BE), "one rider's loss spent a team life")
            check(word(data, 0xA36) == 0, "TEAM handoff froze the cart")
            rows = run(f"survivor-jump-{fallen}", [(1 << (survivor * 12), 15), (0, 80)], checkpoint)
            check(any(r[live]["s"] == "0067" for r in rows), "survivor cannot jump on its own port")
            check(rows[-1][live]["x"] > word(data, (0xDE2 if survivor == 0 else 0xE40) + 6) + 150,
                  "cart stopped after damage")
            check(all(r[dead]["s"] == "0013" for r in rows), "input revived the dead rider")
            rows = run(f"dead-select-{fallen}", [(4 << (fallen * 12), 125)], checkpoint)
            check(all(r[dead]["s"] == "0013" for r in rows), "Select revived the lost rider")
            final = directory / f"final-{fallen}.sav"
            rows = run(f"last-loss-{fallen}", [(0, 800)], checkpoint, final)
            check(any(r[live]["s"] in ("003b", "0034") for r in rows) and
                  any(r["sub"] == "00" for r in rows), "last rider did not trigger native level loss")
            check(word(final.read_bytes(), 0x8BE) == word(original, 0x8BE) - 1,
                  "last loss did not spend exactly one team life")
        print("Either rider hurt: native animation, live cart/control handoff, dead Select and last-loss life accounting pass")

        # A jump avoids the first enemy, leaving both alive at the later cart
        # collision. This exercises native interaction $21, not ordinary hurt.
        rows = run("cart-collision", [(0, 340), (0x1000, 25), (0, 400)])
        check(any(r["b"]["s"] == "006b" and r["a"]["s"] == "005b" for r in rows),
              "cart collision did not preserve the second seated rider")
        check(rows[-1]["a"]["x"] > 2850 and rows[-1]["b"]["s"] == "0013",
              "cart collision froze or revived a rider")
        print("Native cart collision preserves the survivor and continues forward")
        # Controlled exhaustion fixture: only the spare-life counter changes.
        # All movement, hits, handoffs and the Game Over transition remain native.
        exhausted = bytearray(original)
        struct.pack_into("<H", exhausted, 168255 + 0x8BE, 0)
        exhausted_source = directory / "zero-spare-lives.sav"
        exhausted_source.write_bytes(exhausted)
        one_alive = directory / "zero-spare-survivor.sav"
        rows = run("zero-spare-hurt", [(0, 430)], exhausted_source, one_alive)
        check(rows[-1]["a"]["s"] == "005b" and word(one_alive.read_bytes(), 0x8BE) == 0,
              "one lost rider caused premature Game Over")
        game_over = directory / "game-over.sav"
        run("zero-spare-final", [(0, 900)], one_alive, game_over)
        data = game_over.read_bytes()
        check(word(data, 0x8BE) == 0xFFFF and word(data, 0x24) == 0xFA80,
              "exhausted last rider did not enter native Game Over")
        print("Controlled zero-spare-lives copy: survivor continues, final loss enters native Game Over")
    check(state.read_bytes() == original, "external checkpoint changed")


if __name__ == "__main__":
    main()
