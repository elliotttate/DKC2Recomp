#!/usr/bin/env python3
"""Private Select rescue from/to a sticky wall using copied external checkpoints."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def word(data, address):
    return struct.unpack_from("<H", data, 168255 + address)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--hold-frames", required=True, type=int,
                        help="compiled Select hold ticks from test_dkc2_coop output")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    ticks = args.hold_frames
    check(ticks > 1, "expected a Select hold longer than one tick")
    with tempfile.TemporaryDirectory(prefix="dkc2-honey-teleport-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        ready = run("inspect", [(0, 1)])[0]
        waller = next((i for i, p in enumerate(("a", "b")) if ready[p]["s"] == "0041"), None)
        check(waller is not None and ready[("b", "a")[waller]]["s"] in ("0000", "0043", "007a"),
              "expected one attached player and one grounded player")
        slot = (0xDE2, 0xE40)[waller]
        away = 0x80 if word(original, slot + 0x12) & 0x4000 else 0x40
        # Build the reverse-role fixture entirely through player inputs.
        shared = directory / "shared.sav"
        run("join-wall", [(4 << (12 * (1-waller)), ticks + 30)], save=shared)
        reverse = directory / "reverse.sav"
        run("reverse-roles", [((away | 1) << (12*waller), 25), (0, 120)], shared, reverse)
        for fixture, checkpoint in (("original", source), ("reversed", reverse)):
            neutral = run(fixture + "-neutral", [(0, ticks + 30)], checkpoint)
            check(sorted(neutral[0][p]["s"] for p in ("a", "b")) in
                  (["0000", "0041"], ["0041", "0043"], ["0041", "007a"]),
                  "reverse fixture did not settle on the floor")
            for who, mover in enumerate(("a", "b")):
                target = ("b", "a")[who]
                output = directory / f"{fixture}-{who}.sav"
                rows = run(f"{fixture}-{who}", [(4 << (12*who), ticks + 30)], checkpoint, output)
                check(all(rows[i][mover] == neutral[i][mover] for i in range(ticks-1)),
                      "honey teleport fired before configured hold")
                moved = rows[ticks-1]
                check(abs(moved[mover]["y"] - moved[target]["y"]) <= 1,
                      "honey teleport did not reach teammate height")
                attached = neutral[0][target]["s"] == "0041"
                check(abs(moved[mover]["x"] - moved[target]["x"]) == (0 if attached else 16),
                      "incorrect destination anchor/offset")
                check(all(r[target] == neutral[i][target] for i, r in enumerate(rows)),
                      "teleport disturbed destination player")
                if attached:
                    check(all(r[mover]["s"] == "0041" for r in rows[ticks-1:]),
                          "rescued player fell off shared wall")
                jump_word = (away | 1) << (12*who)
                jumped = run(f"{fixture}-{who}-jump", [(jump_word, 25)], output)
                check(jumped[-1][mover]["y"] < rows[-1][mover]["y"] - 20 and
                      any(r[mover]["s"] in ("0006", "0042", "0044") for r in jumped),
                      "rescued player cannot jump independently after reload")
                check(all(r[target] == rows[-1][target] for r in jumped),
                      "jump detached/moved teammate")
    check(state.read_bytes() == original, "external save changed")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Both players: wall-to-floor and floor-to-wall timing, safe attachment, own jump and reload pass; save unchanged")


if __name__ == "__main__":
    main()
