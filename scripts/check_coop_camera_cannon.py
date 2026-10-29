#!/usr/bin/env python3
"""Private input-only midpoint camera and shared transport-cannon checks."""
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
    parser.add_argument("--state", required=True, type=Path,
                        help="External reported transport-cannon snapshot")
    parser.add_argument("--camera-state", type=Path,
                        help="External reported water snapshot")
    parser.add_argument("--loop-state", type=Path,
                        help="Optional external pre-fix repeating-entry snapshot")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    originals = {p.resolve(strict=True): p.read_bytes() for p in
                 (state, args.camera_state, args.loop_state) if p}
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    for path, data in originals.items():
        print(path.name + "_sha256=" + hashlib.sha256(data).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-camera-cannon-") as tmp:
        directory = Path(tmp)

        def run(name, route, *, source=state, save=None, submode="08"):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=submode)

        # Either entrant recruits the living partner into native paired flight.
        away = directory / "away.sav"
        run("separate", [(0x80, 65), (0, 20)], save=away)
        for who, source, route in ((0, state, [(0x40, 40), (0, 350)]),
                                   (1, away, [(0x40000, 80), (0, 350)])):
            occupied = directory / f"flight-{who}.sav"
            rows = run(f"entry-{who}", route, source=source, save=occupied, submode=None)
            check(any(r["a"]["s"] in ("002f", "0031", "0032") and
                      r["b"]["s"] in ("0030", "0011") for r in rows),
                  "cannon did not recruit the living partner")
            check(any(r["a"]["s"] == "0022" and r["b"]["s"] == "0012" and
                      abs(r["a"]["y"] - r["b"]["y"]) < 32 for r in rows),
                  "both players did not travel in native paired flight")
            arrived = directory / f"arrived-{who}.sav"
            rows = run(f"confirm-{who}", [(0x1001, 1), (0, 240)], source=occupied,
                       save=arrived, submode=None)
            last = rows[-1]
            check(last["level_id"] == 0x79 and
                  last["a"]["s"] == last["b"]["s"] == "0000" and
                  last["a"]["y"] == last["b"]["y"] == 589,
                  "paired barrel flight did not deliver both players")
            for player, mover in enumerate(("a", "b")):
                controls = run(f"arrived-control-{who}-{player}",
                               [(0x81 << (player*12), 20)], source=arrived, submode=None)
                check(any(r[mover]["s"] == "0006" for r in controls),
                      "arriving player cannot jump after save/load")
        print("Either entrant: paired cannon flight, both bonus arrivals and controls passed")

        if args.loop_state:
            rows = run("old-loop", [(0x80, 180), (0, 180), (0x1001, 1), (0, 240)],
                       source=args.loop_state.resolve(), submode=None)
            check(rows[0]["a"]["s"] == "002f" and
                  all(r["a"]["s"] not in ("002f", "0031", "0030") for r in rows[80:]) and
                  rows[-1]["a"]["s"] == rows[-1]["b"]["s"] == "0000" and
                  rows[-1]["a"]["y"] == rows[-1]["b"]["y"] == 589,
                  "pre-fix follower loop did not release and arrive with partner")
            print("Pre-fix repeated-entry snapshot recovery passed")

        if args.camera_state:
            camera_state = args.camera_state.resolve()
            saved = directory / "camera.sav"
            rows = run("camera", [(0, 180)], source=camera_state, save=saved, submode="02")
            settled = rows[-1]
            for axis in ("x", "y"):
                check(settled["camera_target_" + axis] ==
                      (settled["a"][axis] + settled["b"][axis]) // 2,
                      axis + " camera target is not the two-player midpoint")
            rows = run("camera-reload", [(0, 5)], source=saved, submode="02")
            check(all(r["camera_target_x"] == settled["camera_target_x"] and
                      r["camera_target_y"] == settled["camera_target_y"] for r in rows),
                  "midpoint camera did not survive save/load")
            print("Separated swimming/landed midpoint on both axes and save/load passed")
    check(all(p.read_bytes() == data for p, data in originals.items()),
          "external snapshot changed")


if __name__ == "__main__":
    main()
