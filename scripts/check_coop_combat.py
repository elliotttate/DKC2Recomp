#!/usr/bin/env python3
"""Private blank-SRAM TEAM combat acceptance; no game data is stored in Git.

Drive both real SNES input ports, observe enemy/player state and CGRAM, and
compare normal Kong colors against the owner's supported ROM. Temporary input
and trace files are removed after the check. No guest memory is patched here.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import tempfile

from check_coop_route import TEAM_START, SEPARATE_PLAYERS, check


def run_route(runner, rom, directory, name, route, *, state=None, save=None,
              aspect=None, edge=None, kongs_pack=None, submode="06"):
    frames = sum(duration for _, duration in route)
    inputs, trace = directory / f"{name}.input", directory / f"{name}.jsonl"
    inputs.write_text("\n".join(f"{word:06x}*{duration}" for word, duration in route),
                      encoding="ascii")
    env = {k: v for k, v in os.environ.items()
           if not k.startswith(("DKC2_", "SNESRECOMP_"))}
    env.update(DKC2_COOP="simultaneous", DKC2_COOP_TRACE=str(trace),
               DKC2_COOP_TRACE_SPRITES="1", SNESRECOMP_INPUT_PLAY=str(inputs))
    if aspect:
        env["DKC2_ASPECT"] = aspect
    if edge:
        env["DKC2_WIDESCREEN_EDGE"] = edge
    if state:
        env["DKC2_SAVESTATE_INPUT"] = str(state)
    if save:
        env["DKC2_SAVESTATE_OUTPUT"] = str(save)
    if kongs_pack:
        env.update(DKC2_KONGS_PACK=str(kongs_pack), DKC2_KONGS="1,2")
    result = subprocess.run([str(runner), str(rom), str(frames)], cwd=directory,
                            env=env, capture_output=True, text=True, timeout=90)
    check(result.returncode == 0, result.stdout + result.stderr)
    data = [json.loads(line) for line in trace.read_text().splitlines()]
    check(len(data) == frames, f"{name}: incomplete trace")
    if submode is not None:
        check(all(row["mode"] == 1 and row["sub"] == submode for row in data[0 if state else 3017:]),
              f"{name}: not in TEAM gameplay")
    return data


def first_enemy_death(data, start, label):
    alive_seen = False
    for row in data[start:]:
        for sprite in row["sprites"][2:]:
            if sprite["id"] != 0x01E4:
                continue
            alive_seen |= sprite["state"] == 0 and sprite["flags"] != 0
            if sprite["state"] == 1 and sprite["flags"] == 0:
                check(alive_seen, f"{label}: enemy was never alive")
                return row
    raise AssertionError(f"{label}: attack did not defeat the first enemy")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--runner", required=True, type=Path)
    parser.add_argument("--rom", required=True, type=Path)
    args = parser.parse_args()
    runner, rom = args.runner.resolve(strict=True), args.rom.resolve(strict=True)
    content = rom.read_bytes()
    check(hashlib.sha256(content).hexdigest() ==
          "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633",
          "combat route requires the supported headerless USA v1.0 ROM")
    palettes = [list(struct.unpack_from("<15H", content, offset))
                for offset in (0x3D6484, 0x3D6574)]
    prefix = TEAM_START + SEPARATE_PLAYERS
    recovery = [(0, 180), (0x40000, 120), (0, 12), (0x1000, 1), (0, 35)]
    cases = {
        "p2_roll": prefix + [(0x80080, 90), (0x82080, 30)] + recovery,
        "p2_stomp": prefix + [(0x80080, 72), (0x81080, 48)] + recovery,
        "p2_contact": prefix + [(0x80080, 120), (0, 180)],
        "p1_roll": TEAM_START + [(0x80, 280), (0x82, 60), (0, 120)],
    }
    with tempfile.TemporaryDirectory(prefix="dkc2-coop-combat-") as tmp:
        runs = {name: run_route(runner, rom, Path(tmp), name, route)
                for name, route in cases.items()}
        for name, data in runs.items():
            for slot, expected in enumerate(palettes):
                check(data[3017]["sprites"][slot]["colors"] == expected,
                      f"{name}: player {slot + 1} has a dim/incorrect normal palette")
        for name in ("p2_roll", "p2_stomp"):
            data = runs[name]
            hit = first_enemy_death(data, 3338, name)
            check(hit["b"]["x"] - hit["a"]["x"] > 100,
                  f"{name}: P1 too close to attribute the hit to P2")
            check(hit["a"]["s"] == "0000", f"{name}: P2 attack affected P1")
            check(hit["sprites"][1]["flags"] != 0, f"{name}: P2 still intangible")
            if name == "p2_stomp":
                check(hit["b"]["s"] == "0016", "stomp did not bounce P2")
                check(hit["stomp_events"] == 2, "P2 stomp did not route feedback solely to P2")
                check(data[hit["frame"] + 10]["b"]["y"] < hit["b"]["y"] - 20,
                      "P2 bounce did not move upward")
            else:
                check(hit["b"]["s"] == "0002", "P2 kill was not a roll")
            check(data[3637]["b"]["s"] not in ("0004", "0016"),
                  f"{name}: attack recovery left P2 in a locked state")
            check(data[3637]["b"]["x"] - data[3757]["b"]["x"] > 150,
                  f"{name}: P2 cannot walk after defeating the enemy")
            check(data[3757]["a"]["x"] == data[3637]["a"]["x"],
                  f"{name}: P2 recovery moved P1")
            check(min(row["b"]["y"] for row in data[-35:]) < data[-37]["b"]["y"] - 20,
                  f"{name}: P2 cannot jump after recovery")
            print(f"{name}: enemy defeated at frame {hit['frame']}; P2 walked and jumped afterward")
        check(not any(row["stomp_events"] for row in runs["p2_roll"]),
              "roll/jump without stomp incorrectly triggered rumble")
        contact = runs["p2_contact"][3338:]
        hurt = next((row for row in contact if row["b"]["s"] in ("0024", "0025")), None)
        check(hurt is not None, "P2 cannot take contact damage")
        check(hurt["sprites"][1]["flags"] == 0, "P2 hurt collision mask not preserved")
        check(hurt["a"]["s"] == "0000", "P2 contact damage affected P1")
        check(any(s["id"] == 0x01E4 and s["state"] == 0 for s in hurt["sprites"]),
              "ordinary P2 contact incorrectly killed the enemy")
        print(f"p2_contact: P2 hurt, P1 unaffected at frame {hurt['frame']}")
        hit = first_enemy_death(runs["p1_roll"], 3018, "p1_roll")
        check(hit["a"]["x"] - hit["b"]["x"] > 100, "P2 too close to attribute P1 kill")
        print(f"p1_roll: enemy defeated at frame {hit['frame']}")
        print("TEAM combat passed: P2 roll, stomp/bounce, contact damage, P1 roll, both normal palettes")


if __name__ == "__main__":
    main()
