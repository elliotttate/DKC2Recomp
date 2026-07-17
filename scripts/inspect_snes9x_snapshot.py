#!/usr/bin/env python3
"""Fingerprint display state in a Snes9x v12 file or libretro state."""

from __future__ import annotations

import argparse
import gzip
import hashlib
from pathlib import Path
import sys


MAGIC = b"#!s9xsnp:0012\n"
PPU_BLOCK_SIZE = 2652
PPU_CGRAM_OFFSET = 64
PPU_CGRAM_SIZE = 512
PPU_OAM_OFFSET = 2003
PPU_OAM_SIZE = 544
PPU_REGISTER_OFFSET = 0x2100
PPU_REGISTER_SIZE = 0x34


def parse_blocks(data: bytes) -> dict[bytes, bytes]:
    if not data.startswith(MAGIC):
        raise ValueError("expected a Snes9x snapshot-format version 12 stream")
    position = len(MAGIC)
    blocks: dict[bytes, bytes] = {}
    while position < len(data):
        if position + 11 > len(data):
            raise ValueError("truncated Snes9x block header")
        header = data[position:position + 11]
        position += 11
        if header[3:4] != b":" or header[10:11] != b":":
            raise ValueError("invalid Snes9x block header")
        name = header[:3]
        if header[4:10] == b"------":
            size = int.from_bytes(header[6:10], "big")
        else:
            try:
                size = int(header[4:10])
            except ValueError as error:
                raise ValueError("invalid Snes9x block size") from error
        end = position + size
        if end > len(data):
            raise ValueError(f"truncated {name.decode('ascii')} block")
        if name in blocks:
            raise ValueError(f"duplicate {name.decode('ascii')} block")
        blocks[name] = data[position:end]
        position = end
    return blocks


def require_block(blocks: dict[bytes, bytes],
                  name: bytes,
                  size: int) -> bytes:
    block = blocks.get(name)
    if block is None:
        raise ValueError(f"snapshot has no {name.decode('ascii')} block")
    if len(block) != size:
        raise ValueError(
            f"unexpected {name.decode('ascii')} block size: {len(block)}"
        )
    return block


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Read an official Snes9x snapshot-format v12 save state and "
            "print hashes compatible with dkc2_boot's display-state report."
        )
    )
    parser.add_argument("snapshot", type=Path)
    parser.add_argument("--expect-vram")
    parser.add_argument("--expect-wram")
    parser.add_argument("--expect-cgram")
    parser.add_argument("--expect-oam")
    parser.add_argument("--expect-ppu")
    parser.add_argument("--dump-oam", type=Path)
    parser.add_argument("--dump-wram", type=Path)
    args = parser.parse_args()

    try:
        stored = args.snapshot.read_bytes()
        data = stored if stored.startswith(MAGIC) else gzip.decompress(stored)
        blocks = parse_blocks(data)
        ppu = require_block(blocks, b"PPU", PPU_BLOCK_SIZE)
        vram = require_block(blocks, b"VRA", 65536)
        wram = require_block(blocks, b"RAM", 131072)
        fill_ram = require_block(blocks, b"FIL", 32768)
        cgram_big_endian = ppu[
            PPU_CGRAM_OFFSET:PPU_CGRAM_OFFSET + PPU_CGRAM_SIZE
        ]
        cgram = b"".join(
            cgram_big_endian[offset:offset + 2][::-1]
            for offset in range(0, PPU_CGRAM_SIZE, 2)
        )
        oam = ppu[PPU_OAM_OFFSET:PPU_OAM_OFFSET + PPU_OAM_SIZE]
        ppu_registers = fill_ram[
            PPU_REGISTER_OFFSET:PPU_REGISTER_OFFSET + PPU_REGISTER_SIZE
        ]
        if args.dump_oam is not None:
            args.dump_oam.parent.mkdir(parents=True, exist_ok=True)
            args.dump_oam.write_bytes(oam)
        if args.dump_wram is not None:
            args.dump_wram.parent.mkdir(parents=True, exist_ok=True)
            args.dump_wram.write_bytes(wram)
    except (OSError, EOFError, gzip.BadGzipFile, ValueError) as error:
        print(f"snapshot inspection failed: {error}", file=sys.stderr)
        return 2

    actual = {
        "VRAM": sha256(vram),
        "WRAM": sha256(wram),
        "CGRAM": sha256(cgram),
        "OAM": sha256(oam),
        "PPU writes": sha256(ppu_registers),
    }
    expected = {
        "WRAM": args.expect_wram,
        "VRAM": args.expect_vram,
        "CGRAM": args.expect_cgram,
        "OAM": args.expect_oam,
        "PPU writes": args.expect_ppu,
    }
    mismatches = 0
    for name in ("WRAM", "VRAM", "CGRAM", "OAM", "PPU writes"):
        print(f"{name} SHA-256: {actual[name]}")
        if expected[name] is not None:
            match = actual[name].lower() == expected[name].lower()
            print(f"{name} expected:   {'match' if match else 'MISMATCH'}")
            if not match:
                mismatches += 1
    print("Result: all expected hashes match" if mismatches == 0 else
          f"Result: {mismatches} expected hash(es) differ")
    return 0 if mismatches == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
