#!/usr/bin/env python3
"""Look up curated DKC2 functions, WRAM objects, and structure fields."""

from __future__ import annotations

import argparse
import json
import re

from dkc2_symbols_generated import (
    FUNCTION_ALIASES,
    FUNCTION_SYMBOLS,
    STRUCTURE_FIELDS,
    WRAM_OBJECTS,
)


def parse_address(query: str) -> tuple[str, int] | None:
    compact = query.strip().upper().replace("$", "").replace("0X", "")
    function = re.fullmatch(r"([0-9A-F]{2}):?([0-9A-F]{4})", compact)
    if function:
        return "function", (int(function.group(1), 16) << 16) | int(function.group(2), 16)
    wram = re.fullmatch(r"(?:WRAM:|7E:)?([0-9A-F]{1,5})", compact)
    if wram:
        return "wram", int(wram.group(1), 16)
    return None


def search(query: str) -> list[dict]:
    parsed = parse_address(query)
    results: list[dict] = []
    if parsed:
        kind, address = parsed
        if kind == "function" and address in FUNCTION_SYMBOLS:
            results.append({"kind": "function", "address": address, **FUNCTION_SYMBOLS[address]})
        if kind == "wram":
            for start, item in WRAM_OBJECTS.items():
                if start <= address < start + item["size"]:
                    results.append({"kind": "wram_object", "address": start, "queried_offset": address - start, **item})
        return results

    folded = query.casefold()
    for address, item in FUNCTION_SYMBOLS.items():
        searchable = [item["name"], item["note"], item["provenance"], *item["aliases"], *item["tags"]]
        if any(folded in value.casefold() for value in searchable):
            results.append({"kind": "function", "address": address, **item})
    for address, item in WRAM_OBJECTS.items():
        searchable = [item["name"], item["constant"], item["note"], item["provenance"], *item["tags"]]
        if any(folded in value.casefold() for value in searchable):
            results.append({"kind": "wram_object", "address": address, **item})
    for structure, fields in STRUCTURE_FIELDS.items():
        for offset, item in fields.items():
            searchable = [structure, item["name"], item["constant"], item["note"]]
            if any(folded in value.casefold() for value in searchable):
                results.append({"kind": "field", "structure": structure, "offset": offset, **item})
    exact = FUNCTION_ALIASES.get(query)
    if exact is not None:
        results.sort(key=lambda item: item.get("address") != exact)
    return results


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("query", help="name, tag, BB:OOOO function address, or WRAM offset")
    args = parser.parse_args()
    results = search(args.query)
    print(json.dumps(results, indent=2, sort_keys=True))
    return 0 if results else 1


if __name__ == "__main__":
    raise SystemExit(main())
