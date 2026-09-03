#!/usr/bin/env python3
"""Mark every level of a DKC2 save file as cleared.

DKC2 keeps three 680-byte save files in its 2 KiB SRAM at offsets 8, 688,
and 1368. A file starts with a 16-bit sum and a 16-bit exclusive-or of its
words from offset 6 to the end, a signature byte $52 (bit 0 set for the
second data set), and the data-set selector; its data block begins at
offset 6 (or 6 + $154 for the second set). The cleared-level flags are
sixteen words at data offset $8D: one bit per level number, word
(number >> 4), bit (number & 15), exactly as the cartridge's
set_current_level_as_cleared at $BB:8158 writes them. The level numbers
that are real levels come from the pointer table at $FD:0000, whose
placeholder entries are two bytes apart.

The tool never touches the ROM and never writes ROM data; it reads the
pointer table to learn which numbers to set. The save file is backed up
beside itself before it is changed. The game reads the file at the file
select screen, so start from there afterwards: a restored quick save still
holds the old flags in memory and would write them back at its next save.
"""
import argparse
import shutil
import sys
from pathlib import Path

SRAM_SIZE = 2048
FILE_SIZE = 0x2A8
FILE_OFFSETS = (0x008, 0x2B0, 0x558)
DATA_OFFSET = 6
SECOND_SET_OFFSET = 0x154
CLEARED_OFFSET = 0x8D
CLEARED_WORDS = 16
LEVEL_TABLE_ROM_OFFSET = (0xFD - 0xC0) * 0x10000
MAX_LEVEL_NUMBER = 255


def checksums(file_bytes: bytes):
    total = 0
    xor = 0
    for offset in range(DATA_OFFSET, 0x2A2, 2):
        word = file_bytes[offset] | (file_bytes[offset + 1] << 8)
        total = (total + word) & 0xFFFF
        xor ^= word
    return total, xor


def file_valid(file_bytes: bytes) -> bool:
    signature = file_bytes[4] & 0xFE
    if signature != 0x52:
        return False
    total, xor = checksums(file_bytes)
    stored_total = file_bytes[0] | (file_bytes[1] << 8)
    stored_xor = file_bytes[2] | (file_bytes[3] << 8)
    return stored_total == total and stored_xor == xor


def real_level_numbers(rom: bytes):
    """Level numbers whose header pointer is followed by more than two
    bytes of header: the two-byte entries are placeholders."""
    pointers = []
    for number in range(0, MAX_LEVEL_NUMBER + 2):
        offset = LEVEL_TABLE_ROM_OFFSET + number * 2
        pointers.append(rom[offset] | (rom[offset + 1] << 8))
    numbers = []
    for number in range(1, MAX_LEVEL_NUMBER + 1):
        if pointers[number] == 0:
            break
        length = pointers[number + 1] - pointers[number]
        if length > 2:
            numbers.append(number)
    return numbers


