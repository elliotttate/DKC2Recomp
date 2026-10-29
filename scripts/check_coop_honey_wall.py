#!/usr/bin/env python3
"""Private sticky-wall contact ownership, wall jumps and snapshot reload."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def check_camera(rows):
    for row in rows:
        for axis in ("x", "y"):
            # Trace coordinates omit the subpixels used by the native camera.
            check(abs(row["camera_target_" + axis] -
                      (row["a"][axis] + row["b"][axis]) // 2) <= 1,
                  "sticky-wall camera did not track the midpoint on " + axis)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--camera-state", type=Path,
                        help="Optional copied checkpoint with Kongs on opposite sticky walls")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    camera_original = args.camera_state.read_bytes() if args.camera_state else None
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-honey-wall-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for port, player in enumerate(("a", "b")):
            other = "b" if port == 0 else "a"
            attached = directory / f"attached-{port}.sav"
            rows = run(f"attach-{port}", [(0x81 << (12*port), 60), (0, 30)], save=attached)
            last = rows[-1]
            check_camera(rows)
            check(last[player]["s"] == "0041" and last[player]["x"] == 1839 and
                  all(r[other] == rows[0][other] for r in rows), "wall contact attached the wrong player")
            idle = run(f"hang-reload-{port}", [(0, 30)], attached)
            check_camera(idle)
            check(all(r[player] == last[player] for r in idle), "wall hang did not persist across reload")
            word = 0x41 << (12*port)
            jump = run(f"wall-jump-{port}", [(word, 25)], attached)
            check_camera(jump)
            check(any(r[player]["s"] == "0042" for r in jump) and
                  jump[-1][player]["x"] < last[player]["x"] - 20 and
                  jump[-1][player]["y"] < last[player]["y"] - 20 and
                  all(r[other] == last[other] for r in jump), "wall jump controls are coupled")
            middle = directory / f"jump-{port}.sav"
            run(f"mid-jump-{port}", [(word, 10)], attached, middle)
            resumed = run(f"reload-jump-{port}", [(word, 15)], middle)
            check_camera(resumed)
            check([(r["a"], r["b"]) for r in jump[10:]] ==
                  [(r["a"], r["b"]) for r in resumed], "wall jump reload changed trajectory")
            print(f"P{port+1}: own wall contact, stable hang, independent jump and reload pass")

        both = directory / "both.sav"
        rows = run("shared-wall", [(0x81081, 60), (0, 30)], save=both)
        check_camera(rows)
        check(all(rows[-1][p]["s"] == "0041" for p in ("a", "b")), "both players cannot share sticky wall")
        for port, player in enumerate(("a", "b")):
            other = "b" if port == 0 else "a"
            jump = run(f"shared-wall-jump-{port}", [(0x41 << (12*port), 25)], both)
            check_camera(jump)
            check(any(r[player]["s"] == "0042" for r in jump) and
                  all(r[other] == rows[-1][other] for r in jump), "one wall jump detached the teammate")
        print("Both on the sticky wall: either player jumps away while teammate stays attached")
        if camera_original is not None:
            camera_source = directory / "camera-original.sav"
            camera_source.write_bytes(camera_original)
            print("camera_state_sha256=" + hashlib.sha256(camera_original).hexdigest())
            idle = run("opposite-walls-idle", [(0, 30)], camera_source)
            check_camera(idle)
            check(all(r[p]["s"] == "0041" for r in idle for p in ("a", "b")),
                  "camera fixture is not two hanging Kongs")
            for name, word in (("p1", 0x81), ("p2", 0x41000), ("both", 0x41081)):
                rows = run(name + "-camera", [(word, 25), (0, 25)], camera_source)
                check_camera(rows)
                check(any(r[p]["s"] == "0042" for r in rows for p in ("a", "b")),
                      "camera check did not exercise a wall jump")
                middle = directory / (name + "-camera.sav")
                run(name + "-camera-save", [(word, 15)], camera_source, middle)
                resumed = run(name + "-camera-reload", [(word, 10), (0, 25)], middle)
                check_camera(resumed)
                keys = ("a", "b", "camera_target_x", "camera_target_y")
                check([[r[k] for k in keys] for r in rows[15:]] ==
                      [[r[k] for k in keys] for r in resumed],
                      "wall-camera reload changed players or midpoint")
            print("Opposite walls: idle, either/both wall jumps and reload retain midpoint on both axes")
    check(state.read_bytes() == original, "external snapshot changed")
    if camera_original is not None:
        check(args.camera_state.read_bytes() == camera_original, "external camera snapshot changed")


if __name__ == "__main__":
    main()
