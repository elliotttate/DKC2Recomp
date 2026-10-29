#!/usr/bin/env python3
"""Private shared stomp acceptance from an external checkpoint, using inputs only."""
import argparse
import hashlib
from pathlib import Path
import tempfile

from check_coop_combat import run_route
from check_coop_route import check


def check_bounce_camera(rows):
    frames = [r for r in rows if
              any(r[p]["s"] == "0016" for p in ("a", "b")) and
              all(r[p]["s"] in ("0000", "0001", "0002", "0004", "0006", "0016")
                  for p in ("a", "b"))]
    check(frames, "camera check did not exercise a live enemy bounce")
    for r in frames:
        for axis in ("x", "y"):
            # Trace integers omit the native subpixel words used by the camera.
            check(abs(r["camera_target_" + axis] -
                      (r["a"][axis] + r["b"][axis]) // 2) <= 1,
                  "enemy bounce restored a single-player camera target on " + axis)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    parser.add_argument("--state", required=True, type=Path)
    parser.add_argument("--camera-state", type=Path,
                        help="Optional external later enemy checkpoint for bounce camera/reload")
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original = state.read_bytes()
    check(hashlib.sha256(rom.read_bytes()).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "requires the supported USA v1.0 ROM")
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    camera_original = args.camera_state.read_bytes() if args.camera_state else None
    with tempfile.TemporaryDirectory(prefix="dkc2-shared-stomp-") as tmp:
        directory = Path(tmp)
        source = directory / "original.sav"
        source.write_bytes(original)

        def run(name, route, source=source, save=None):
            return run_route(runner, rom, directory, name, route,
                             state=source, save=save, submode=None)

        for leader, prefix in (("p2-first", []), ("p1-first", [(0x80, 8)])):
            shared = directory / (leader + ".sav")
            approach = prefix + [(0x80080, 16), (0x81081, 20), (0, 21)]
            rows = run(leader, approach, save=shared)
            check_bounce_camera(rows)
            hit = next((r for r in rows if r["stomp_events"]), None)
            check(hit is not None and hit["stomp_events"] == 3 and
                  hit["a"]["s"] == hit["b"]["s"] == "0016",
                  leader + ": the simultaneous landing did not bounce both players")
            check(any(s["id"] == 0x220 for s in hit["sprites"]),
                  "expected the reported bouncing enemy checkpoint")
            hit_index = hit["frame"]
            previous = rows[hit_index - 1]
            check(previous["a"]["s"] == previous["b"]["s"] == "0006",
                  "this test did not reach a joint airborne landing")
            check(rows[-1]["a"]["s"] == rows[-1]["b"]["s"] == "0016",
                  "shared bounce checkpoint is not in paired flight")
            # The platforms ahead are reachable only if the partner gets the
            # bounce too. Prove equal initial lift, then independent movement.
            flight = run(leader + "-flight", [(0, 10)], shared)
            check_bounce_camera(flight)
            for player in ("a", "b"):
                check(min(r[player]["y"] for r in flight) < rows[-1][player]["y"] - 25,
                      leader + ": one player did not rise after reload")
            check(abs((flight[-1]["a"]["y"] - rows[-1]["a"]["y"]) -
                      (flight[-1]["b"]["y"] - rows[-1]["b"]["y"])) <= 10,
                  "shared bounce strength differs excessively")
            direct = run(leader + "-direct", approach + [(0x80080, 45)])
            reload = run(leader + "-reload", [(0x80080, 45)], shared)
            check_bounce_camera(reload)
            check([(r["a"], r["b"]) for r in direct[-45:]] ==
                  [(r["a"], r["b"]) for r in reload],
                  "mid-bounce reload changed the trajectory")
            check(all(reload[-1][p]["s"] == "0000" and reload[-1][p]["y"] == 645 and
                      reload[-1][p]["x"] > 1700 for p in ("a", "b")),
                  leader + ": both players did not reach the next platform")
            left = run(leader + "-p1-left", [(0x40, 10)], shared)
            right = run(leader + "-p1-right", [(0x80, 10)], shared)
            check(left[-1]["a"]["x"] + 5 < right[-1]["a"]["x"] and
                  all(l["b"] == r["b"] for l, r in zip(left, right)),
                  "shared bounce coupled the players' horizontal controls")
            print(leader + ": joint bounce, lift, separate steering, reload and next platform pass")

        for player in range(2):
            rows = run(f"single-{player}", [(0x80080, 16),
                       [(0x81, 20), (0x81000, 20)][player], (0, 45)])
            hit = next((r for r in rows if r["stomp_events"]), None)
            # The P1-only fixture loses its grounded partner to the pit before
            # impact; only the P2-only route has two live camera contributors.
            if player == 1:
                check_bounce_camera(rows)
            check(hit is not None and hit["stomp_events"] == (1 << player),
                  "single stomp incorrectly boosted the grounded/distant partner")
        # Both jump, with P2 two frames earlier: their contact is slightly
        # staggered but remains within the configured default distance window.
        staggered = run("staggered", [(0x80080, 16), (0x81000, 2),
                                     (0x81081, 18), (0, 25)])
        check(any(r["stomp_events"] == 3 for r in staggered),
              "slightly staggered descent was not forgiven")
        print("Single-player exclusion and slightly staggered joint descent pass")
        if camera_original is not None:
            camera_source = directory / "camera-original.sav"
            camera_source.write_bytes(camera_original)
            print("camera_state_sha256=" + hashlib.sha256(camera_original).hexdigest())
            for name, word, event in (("single", 0x81, 1), ("shared", 0x81081, 3)):
                prefix = [(word, 35), (0, 8)]
                saved = directory / (name + "-camera.sav")
                rows = run(name + "-camera", prefix, camera_source, saved)
                check(any(r["stomp_events"] == event for r in rows),
                      "reported camera checkpoint did not produce the intended stomp")
                check_bounce_camera(rows)
                reloaded = run(name + "-camera-reload", [(0, 12)], saved)
                check_bounce_camera(reloaded)
                direct = run(name + "-camera-direct", prefix + [(0, 12)], camera_source)
                keys = ("a", "b", "camera_target_x", "camera_target_y")
                check([[r[k] for k in keys] for r in direct[-12:]] ==
                      [[r[k] for k in keys] for r in reloaded],
                      "mid-bounce reload changed players or midpoint camera")
            print("Reported single/shared bounce: midpoint on both axes and reload pass")
    check(state.read_bytes() == original, "original checkpoint changed")
    if camera_original is not None:
        check(args.camera_state.read_bytes() == camera_original, "camera checkpoint changed")


if __name__ == "__main__":
    main()
