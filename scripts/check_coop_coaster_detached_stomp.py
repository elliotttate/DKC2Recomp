#!/usr/bin/env python3
"""Input-only shared stomp acceptance after both riders leave the coaster."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_coaster import word
from check_coop_coaster_pair_landing import route
from check_coop_route import check


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--"+name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner,args.rom,args.state))
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    original = state.read_bytes()
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-detached-stomp-") as tmp:
        directory = Path(tmp)
        source = directory/"original.sav";source.write_bytes(original)

        def run(name, inputs, checkpoint=source, save=None):
            return run_route(runner,rom,directory,name,inputs,state=checkpoint,save=save,submode=None)

        for a,b in ((60,60),(65,65),(70,70),(75,75),(65,60),(70,65),(75,60)):
            final = directory/f"landed-{a}-{b}.sav"
            rows = run(f"stomp-{a}-{b}",route(a,b,220),save=final)
            hit = next((i for i,r in enumerate(rows) if r["stomp_events"] == 3),None)
            check(hit is not None,f"{a}/{b}: detached stomp did not bounce both riders")
            for player in ("a","b"):
                check(rows[hit][player]["s"] == "0068", "partner lost native coaster bounce state")
                check(min(r[player]["y"] for r in rows[hit:hit+35]) < rows[hit][player]["y"]-45,
                      "shared feedback did not produce a real upward bounce")
                check(rows[-1][player]["s"] in ("005b","005c") and rows[-1][player]["x"] > 9400,
                      "bounced rider did not reach the next cart")
            before = directory/"before.sav";after = directory/"after.sav"
            run("before",route(a,b,hit),save=before)
            run("after",route(a,b,hit+1),save=after)
            pre,post=before.read_bytes(),after.read_bytes()
            check(all(word(pre,slot+0x42) == 0 for slot in (0xDE2,0xE40)),
                  "stomp check did not exercise detached riders")
            impulse=word(post,0xDE2+0x24)
            check(impulse & 0x8000 and impulse == word(post,0xE40+0x24),
                  "detached riders did not receive the same upward impulse")
            data=final.read_bytes()
            check(data[-4] == 0 and word(data,0x8BE) == word(original,0x8BE),
                  "shared bounce lost a rider or team life")
            print(f"{a}/{b}: frame {hit}, both detached, both bounce, both land")

        # This route puts P1 more than 32 vertical pixels from P2 at contact.
        rows=run("outside-threshold",route(70,60,130))
        hit=next(i for i,r in enumerate(rows) if r["stomp_events"])
        check(rows[hit]["stomp_events"] == 2 and rows[hit]["a"]["s"] == "0067",
              "outside-threshold player received an unearned shared bounce")

        snapshot=directory/"mid-bounce.sav"
        run("save-bounce",route(65,65,112),save=snapshot)
        direct=run("direct",route(65,65,220))[-108:]
        resumed=run("reload",[(0x80080,108)],checkpoint=snapshot)
        keys=("a","b","active","camera_x","camera_y","sprites","kong_flags")
        check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in resumed],
              "mid-bounce reload changed either rider")
    check(state.read_bytes() == original,"external checkpoint changed")
    print("Detached shared stomp, threshold rejection, next-cart landing and exact reload pass")


if __name__ == "__main__":
    main()
