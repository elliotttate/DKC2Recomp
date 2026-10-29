#!/usr/bin/env python3
"""Private input-only TEAM Select-hold rescue acceptance; no guest memory patches."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import TEAM_START, SEPARATE_PLAYERS, check


def distance(row):
    return abs(row["a"]["x"] - row["b"]["x"])


def check_rope_snapshot(runner, rom, state):
    """Inspect/replay an external P2-rope/P1-grounded save without editing it."""
    original = state.read_bytes()
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-rope-rescue-") as tmp:
        directory = Path(tmp)
        def run(name, route, *, source=state, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode="08")

        baseline = run("inspect", [(0, 295)])
        ready = baseline[0]
        check(ready["b"]["s"] in ("0035", "0036", "0037", "0038") and
              ready["a"]["s"] == "0000", "expected P2 on rope and P1 standing")
        check(abs(ready["b"]["y"] - ready["a"]["y"]) > 500,
              "expected the reported offscreen rope separation")
        rows = run("rescue", [(0x4000, 240), (0x40000, 12),
                              (0x80000, 12), (0x1000, 1), (0, 30)])
        check(all(row["b"]["s"] == ready["b"]["s"] and
                  abs(row["b"]["y"] - row["a"]["y"]) > 500
                  for row in rows[:180]), "rope rescue fired early")
        moved = rows[180]
        check(distance(moved) == 16 and moved["a"]["y"] == moved["b"]["y"] and
              moved["b"]["s"] == "0000", "rope rescue did not detach beside teammate")
        # Midpoint scrolling moves the native left screen edge while the
        # Kongs are far apart. Compare the untouched teammate to a neutral
        # replay, rather than treating native screen clamping as teleporting.
        check(all(row["a"] == baseline[i]["a"] and row["active"] == ready["active"]
                  for i, row in enumerate(rows)), "rope rescue disturbed teammate/leader ownership")
        check(all(row["b"]["s"] not in ("0035", "0036", "0037", "0038", "0013")
                  for row in rows[180:]), "rescued Kong reentered rope/wait state")
        check(rows[251]["b"]["x"] < rows[239]["b"]["x"] - 8,
              "rescued Kong cannot walk")
        check(min(row["b"]["y"] for row in rows[265:]) < moved["b"]["y"] - 20 and
              any(row["b"]["s"] == "0006" for row in rows[265:]),
              "rescued Kong cannot jump")
        restored = directory / "rescued.sav"
        run("save", [(0x4000, 240)], save=restored)
        loaded = run("load", [(0, 60)], source=restored)
        check(distance(loaded[-1]) == 16 and loaded[-1]["b"]["s"] == "0000",
              "rope rescue did not survive save/load")
    check(state.read_bytes() == original, "external save changed")
    print("Reported rope save: timing, detachment, neutral-baseline teammate, walk/jump and reload passed")


def check_water_snapshot(runner, rom, state):
    """Replay the reported P1-water/P2-land snapshot using real inputs."""
    original = state.read_bytes()
    print("water_state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-water-rescue-") as tmp:
        directory = Path(tmp)
        def run(name, route, *, source=state, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode="02")

        neutral = run("neutral", [(0, 240)])
        check(neutral[0]["a"]["s"] == "003d" and neutral[0]["b"]["s"] == "0000",
              "expected the reported P1 swimming/P2 standing save")
        for who in range(2):
            shift = who * 12
            mover, target = ("a", "b") if who == 0 else ("b", "a")
            after = [(0x40, 20), (1, 30), (0, 30)] if who == 0 else [(1, 60), (0x40, 30), (0x80, 30)]
            saved = directory / f"water-p{who + 1}.sav"
            rows = run(f"p{who + 1}", [(4 << shift, 240)] +
                       [(word << shift, frames) for word, frames in after], save=saved)
            check(all(rows[i][mover] == neutral[i][mover] for i in range(180)),
                  "water rescue fired before three seconds")
            moved = rows[180]
            check(distance(moved) == 16 and moved[mover]["y"] == moved[target]["y"] and
                  moved[mover]["s"] == ("0000" if who == 0 else "003d"),
                  "water crossing did not initialize destination movement")
            check(all(row[target] == neutral[min(i,239)][target] and
                      row["active"] == neutral[0]["active"] for i, row in enumerate(rows)),
                  "water rescue disturbed teammate or camera owner")
            if who == 0:
                check(rows[259]["a"]["x"] < rows[239]["a"]["x"] - 8 and
                      min(row["a"]["y"] for row in rows[260:]) < rows[239]["a"]["y"] - 20,
                      "water-to-land rescue did not restore walking/jumping")
            else:
                check(min(row["b"]["y"] for row in rows[240:300]) < rows[239]["b"]["y"] - 25,
                      "land-to-water rescue did not initialize swim physics")
                check(rows[329]["sprites"][1]["attr"] & 0x4000 and
                      not rows[359]["sprites"][1]["attr"] & 0x4000,
                      "P2 swimming sprite did not turn with movement")
                # The source save has P1 as the camera follower. Check the
                # shared turn callback for that independently controlled Kong.
                turns = run("follower-turn", [(0x40, 30), (0x80, 30)], source=saved)
                check(turns[29]["sprites"][0]["attr"] & 0x4000 and
                      not turns[59]["sprites"][0]["attr"] & 0x4000,
                      "independent follower swimming turn was skipped")
            loaded = run(f"reload-p{who + 1}", [(0, 60)], source=saved)
            check(all(row[mover]["s"] not in ("0013", "0024", "003b") for row in loaded),
                  "water rescue did not remain playable after save/load")
    check(state.read_bytes() == original, "external water save changed")
    print("Reported water save: both rescue directions, timing, walk/jump, swim physics, facing and reload passed")


def check_mount_snapshot(runner, rom, state):
    """Recover the reported live P2 waiting below P1's mounted Rambi."""
    original = state.read_bytes()
    print("mount_state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-mount-rescue-") as tmp:
        directory = Path(tmp)
        def run(name, route, *, source=state, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save)

        baseline = run("inspect", [(0, 304)])
        ready = baseline[0]
        check(ready["a"]["s"] == "0009" and ready["b"]["s"] == "0013" and
              ready["animal_type"] == 0x19C and ready["active"] == "0de2" and
              ready["sprites"][1]["flags"] & 0x10,
              "expected the reported live waiting P2 / P1 riding Rambi save")
        animal_index = (ready["animal_slot"] - 0x0DE2) // 0x5E
        saved = directory / "rescued.sav"
        rows = run("rescue", [(0x4000, 240), (0x40000, 18), (0x1000, 1), (0, 45)], save=saved)
        check(all(row["b"]["s"] == "0013" and row["b"]["y"] > row["a"]["y"] + 1000
                  for row in rows[:180]), "mounted partner rescue fired early")
        check(distance(rows[180]) == 16 and rows[180]["a"]["y"] == rows[180]["b"]["y"] and
              rows[180]["b"]["s"] == "0000", "waiting partner did not recover beside rider")
        check(all(row["a"] == ready["a"] and row["active"] == ready["active"] and
                  row["animal_slot"] == ready["animal_slot"] and row["animal_type"] == 0x19C
                  for row in rows), "rescue changed rider or mount ownership")
        for key in ("id", "x", "y", "state"):
            check(all(row["sprites"][animal_index][key] == baseline[i]["sprites"][animal_index][key]
                      for i, row in enumerate(rows)), "partner rescue changed Rambi's " + key)
        check(all(row["b"]["s"] != "0013" for row in rows[180:]), "partner returned to limbo")
        check(rows[257]["b"]["x"] < rows[239]["b"]["x"] - 15 and
              min(row["b"]["y"] for row in rows[259:]) < rows[257]["b"]["y"] - 20,
              "rescued partner cannot walk/jump independently")
        loaded = run("reload", [(0, 120)], source=saved)
        check(all(row["b"]["s"] == "0000" and row["a"] == ready["a"] and
                  row["animal_type"] == 0x19C for row in loaded), "reloaded partner/mount did not remain playable")
        both = run("both", [(0x4004, 240)])
        check(distance(both[180]) == 16 and both[180]["a"] == ready["a"],
              "both Select holds did not recover the follower beside Rambi")
        rider = run("rider-only", [(4, 240)])
        check(all(row["a"] == ready["a"] and row["animal_type"] == 0x19C for row in rider),
              "rider Select moved/dismounted the animal")
    check(state.read_bytes() == original, "external mounted save changed")
    print("Reported Rambi save: timed waiting recovery, independent walk/jump, unchanged rider/animal, both-hold and reload passed")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--state", type=Path,
                        help="also replay the reported external P2 rope snapshot")
    parser.add_argument("--water-state", type=Path,
                        help="also replay the reported external P1 swimming snapshot")
    parser.add_argument("--mount-state", type=Path,
                        help="also replay the reported live P2 waiting below Rambi snapshot")
    args = parser.parse_args()
    runner, rom = args.runner.resolve(strict=True), args.rom.resolve(strict=True)
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "rescue replay requires the supported headerless USA v1.0 ROM")
    if args.state:
        check_rope_snapshot(runner, rom, args.state.resolve(strict=True))
    if args.water_state:
        check_water_snapshot(runner, rom, args.water_state.resolve(strict=True))
    if args.mount_state:
        check_mount_snapshot(runner, rom, args.mount_state.resolve(strict=True))
    with tempfile.TemporaryDirectory(prefix="dkc2-coop-rescue-") as tmp:
        directory = Path(tmp)

        def run(name, route, *, state=None, save=None):
            return run_route(runner, rom, directory, name, route, state=state, save=save)

        separated = directory / "separated.sav"
        initial = run("join", TEAM_START + SEPARATE_PLAYERS + [(0, 60)], save=separated)[-1]
        check(distance(initial) > 60 and initial["a"]["s"] == initial["b"]["s"] == "0000",
              "rescue seed must contain two separated idle players")
        neutral = run("neutral-seed", [(0, 185)], state=separated)
        for who in range(2):
            name = f"p{who + 1}"
            mover, target = ("a", "b") if who == 0 else ("b", "a")
            select = 4 << (who * 12)
            rows = run(name + "-hold", [(select, 185)], state=separated)
            check(all(distance(row) > 60 for row in rows[:180]), name + ": rescued too early")
            first = next((i for i, row in enumerate(rows) if distance(row) <= 20), None)
            check(first is not None and 180 <= first <= 183, name + ": three-second rescue did not fire")
            check(abs(rows[first][mover]["x"] - rows[first][target]["x"]) == 16,
                  name + ": destination was not beside teammate")
            check(rows[first][mover]["y"] == rows[first][target]["y"],
                  name + ": destination height did not match teammate")
            check(rows[first][target] == neutral[first][target], name + ": rescue moved the teammate")
            check(all(row["active"] == initial["active"] for row in rows),
                  name + ": Select triggered the native tag action")
            # A brief release cancels an incomplete charge, including during
            # a guest lag frame. Keep this check on the known idle seed; the
            # one-shot latch and rearming after firing are covered by units.
            rows = run(name + "-release", [(select, 90), (0, 1), (select, 185)], state=separated)
            check(all(distance(row) > 60 for row in rows[:271]) and distance(rows[-1]) <= 20,
                  name + ": brief release did not reset the charge")
            pending = directory / f"{name}-pending.sav"
            run(name + "-pending", [(select, 90)], state=separated, save=pending)
            rows = run(name + "-reload", [(select, 185)], state=pending)
            check(all(distance(row) > 60 for row in rows[:180]) and distance(rows[-1]) <= 20,
                  name + ": loading did not reset pending hold")
            print(name + ": three-second timing, own-player destination, no tag, release and load resets passed")

        rows = run("both", [(0x4004, 185)], state=separated)
        check(distance(rows[-1]) <= 20 and rows[-1]["a"] == neutral[-1]["a"],
              "simultaneous holds must bring follower to camera leader once")
        rows = run("pause", [(4, 90), (12, 1), (4, 210), (12, 1), (4, 185)], state=separated)
        check(any(row["kong_flags"] & 0x40 for row in rows[95:295]), "pause route never paused")
        check(all(distance(row) > 60 for row in rows[:481]) and distance(rows[-1]) <= 20,
              "pause did not reset the Select hold")
        print("Both-hold priority and pause reset passed")


if __name__ == "__main__":
    main()
