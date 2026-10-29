#!/usr/bin/env python3
"""Private animal-only hiding, native damage turn, health and reload checks."""
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
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633", "unsupported ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-animal-turns-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route, state=source, save=save, submode=None)

        def word(snapshot, address):
            data = snapshot.read_bytes()
            check(data[:4] == b"SLTR" and struct.unpack_from("<I", data, 4)[0] == 7,
                  "requires native v7 snapshot")
            return struct.unpack_from("<H", data, 168255 + address)[0]

        def hidden(rows):
            check(all(r["animal_type"] == 0x194 and r["animal_slot"] == 0 and
                      r["active"] == "0e40" and r["sprites"][0]["flags"] == 0
                      for r in rows), "animal-only play spawned a Kong or changed the animal actor")

        entry = directory / "entry.sav"
        run("transform", [(0x1000, 80), (0, 140)], save=entry)
        section = directory / "section.sav"
        rows = run("enter-section", [(0x80000, 400), (0, 60)], entry, section)
        hidden(rows)
        check(rows[-1]["level_id"] == 5 and word(section, 0x08A2) == 2 and
              word(section, 0x08C2) & 0x4000, "native animal section setup failed")
        neutral = run("neutral", [(0, 30)], section)
        for button in (1, 2, 4, 0x10, 0x20, 0x40, 0x80, 0x100, 0x200):
            inputs = run(f"inactive-{button:x}", [(button, 30)], section)
            hidden(inputs)
            check([r["b"] for r in inputs] == [r["b"] for r in neutral],
                  "inactive controller moved the animal or spawned its Kong")
        damaged = directory / "damaged.sav"
        rows = run("hit", [(0x80000, 180), (0, 40)], section, damaged)
        hidden(rows)
        check(any(r["b"]["s"] == "0016" for r in rows) and
              word(damaged, 0x08A2) == 1 and not (word(damaged, 0x08C2) & 0x4000),
              "native surviving hit did not consume health and hand control to P1")
        base = run("post-hit-neutral", [(0, 30)], damaged)
        wrong = run("old-controller", [(0x80000, 30)], damaged)
        right = run("new-controller", [(0x80, 30)], damaged)
        hidden(wrong + right)
        check([r["b"] for r in wrong] == [r["b"] for r in base] and
              right[-1]["b"]["x"] > base[-1]["b"]["x"] + 10,
              "old controller retained movement or new controller cannot steer")
        prefix = directory / "before-hit.sav"
        run("save-before-hit", [(0x80000, 100)], section, prefix)
        after_reload = directory / "after-reload.sav"
        resumed = run("reload-hit", [(0x80000, 80), (0, 40)], prefix, after_reload)
        check([(r["a"], r["b"]) for r in resumed] == [(r["a"], r["b"]) for r in rows[-120:]] and
              word(after_reload, 0x08A2) == 1, "hit reload changed physics or switched twice")
        healed = directory / "healed.sav"
        healing = run("native-dk-health", [(0x40, 120), (1, 70), (0, 50)], damaged, healed)
        hidden(healing)
        check(word(healed, 0x08C2) & 0x4000 and word(healed, 0x08A2) == 1,
              "DK barrel did not restore native health without spawning a Kong or changing turns")
        final = run("last-life", [(0x80, 500), (0, 350)], damaged)
        check(any(r["b"]["s"] == "003b" for r in final) and any(r["sub"] == "00" for r in final),
              "last health loss no longer retains native level loss")
        print("Animal-only Rattly: hidden inactive port, real-hit turn/reload, sole controller, native DK health and final loss pass")
    check(state.read_bytes() == original, "original checkpoint changed")


if __name__ == "__main__":
    main()
