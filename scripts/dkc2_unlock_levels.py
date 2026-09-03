#!/usr/bin/env python3
"""Mark every level of a DKC2 save file as cleared, and optionally open the
Lost World and grant coins.

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

The rest of the record follows update_save_buffer at $BB:C5E0: data byte
5 is the Kremkoin count (WRAM $08CC), and data bytes $B4..$E3 mirror WRAM
$08D2..$0901, which hold the shop and kiosk state. Klubba's kiosk at
$B4:91F4 treats bit (1 << world) of WRAM $08FA as "toll already paid",
and the Lost World map compares WRAM $08F9, the number of Lost World
levels beaten, with 5 before it opens Krocodile Kore. Banana Coins (WRAM
$08CA) are never written to the file: the map loader at $B4:800E zeroes
them before it reads a file, so they can only be granted inside a quick
save's memory image.

The tool never touches the ROM and never writes ROM data; it reads the
pointer table to learn which numbers to set. The save file is backed up
beside itself before it is changed. The game reads the file at the file
select screen, so start from there afterwards: a restored quick save still
holds the old values in memory and would write them back at its next save,
unless it is patched too with --snapshot.
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
KREMKOIN_OFFSET = 0x05
STATE_OFFSET = 0xB4
STATE_WRAM_BASE = 0x08D2
LEVEL_TABLE_ROM_OFFSET = (0xFD - 0xC0) * 0x10000
MAX_LEVEL_NUMBER = 255

WRAM_BANANA_COINS = 0x08CA
WRAM_KREMKOINS = 0x08CC
WRAM_LOST_WORLD_CLEARED = 0x08F9
WRAM_KIOSKS_PAID = 0x08FA
LOST_WORLD_LEVELS = 5
# Klubba's kiosks stand in worlds 2..6 (Crocodile Cauldron to K. Rool's
# Keep); the mask also covers worlds 1 and 7, which have no kiosk, so the
# unlock does not depend on the world numbering being read exactly.
KIOSK_MASK = 0xFE
MAX_COINS = 99


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


def seal(file_bytes: bytearray) -> None:
    total, xor = checksums(file_bytes)
    file_bytes[0] = total & 0xFF
    file_bytes[1] = total >> 8
    file_bytes[2] = xor & 0xFF
    file_bytes[3] = xor >> 8
    assert file_valid(file_bytes)


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


def state_offset(wram_address: int) -> int:
    """Data offset of the mirror of a WRAM byte in $08D2..$0901."""
    return STATE_OFFSET + (wram_address - STATE_WRAM_BASE)


def wram_writes(args):
    """The WRAM bytes a quick save should hold after the change, as
    address -> (or_mask, value); value None keeps the byte and ors the
    mask in."""
    writes = {}
    if args.kremkoins is not None:
        writes[WRAM_KREMKOINS] = (0, args.kremkoins)
    if args.banana_coins is not None:
        writes[WRAM_BANANA_COINS] = (0, args.banana_coins)
    if args.lost_world:
        writes[WRAM_LOST_WORLD_CLEARED] = (0, LOST_WORLD_LEVELS)
        writes[WRAM_KIOSKS_PAID] = (KIOSK_MASK, None)
    return writes


def apply_byte(buffer: bytearray, offset: int, or_mask: int, value):
    before = buffer[offset]
    after = before | or_mask if value is None else value
    buffer[offset] = after
    return before != after


def back_up(path: Path) -> Path:
    """Copy a file beside itself before changing it, never over an
    earlier backup: the first copy is .before-unlock, later ones are
    numbered."""
    backup = path.with_name(path.name + ".before-unlock")
    index = 1
    while backup.exists():
        index += 1
        backup = path.with_name(f"{path.name}.before-unlock{index}")
    shutil.copyfile(path, backup)
    return backup


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--save", required=True, type=Path,
                        help="path to save.srm (2 KiB)")
    parser.add_argument("--rom", required=True, type=Path,
                        help="path to the DKC2 (USA) ROM, read only")
    parser.add_argument("--file", default="0", choices=("0", "1", "2", "all"),
                        help="save file slot to change, or all (default 0)")
    parser.add_argument("--no-levels", action="store_true",
                        help="leave the cleared-level flags alone")
    parser.add_argument("--lost-world", action="store_true",
                        help="mark every Klubba kiosk as paid and the five "
                             "Lost World levels as beaten, which opens "
                             "Krocodile Kore")
    parser.add_argument("--kremkoins", type=int,
                        help="set the Kremkoin count (0..99; the game "
                             "holds 75 in total, one kiosk costs 15)")
    parser.add_argument("--banana-coins", type=int,
                        help="set the Banana Coin count in a quick save "
                             "(0..99); the file never stores it")
    parser.add_argument("--snapshot", type=Path,
                        help="a quick save (dkc2s0.sav) whose in-memory "
                             "state should be changed too, so restoring it "
                             "does not write the old values back")
    parser.add_argument("--output", type=Path,
                        help="write the changed image here instead of in place")
    parser.add_argument("--dry-run", action="store_true",
                        help="report what would change without writing")
    parser.add_argument("--repair", action="store_true",
                        help="recompute the header of a file whose signature "
                             "is intact but whose sums disagree, which the "
                             "game otherwise shows as empty")
    args = parser.parse_args()

    for name in ("kremkoins", "banana_coins"):
        value = getattr(args, name)
        if value is not None and not 0 <= value <= MAX_COINS:
            print(f"--{name.replace('_', '-')} must be 0..{MAX_COINS}",
                  file=sys.stderr)
            return 2
    if args.banana_coins is not None and not args.snapshot:
        print("Banana Coins are not stored in the save file: the game zeroes "
              "them when it loads a file. Pass --snapshot to set them in a "
              "quick save.", file=sys.stderr)
        return 2

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
    writes = wram_writes(args)
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
                seal(file_bytes)
                sram[start:start + FILE_SIZE] = file_bytes
                changed += 1
            else:
                print(f"save file {slot}: empty or does not validate; skipped")
                continue
        data_start = DATA_OFFSET + (SECOND_SET_OFFSET if file_bytes[5] == 2
                                    and (file_bytes[4] & 1) else 0)
        before = cleared_numbers(file_bytes[data_start:])
        added = [] if args.no_levels else [n for n in numbers
                                           if n not in before]
        report = [f"{len(before)} level numbers cleared, {len(added)} to "
                  f"mark of {len(numbers)} real levels"
                  + (f" ({added[0]}..{added[-1]})" if added else "")]
        edits = []
        for address, (or_mask, value) in writes.items():
            if address == WRAM_BANANA_COINS:
                continue
            offset = data_start + (KREMKOIN_OFFSET if address == WRAM_KREMKOINS
                                   else state_offset(address))
            current = file_bytes[offset]
            wanted = current | or_mask if value is None else value
            label = {WRAM_KREMKOINS: "Kremkoins",
                     WRAM_LOST_WORLD_CLEARED: "Lost World levels beaten",
                     WRAM_KIOSKS_PAID: "kiosks paid mask"}[address]
            report.append(f"{label} {current:#04x} -> {wanted:#04x}")
            if wanted != current:
                edits.append((offset, or_mask, value))
        print(f"save file {slot}: " + "; ".join(report))
        if args.dry_run or not (added or edits):
            continue
        for number in numbers if added else ():
            offset = data_start + CLEARED_OFFSET + (number >> 4) * 2
            word = file_bytes[offset] | (file_bytes[offset + 1] << 8)
            word |= 1 << (number & 15)
            file_bytes[offset] = word & 0xFF
            file_bytes[offset + 1] = word >> 8
        for offset, or_mask, value in edits:
            apply_byte(file_bytes, offset, or_mask, value)
        seal(file_bytes)
        sram[start:start + FILE_SIZE] = file_bytes
        changed += 1
    if args.dry_run:
        return 0
    if not changed and not (args.snapshot and writes):
        print("nothing to change")
        return 0

    target = args.output or args.save
    if changed:
        if target == args.save:
            backup = back_up(args.save)
            print(f"backup written: {backup}")
        target.write_bytes(bytes(sram))
        print(f"written: {target}")
    if args.snapshot:
        patch_snapshot(args.snapshot, numbers, not args.no_levels, writes,
                       args.output is not None)
    else:
        print("Start the game from the file select screen; a restored quick "
              "save still holds the old values.")
    return 0


WRAM_CLEARED_ADDRESS = 0x59F2
WRAM_BLOCK_BYTES = 0x20000


def patch_snapshot(path: Path, numbers, set_levels: bool, writes,
                   to_scratch: bool) -> None:
    """Set the same flags and bytes in a quick save's in-memory copy. The
    runtime stores WRAM as one raw 128 KiB block inside the snapshot; the
    block is located by checking that a candidate base holds cleared-flag
    words that are a plausible subset of the real levels and the game's
    map mode word at $24."""
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
    changed = False
    for number in numbers if set_levels else ():
        offset = base + WRAM_CLEARED_ADDRESS + (number >> 4) * 2
        word = snap[offset] | (snap[offset + 1] << 8)
        if word & (1 << (number & 15)):
            continue
        word |= 1 << (number & 15)
        snap[offset] = word & 0xFF
        snap[offset + 1] = word >> 8
        changed = True
    for address, (or_mask, value) in writes.items():
        before = snap[base + address]
        if apply_byte(snap, base + address, or_mask, value):
            changed = True
        print(f"snapshot WRAM ${address:04X}: {before:#04x} -> "
              f"{snap[base + address]:#04x}")
    if not changed:
        print(f"snapshot {path}: already holds the values")
        return
    if not to_scratch:
        backup = back_up(path)
        print(f"backup written: {backup}")
    target = path if not to_scratch else path.with_name(path.name + ".unlocked")
    target.write_bytes(bytes(snap))
    print(f"snapshot changed: {target} (WRAM block at {base:#x})")
    return


if __name__ == "__main__":
    sys.exit(main())
