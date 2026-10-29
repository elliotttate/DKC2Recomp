#!/usr/bin/env python3
"""Private input-only coaster stomp and gap survival from two external saves."""
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
    for name in ("runner", "rom", "state", "gap-state"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state, gap = (p.resolve(strict=True) for p in
                               (args.runner, args.rom, args.state, args.gap_state))
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    originals = [p.read_bytes() for p in (state, gap)]
    for name, data in zip(("stomp", "gap"), originals):
        print(name + "_sha256=" + hashlib.sha256(data).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-coaster-stomp-") as tmp:
        directory = Path(tmp)
        source, gap_source = directory / "stomp.sav", directory / "gap.sav"
        source.write_bytes(originals[0]); gap_source.write_bytes(originals[1])

        def run(name, route, checkpoint, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=checkpoint, save=save, submode=None)

        for who in (0, 1, 2):
            key = (1 << (who * 12)) if who < 2 else 0x1001
            rows = run(f"stomp-{who}", [(0, 55), (key, 25), (0, 100)], source)
            expected = (1 << who) if who < 2 else 3
            hit = next((i for i, r in enumerate(rows) if r["stomp_events"] == expected), None)
            check(hit is not None, "native stomp did not belong to the expected rider(s)")
            for player in ((who,) if who < 2 else (0, 1)):
                name = "a" if player == 0 else "b"
                check(rows[hit][name]["s"] == "0068", "coaster stomp did not enter native bounce")
                check(min(r[name]["y"] for r in rows[hit:hit+40]) < rows[hit][name]["y"] - 45,
                      "stomp defeated the enemy without an upward bounce")
            if who < 2:
                partner = "b" if who == 0 else "a"
                check(all(r[partner]["s"] in ("005b", "005c") for r in rows),
                      "solo stomp moved the seated partner")
            check(all(rows[-1][p]["s"] in ("005b", "005c") for p in ("a", "b")),
                  "stomp did not return to a seat")
        print("Either rider and shared stomp: native upward bounce, own seats and landing pass")

        lives = word(originals[1], 0x8BE)
        for survivor in (0, 1):
            live, dead = ("a", "b") if survivor == 0 else ("b", "a")
            jump, forward = 0x81 << (survivor * 12), 0x80 << (survivor * 12)
            prefix = [(0, 200), (jump, 30)]
            airborne = directory / f"gap-air-{survivor}.sav"
            run(f"gap-save-{survivor}", prefix, gap_source, airborne)
            direct = run(f"gap-full-{survivor}", prefix + [(forward, 140)], gap_source)
            resumed = run(f"gap-reload-{survivor}", [(forward, 140)], airborne)
            keys = ("a", "b", "active", "camera_x", "camera_y", "sprites", "kong_flags")
            check([[r[k] for k in keys] for r in direct[-140:]] ==
                  [[r[k] for k in keys] for r in resumed], "mid-gap reload changed the survivor")
            data = airborne.read_bytes()
            check(data[-4] == 1 << (1 - survivor), "gap lost the wrong player")
            check(word(data, 0x8BE) == lives, "one falling rider spent a team life")
            check(direct[-1][live]["s"] == "005b" and direct[-1][live]["x"] > 3750,
                  "survivor did not reach and continue on the new cart")
            check(all(r[dead]["s"] == "0013" and r["sprites"][1-survivor]["flags"] == 0
                      for r in resumed), "lost rider retained a live seat or revived")
            check(direct[-1]["active"] == ("0de2" if survivor == 0 else "0e40"),
                  "survivor lost its control ownership")
            rows = run(f"dead-select-{survivor}", [(4 << ((1-survivor)*12), 125)], airborne)
            check(all(r[dead]["s"] == "0013" for r in rows), "dead Select revived the faller")
        print("Either gap survivor: old-cart release, lost teammate, new-cart landing, no life loss and exact reload pass")

        final = directory / "both-fall.sav"
        rows = run("both-fall", [(0, 520)], gap_source, final)
        check(any(r["a"]["s"] == "003b" or r["b"]["s"] == "003b" for r in rows),
              "both missing the gap did not trigger native level loss")
        check(word(final.read_bytes(), 0x8BE) == lives - 1, "both losses spent the wrong number of lives")
        # Separate controlled life-exhaustion check, with no geometry/physics edits.
        exhausted = bytearray(originals[1])
        struct.pack_into("<H", exhausted, 168255 + 0x8BE, 0)
        zero = directory / "zero-spare-lives.sav"; zero.write_bytes(exhausted)
        game_over = directory / "game-over.sav"
        run("game-over", [(0, 700)], zero, game_over)
        check(word(game_over.read_bytes(), 0x8BE) == 0xFFFF and
              word(game_over.read_bytes(), 0x24) == 0xFA80, "final exhausted loss did not reach Game Over")
        print("Both fallers: one native team-life loss; controlled zero-spare copy reaches Game Over")
    check(all(p.read_bytes() == data for p, data in zip((state, gap), originals)),
          "an external checkpoint changed")


if __name__ == "__main__":
    main()
