#!/usr/bin/env python3
"""Input-only early coaster-to-cannon capture, shot and landing acceptance."""
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
    check(original[-5] == 0xC2 and original[-4] in (1,2),"requires one lost rider")
    who=1 if original[-4] == 1 else 0
    live,dead=("a","b") if who == 0 else ("b","a")
    slot=0xDE2 if who == 0 else 0xE40
    print("state_sha256="+hashlib.sha256(original).hexdigest())
    with tempfile.TemporaryDirectory(prefix="dkc2-coaster-cannon-") as tmp:
        directory=Path(tmp)
        source=directory/"original.sav";source.write_bytes(original)

        def run(name,inputs,checkpoint=source,save=None):
            return run_route(runner,rom,directory,name,inputs,state=checkpoint,save=save,submode=None)

        for delay,buttons in ((66,0x83),(68,0x83),(70,0x83),(75,0x83),(80,0x83),
                              (71,0x81),(75,0x81),(80,0x81)):
            # Run+Right makes earlier contact; Right alone exercises later entry.
            jump,forward=buttons << (who*12),(buttons & ~1) << (who*12)
            inputs=[(0,delay),(jump,30),(forward,270-delay)]
            final=directory/f"landed-{delay}-{buttons}.sav"
            rows=run(f"route-{delay}-{buttons}",inputs,save=final)
            capture=next((i for i,r in enumerate(rows) if r[live]["s"] == "0011"),None)
            check(capture is not None,"jump missed the cannon")
            shot=next((i for i,r in enumerate(rows[capture:],capture) if r[live]["s"] == "0012"),None)
            check(shot is not None,"accepted cannon did not shoot")
            check(all(r[live]["s"] == "0011" for r in rows[capture:shot]),
                  "old cart reclaimed the cannon occupant")
            check(all(r[live]["s"] not in ("003b","0034","0013") for r in rows),
                  "accepted early cannon landing triggered a death")
            check(rows[-1][live]["s"] == "005b" and rows[-1][live]["x"] > 14800,
                  "cannon shot did not reach the next cart")
            data=final.read_bytes()
            cart=word(data,slot+0x42)
            check(word(data,cart) == 0x158 and word(data,cart+6) > 14800 and data[-4] == original[-4] and
                  word(data,0x8BE) >= word(original,0x8BE),"cannon crossing changed loss/life ownership")
            check(all(r[dead]["s"] == "0013" and r["sprites"][1-who]["flags"] == 0 for r in rows),
                  "cannon crossing revived the dead teammate")
            print(f"{delay}/{buttons:02x}: captured {capture}, shot {shot}, landed without life loss")

        for split in (120,180):
            snapshot=directory/f"reload-{split}.sav"
            jump,forward=0x83 << (who*12),0x82 << (who*12)
            run(f"save-{split}",[(0,68),(jump,30),(forward,split-98)],save=snapshot)
            data=snapshot.read_bytes()
            check(word(data,0xD5E) == 0,"old global cart reference survived cannon capture")
            direct=run(f"direct-{split}",[(0,68),(jump,30),(forward,202)])[-(300-split):]
            resumed=run(f"resumed-{split}",[(forward,300-split)],checkpoint=snapshot)
            keys=("a","b","active","camera_x","camera_y","sprites","kong_flags")
            check([[r[k] for k in keys] for r in direct] == [[r[k] for k in keys] for r in resumed],
                  "cannon/shot reload changed gameplay")

        missed=directory/"missed.sav"
        rows=run("actual-miss",[(0,65),(0x81 << (who*12),30),(0x80 << (who*12),400)],save=missed)
        check(not any(r[live]["s"] == "0011" for r in rows) and
              any(r[live]["s"] == "003b" for r in rows),"actual missed cannon no longer triggers native loss")
        before_loss=directory/"before-loss.sav"
        first_loss=next(i for i,r in enumerate(rows) if r[live]["s"] == "003b")
        # Bananas award extra lives on this route; compare immediately before
        # the loss rather than against the original pre-award life counter.
        run("before-loss",[(0,65),(0x81 << (who*12),30),
                           (0x80 << (who*12),first_loss-95)],save=before_loss)
        check(word(missed.read_bytes(),0x8BE) == word(before_loss.read_bytes(),0x8BE)-1,
              "actual final pit loss spent wrong number of lives")
    check(state.read_bytes() == original,"external checkpoint changed")
    print("Early/later cannon capture, shot, next-cart landing, exact reload and actual missed-pit loss pass")


if __name__ == "__main__":
    main()
