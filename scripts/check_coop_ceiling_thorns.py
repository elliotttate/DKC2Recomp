#!/usr/bin/env python3
"""Input-only ceiling thorn victim ownership, survivor and reload acceptance."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_coaster import word
from check_coop_route import check


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ("runner","rom","state"):
        parser.add_argument("--"+name,required=True,type=Path)
    args=parser.parse_args()
    runner,rom,state=(p.resolve(strict=True) for p in (args.runner,args.rom,args.state))
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires supported USA v1.0 ROM")
    original=state.read_bytes()
    check(original[-5] == 0xC2 and original[-4] == 0,"requires both players alive")
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-ceiling-thorns-") as tmp:
        directory=Path(tmp)
        source=directory/"original.sav";source.write_bytes(original)

        def run(name,inputs,checkpoint=source,save=None):
            return run_route(runner,rom,directory,name,inputs,state=checkpoint,save=save,submode=None)

        for who in (0,1):
            victim,peer=("a","b") if who == 0 else ("b","a")
            slot=0xDE2 if who == 0 else 0xE40
            final=directory/f"one-lost-{who}.sav"
            rows=run(f"ceiling-{who}",[(1 << (who*12),30),(0,200)],save=final)
            hurt=next((i for i,r in enumerate(rows) if r[victim]["s"] == "0024"),None)
            check(hurt is not None,"ceiling collider did not receive native hurt")
            check(rows[hurt]["active"] == ("0de2" if who == 0 else "0e40"),
                  "wrong Kong owns accepted terrain damage")
            check(rows[hurt][victim]["y"] < 1450 and rows[hurt][peer]["y"] == 1529,
                  "damage was assigned to the player below the ceiling")
            check(sum(r[victim]["s"] == "0025" for r in rows)>20,"native death sequence did not run")
            check(not any(r[peer]["s"] in ("0024","0025","0013") for r in rows),
                  "ceiling hit killed the unhurt teammate")
            check(rows[-1][victim]["s"] == "0013" and rows[-1]["sprites"][who]["flags"] == 0 and
                  rows[-1][peer]["s"] == "0038","survivor did not continue in its own pose")
            data=final.read_bytes()
            check(data[-4] == 1 << who and word(data,0x8BE) == word(original,0x8BE),
                  "wrong lost mask or premature team life loss")

            middle=directory/f"mid-hurt-{who}.sav"
            run(f"save-hurt-{who}",[(1 << (who*12),30),(0,5)],save=middle)
            resumed=run(f"reload-hurt-{who}",[(0,195)],checkpoint=middle)
            keys=("a","b","active","camera_x","camera_y","sprites","kong_flags")
            check([[r[k] for k in keys] for r in rows[35:]] == [[r[k] for k in keys] for r in resumed],
                  "mid-hurt reload changed death or control handoff")
            controls=run(f"survivor-jump-{who}",[(1 << ((1-who)*12),5)],checkpoint=final)
            check(controls[-1][peer]["y"] < rows[-1][peer]["y"] and
                  all(r[victim]["s"] == "0013" for r in controls),"survivor lost its own jump controls")
            dead_select=run(f"dead-select-{who}",[(4 << (who*12),125)],checkpoint=final)
            check(all(r[victim]["s"] == "0013" for r in dead_select),"lost player revived through Select")
            last=directory/f"last-loss-{who}.sav"
            ending=run(f"last-ceiling-{who}",[(1 << ((1-who)*12),30),(0,400)],checkpoint=final,save=last)
            check(any(r[peer]["s"] == "0005" for r in ending) and
                  word(last.read_bytes(),0x8BE) == word(data,0x8BE)-1,
                  "last survivor's thorn hit did not take native final loss")
            print(f"P{who+1}: own ceiling damage, native death, survivor controls, exact reload and final loss pass")
    check(state.read_bytes() == original,"external checkpoint changed")


if __name__ == "__main__":
    main()
