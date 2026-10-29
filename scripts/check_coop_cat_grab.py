#!/usr/bin/env python3
"""Private Cat O' Nine Tails capture/reload checks; original saves stay intact."""
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
        parser.add_argument("--"+name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    check(len(original) == 299458 and original[:4] == b"SLTR", "requires v7 checkpoint")
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-cat-grab-") as tmp:
        directory = Path(tmp)
        source = directory/"original.sav"; source.write_bytes(original)
        def run(name, route, checkpoint=source, save=None):
            rows = run_route(runner, rom, directory, name, route,
                             state=checkpoint, save=save, submode=None)
            check(all(r["mode"] == 1 for r in rows), "left gameplay")
            return rows
        route = [(0x8000,1),(0,155)]  # P2 Start unpauses the supplied checkpoint.
        direct = run("native-capture-release", route)
        check(any(r["b"]["s"] == "0048" for r in direct), "P2 did not contact cat")
        check(any(r["b"]["s"] == "0012" for r in direct), "native release did not launch P2")
        check(all(r["a"] == {"x":2675,"y":589,"s":"0000"} for r in direct),
              "distant P1 was recruited, hidden, moved or launched")
        check(all(abs(r["camera_x"]+128-(r["a"]["x"]+r["b"]["x"])/2) < 20
                  for r in direct[20:100]), "camera abandoned distant teammate during spin")
        mid = directory/"captured.sav"
        run("save", [(0x8000,1),(0,30)], save=mid)
        resumed = run("reload", [(0,125)], mid)
        keys = ("a","b","active","camera_x","camera_y","sprites")
        check([[r[k] for k in keys] for r in direct[31:]] ==
              [[r[k] for k in keys] for r in resumed], "spin/release reload diverged")
        controls = run("free-player-controls", [(1,20)], mid)
        check(any(r["a"]["s"] == "0006" for r in controls) and
              controls[-1]["a"]["y"] < 549, "free teammate cannot jump")
        check(all(r["b"]["s"] == "0048" for r in controls), "free input released the wrong player")
        # Controlled owner-selector copy: positions/physics/enemy remain intact;
        # P2 now collides while inactive, so accepted contact must choose P2.
        data = bytearray(original)
        for address, value in ((0x593,0xDE2),(0x597,0xE40),(0x595,0x16B2),
                               (0x599,0x16D8),(0x8A2,1),(0x8A4,0)):
            struct.pack_into("<H",data,168255+address,value)
        inactive = directory/"inactive-collider.sav"; inactive.write_bytes(data)
        rows = run("inactive-collider", [(8,1),(0,80)], inactive)
        check(rows[-1]["b"]["s"] == "0048" and rows[-1]["active"] == "0e40",
              "inactive actual collider did not own grab")
        check(all(r["a"] == {"x":2675,"y":589,"s":"0000"} for r in rows),
              "inactive grab stole the non-contacting active player")
    check(state.read_bytes() == original, "source save changed")
    print("Only contacting P2 captured/launched; distant P1 stays free, midpoint camera, controls and exact reload pass")
    print("Controlled inactive-collider owner-selector copy also passes")


if __name__ == "__main__":
    main()
