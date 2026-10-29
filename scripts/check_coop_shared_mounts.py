#!/usr/bin/env python3
"""Private input-only Squawks sharing/handoff and Rattly pit cleanup checks."""
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
    parser.add_argument("--parrot-state", required=True, type=Path)
    parser.add_argument("--rattly-state", required=True, type=Path)
    parser.add_argument("--orphan-state", type=Path)
    args = parser.parse_args()
    runner, rom = (p.resolve(strict=True) for p in (args.runner, args.rom))
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    originals = {p.resolve(strict=True): p.read_bytes() for p in
                 (args.parrot_state, args.rattly_state, args.orphan_state) if p}
    with tempfile.TemporaryDirectory(prefix="dkc2-shared-mounts-") as tmp:
        directory = Path(tmp)
        copies = {}
        for i, (path, data) in enumerate(originals.items()):
            print(path.name + "_sha256=" + hashlib.sha256(data).hexdigest())
            copies[path] = directory / f"source-{i}.sav"
            copies[path].write_bytes(data)

        def run(name, route, source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        bird = copies[args.parrot_state.resolve()]
        shared = directory / "shared.sav"
        rows = run("shared", [(0x80080, 120)], bird, shared)
        check(all(r["a"]["s"] == "0022" and r["b"]["s"] == "0010" and
                  r["animal_type"] == 0x198 and
                  abs(r["a"]["x"] - r["b"]["x"]) <= 4 and
                  abs(r["a"]["y"] - r["b"]["y"]) <= 32 for r in rows),
              "both players did not remain attached under movement input")
        check(rows[-1]["b"]["x"] > rows[0]["b"]["x"] + 100,
              "parrot controller cannot steer with both players hanging")
        rows = run("shared-reload", [(0, 60)], shared)
        check(all(r["a"]["s"] == "0022" and r["b"]["s"] == "0010" for r in rows),
              "shared parrot did not persist across save/load")

        lost = directory / "bird-loss.sav"
        rows = run("bird-hit", [(0x1000, 1), (0, 6)] * 100, bird, lost)
        check(any(r["b"]["s"] == "0059" for r in rows) and
              rows[-1]["active"] == "0de2" and rows[-1]["a"]["s"] == "0010",
              "native bird hit did not hand flight to the survivor")
        controls = run("survivor-flight", [(0x81, 1), (0x80, 6)] * 40, lost)
        check(controls[-1]["a"]["x"] > controls[0]["a"]["x"] + 80 and
              controls[-1]["a"]["y"] < controls[0]["a"]["y"] - 200 and
              all(r["a"]["s"] == "0010" and r["animal_type"] == 0x198 for r in controls),
              "survivor cannot steer and flap after bird handoff/reload")
        wrong = run("lost-controller", [(0x81000, 1), (0x80000, 6)] * 40, lost)
        check(wrong[-1]["a"]["x"] == wrong[0]["a"]["x"] and
              all(r["b"]["s"] in ("0059", "0013") for r in wrong),
              "lost controller moved the parrot or revived its player")
        print("Shared Squawks, save/load, native hit and survivor flight controls passed")

        snake = copies[args.rattly_state.resolve()]
        fallen = directory / "rattly-loss.sav"
        baseline = run("rattly-before", [(0, 1)], snake)[0]
        rows = run("rattly-pit", [(0x40, 140), (0, 200)], snake, fallen)
        check(rows[-1]["active"] == "0e40" and rows[-1]["a"]["s"] == "0013" and
              rows[-1]["b"]["s"] == "0000" and rows[-1]["animal_type"] == 0 and
              rows[-1]["animal_slot"] == 0,
              "fallen Rattly rider left an inherited mount")
        check(rows[-1]["sprites"][1]["colors"] == baseline["sprites"][1]["colors"],
              "Rattly loss corrupted the survivor palette")
        # Mounted pit cleanup restores the animal's native unmounted idle.
        # It may remain alive until normal despawning; deletion is not required.
        check(all(s["state"] == 0 for s in rows[-1]["sprites"] if s["id"] == 0x194),
              "former Rattly mount did not return to native unmounted idle")
        controls = run("on-foot-survivor", [(0x81000, 20), (0, 80)], fallen)
        check(any(r["b"]["s"] == "0006" for r in controls) and
              all(r["animal_type"] == 0 for r in controls),
              "survivor cannot jump on foot after Rattly loss/reload")
        if args.orphan_state:
            rows = run("old-orphan-mount", [(0, 90)], copies[args.orphan_state.resolve()])
            check(rows[-1]["animal_type"] == rows[-1]["animal_slot"] == 0 and
                  rows[-1]["sprites"][1]["colors"] == baseline["sprites"][1]["colors"],
                  "older orphaned-mount snapshot did not recover")
        print("Rattly pit: native animal cleanup, survivor palette, on-foot controls and reload passed")
    check(all(p.read_bytes() == data for p, data in originals.items()),
          "external snapshot changed")


if __name__ == "__main__":
    main()
