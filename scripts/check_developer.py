#!/usr/bin/env python3
"""Private developer-command acceptance using an external gameplay save."""
import argparse
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile


def word(data, address):
    if data[:4] != b"SLTR":
        raise AssertionError("expected an SLTR snapshot")
    return struct.unpack_from("<H", data, 168255 + address)[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("runner", "rom", "state"):
        parser.add_argument("--" + name, required=True, type=Path)
    args = parser.parse_args()
    runner, rom, state = (p.resolve(strict=True) for p in (args.runner, args.rom, args.state))
    original, content = state.read_bytes(), rom.read_bytes()
    assert hashlib.sha256(content).hexdigest() == "35421a9af9dd011b40b91f792192af9f99c93201d8d394026bdfb42cbf2d8633"
    kind = word(original, 0x515)
    assert kind in (0, 1, 2, 3), "gameplay checkpoint required"
    room = word(original, 0xD3)
    level = word(original, 0x8A8)
    mask = struct.unpack_from("<H", content, 0x3B817F + 2 * (level & 15))[0]
    address = 0x59F2 + 2 * (level >> 4)
    with tempfile.TemporaryDirectory(prefix="dkc2-developer-") as tmp:
        directory = Path(tmp)
        source, output = directory / "original.sav", directory / "completed.sav"
        source.write_bytes(original)
        env = {k: v for k, v in os.environ.items() if not k.startswith(("DKC2_", "SNESRECOMP_"))}
        env.update(DKC2_COOP="simultaneous", DKC2_SAVESTATE_INPUT=str(source),
                   DKC2_SAVESTATE_OUTPUT=str(output), DKC2_DEVELOPER_COMMAND="beat_level",
                   DKC2_DEVELOPER_COMMAND_FRAME="0")
        result = subprocess.run([str(runner), str(rom), "500"], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=45)
        assert result.returncode == 0, result.stdout + result.stderr
        assert "developer command=beat_level result=3" in result.stdout, "command was not executed"
        completed = output.read_bytes()
        expected_clear = word(original, address) | (0 if kind == 1 else mask)
        assert word(completed, address) == expected_clear, "incorrect map completion credit"
        if kind in (1, 3):
            reward_address = 0x59B2 + 2 * (room >> 4)
            reward_mask = struct.unpack_from("<H", content, 0x3B817F + 2 * (room & 15))[0]
            assert word(completed, reward_address) == word(original, reward_address) | reward_mask
            reward = 0 if word(original, reward_address) & reward_mask else 1
            assert word(completed, 0x8CC) == min(75, word(original, 0x8CC) + reward)
        if kind == 1:
            assert word(completed, 0x515) != 1 and word(completed, 0xD3) == level, "bonus did not return to parent"
            assert word(completed, 0x6E) == word(original, 0x6E), "bonus lost animal form"
        else:
            assert word(completed, 0x515) == 5 or 0x8C9A <= word(completed, 0x20) <= 0x8CC2, "native exit did not reach map loop"
            assert word(completed, 0x59D) == 0, "developer command entered the death exit"
        # Commands on the map must be refused, rather than clearing another level.
        if kind == 1:
            assert state.read_bytes() == original
            print("Bonus reward and parent/animal return pass; source unchanged")
            return
        env.update(DKC2_SAVESTATE_INPUT=str(output), DKC2_SAVESTATE_OUTPUT=str(directory / "map.sav"))
        result = subprocess.run([str(runner), str(rom), "30"], cwd=directory, env=env,
                                capture_output=True, text=True, timeout=45)
        assert result.returncode == 0 and "developer command=beat_level result=2" in result.stdout
    assert state.read_bytes() == original, "external source save changed"
    print("state_sha256=" + hashlib.sha256(original).hexdigest())
    print("Developer beat_level: native completion/reward, world-map exit and map rejection pass; source unchanged")


if __name__ == "__main__":
    main()
