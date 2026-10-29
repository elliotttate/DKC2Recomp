#!/usr/bin/env python3
"""Private shared stomp after barrel flight, using only copied saves and inputs."""
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
    with tempfile.TemporaryDirectory(prefix="dkc2-barrel-stomp-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for delay in (8, 62):
            for shooter in range(2):
                name = f"delay-{delay}-shooter-{shooter}"
                prefix = [(0, delay), (1 << (12*shooter), 1)]
                rows = run(name, prefix + [(0x80080, 70)])
                hit = next((i for i, row in enumerate(rows) if row["stomp_events"]), None)
                check(hit is not None and rows[hit]["stomp_events"] == 3 and
                      all(rows[hit][p]["s"] == "0016" and rows[hit-1][p]["s"] == "0012"
                          for p in ("a", "b")), "barrel descent did not bounce both Kongs")
                check(any(s["id"] == 0x220 for s in rows[hit]["sprites"]), "wrong enemy fixture")
                for p in ("a", "b"):
                    check(rows[hit+10][p]["y"] < rows[hit][p]["y"] - 30,
                          "partner did not rise after barrel stomp")
                check(abs(rows[hit+10]["a"]["y"] - rows[hit+10]["b"]["y"]) <= 1,
                      "paired bounce has unequal lift")
                for phase, count in (("before", hit - 3), ("after", hit + 3)):
                    saved = directory / (name + phase + ".sav")
                    steering = count - delay - 1
                    run(name + phase, prefix + [(0x80080, steering)], save=saved)
                    resumed = run(name + phase + "-reload", [(0x80080, 20)], saved)
                    keys = ("a", "b", "active", "stomp_events", "camera_target_x", "camera_target_y")
                    check([[r[k] for k in keys] for r in resumed] ==
                          [[r[k] for k in keys] for r in rows[count:count+20]],
                          "pre-impact or mid-bounce reload changed the result")
                    if phase == "after":
                        left = run(name + "-left", [(0x40, 10)], saved)
                        right = run(name + "-right", [(0x80, 10)], saved)
                        check(left[-1]["a"]["x"] + 5 < right[-1]["a"]["x"] and
                              all(a["b"] == b["b"] for a, b in zip(left, right)),
                              "bounce coupled horizontal controls")
                print(f"{name}: joint barrel stomp, equal lift, separate steering and both reload phases pass")
    check(state.read_bytes() == original, "external snapshot changed")


if __name__ == "__main__":
    main()
