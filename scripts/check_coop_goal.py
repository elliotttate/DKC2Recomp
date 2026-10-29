#!/usr/bin/env python3
"""Private input-only goal regression from the reported external Pirate Panic save."""
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
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "goal replay requires the supported USA v1.0 ROM")
    with tempfile.TemporaryDirectory(prefix="dkc2-coop-goal-") as tmp:
        directory = Path(tmp)
        def run(name, route, *, source=state, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        ready = run("inspect", [(0, 1)])[0]
        goals = [i for i, sprite in enumerate(ready["sprites"]) if sprite["id"] == 0x160]
        check(len(goals) == 1 and ready["sub"] == "06" and ready["active"] == "0de2",
              "expected the reported P1-leader goal snapshot")
        goal = goals[0]
        for who in range(2):
            name = f"p{who+1}"
            mover, other = ("a", "b") if who == 0 else ("b", "a")
            rows = run(name, [(0x81 << (who*12), 50 if who == 0 else 65), (0, 420)])
            hit = next((i for i, row in enumerate(rows) if row["sprites"][goal]["state"] == 2), None)
            check(hit is not None, name + ": native goal collision did not trigger")
            check(rows[hit]["active"] == ("0de2" if who == 0 else "0e40"),
                  name + ": winning Kong did not own the ending")
            check(any(row[mover]["s"] in ("0046", "0033") for row in rows[hit:]),
                  name + ": native ending animation did not run")
            check(any(row["sub"] != "06" for row in rows[hit:]),
                  name + ": level did not finish")
            if who == 1:
                check(all(row["a"] == ready["a"] for row in rows[:hit]),
                      "P2 goal approach moved P1 before contact")
            print(name + ": native goal contact, correct winner, ending and level exit passed")

        rows = run("grounded", [(0x80000, 65), (0, 180)])
        check(all(row["sprites"][goal]["state"] == 1 and row["active"] == "0de2" for row in rows),
              "walking beside goal bypassed native landing requirement")
        pending = directory / "approach.sav"
        run("save-approach", [(0x81000, 35)], save=pending)
        rows = run("reload-approach", [(0x80000, 30), (0, 420)], source=pending)
        check(any(row["sprites"][goal]["state"] == 2 and row["active"] == "0e40" for row in rows) and
              any(row["sub"] != "06" for row in rows), "goal approach did not survive save/load")
        print("Grounded rejection and P2 mid-approach save/load passed")
    check(state.read_bytes() == original, "external goal snapshot changed")


if __name__ == "__main__":
    main()
