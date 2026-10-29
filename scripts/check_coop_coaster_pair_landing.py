#!/usr/bin/env python3
"""Input-only paired coaster landing acceptance from an external checkpoint."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_coaster import word
from check_coop_route import check


def route(a, b, end=150):
    points = sorted({0, a, b, a+30, b+30, end})
    return [(sum(((0x80 if start <= t else 0) | (1 if start <= t < start+30 else 0)) << (who*12)
                 for who, start in enumerate((a, b))), stop-t)
            for t, stop in zip(points, points[1:]) if t < end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--"+name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    original = state.read_bytes()
    check(original[-5] == 0xC2 and original[-4] == 0, "requires both riders alive")
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-pair-landing-") as tmp:
        directory = Path(tmp)
        source = directory/"original.sav"; source.write_bytes(original)

        def run(name, inputs, checkpoint=source, save=None):
            return run_route(runner, rom, directory, name, inputs,
                             state=checkpoint, save=save, submode=None)

        for a, b in ((55,55), (57,57), (69,69), (75,75), (85,85), (95,95), (65,55), (65,60), (65,70), (80,85), (90,85)):
            final = directory/f"landing-{a}-{b}.sav"
            rows = run(f"landing-{a}-{b}", route(a,b), save=final)
            data = final.read_bytes()
            carts = [word(data, slot+0x42) for slot in (0xDE2,0xE40)]
            check(carts[0] == carts[1] and carts[0] != word(original, 0xDE2+0x42),
                  f"{a}/{b}: paired jump did not board the same next cart")
            check(all(rows[-1][p]["s"] in ("005b","005c") and rows[-1][p]["x"] > 9900
                      for p in ("a","b")), "one rider missed the next cart")
            check(data[-4] == 0 and word(data,0x8BE) == word(original,0x8BE),
                  "paired landing lost a rider or team life")
            for who, p in enumerate(("a","b")):
                jump = run(f"own-jump-{a}-{b}-{who}", [(1 << (who*12),1)], checkpoint=final)
                peer = "b" if who == 0 else "a"
                check(jump[-1][p]["s"] == "0067" and jump[-1][peer]["s"] == rows[-1][peer]["s"],
                      "landing removed independent jump controls")

        # A sufficiently early jumper is already well below the next seat
        # when the teammate lands: the bounded catch must not revive it.
        missed = directory/"missed.sav"
        rows = run("missed", route(55,60), save=missed)
        data = missed.read_bytes()
        check(rows[-1]["a"]["s"] == "0013" and rows[-1]["b"]["s"] == "005b" and
              data[-4] == 1 and word(data,0x8BE) == word(original,0x8BE),
              "distant pit faller was revived or ended the survivor's run")

        for timing, split in ((55,108),(69,108)):
            airborne = directory/f"mid-landing-{timing}.sav"
            prefix = route(timing,timing,split)
            run(f"save-{timing}",prefix,save=airborne)
            direct = run(f"direct-{timing}",route(timing,timing))[-(150-split):]
            resumed = run(f"reload-{timing}",[(0x80080,150-split)],checkpoint=airborne)
            keys = ("a","b","active","camera_x","camera_y","sprites","kong_flags")
            check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in resumed],
                  "mid-landing reload changed either rider")
    check(state.read_bytes() == original,"external checkpoint changed")
    print("Both riders land across simultaneous/staggered timings, retain own jumps and exact reload")


if __name__ == "__main__":
    main()
