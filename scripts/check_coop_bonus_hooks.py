#!/usr/bin/env python3
"""Private input-only bonus arrival and shared fixed-hook acceptance."""
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
    parser.add_argument("--bonus-state", type=Path)
    parser.add_argument("--hook-state", type=Path)
    parser.add_argument("--team-barrel-state", type=Path,
                        help="Gangplank snapshot before carrying/throwing into the elevated barrel")
    parser.add_argument("--waiting-state", type=Path,
                        help="Optional older snapshot stranded at the bonus entrance")
    args = parser.parse_args()
    runner, rom = args.runner.resolve(strict=True), args.rom.resolve(strict=True)
    check(args.bonus_state or args.hook_state or args.team_barrel_state, "provide an external bonus, hook or team barrel snapshot")
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    for field in ("bonus_state", "hook_state", "waiting_state", "team_barrel_state"):
        value = getattr(args, field)
        if value:
            setattr(args, field, value.resolve(strict=True))
    originals = {p: p.read_bytes() for p in
                 (args.bonus_state, args.hook_state, args.waiting_state, args.team_barrel_state) if p}
    for path, data in originals.items():
        print(path.name + "_sha256=" + hashlib.sha256(data).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-bonus-hooks-") as tmp:
        directory = Path(tmp)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        if args.team_barrel_state:
            for who in range(2):
                saved = directory / f"team-barrel-{who}.sav"
                rows = run(f"team-barrel-{who}",
                           [(0x80000, 15), (0, 12), (0x100 << (who*12), 1),
                            (0, 40), (0x12 << (who*12), 1),
                            (0x10 << (who*12), 45), (0, 160)], args.team_barrel_state, saved)
                check(any(r["a"]["s"] == "0018" or r["b"]["s"] == "0018" for r in rows),
                      "team carry did not start")
                last = rows[-1]
                check(last["a"]["s"] == last["b"]["s"] == "0000" and
                      last["a"]["y"] == last["b"]["y"] == 401,
                      "team throw into elevated barrel stranded a player")
                for player in range(2):
                    mover = "a" if player == 0 else "b"
                    control = run(f"team-barrel-controls-{who}-{player}",
                                  [(0x81 << (player*12), 20)], saved)
                    check(any(r[mover]["s"] == "0006" for r in control) and
                          control[-1][mover]["y"] < last[mover]["y"] - 40,
                          "player cannot jump independently after team barrel entry/reload")
            print("Either carrier: team throw, elevated barrel entry, both arrivals and controls passed")

        if args.bonus_state:
            for who in range(2):
                saved = directory / f"bonus-p{who+1}.sav"
                # Reach the teleporter, confirm the native bonus introduction,
                # then leave both controllers neutral throughout arrival.
                rows = run(f"bonus-p{who+1}",
                           [(0x40 << (who*12), 100), (0, 350),
                            (0x1001, 1), (0, 240)], args.bonus_state, saved)
                last = rows[-1]
                check(last["level_id"] == 0x79 and last["bonus_mode"] == 1,
                      "reported teleporter did not reach its native bonus room")
                check(last["a"]["s"] == last["b"]["s"] == "0000" and
                      last["a"]["y"] == last["b"]["y"] and
                      abs(last["a"]["x"] - last["b"]["x"]) <= 16,
                      "both players did not arrive on the platform without Select")
                check(all(last["sprites"][p]["flags"] & 0x10 for p in range(2)),
                      "bonus partner is not live/visible")
                rows = run(f"bonus-reload-{who}", [(0, 45)], saved)
                check(all(r["a"]["s"] == r["b"]["s"] == "0000" for r in rows),
                      "bonus partner disappeared after release/save/load")
                partner = 1 - who
                rows = run(f"bonus-controls-{who}", [(0x81 << (partner*12), 20)], saved)
                mover = "a" if partner == 0 else "b"
                check(any(r[mover]["s"] == "0006" for r in rows) and
                      rows[-1][mover]["y"] < last[mover]["y"] - 20,
                      "arriving partner cannot jump independently")
            print("Either teleporter entrant: both neutral arrivals, independent control and reload passed")
        if args.waiting_state:
            rows = run("old-bonus-wait", [(0, 60)], args.waiting_state)
            check(rows[-1]["a"]["s"] == rows[-1]["b"]["s"] == "0000" and
                  all(r["b"]["s"] != "0013" for r in rows),
                  "older stranded bonus snapshot did not recover without Select")
            print("Older bonus entrance snapshot recovers automatically")

        if args.hook_state:
            ready = run("hook-inspect", [(0, 1)], args.hook_state)[0]
            check(any(s["id"] == 0x14C for s in ready["sprites"]),
                  "expected the reported fixed-hook snapshot")
            for who in range(2):
                mover, other = ("a", "b") if who == 0 else ("b", "a")
                first = directory / f"first-p{who+1}.sav"
                rows = run(f"grab-p{who+1}", [(0x81 << (who*12), 60), (0, 60)],
                           args.hook_state, first)
                check(rows[-1][mover]["s"] == "003c" and
                      all(r[other]["s"] not in ("0071", "0030", "003c") for r in rows),
                      "hook grab recruited or attached the wrong player")
                rows = run(f"free-partner-{who}", [(0x41 << ((1-who)*12), 20)], first)
                check(all(r[mover]["s"] == "003c" for r in rows) and
                      any(r[other]["s"] == "0006" for r in rows),
                      "partner cannot move/jump while the other hangs")
                shared = directory / f"shared-p{who+1}.sav"
                approach = [(0x80000, 13), (0, 5)] if who == 0 else []
                rows = run(f"shared-{who}", approach + [(0x81 << ((1-who)*12), 60), (0, 40)], first, shared)
                check(rows[-1]["a"] == rows[-1]["b"] and rows[-1]["a"]["s"] == "003c",
                      "both players cannot hang on the same prop")
                rows = run(f"shared-reload-{who}", [(0, 60)], shared)
                check(all(r["a"] == r["b"] and r["a"]["s"] == "003c" for r in rows),
                      "shared hook did not persist across save/load")
                for jumper in range(2):
                    moving, hanging = ("a", "b") if jumper == 0 else ("b", "a")
                    word = 0x41 if jumper == 0 else 0x81
                    rows = run(f"release-{who}-{jumper}",
                               [(word << (jumper*12), 25), (0, 30)], shared)
                    check(all(r[hanging]["s"] == "003c" for r in rows) and
                          any(r[moving]["s"] == "0006" for r in rows) and
                          rows[-1][moving]["s"] != "003c",
                          "one player's jump detached the other or could not release")
                    # Leave enough time for the first native cooldown to expire,
                    # then release the remaining actor with one press only.
                    departed = directory / f"departed-{who}-{jumper}.sav"
                    run(f"first-departure-{who}-{jumper}",
                        [(word << (jumper*12), 1),
                         ((word & ~1) << (jumper*12), 54)], shared, departed)
                    second = 1 - jumper
                    second_word = 0x41 if second == 0 else 0x81
                    rows = run(f"last-departure-{who}-{jumper}",
                               [(second_word << (second*12), 1),
                                ((second_word & ~1) << (second*12), 44), (0, 30)], departed)
                    check(all(r[hanging]["s"] not in ("0030", "003c") for r in rows[:25]) and
                          rows[0][hanging]["s"] == "0006" and
                          rows[15][hanging]["y"] < rows[0][hanging]["y"] - 40,
                          f"remaining occupant re-grabbed hook: grab={who} first={jumper} start={rows[0][hanging]} end={rows[-1][hanging]} states={[r[hanging]['s'] for r in rows[:10]]}")
            print("Either first grab, free partner, shared hook, independent releases and reload passed")
    check(all(path.read_bytes() == data for path, data in originals.items()),
          "external snapshot changed")


if __name__ == "__main__":
    main()
