#!/usr/bin/env python3
"""Private reported-pole stale-anchor, first grab and repeat-grab acceptance."""
import argparse
import hashlib
from pathlib import Path
import struct
import tempfile

from check_coop_combat import run_route
from check_coop_kleever_hooks import invincible_copy
from check_coop_route import check


def word(data, address):
    return struct.unpack_from("<H", data, 168255 + address)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    parser.add_argument("--invincible-repeat", action="store_true",
                        help="protect only repeat-route temporary copies from the nearby wasp")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    with tempfile.TemporaryDirectory(prefix="dkc2-pole-regrab-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for who, player in enumerate(("a", "b")):
            other = ("b", "a")[who]
            slot = (0xDE2, 0xE40)[who]
            check(word(original, slot + 0x4C) == 0xEFC, "expected reported stale pole reference")
            approach = [(0x82 << (12*who), 10), (0x93 << (12*who), 40), (0x10 << (12*who), 35)]
            first = directory / f"first-{who}.sav"
            rows = run(f"grab-{who}", approach, save=first)
            last = rows[-1]
            check(last[player]["s"] == "0045" and last[player]["x"] == 3024,
                  "stale reference blocked reported pole")
            check(all(r[other] == rows[0][other] for r in rows), "grab moved the wrong player")
            idle = run(f"reload-{who}", [(0, 30)], first)
            check(all(r[player]["s"] == "0045" for r in idle), "attachment did not survive reload")
            up = run(f"up-{who}", [(0x10 << (12*who), 20)], first)
            down = run(f"down-{who}", [(0x20 << (12*who), 20)], first)
            check(up[-1][player]["y"] + 12 < down[-1][player]["y"], "own climb controls failed")
            left = directory / f"left-{who}.sav"
            released = run(f"leave-{who}", [(0x43 << (12*who), 35), (0, 80)], first, left)
            start = next((i for i, r in enumerate(released[:6]) if r[player]["s"] == "0006"), None)
            check(start is not None and all(r[player]["s"] != "0045" for r in released[start:start+10]),
                  "pole jump immediately regrabbed")
            check(word(left.read_bytes(), slot + 0x4C) == 0, "departure did not expire regrab latch")
            if args.invincible_repeat:
                protected = directory / f"repeat-protected-{who}.sav"
                invincible_copy(left, protected)
                left = protected
            repeated = run(f"regrab-{who}", approach, left)
            check(repeated[-1][player]["s"] == "0045" and repeated[-1][player]["x"] == 3024,
                  "second approach could not regrab pole")
    check(state.read_bytes() == original, "external source changed")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Either player: stale-anchor grab, own climb, safe release, latch expiry and regrab pass; save unchanged")
    if args.invincible_repeat:
        print("Repeat only: copied native invincibility excludes wasp interference; first grab/release used clean gameplay")


if __name__ == "__main__":
    main()
