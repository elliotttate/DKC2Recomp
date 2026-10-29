#!/usr/bin/env python3
"""Private input-only coaster scripted exit through normal goal/map return."""
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
    who=0 if word(original,0x593) == 0xDE2 else 1
    player,dead=("a","b") if who == 0 else ("b","a")
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-coaster-exit-") as tmp:
        directory=Path(tmp)
        source=directory/"original.sav";source.write_bytes(original)

        def run(name,inputs,checkpoint=source,save=None):
            return run_route(runner,rom,directory,name,inputs,state=checkpoint,save=save,submode=None)

        for name,approach in (("neutral",[(0,370)]),
                              ("jump",[(1 << (who*12),30),(0,340)])):
            ready=directory/f"goal-area-{name}.sav"
            rows=run(name,approach,save=ready)
            exit_rows=[r for r in rows if r[player]["s"] == "0070"]
            check(len(exit_rows)>10,"native scripted exit did not start")
            check(exit_rows[-1][player]["x"] > exit_rows[0][player]["x"]+150,
                  "scripted rider froze while the cart continued")
            check(any(r[player]["s"] == "0066" for r in rows),"native end fade did not run")
            data=ready.read_bytes()
            check(word(data,0xD3) == 0x1E and rows[-1][player]["s"] in ("0000","0001"),
                  "coaster did not deliver the rider to the goal area")
            check(data[-4] == original[-4] and word(data,0x8BE) == word(original,0x8BE),
                  "scripted exit changed lost/life ownership")
            check(all(r[dead]["s"] == "0013" for r in rows[:150]),"exit revived the lost teammate")

            final=directory/f"finished-{name}.sav"
            ending=run(f"finish-{name}",[(0x83 << (who*12),50),(0,450)],checkpoint=ready,save=final)
            check(any(s["id"] == 0x160 and s["state"] == 2 for r in ending for s in r["sprites"]),
                  "normal goal collision did not activate")
            check(ending[-1]["sub"] == "10" and word(final.read_bytes(),0xD3) == 0x2E,
                  "normal goal did not return to the map")
            check(word(final.read_bytes(),0x8BE) == word(original,0x8BE),"finishing spent a team life")
            print(name+": moving scripted exit, native goal area, goal contact and map return pass")

        pending=directory/"mid-exit.sav"
        run("save-exit",[(0,100)],save=pending)
        direct=run("direct",[(0,370)])[-270:]
        resumed=run("reload",[(0,270)],checkpoint=pending)
        keys=("a","b","active","camera_x","camera_y","sprites","kong_flags")
        check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in resumed],
              "mid-exit save/reload changed gameplay")
    check(state.read_bytes() == original,"external checkpoint changed")
    print("Exact scripted-exit reload passes; source unchanged")


if __name__ == "__main__":
    main()
