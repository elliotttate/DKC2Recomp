#!/usr/bin/env python3
"""Private native barrel gravity/terminal-speed regression from an external save."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original, cartridge = state.read_bytes(), rom.read_bytes()
    check(hashlib.sha256(cartridge).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-barrel-gravity-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        def physics(snapshot, grounded=False):
            data = snapshot.read_bytes()
            check(data[:4] == b"SLTR" and struct.unpack_from("<I", data, 4)[0] == 7,
                  "requires native v7 snapshots")
            word = lambda a: struct.unpack_from("<H", data, 168255 + a)[0]
            for slot in (0xDE2, 0xE40):
                work = word(0x595 if slot == word(0x593) else 0x599)
                check(work in (0x16B2, 0x16D8), "invalid Kong work pointer")
                prop = word(slot + 0x54)
                for offset, field in ((0, 8), (2, 10)):
                    if grounded and field == 8:
                        continue  # Jump initialization owns normal movement gravity.
                    if field == 8 and word(slot + 0x2E) == 0x12:
                        check(word(slot + 0x48) == 0, "shot still owns a locked gravity phase")
                        offset = 0x4A  # Native unlocked barrel-shot gravity.
                    expected = struct.unpack_from("<H", cartridge, (0xFF0000 + prop + offset) & 0x3FFFFF)[0]
                    check(word(work + field) == expected,
                          "Kong retained follower gravity/terminal speed instead of native physics")

        repaired = directory / "grounded.sav"
        run("grounded-repair", [(0, 1)], save=repaired)
        physics(repaired, grounded=True)
        jump = run("grounded-jump", [(0x1001, 1), (0, 55)])
        check(jump[-1]["a"]["s"] == jump[-1]["b"]["s"] == "0000",
              "old grounded snapshot left a player floating after a normal jump")
        swapped = directory / "p2-leader.sav"
        rows = run("swap", [(0x100000, 1), (0, 50), (0x100000, 1), (0, 80)], save=swapped)
        check(rows[-1]["active"] == "0e40", "native reverse-leadership setup failed")
        for owner, checkpoint in ((0, source), (1, swapped)):
            prefix = [(0x40040, 25), (0, 30)]
            airborne = directory / f"flight-{owner}.sav"
            run(f"save-flight-{owner}", prefix, checkpoint, airborne)
            physics(airborne)
            direct = run(f"direct-{owner}", prefix + [(0, 30)], checkpoint)
            resumed = run(f"reload-{owner}", [(0, 30)], airborne)
            keys = ("a", "b", "active")
            check([[r[k] for k in keys] for r in direct[-30:]] ==
                  [[r[k] for k in keys] for r in resumed], "flight reload changed shot trajectory")
            paired = [r for r in direct if r["a"]["s"] == r["b"]["s"] == "0012"]
            check(len(paired) >= 35, "did not exercise a complete native barrel arc")
            peaks = [min(r[p]["y"] for r in paired) for p in ("a", "b")]
            check(abs(peaks[0] - peaks[1]) <= 1 and max(peaks) < 580,
                  "barrel shot gave unequal upward height")
            check(all(abs(r["a"]["y"] - r["b"]["y"]) <= 8 for r in paired),
                  "barrel players diverged beyond one native update during descent")
            check(all(paired[-1][p]["y"] > peaks[i] + 30 for i, p in enumerate(("a", "b"))),
                  "one player kept the follower's floating descent")
            print(f"owner port {owner+1}: native gravity/terminal speed, equal peak/descent and flight reload pass")
    check(state.read_bytes() == original, "original checkpoint changed")


if __name__ == "__main__":
    main()