def cleared_numbers(data: bytes):
    numbers = []
    for word_index in range(CLEARED_WORDS):
        offset = CLEARED_OFFSET + word_index * 2
        word = data[offset] | (data[offset + 1] << 8)
        for bit in range(16):
            if word & (1 << bit):
                numbers.append(word_index * 16 + bit)
    return numbers


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--save", required=True, type=Path,
                        help="path to save.srm (2 KiB)")
    parser.add_argument("--rom", required=True, type=Path,
                        help="path to the DKC2 (USA) ROM, read only")
    parser.add_argument("--file", default="0", choices=("0", "1", "2", "all"),
                        help="save file slot to change, or all (default 0)")
    parser.add_argument("--snapshot", type=Path,
                        help="a quick save (dkc2s0.sav) whose in-memory "
                             "flags should be set too, so restoring it does "
                             "not write the old flags back")
    parser.add_argument("--output", type=Path,
                        help="write the changed image here instead of in place")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would change without writing")
    parser.add_argument("--repair", action="store_true",
                        help="recompute the header of a file whose signature "
                             "is intact but whose sums disagree, which the "
                             "game otherwise shows as empty")
    args = parser.parse_args()

    sram = bytearray(args.save.read_bytes())
    if len(sram) != SRAM_SIZE:
        print(f"{args.save}: expected {SRAM_SIZE} bytes, got {len(sram)}",
              file=sys.stderr)
        return 2
    rom = args.rom.read_bytes()
    if len(rom) < LEVEL_TABLE_ROM_OFFSET + 0x200:
        print(f"{args.rom}: too small to hold the level table",
              file=sys.stderr)
        return 2

    numbers = real_level_numbers(rom)
    slots = (0, 1, 2) if args.file == "all" else (int(args.file),)
    changed = 0
    for slot in slots:
        start = FILE_OFFSETS[slot]
        file_bytes = sram[start:start + FILE_SIZE]
        if not file_valid(file_bytes):
            if args.repair and (file_bytes[4] & 0xFE) == 0x52:
                total, xor = checksums(file_bytes)
                stored = (file_bytes[0] | (file_bytes[1] << 8),
                          file_bytes[2] | (file_bytes[3] << 8))
                print(f"save file {slot}: signature intact, stored sums "
                      f"{stored[0]:04x}/{stored[1]:04x} against computed "
                      f"{total:04x}/{xor:04x}; header recomputed")
                file_bytes[0] = total & 0xFF
                file_bytes[1] = total >> 8
                file_bytes[2] = xor & 0xFF
                file_bytes[3] = xor >> 8
                sram[start:start + FILE_SIZE] = file_bytes
                changed += 1
            else:
                print(f"save file {slot}: empty or does not validate; skipped")
                continue
        data_start = DATA_OFFSET + (SECOND_SET_OFFSET if file_bytes[5] == 2
                                    and (file_bytes[4] & 1) else 0)
        before = cleared_numbers(file_bytes[data_start:])
        added = [n for n in numbers if n not in before]
        print(f"save file {slot}: {len(before)} level numbers cleared, "
              f"{len(added)} to mark of {len(numbers)} real levels"
              + (f" ({added[0]}..{added[-1]})" if added else ""))
        if args.dry_run or not added:
            continue
        for number in numbers:
            offset = data_start + CLEARED_OFFSET + (number >> 4) * 2
            word = file_bytes[offset] | (file_bytes[offset + 1] << 8)
            word |= 1 << (number & 15)
            file_bytes[offset] = word & 0xFF
            file_bytes[offset + 1] = word >> 8
        total, xor = checksums(file_bytes)
        file_bytes[0] = total & 0xFF
        file_bytes[1] = total >> 8
        file_bytes[2] = xor & 0xFF
        file_bytes[3] = xor >> 8
        assert file_valid(file_bytes)
        sram[start:start + FILE_SIZE] = file_bytes
        changed += 1
    if args.dry_run:
        return 0
    if not changed:
        print("nothing to change")
        return 0

    target = args.output or args.save
    if target == args.save:
        backup = args.save.with_name(args.save.name + ".before-unlock")
        shutil.copyfile(args.save, backup)
        print(f"backup written: {backup}")
    target.write_bytes(bytes(sram))
    print(f"written: {target}")
    if args.snapshot:
        patch_snapshot(args.snapshot, numbers, args.output is not None)
    else:
        print("Start the game from the file select screen; a restored quick "
              "save still holds the old flags.")
    return 0


WRAM_CLEARED_ADDRESS = 0x59F2
WRAM_BLOCK_BYTES = 0x20000


def patch_snapshot(path: Path, numbers, to_scratch: bool) -> None:
    """Set the same flags in a quick save's in-memory copy. The runtime
    stores WRAM as one raw 128 KiB block inside the snapshot; it is found
    by the game's own camera words, which sit 0x17BA bytes after the block
    starts and read back at offset 0x17BA of the block, so the block is
    located by checking that a candidate base reproduces the file's
    header and the level-number word the file select would show. The
    search anchors on the cleared-flag words themselves being a plausible
    subset of the real levels."""
    snap = bytearray(path.read_bytes())
    candidates = []
    for base in range(0, len(snap) - WRAM_BLOCK_BYTES + 1):
        words = [snap[base + WRAM_CLEARED_ADDRESS + 2 * i]
                 | (snap[base + WRAM_CLEARED_ADDRESS + 2 * i + 1] << 8)
                 for i in range(CLEARED_WORDS)]
        flagged = [i * 16 + b for i in range(CLEARED_WORDS)
                   for b in range(16) if words[i] & (1 << b)]
        if not flagged or any(n not in numbers for n in flagged):
            continue
        mode = snap[base + 0x24] | (snap[base + 0x25] << 8)
        if mode not in (0x8819,):
            continue
        candidates.append(base)
    if len(candidates) != 1:
        print(f"snapshot {path}: WRAM block not identified "
              f"({len(candidates)} candidates); not changed")
        return
    base = candidates[0]
    if not to_scratch:
        backup = path.with_name(path.name + ".before-unlock")
        shutil.copyfile(path, backup)
        print(f"backup written: {backup}")
    for number in numbers:
        offset = base + WRAM_CLEARED_ADDRESS + (number >> 4) * 2
        word = snap[offset] | (snap[offset + 1] << 8)
        word |= 1 << (number & 15)
        snap[offset] = word & 0xFF
        snap[offset + 1] = word >> 8
    target = path if not to_scratch else path.with_name(path.name + ".unlocked")
    target.write_bytes(bytes(snap))
    print(f"snapshot flags set: {target} (WRAM block at {base:#x})")
    return


if __name__ == "__main__":
    sys.exit(main())
