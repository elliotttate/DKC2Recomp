#!/usr/bin/env python3
"""Private Kudgel ground-shake eligibility, native stun recovery and reload."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-ground-shake-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        before = directory / "before-impact.sav"
        approach = [(0, 212), (0x80080, 65), (0, 53), (0x40040, 50)]
        setup = run("dodge-boss", approach, save=before)
        check(all(setup[-1][p]["s"] == "0000" for p in ("a", "b")), "boss dodge setup failed")
        for name, word, affected in (("both-grounded", 0, (True, True)),
                                     ("p1-jumps", 1, (False, True)),
                                     ("p2-jumps", 0x1000, (True, False)),
                                     ("both-jump", 0x1001, (False, False))):
            route = [(word, 20), (0, 140)]
            rows = run(name, route, before)
            for player, stunned in zip(("a", "b"), affected):
                check(any(r[player]["s"] == "0063" for r in rows) == stunned,
                      name + ": wrong ground-shake target")
                check(not any(r[player]["s"] in ("0024", "0025", "0005") for r in rows),
                      name + ": direct boss damage invalidates quake check")
                check(rows[-1][player]["s"] == "0000", name + ": stun did not recover")
            if name == "both-grounded":
                durations = [sum(r[p]["s"] == "0063" for r in rows) for p in ("a", "b")]
                check(min(durations) >= 100 and abs(durations[0] - durations[1]) <= 1,
                      "paired stun shortened the native timer or left one player behind")
                middle = directory / "mid-stun.sav"
                run("mid-stun", [(0, 25)], before, middle)
                resumed = run("mid-stun-reload", [(0, 135)], middle)
                check([(r["a"], r["b"], r["active"]) for r in rows[25:]] ==
                      [(r["a"], r["b"], r["active"]) for r in resumed],
                      "mid-stun reload changed recovery")
                recovered = directory / "recovered.sav"
                run("recover-save", route, before, recovered)
                for port, p in enumerate(("a", "b")):
                    controls = run("recovered-jump-" + p, [(1 << (12*port), 10)], recovered)
                    check(any(r[p]["s"] == "0006" for r in controls), "recovered player cannot jump")
            print(name + ": native eligibility and recovery pass")
    check(state.read_bytes() == original, "external snapshot changed")


if __name__ == "__main__":
    main()
