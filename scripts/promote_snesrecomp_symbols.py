#!/usr/bin/env python3
"""Promote validated contextual symbols into SNESRecomp CFG function names.

The supported DKC2 revision changes the physical address of some routines
relative to addresses embedded in research-label names.  Consequently, this
tool does not rename by a CFG range start.  It only expands a generic
``CODE_BBXXXX`` name when a private WLA symbol map contains exactly one valid
``context_CODE_BBXXXX`` alias.  Retaining the original generic identity in the
candidate makes the operation independent of revision-specific address drift.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
from pathlib import Path
import re
import sys


FUNC_RE = re.compile(
    r"^(?P<prefix>\s*func\s+)"
    r"(?P<name>[A-Za-z_][A-Za-z0-9_]*)"
    r"(?P<suffix>\s+[0-9A-Fa-f]{4}\s+end:[0-9A-Fa-f]{4}.*)$"
)
BANK_RE = re.compile(
    r"^\s*bank\s*=\s*(?:0x([0-9A-Fa-f]{1,2})|([0-9]{1,3}))\s*$"
)
GENERIC_RE = re.compile(r"^CODE_([0-9A-Fa-f]{6})$")
SYMBOL_RE = re.compile(
    r"^\s*([0-9A-Fa-f]{2}):([0-9A-Fa-f]{4})\s+"
    r"([A-Za-z_][A-Za-z0-9_]*)\s*$"
)
IDENTIFIER_RE = re.compile(r"^[A-Za-z_][A-Za-z0-9_]*$")
DISALLOWED_CONTEXT_PREFIXES = ("CODE_", "DATA_")


@dataclass(frozen=True)
class FunctionEntry:
    path: Path
    line_index: int
    bank: str
    name: str


@dataclass(frozen=True)
class Promotion:
    entry: FunctionEntry
    new_name: str
    symbol_address: str


def load_wla_symbols(path: Path) -> dict[str, set[str]]:
    """Load address-to-alias sets from a WLA ``[labels]`` file."""
    aliases: dict[str, set[str]] = {}
    in_labels = False
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if stripped.startswith("["):
            in_labels = stripped.lower() == "[labels]"
            continue
        if not in_labels or not stripped or stripped.startswith(";"):
            continue
        match = SYMBOL_RE.match(line)
        if match is None:
            continue
        address = f"{match.group(1).upper()}:{match.group(2).upper()}"
        aliases.setdefault(address, set()).add(match.group(3))
    if not aliases:
        raise ValueError(f"symbol file has no WLA [labels] entries: {path}")
    return aliases


def load_cfg_entries(cfg_dir: Path) -> tuple[list[FunctionEntry], set[str]]:
    entries: list[FunctionEntry] = []
    names: set[str] = set()
    paths = sorted(cfg_dir.glob("bank[0-9A-Fa-f][0-9A-Fa-f].cfg"))
    if not paths:
        raise ValueError(f"no bankXX.cfg files found in {cfg_dir}")

    for path in paths:
        lines = path.read_text(encoding="utf-8").splitlines()
        bank: str | None = None
        for line in lines:
            match = BANK_RE.match(line)
            if match is not None:
                if match.group(1) is not None:
                    bank_value = int(match.group(1), 16)
                else:
                    bank_value = int(match.group(2), 10)
                if bank_value > 0xFF:
                    raise ValueError(f"CFG bank is outside byte range: {path}")
                bank = f"{bank_value:02X}"
                break
        if bank is None:
            raise ValueError(f"CFG has no bank declaration: {path}")

        for line_index, line in enumerate(lines):
            match = FUNC_RE.match(line)
            if match is None:
                continue
            name = match.group("name")
            if name in names:
                raise ValueError(f"duplicate CFG function name: {name}")
            names.add(name)
            entries.append(FunctionEntry(path, line_index, bank, name))
    return entries, names


def contextual_candidates(current: str, aliases: set[str]) -> list[str]:
    suffix = f"_{current}"
    candidates: set[str] = set()
    for alias in aliases:
        if not alias.endswith(suffix) or not IDENTIFIER_RE.fullmatch(alias):
            continue
        context = alias[: -len(suffix)]
        if not context or context.startswith(DISALLOWED_CONTEXT_PREFIXES):
            continue
        candidates.add(alias)
    return sorted(candidates)


def find_promotions(cfg_dir: Path, symbols_path: Path) -> list[Promotion]:
    aliases_by_address = load_wla_symbols(symbols_path)
    entries, existing_names = load_cfg_entries(cfg_dir)
    promotions: list[Promotion] = []

    for entry in entries:
        generic = GENERIC_RE.fullmatch(entry.name)
        if generic is None:
            continue
        encoded = generic.group(1).upper()
        encoded_bank = encoded[:2]
        if encoded_bank != entry.bank:
            raise ValueError(
                f"{entry.path}:{entry.line_index + 1}: {entry.name} encodes "
                f"bank {encoded_bank}, but the CFG declares bank {entry.bank}"
            )
        address = f"{encoded_bank}:{encoded[2:]}"
        candidates = contextual_candidates(
            entry.name, aliases_by_address.get(address, set())
        )
        if len(candidates) > 1:
            joined = ", ".join(candidates)
            raise ValueError(
                f"ambiguous contextual aliases for {entry.name}: {joined}"
            )
        if not candidates:
            continue
        candidate = candidates[0]
        if candidate in existing_names:
            raise ValueError(
                f"contextual alias collides with an existing CFG function: "
                f"{candidate}"
            )
        existing_names.add(candidate)
        promotions.append(Promotion(entry, candidate, address))
    return promotions


def apply_promotions(promotions: list[Promotion]) -> None:
    by_path: dict[Path, dict[int, Promotion]] = {}
    for promotion in promotions:
        by_path.setdefault(promotion.entry.path, {})[
            promotion.entry.line_index
        ] = promotion

    for path, line_promotions in by_path.items():
        original = path.read_text(encoding="utf-8")
        had_final_newline = original.endswith("\n")
        lines = original.splitlines()
        for line_index, promotion in line_promotions.items():
            match = FUNC_RE.match(lines[line_index])
            if match is None or match.group("name") != promotion.entry.name:
                raise ValueError(
                    f"CFG changed while applying symbol promotion: "
                    f"{path}:{line_index + 1}"
                )
            lines[line_index] = (
                f"{match.group('prefix')}{promotion.new_name}"
                f"{match.group('suffix')}"
            )
        rendered = "\n".join(lines)
        if had_final_newline:
            rendered += "\n"
        path.write_text(rendered, encoding="utf-8", newline="\n")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cfg-dir", type=Path, default=Path("recomp"))
    parser.add_argument("--symbols", type=Path, required=True)
    parser.add_argument(
        "--apply",
        action="store_true",
        help="write validated names; otherwise report a dry run",
    )
    args = parser.parse_args(argv)

    try:
        promotions = find_promotions(args.cfg_dir, args.symbols)
        for promotion in promotions:
            relative = promotion.entry.path
            print(
                f"{relative}:{promotion.entry.line_index + 1}: "
                f"{promotion.entry.name} -> {promotion.new_name} "
                f"({promotion.symbol_address})"
            )
        if args.apply:
            apply_promotions(promotions)
    except (OSError, ValueError) as error:
        print(f"cannot promote symbols: {error}", file=sys.stderr)
        return 1

    action = "applied" if args.apply else "found"
    print(f"{action} {len(promotions)} validated contextual promotion(s)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
